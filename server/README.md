# tycho-httpd

A static web server written in Tycho. Not a fixture and not a demo — build it,
point it at a directory, and point a browser at that.

```sh
make server
./tycho-httpd --root server/www --port 8080
# then open http://127.0.0.1:8080/
```

`server/www/` is a real little site: an HTML page, a stylesheet, a script that
does a `fetch`, a 480×270 PNG and a 16×16 one, a 32×32 PNG-in-ICO favicon, a
95 KB TrueType font and its licence, a JSON document, a `robots.txt`, and a
subdirectory with its own index. It exists so that loading one page exercises
every code path the server has.

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
| **Statuses** | 200, 301, 400, 403, 404, 405, 408, 431. A peer that hangs up before a complete request arrives gets no response and no log line at all — the transport cause (`httpd.ReqErr`) is what separates that from the 400 a malformed head earns. |
| **Logging** | One line per request on stderr: worker, **client address**, method, target, status, body bytes, duration — `w1 127.0.0.1 GET / 200 2659 0.081ms` (`server/main.ty:342-352`). A response the peer hung up on gains a ` write-failed` tail and reports 0 bytes rather than claiming a body nobody received. The target is control-byte-scrubbed and truncated, so a hostile URL cannot inject newlines into the log. |

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
keep N live handles is N stack frames. Phase 1 of
`docs/internals/plan-webserver-DONE.md` measured the alternatives —
`parallel for` silently collapses to `min(N, ncpu)` live iterations, and
accept-on-main-spawn-per-connection serialises completely because the compiler
emits an implicit join at the handle's scope exit. Both are still open as
`FRICTION.md` items 3 and 4.

It is one connection per worker at a time, so N workers means N concurrent
connections. **Recorded measurements, 2026-07-26**, on loopback, 8 workers,
`--quiet` (`docs/internals/plan-webserver-DONE.md:814-816`). These are history,
not a gate: `make server-check` asserts the *structural* fact that all four
workers take traffic, and deliberately asserts no wall-clock number, because
timing on a shared machine is the one flake a CI lane must not have.

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
2. require origin-form — the target must start with `/`, else **400**
3. percent-decode **once** — so `%2e%2e` cannot smuggle a `..` past step 5,
   and decoding twice cannot let `%252e%252e` through (the historical IIS bug)
4. reject control bytes including NUL — path smuggling and log injection; **400**
5. reject any segment starting with `.` — `.git`, `.env`, `.htpasswd`; **403**
6. `path.safe_join(root, rel)`, which returns `""` for an absolute `rel` or one
   that climbs above the root, and is treated as a refusal; **403**

The split between 400 and 403 is deliberate and it is the order above that
decides it. A **403** means the path parsed and was declined — understood, and
not yours. A **400** means it was never a path this server would take: steps 2
and 4 fire before there is anything to resolve, so a NUL byte is malformed
input rather than a refused location.

Verified against 13 vectors (`docs/internals/plan-webserver-DONE.md:748`) and
re-checked live at HEAD: `/../../etc/passwd`, `/..%2f..%2f..%2fetc/passwd`,
`/%2e%2e/%2e%2e/etc/passwd`, `/....//....//etc/passwd`, `//etc/passwd` and
`/.git/config` are each **403**; `/%00` is **400**, at step 4. All refused, with
zero bytes of `/etc/passwd` returned, while a control confirms the process can
in fact read that file. `make server-check` asserts both groups.

## Deliberately not implemented

TLS (terminate it in front), HTTP/2, byte ranges, compression, conditional
requests (`ETag`/`If-Modified-Since`), virtual hosts, directory listings, and
request pipelining. None of them is load-bearing for "serve a directory of files
to a browser", and each would have been scope the plan did not ask for.

### Two rough edges that were here, and are not any more

Both were real limitations of the *corelib*, not of this program, and both were
closed on **2026-07-26**. The history is kept rather than deleted: what was hard
is worth as much as what is true now, and each of these is why a syscall exists.

- **An empty directory used to be answered `404`** instead of the `301` to
  `<path>/` a real directory gets. `resolve()` could not ask "is this a
  directory": the only test the corelib offered was `len(io.list(p)) > 0`, which
  reports an empty directory as a *file* — a directory's **contents** were
  deciding its **status**. That was never fixable by a return type; `stat(2)` was
  the missing question, not the missing answer.
  **Fixed 2026-07-26** (`4fa192d`, `docs/internals/plan-option-result-DONE.md`
  phase 4) by adding `fn is_dir(p: string) -> Result(bool, IoErr)` over a real
  `stat(2)` (`corelib/io/io.ty:133`), which `resolve()` matches on at
  `server/main.ty:301`. One `stat(2)` now answers all three tails: `Ok(true)`
  with no trailing slash is the `301`, `Ok(false)` is a file to serve, and
  `Err(_)` is a `404` that fails closed.
  **Measured live at HEAD**, against a document root holding an empty
  `emptydir/`: `GET /emptydir` → `301 Moved Permanently`, `Location:
  /emptydir/`; `GET /emptydir/` → `404`, because there is no `index.html`
  behind it. That second `404` is correct and is not the old bug — the old bug
  was that the *first* request never redirected at all.
