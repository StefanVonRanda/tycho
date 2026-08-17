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

`--idle-ms` bounds two things rather than one, and the second is easy to miss.
It is how long a silent keep-alive peer may pin a worker — and, because a worker
can only notice a shutdown *between* requests, it is also the worst-case time a
`SIGTERM` takes. Measured at `--workers 4 --idle-ms 5000`: with no connection
held, or one, shutdown completes in **1 ms**; with all four workers parked on
idle keep-alive connections it takes **5141 ms**, one full idle timeout. Exit
status is 0 and the stopped line prints in every case, so a busy shutdown is
slow, not hung — but a large `--idle-ms` is a proportionally slow `SIGTERM`.
`signal.shutdown_requested()` (`corelib/signal/signal.ty:18@shutdown_requested`)
exists to cut that to one in-flight request and has no caller in the tree yet;
filed as phase 15 of the signals plan.

## What it does

| | |
|---|---|
| **Methods** | `GET`, `HEAD`. Anything else is `405` with `Allow: GET, HEAD`. |
| **Bodies** | `bytes` end to end, so images and fonts come back byte-identical. |
| **Content types** | By extension, via `httpd.content_type`, extended locally with `.ttf`/`.otf`/`.map`/`.webmanifest`/`.md`. An unknown extension is `application/octet-stream`, never `text/plain`. |
| **Index resolution** | `/` and `/dir/` append `index.html`. `/dir` (no slash) gets a `301` to `/dir/`, so relative links on the page resolve correctly. |
| **Conditional GET** | Every `200` for a file carries `Last-Modified` (`io.mtime`, formatted as an RFC 7231 IMF-fixdate). An `If-Modified-Since` that the file's mtime is **not newer** than gets `304` with no body. Absent, unparseable, one of the two obsolete date forms, or an mtime that could not be read: all `200`. The comparison never fails open, because a wrong `304` is a browser keeping a file that changed. |
| **Byte ranges** | One range. `Range: bytes=A-B` (both ends inclusive, `B` clamped to the last byte), `bytes=A-` and the suffix `bytes=-N` get `206` with `Content-Range: bytes A-B/LEN` and a `Content-Length` of the **slice**. A range with no satisfiable byte is `416` with `Content-Range: bytes */LEN`. An invalid spec, an unknown unit or a multipart request is ignored: `200`, whole file. `Accept-Ranges: bytes` goes on exactly the `200` for a file and the `206`. A `304` outranks a `Range` (RFC 7232 §6). |
| **Keep-alive** | HTTP/1.1 default-on, `Connection: close` honoured, HTTP/1.0 defaults to close. Up to 1024 requests per connection. |
| **Idle timeout** | `SO_RCVTIMEO` on each accepted socket, so a silent peer cannot pin a worker. |
| **Shutdown** | `SIGTERM` and `SIGINT` are caught (`core:signal`): every accept loop retires, every worker is joined, the process prints `tycho-httpd: stopped after N requests` (`server/main.ty:698@stopped`) and exits **0**. `SIGKILL` is uncatchable and still stops it where it stands, with no such line. Shutdown latency is bounded by `--idle-ms`, not by the signal — see Usage above. |
| **Statuses** | 200, 206, 301, 304, 400, 403, 404, 405, 408, 416, 431. A peer that hangs up before a complete request arrives gets no response and no log line at all — the transport cause (`httpd.ReqErr`) is what separates that from the 400 a malformed head earns. |
| **Logging** | One line per request on stderr: worker, **client address**, method, target, status, body bytes, duration — `w1 127.0.0.1 GET / 200 2659 0.081ms` (`server/main.ty:204-214`). A response the peer hung up on gains a ` write-failed` tail and reports 0 bytes rather than claiming a body nobody received. The target is control-byte-scrubbed and truncated, so a hostile URL cannot inject newlines into the log. |

## Concurrency

A fixed pool of worker tasks, each running its own `accept` loop on the one
shared listening fd. The pool is built by **recursive fan-out** — worker *k*
spawns worker *k+1* into a frame-local, then enters its own loop:

```tycho
struct Config:
    root: string

fn accept_loop(cfg: Config, srv: int, wid: int) -> int:
    return wid

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
the webserver plan measured the alternatives —
`parallel for` silently collapses to `min(N, ncpu)` live iterations, and
accept-on-main-spawn-per-connection serialises completely because the compiler
emits an implicit join at the handle's scope exit. Both are still open as
`docs/internals/FRICTION.md` items 3 and 4.

It is one connection per worker at a time, so N workers means N concurrent
connections. **Recorded measurements, 2026-07-26**, on loopback, 8 workers,
`--quiet`. These are history,
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

Verified against 13 vectors and
re-checked live at HEAD: `/../../etc/passwd`, `/..%2f..%2f..%2fetc/passwd`,
`/%2e%2e/%2e%2e/etc/passwd`, `/....//....//etc/passwd`, `//etc/passwd` and
`/.git/config` are each **403**; `/%00` is **400**, at step 4. All refused, with
zero bytes of `/etc/passwd` returned, while a control confirms the process can
in fact read that file. `make server-check` asserts both groups.

## Deliberately not implemented

TLS (terminate it in front), HTTP/2, compression, virtual hosts, directory
listings, request pipelining, **`ETag`** — the validator half of conditional
requests, which `sha256.hex` would make cheap but which is a separate feature the
costing did not rank — and **multipart ranges**. None of them is load-bearing for
"serve a directory of files to a browser", and each would have been scope the
plan did not ask for.

**Multipart is a decision, not an omission, and it is visible on the wire.**
`Range: bytes=0-99,200-299` is answered **`200` with the whole file**, which the
RFC permits (a server may ignore a `Range` header) and which most servers do. A
`multipart/byteranges` body is a second serialization format rather than a bigger
parser, so it was declined. The two alternatives are both worse: `416` would be
false, since those ranges *are* satisfiable and only this server's response
format is missing; and returning just the first range would be a lie about what
was sent.

### Five rough edges that were here, and are not any more

The first two were real limitations of the *corelib*, not of this program, and
both were closed on **2026-07-26**; the third was a gap in the language itself,
and the last two were `core:io` gaps this program is what exposed — all three
closed on **2026-07-31**. The history is kept rather than deleted: what was hard
is worth as much as what is true now, and each of these is why a syscall exists.

- **An empty directory used to be answered `404`** instead of the `301` to
  `<path>/` a real directory gets. `resolve()` could not ask "is this a
  directory": the only test the corelib offered was `len(io.list(p)) > 0`, which
  reports an empty directory as a *file* — a directory's **contents** were
  deciding its **status**. That was never fixable by a return type; `stat(2)` was
  the missing question, not the missing answer.
  **Fixed 2026-07-26** (`c56be8e`) by adding `fn is_dir(p: string) -> Result(bool, IoErr)` over a real
  `stat(2)` (`corelib/io/io.ty@is_dir`), which `resolve()` matches on at
  `server/main.ty@is_dir`. One `stat(2)` now answers all three tails: `Ok(true)`
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
  **Fixed 2026-07-26** (`9878c8c`): `netx_peer_addr` is `getpeername` + `inet_ntop`
  (`corelib/net/net_shim.c@netx_peer_addr`), surfaced as
  `fn peer_addr(fd: int) -> Result(string, NetErr)`
  (`corelib/net/net.ty@peer_addr`) and used at
  `server/main.ty@peer_addr` — asked **once per connection**, not once per
  request, because the peer of an accepted fd cannot change. It is `-` when the
  fd could not be asked, which is honest rather than blank. `make server-check`
  asserts a peer address on every log line, so this cannot silently regress.

And a third, closed on **2026-07-31**:

- **There used to be no graceful shutdown.** Nothing installed a `SIGTERM` or
  `SIGINT` handler, so the default disposition terminated the process where it
  stood: `kill -TERM` gave wait status 143, the `tycho-httpd: stopped after N
  requests` line never printed, and the access log's last entry was a request.
  The accept loop already *had* its wind-down path — `accept_loop` sets
  `running = false` on an `Err` from `net.accept` (`server/main.ty:490-491`) —
  so what was missing was the signal that triggers it, and that was a language
  gap rather than a server one.
  **Fixed 2026-07-31** by `core:signal` (`docs/spec/18-library.md` §32.27), a
  two-function package whose handler's only action is
  `shutdown(srv, SHUT_RDWR)` on the listening socket. **One call arms the whole
  pool** (`server/main.ty@on_shutdown`) — the handler is per-process and
  acts on the shared listener, so which thread the kernel delivers to does not
  matter, which is exactly why `core:signal` shuts the descriptor down instead
  of closing it. Its position is forced three ways: after `net.listen`, because
  before that there is no descriptor to register; before the fan-out, because
  `worker` does not *return* until the whole pool has wound down; and before the
  readiness banner, so no reader is told "serving" while `SIGTERM` still has its
  default disposition. **No new control flow was added** — every blocked
  `accept` returns `Err`, every loop retires, every spawned peer is joined, and
  `server/main.ty:698@stopped` prints the count that was always unreachable. It fails
  closed: a handler that cannot be installed warns on stderr and the server runs
  on with the old behaviour.

And two `core:io` gaps, both closed on **2026-07-31**. Each is the same shape as
`is_dir` above: a syscall the corelib did not expose, found by trying to answer
an HTTP request without it.

- **A file's modification time was unreachable**, so nothing could send
  `Last-Modified` and a conditional GET was impossible — every request re-sent a
  body the client already held. `mtime` existed nowhere in the tree, and the
  `stat(2)` that answers it was **already being made**: the shim behind `is_dir`
  filled a `struct stat`, returned `S_ISDIR` and dropped `st_mtim` on the floor,
  one field short of the answer.
  **Fixed 2026-07-31** (`b5dae09`) by `fn mtime(p: string) -> Result(int, IoErr)`
  over its own `stat(2)` (`corelib/io/io.ty@mtime`), in whole seconds on the
  clock `now()` reads. A directory is `Ok` there, not an error — it has a
  modification time and `stat(2)` reports it. The server formats it with
  `server/main.ty@http_date_at`, which is the existing `Date` formatter
  generalised to take a timestamp rather than a second spelling of IMF-fixdate,
  and reads the client's copy back with `server/main.ty@parse_http_date`, which
  validates the fixed positions itself and then rearranges the fields into
  `datetime.parse_clf`'s shape rather than being a second date parser to rot.
  **The `304` path is cheaper than the `200` it replaces**: one `stat(2)`, no
  `open`, no read. `make server-check` asserts both directions, and the negative
  one is the point — a stamp *older* than the file must be `200` **with** the
  body, because a comparison that is wrong in the permissive direction is a
  browser keeping a file that changed.
- **Serving part of a file meant reading all of it.** `pread`/`lseek` appeared
  nowhere in `corelib/`, and neither did any way to ask a file's length:
  `io.read_bytes` reads whole files, so `len(io.read_bytes(p))` was the only size
  available — a gigabyte read to learn that it is a gigabyte. A `Range` server
  built on that allocates the whole file to send a kilobyte of it.
  **Fixed 2026-07-31** by two calls. `fn read_at(p, off, n) -> Result(bytes,
  IoErr)` over `pread(2)` (`dfb435a`, `corelib/io/io.ty@read_at`), whose
  **allocation is bounded by the file and not by `n`** — it is `min(n, size -
  off)` — so a `Range` header naming a terabyte allocates only what the file
  holds past `off`, and no arbitrary cap had to be invented on top. And
  `fn size(p) -> Result(int, IoErr)` over one `stat(2)` (`8923ec1`,
  `corelib/io/io.ty@size`), because every range form needs the length *before* it
  knows what to read: a `416` emits `bytes */LEN` and opens nothing, and
  `bytes=-N` has no start until the length is known. A directory is `Err(IsDir)`
  there — the opposite of `mtime`, and deliberately: `st_size` on a directory is
  its own entry structure, not a count of bytes anyone can read.
  The serve path is now `io.size` then `io.read_at` — one `stat(2)` and one
  `pread(2)`, never `read_bytes` (`server/main.ty@parse_range`, `77960a1`).
  `make server-check` asserts the **bytes**, not the lengths: every `206` body is
  compared against the same slice cut from the file on disk, over a **binary**
  asset, because `Content-Length: 100` is satisfied just as well by the wrong
  hundred bytes.

## Two socket fixes this program forced into `core:net`

