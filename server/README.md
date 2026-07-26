# tycho-httpd

A static web server written in Tycho. Not a fixture and not a demo — build it,
point it at a directory, and point a browser at that.

```sh
make server
./tycho-httpd --root server/www --port 8080
# then open http://127.0.0.1:8080/
```

`server/www/` is a real little site: an HTML page, a stylesheet, a script that
does a `fetch`, a 480×270 PNG, a 32×32 PNG-in-ICO favicon, a 95 KB TrueType
font, a JSON document, a `robots.txt`, and a subdirectory with its own index. It
exists so that loading one page exercises every code path the server has.

## Usage

```
usage: tycho-httpd [options]

  --root DIR       directory to serve            (default: .)
  --host ADDR      address to bind               (default: 127.0.0.1)
  --port N         port to bind, 0 = pick free   (default: 8080)
  --workers N      concurrent accept loops       (default: 8)
  --idle-ms N      keep-alive idle timeout, ms   (default: 5000)
  --quiet, -q      do not log requests
  --help, -h       this text
```

Both `--port 8080` and `--port=8080` work. `--port 0` binds an ephemeral port
and prints the one it got, which is what the test scripts use.

## What it does

| | |
|---|---|
| **Methods** | `GET`, `HEAD`. Anything else is `405` with `Allow: GET, HEAD`. |
| **Bodies** | `bytes` end to end, so images and fonts come back byte-identical. |
| **Content types** | By extension, via `httpd.content_type`, extended locally with `.ttf`/`.otf`/`.map`/`.webmanifest`/`.md`. An unknown extension is `application/octet-stream`, never `text/plain`. |
| **Index resolution** | `/` and `/dir/` append `index.html`. `/dir` (no slash) gets a `301` to `/dir/`, so relative links on the page resolve correctly. |
| **Keep-alive** | HTTP/1.1 default-on, `Connection: close` honoured, HTTP/1.0 defaults to close. Up to 1024 requests per connection. |
| **Idle timeout** | `SO_RCVTIMEO` on each accepted socket, so a silent peer cannot pin a worker. |
| **Statuses** | 200, 301, 400, 403, 404, 405, 431. |
| **Logging** | One line per request on stderr: worker, method, target, status, body bytes, duration. The target is control-byte-scrubbed and truncated, so a hostile URL cannot inject newlines into the log. |

## Concurrency

A fixed pool of worker tasks, each running its own `accept` loop on the one
shared listening fd. The pool is built by **recursive fan-out** — worker *k*
spawns worker *k+1* into a frame-local, then enters its own loop:

```tycho
fn worker(cfg: Config, srv: int, wid: int, remaining: int) -> int:
    if remaining > 1:
        peer := spawn worker(cfg, srv, wid + 1, remaining - 1)
        n := accept_loop(cfg, srv, wid)
        return n + wait(peer)
    return accept_loop(cfg, srv, wid)
```

That shape is forced, not chosen. A task handle in Tycho is **affine**: it
cannot be stored in an array, a struct, or any aggregate, so the only place to
keep N live handles is N stack frames. `plan.md` phase 1 measured the
alternatives — `parallel for` silently collapses to `min(N, ncpu)` live
iterations, and accept-on-main-spawn-per-connection serialises completely
because the compiler emits an implicit join at the handle's scope exit.

It is one connection per worker at a time, so N workers means N concurrent
connections. Measured on loopback, 8 workers, `--quiet`:

| clients | requests | wall | throughput |
|---|---|---|---|
| 1 | 800 × 294 B | 64 ms | 12,456 req/s |
| 4 | 3200 × 294 B | 78 ms | 41,046 req/s |
| 8 | 6400 × 294 B | 80 ms | 79,712 req/s |
| 8 | 1600 × 95 KB | 37 ms | 42,867 req/s (≈ 4.1 GB/s) |

Scaling is linear in worker count, which is the evidence that the N accept
loops are genuinely parallel rather than interleaved.

## Path traversal

This is the one security property that matters in a static file server, so the
order of operations in `resolve()` is deliberate:

