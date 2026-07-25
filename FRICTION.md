# FRICTION

One line per moment the language got in the way while writing a real web server.
Non-blocking by construction: anything that blocks the server earns a phase in
`plan.md` instead. This file is a deliverable; fixing everything in it is not.

The program these notes came from is `server/` — `tycho-httpd`, ~440 lines,
serving `server/www` to a real browser. Everything below was hit while writing
it, not imagined about it.

## The headline

**The language has a good answer for fallible calls and the standard library
does not use it.** `Option`/`Result` are real types and `or_return` is a real
postfix operator that unwraps or short-circuits (`docs/spec/10-statements.md:75`).
Exactly **1 of the corelib's 386 functions returns an `Option`** —
`io.read_line` (`corelib/io/io.ty:69`). Every other fallible call in this server
reports failure with a sentinel, and the sentinel is different each time:

| call | failure is… | also means |
|---|---|---|
| `net.accept` | a negative int | — |
| `net.write`, `httpd.write_response` | `-1` | — |
| `net.read` | empty `bytes` | EOF **and** idle timeout |
| `io.read_bytes` | empty `bytes` | empty file **and** missing file **and** a directory |
| `path.safe_join` | `""` | — |
| `httpd.parse_request` | `method == ""` | EOF **and** timeout **and** malformed |
| `net.set_read_timeout_ms` | `false` | — |

Six spellings of "it went wrong", three of which are indistinguishable from a
legitimate success. The server code reads as `if x < 0` / `if len(x) == 0` /
`if s == ""` scattered through every IO path, and two of the phase's real bugs
were exactly this collision: the server could not tell a malformed request from
a hangup, and cannot tell an empty directory from a file. Nothing about the
language forced that — `or_return` was sitting right there.

## Phase 7 — writing the server

- **Phase 7** — `send` is a builtin, and defining `fn send(conn, r, head_only, keep) -> int` is accepted **silently**; the collision surfaces only at the call site as `error: send(ch, v) takes a channel and a value`, which points at my call and describes a channel operation I never wrote. Nothing is reported at the definition, which is where the mistake is.
- **Phase 7** — `httpd.read_request` collapses EOF, idle timeout and a malformed request line into `method == ""`, so a server that must answer `400` to garbage but hang up silently on a disconnect cannot use it at all; `server/main.ty` reimplements the read loop (`read_head`) purely to keep the raw buffer and recover that one bit.
- **Phase 7** — `httpd.reason_phrase` is a closed `if`-chain and `httpd.response()` takes no reason, so status `431` goes on the wire as `HTTP/1.1 431 Status`. The workaround is to bypass the constructor and build `httpd.Response(431, "Request Header Fields Too Large", []string, []string, body)` positionally — it works across a package boundary, which is good, but it couples the caller to the struct's field order to set a string.
- **Phase 7** — no `\r` escape in string literals (`\n \t \\ \"` only), so the most common byte pair in HTTP is a function call, `httpd.crlf()`. And `const TERM = httpd.crlf() + httpd.crlf()` is rejected — `error: const value must be a literal` — so the header terminator has to be a function that reallocates two strings on every loop iteration.
- **Phase 7** — no multi-line string literal and no line continuation, so the 10-line HTML error page is 10 consecutive `s += "..."` statements. (`+=` does exist; I wrote `s = s + ...` for half the file before checking, because nothing in the corelib I had read used it.)
- **Phase 7** — there is no `stat` and no `is_dir`. `io.exists` answers by listing the parent directory, and the only directory test available is `len(io.list(p)) > 0`, which reports an **empty directory as a file** — `server/main.ty`'s `resolve()` ships a documented wrong answer (a 0-byte `200`) because the question cannot be asked.
- **Phase 7** — `args()` includes `argv[0]` but `cli.parse` requires it removed, so every program opens with the same four-line copy loop; `examples/weblog/main.ty:129` has the identical loop with a comment explaining the same thing.
- **Phase 7** — `core:cli` cannot express `--root DIR`: values must be `=`-attached by design, so any CLI wanting the conventional Unix spelling hand-rolls its parser. That is 45 of this server's lines.
- **Phase 7** — `die()` is the language's only exit and it always exits **1**, so `--help` cannot be answered with status 0 through it. The fix was to thread a `help: bool` field through the config struct so `main` could return normally — a data-flow change to work around a missing `exit(0)`.
- **Phase 7** — `net.accept` hands back a bare fd and `core:net` exposes `getsockname` (`netx_port_of`) but no `getpeername`, so an access log cannot record the client address — the single most useful field in a real access log is unreachable from Tycho.
- **Phase 7** — scrubbing control bytes out of a hostile request target (`log_safe`) has to go `string` → `[]int` → `to_bytes` → `to_str`, because a `string` cannot be rebuilt in place and `bytes` cannot be indexed. The Phase 2 `to_str`/`to_bytes` sandwich, biting exactly as predicted, in the one function where a server must be paranoid.
- **Phase 7** — `parallel for` and `spawn` are the only concurrency shapes, and neither can express "hand this connection to whoever is free". One worker owns one connection for its whole life, so N workers is a hard cap of N concurrent connections; there is no way to write an event loop or a work queue over accepted fds without a channel of ints and a hand-rolled dispatcher.