Both were found by running the server, both are C-level socket properties that
**no Tycho program can set** — `SIGPIPE`'s disposition is process-wide and Nagle
is a `setsockopt` — and both are in `corelib/net/net_shim.c`. That first reason
is now narrower than it was, and it still holds: `core:signal` gave Tycho a
signal surface on 2026-07-31, but a deliberately narrow one that arms
`SIGTERM`/`SIGINT` for shutdown and cannot set any other signal's disposition, so
`MSG_NOSIGNAL` is still the only fix for this. (`getpeername` was a
third `core:net` addition this program forced, and `io.is_dir`, `io.mtime`,
`io.read_at` and `io.size` are four more in `core:io`; all five are above, under
the rough edges they closed.)

- **`MSG_NOSIGNAL` / `SO_NOSIGPIPE`** (`corelib/net/net_shim.c:41-53` and
  `corelib/net/net_shim.c:151-152`). Before this, one client that sent a partial request and closed
  without reading killed the entire server — `SIGPIPE`, signal 13, every worker
  and every in-flight connection gone. The server now survives 100 consecutive
  hostile disconnects and logs them as `write-failed` (`server/main.ty:374@write-failed`);
  `make server-check` re-runs a 50-disconnect version of that on every CI sweep.
- **`TCP_NODELAY`** (`corelib/net/net_shim.c:97-98`).
  `httpd.write_response` sends the head and the body as two writes, on purpose,
  so the body is never copied into an intermediate string. With Nagle enabled
  that second small segment waits for the peer's delayed ACK. **Recorded
  measurement, 2026-07-26**:
  **43.73 ms per request, 23 req/s** with Nagle on, against **0.07 ms per
  request, 14,465 req/s** with it off — a 620× difference that is entirely one
  stalled segment per response. Like the concurrency table, this is history and
  not a gate; `make server-check` asserts no timing.

## Verifying it

```sh
make server-check      # ~8s, the gate
```

`server/run.sh` starts the real binary, talks HTTP to it over raw sockets,
asserts what it answers, and kills it on every exit path — and **how it dies is
three assertions now, where it used to be one.** That one asserted wait status
143: the *absence* of clean shutdown. Today `SIGTERM` must give exit status
**0**, the `stopped after N requests` line, an `N` equal to the access-log line
count, and a log whose last entry is the shutdown line rather than a request;
`SIGINT` must give the same exit 0 and the same line, being the same handler on
the one signal `corelib/test/signal` cannot itself test (glibc's `system(3)`
ignores `SIGINT` in the caller for the duration of the call, and that fixture
kills through `os.system`); and `SIGKILL` must give wait status **137** with
**no** stopped line, which is the control — without it, "exit 0 and a line"
would be consistent with the process having printed that on the way out of any
signal at all. **173 assertions in total**, counted from the runner's own output
on 2026-07-31, against 61 before conditional requests and byte ranges landed that
day. Two things the new ones do that a status-code check would not: the runner
pins one file's mtime with `os.utime`, so `Last-Modified` is compared against a
date python formatted independently instead of merely being a plausible-looking
string; and every `206` body is compared against the matching slice cut from the
file on disk, over a binary asset, because the wrong hundred bytes satisfy
`Content-Length: 100` exactly as well as the right hundred. A handful of
assertions no mutation of the feature could redden are **labelled as controls in
the script**, because from the pass line a control and a proof look identical. A
10 s watchdog `SIGKILL`s the `SIGTERM`
case, so a regression that leaves one accept loop blocked reddens this gate
instead of hanging it. `make server-check`
runs it (`Makefile:247-248`), immediately after `entrypoints` —
so a server that does not *build* reddens there with a compile error rather than
arriving here as a readiness timeout. It skips with a `SKIP` line and exit 0 if
`python3` is absent.

Until 2026-07-30 this section said there was no `make` gate for the server,
"because it is a long-running network daemon, not a fixture with a golden".
Both halves were wrong, and the second was wrong as a *principle*: the daemon
shape is exactly what makes it gateable. `--port 0` plus the startup banner
(`server/main.ty:635-639`) hands a runner readiness **and** the bound port on
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
the webserver plan. What the gate asserts today is a
superset of it in behaviour and a subset in timing: it adds the smuggling and
abuse cases and the access-log checks, and drops every wall-clock number.