- **The access log used to have no client address.** `core:net` wrapped
  `getsockname` and nothing else, so the field every real access log leads with
  was unreachable from Tycho — and unreachable in a way no Tycho program could
  work around, since `net.accept` hands back a bare fd.
  **Fixed 2026-07-26** (`7b76fcd`, `docs/internals/plan-friction-DONE.md` phase
  5): `netx_peer_addr` is `getpeername` + `inet_ntop`
  (`corelib/net/net_shim.c:204`), surfaced as
  `fn peer_addr(fd: int) -> Result(string, NetErr)` (`corelib/net/net.ty:143`)
  and used at `server/main.ty:368` — asked **once per connection**, not once per
  request, because the peer of an accepted fd cannot change. It is `-` when the
  fd could not be asked, which is honest rather than blank. `make server-check`
  asserts a peer address on every log line, so this cannot silently regress.

One rough edge that is still here:

- **There is no graceful shutdown.** Nothing installs a `SIGTERM` or `SIGINT`
  handler, so the default disposition terminates the process where it stands and
  the `tycho-httpd: stopped after N requests` line at `server/main.ty:617` never
  prints. `kill -TERM` gives wait status 143 and a log whose last entry is a
  request line. The accept loop already *has* a wind-down path — `accept_loop`
  sets `running = false` on `Err(e)` from `net.accept` (`server/main.ty:494`) —
  so what is missing is the signal that would trigger it, which is a language
  gap and not a server one. Filed as phase 5 of `plan.md`.

## Two socket fixes this program forced into `core:net`

Both were found by running the server, both are C-level socket properties that
**no Tycho program can set** — signal disposition is process-wide and Nagle is a
`setsockopt` — and both are in `corelib/net/net_shim.c`. (`getpeername` was a
third `core:net` addition this program forced, and `io.is_dir` a fourth in
`core:io`; those two are above, under the rough edges they closed.)

- **`MSG_NOSIGNAL` / `SO_NOSIGPIPE`** (`corelib/net/net_shim.c:41-53` and
  `corelib/net/net_shim.c:151-152`). Before this, one client that sent a partial request and closed
  without reading killed the entire server — `SIGPIPE`, signal 13, every worker
  and every in-flight connection gone. The server now survives 100 consecutive
  hostile disconnects and logs them as `write-failed` (`FRICTION.md:432`);
  `make server-check` re-runs a 50-disconnect version of that on every CI sweep.
- **`TCP_NODELAY`** (`corelib/net/net_shim.c:154-155`).
  `httpd.write_response` sends the head and the body as two writes, on purpose,
  so the body is never copied into an intermediate string. With Nagle enabled
  that second small segment waits for the peer's delayed ACK. **Recorded
  measurement, 2026-07-26** (`docs/internals/plan-webserver-DONE.md:855`):
  **43.73 ms per request, 23 req/s** with Nagle on, against **0.07 ms per
  request, 14,465 req/s** with it off — a 620× difference that is entirely one
  stalled segment per response. Like the concurrency table, this is history and
  not a gate; `make server-check` asserts no timing.

## Verifying it

```sh
make server-check      # ~4s, the gate
```

`server/run.sh` starts the real binary, talks HTTP to it over raw sockets,
asserts what it answers, and kills it on every exit path. `make server-check`
runs it (`Makefile:247-248`), and it has been in `make ci` as step **`[3c/13]`**
since 2026-07-30 (`scripts/ci.sh:111`), immediately after `[3b] entrypoints` —
so a server that does not *build* reddens there with a compile error rather than
arriving here as a readiness timeout. It skips with a `SKIP` line and exit 0 if
`python3` is absent.

Until 2026-07-30 this section said there was no `make` gate for the server,
"because it is a long-running network daemon, not a fixture with a golden".
Both halves were wrong, and the second was wrong as a *principle*: the daemon
shape is exactly what makes it gateable. `--port 0` plus the startup banner
(`server/main.ty:610-614`) hands a runner readiness **and** the bound port on
one line, so there is no `sleep` and no fixed port to collide on, and a `trap`
covers teardown. What is genuinely unassertable is wall-clock — the concurrency
table and the `TCP_NODELAY` figures, both above — not behaviour.

`make server` is still a build-only target and still asserts nothing; that is
now a division of labour rather than a gap.

### By hand

The gate does not replace pointing a client at it. Run it and talk to it:

```sh
./tycho-httpd --root server/www --port 8080 &
curl -sI http://127.0.0.1:8080/img/logo.png            # 200, image/png, no body
curl -s  http://127.0.0.1:8080/img/logo.png | cmp - server/www/img/logo.png
curl -so /dev/null -w '%{http_code}\n' --path-as-is \
     http://127.0.0.1:8080/../../etc/passwd            # 403
curl -sv http://127.0.0.1:8080/ http://127.0.0.1:8080/style.css 2>&1 \
  | grep -i reusing                                    # one connection, two assets
```

That backgrounded server is yours to kill — unlike `server/run.sh`, this
transcript has no `trap`, and a forgotten `tycho-httpd` stays bound to 8080.

The full transcript this was originally signed off against — every status code,
the binary `cmp`s, the traversal matrix, the abuse suite, and the concurrency
measurements — is recorded under Phase 7 of
`docs/internals/plan-webserver-DONE.md`. What the gate asserts today is a
superset of it in behaviour and a subset in timing: it adds the smuggling and
abuse cases and the access-log checks, and drops every wall-clock number.