1. cut the query and fragment — `?a=1` and `#frag` are not path
2. require origin-form — the target must start with `/`
3. percent-decode **once** — so `%2e%2e` cannot smuggle a `..` past step 5,
   and decoding twice cannot let `%252e%252e` through (the historical IIS bug)
4. reject control bytes including NUL — path smuggling and log injection
5. reject any segment starting with `.` — `.git`, `.env`, `.htpasswd`
6. `path.safe_join(root, rel)`, which returns `""` for an absolute `rel` or one
   that climbs above the root, and is treated as a refusal

Refusals are `403`, not `404`: the path was understood and declined. Verified
against 13 vectors including `/../../../etc/passwd`, `/..%2f..%2f..%2fetc/passwd`,
`/%2e%2e/%2e%2e/etc/passwd`, `/....//....//etc/passwd`, `//etc/passwd`,
`/%00` and `/.git/config` — all refused, with zero bytes of `/etc/passwd`
returned, while a control confirms the process can in fact read that file.

## Deliberately not implemented

TLS (terminate it in front), HTTP/2, byte ranges, compression, conditional
requests (`ETag`/`If-Modified-Since`), virtual hosts, directory listings, and
request pipelining. None of them is load-bearing for "serve a directory of files
to a browser", and each would have been scope the plan did not ask for.

Two known rough edges, stated rather than hidden:

- An **empty directory** is answered `404`, not the `301` to `<path>/` a real
  directory gets. `resolve()` still cannot ask "is this a directory" — there is no
  `stat` or `is_dir` in the corelib, and the only test available is
  `len(io.list(p)) > 0`, which reports an empty directory as a file. What changed
  on 2026-07-26 (plan.md phase 2) is the *answer*: `io.read_bytes` returns
  `Result(bytes, io.IoErr)`, so the read that follows says `Err(io.IsDir)` and this
  server sends a `404` instead of the 0-byte `200` it used to. Measured, same
  document root: pre-phase-2 binary `GET /emptydir` → `200 0`, now → `404`.
- The **access log has no client address**. `net.accept` returns a bare fd and
  `core:net` exposes `getsockname` but not `getpeername`, so the field a real
  access log most wants is not reachable from Tycho.

## Two fixes this program forced into `core:net`

Both were found by running the server, both are C-level socket properties that
**no Tycho program can set**, and both are in `corelib/net/net_shim.c`:

- **`MSG_NOSIGNAL` / `SO_NOSIGPIPE`.** Before this, one client that sent a
  partial request and closed without reading killed the entire server —
  `SIGPIPE`, signal 13, every worker and every in-flight connection gone. The
  server now survives 100 consecutive hostile disconnects and logs them as
  `write-failed`.
- **`TCP_NODELAY`.** `httpd.write_response` sends the head and the body as two
  writes, on purpose, so the body is never copied into an intermediate string.
  With Nagle enabled that second small segment waits for the peer's delayed ACK:
  **43.73 ms per request, 23 req/s.** With Nagle off, the same server on the same
  bytes does **0.07 ms per request, 14,465 req/s** — a 620× difference that is
  entirely one stalled segment per response.

## Verifying it

There is no `make` gate for this: it is a long-running network daemon, not a
fixture with a golden, so `make server` builds and asserts nothing. Verify it
the way you would verify any server — run it and talk to it:

```sh
./tycho-httpd --root server/www --port 8080 &
curl -sI http://127.0.0.1:8080/img/logo.png            # 200, image/png, no body
curl -s  http://127.0.0.1:8080/img/logo.png | cmp - server/www/img/logo.png
curl -so /dev/null -w '%{http_code}\n' --path-as-is \
     http://127.0.0.1:8080/../../etc/passwd            # 403
curl -sv http://127.0.0.1:8080/ http://127.0.0.1:8080/style.css 2>&1 \
  | grep -i reusing                                    # one connection, two assets
```

The full transcript this was signed off against — every status code, the binary
`cmp`s, the traversal matrix, the abuse suite, and the concurrency measurements
— is recorded under Phase 7 in `plan.md`.