### Two defects that were not expressible in Tycho at all

Both were found by running the server, both are C-level socket properties with
no Tycho spelling, and both therefore earned a `corelib/net/net_shim.c` fix
rather than a line in this file. Recording them here because *the reason they
were unreachable* is the ergonomic finding.

- **`SIGPIPE` killed the whole server.** Nothing in `corelib/`, `runtime/` or `src/` mentioned SIGPIPE. One client that sent a partial request and closed without reading terminated the process — every worker, every in-flight connection — measured as `poll()` = `-13`. A Tycho program cannot fix this: signal disposition is process-wide with no Tycho surface, and `netx_write` loops `send()` internally, so even a single logical `net.write` can issue several syscalls. Fixed with `MSG_NOSIGNAL` / `SO_NOSIGPIPE`; the server now survives 100 consecutive hostile disconnects.
- **Nagle cost 620× on every small response.** `httpd.write_response` deliberately sends head and body as two writes so the body is never copied — a Phase 2 optimization. With Nagle on, that second small segment waits for the peer's delayed ACK. Measured, same server, same bytes: **43.73 ms/req (23 req/s) with two writes vs 0.07 ms/req (14,465 req/s) with one.** `TCP_NODELAY` fixes it and no Tycho program can set it. The lesson is sharper than the bug: a corelib change that saved one `memcpy` cost three orders of magnitude, and nothing in the language or library could have surfaced that to the person who wrote it.

## What was good

An honest account needs this half, and the good is not a consolation prize —
some of it is genuinely better than the mainstream alternatives.

- **Concurrency is the best thing in the language.** `spawn` / `wait`, no async colouring, no executor to configure, no locks anywhere in the server, and real parallelism: 1 / 4 / 8 client processes gave 12,456 / 41,046 / **79,712 req/s**, linear in worker count. The whole pool is five lines. Compare what "8 worker threads sharing an accept loop" costs to write in C.
- **Value semantics made the response builder obviously correct.** `r = httpd.with_header(r, ...)` returns a new value, so a three-call chain has no aliasing question, needs no defensive copy, and cannot be wrong. Zero aliasing bugs across the whole phase. This is the part I would keep unchanged.
- **`path.safe_join` is exemplary corelib design.** Fail-closed (returns `""`), handles both the absolute-path and the climb-out cases, and documents itself with worked examples including the answers. Thirteen traversal vectors — `..`, `%2e%2e`, `%2E%2E%2f`, `....//`, `//etc/passwd`, `/%00`, `.git` — all refused on the first attempt, with the whole defence being one call plus decoding in the right order.
- **`httpd.content_type` defaults to `application/octet-stream`, never `text/plain`, and says why in a comment.** That is the correct call and the reasoning was written down where the next person will read it.
- **Diagnostics are precise when they fire** — file, line, column, caret, and the source line quoted. The `send` collision is a gap in *where* the check happens, not in the quality of the message.
- **`bytes` bodies delivered byte-exact binary on the first try**: a 480×270 PNG, a PNG-in-ICO favicon, and a 95 KB TrueType font, all `cmp`-identical to disk, including a `HEAD` that reports the real `Content-Length` and sends nothing.
- **It compiled on the second attempt** — one rename — and then ran the entire abuse campaign (malformed request lines, binary junk, 60 KB headers, 2000 headers, slow-loris, RST-mid-body, 64-way floods, 6400-request runs) with **zero runtime crashes** once the two C-level socket issues were fixed. No use-after-free, no bounds abort, no leak, no data race across 8 threads sharing a listening fd.
- **The corelib is unusually well-commented.** Learning `path`, `httpd`, `net` and `datetime` well enough to build against them was fast, and the comments were accurate — with one exception, `httpd`'s old claim that an interior NUL truncates a string body, which Phase 2 measured as false.
- **Indentation-block syntax reads well at length.** `server/main.ty` is 440 lines and stayed readable without a formatter fight.

## The honest verdict

Tycho can write a real web server, and the result is fast, safe, and readable.
What it cannot yet do is make *failure* pleasant. The concurrency model, the
value semantics and the diagnostics are all better than the median; the error
model is worse, and it is worse by choice rather than by capability, because the
language already ships `Option`, `Result` and `or_return` and the standard
library uses them once in 386 functions. Adopting them across the IO surface
would remove more friction from this file than every other item combined.

## Earlier phases

- **Phase 1** — `spawn f(x)` as a bare statement is rejected with `a statement must be a declaration, assignment, or call -- a bare expression has no effect`, which never says the real rule: a task handle must be bound so the compiler can hang the implicit join on it.
- **Phase 1** — `parallel for i in range(N)` runs only `min(N, tycho_ncpu())` iterations concurrently (`runtime/tycho_rt.c:843-852`); iterations chunked behind one that never returns never start, and nothing warns. `TYCHO_THREADS=2` silently cut a 4-worker server to 2.
- **Phase 1** — starting N workers has no direct spelling: task handles are affine and unstorable, so it is either N hand-written `spawn` lines or a recursive fan-out where each frame holds one handle.
- **Phase 2** — `bytes` supports **only** `len()`, `to_str()`, and crossing the FFI: `a + b` is rejected (`arithmetic requires two ints or two floats (got bytes, bytes)`), `b[i]` is rejected (`can only index an array, a string, or a map`), `b[i:j]` is rejected (`can only slice an array, soa, or string`) — so every non-trivial `bytes` manipulation has to detour through `to_str`, do the work in `string`, and `to_bytes` back.
- **Phase 2** — the "arithmetic requires two ints or two floats" message for `bytes + bytes` suggests `to_float(x)`/`to_int(x)`, neither of which applies to a buffer; the useful advice would be "`bytes` has no operators — use `to_str` to concatenate".
- **Phase 2** — `string` is already fully byte-safe (interior `0x00` survives concat, index, slice, and `len`, measured), so `httpd`'s old header comment claiming an interior `0x00` truncates the body was **wrong**; the string/bytes split buys type-level intent and FFI shape, not binary safety the string model lacked.
- **Phase 2** — `to_bytes("")` is the only spelling for an empty `bytes`; there is no `bytes` literal and no zero value, so every struct default and early return carries the call.
- **Phase 3** — no `ends_with` without importing `core:strings`, and a corelib package taking a dependency for one predicate is worse than the six-line `has_ext` helper it needs, so the helper gets rewritten per package.
- **Phase 4** — `core:net` had no way to bound a blocking read; `time.sleep_ms` cannot help because it cannot interrupt a `recv` already in progress. The idle timeout required a new shim call (`SO_RCVTIMEO`), which means "do not let a peer pin this worker" was not expressible in Tycho corelib until this commit.
- **Phase 4** — a socket read timeout is indistinguishable from EOF at the Tycho level (both yield empty `bytes`); fine for a server, but a client that needs to retry a timeout while giving up on an EOF cannot tell them apart.
- **Phase 0** — six non-gated runners still build tychoc0 and compare against it (`examples/fetch`, `examples/sqlite`, `examples/webserver`, `examples/weblog`, `bench/run.sh`, `tools/prof/profile.sh`); none is in `make ci`, so none can redden, but each will drift as tychoc0 does.
- **Phase 0** — the harness scripts of the removed gates are still on disk unreferenced (`compiler/run.sh`, `compiler/fixpoint.sh`, `compiler/pkg-split.sh`, `scripts/frontparity.sh`, `tests/rtparity/`, `fuzz/run_pkg.py`, `fuzz/run_typeparity.py`, `run_parforparity.py`, `run_eqparity.py`, `run_unaryparity.py`); kept deliberately so the method behind the recorded self-hosting result stays readable.
- **Phase 0** — the 15 `tests/diag/*.h0err` tychoc0-diagnostic goldens are now orphaned; kept because three archived internals docs cite them.
- **Phase 0** — prepending a 50-line banner to `compiler/tychoc0.ty` invalidated every `:N` self-citation in its own comments (the citation gate only checks docs→source, not source→source), so the file now carries a "+50" correction note instead.
- **Phase 0** — `docs/bootstrap.md` is cited by `compiler/tychoc0.ty`'s original header and by `Makefile`'s old `bootstrap` comment, but the file does not exist anywhere in the tree; the citation gate never caught it because it only validates Markdown-to-Markdown links.
