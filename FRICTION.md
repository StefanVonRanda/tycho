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

> **Status, 2026-07-26.** The `net.*` rows above are historical: `core:net`'s
> fallible TCP calls now return `Result(T, net.NetErr)` and `net.read`
> distinguishes `Eof` / `Timeout` / `Failed` (`plan.md` phase 1). What that phase
> *measured* is that the win is confined to the ambiguous sentinels — converting
> an unambiguous one is line-for-line neutral — and that the conversion surfaced
> five new ergonomic gaps of its own, recorded below.
>
> **Also historical, from `plan.md` phase 2:** the `io.read_bytes` and
> `httpd.parse_request` rows. `read_bytes` returns `Result(bytes, io.IoErr)` —
> an empty file is `Ok`, `Err(NotFound)` and `Err(IsDir)` are distinct — and
> `parse_request` / `read_request` return `Result(Request, httpd.ReqErr)` with
> `Malformed` / `Closed` / `Timeout` / `Failed`, all four recorded as distinct in
> `corelib/test/httpd.out`. The missing `unwrap_or` is now `core:result`. The rows
> that remain true are `path.safe_join`, the `io` write side, `net.udp_*` and
> `net.set_read_timeout_ms` — each deliberately left on a sentinel that has
> exactly one meaning.
>
> **Closed out, from `plan.md` phase 3.** The headline's own worked example — the
> `read_head` reimplementation in the phase-7 list below — is **gone**. `server/`
> calls `httpd.read_request_capped(conn, MAX_HEAD)` and acts on the cause it
> returns: `Malformed` → `400`, `Closed` → no response and no log line,
> `Timeout` → `408` (or a quiet close for a keep-alive idle expiry), `TooLarge` →
> `431`. 19 lines of duplicated corelib loop and a local `Head` struct deleted;
> `server/main.ty` ended the plan at 371 code lines, **exactly where it started**.
> The honest arithmetic: the application did not get shorter, it got *truthful*,
> and the ~100 corelib lines that bought it are recorded in `plan.md`'s verdict.
> One item in this file is closed. The `stat` item is not, and is now known not to
> be an error-model problem at all.
>
> **Closed out, from `plan.md` phase 4 (added on a user directive after phase 3
> called it done).** The `stat` item too: `io.is_dir(p) -> Result(bool, IoErr)`
> exists over a 4-line `stat(2)` shim, and `resolve()` asks it instead of
> `len(io.list(p)) > 0`, so a directory redirects to its slash form whether or not
> it holds an index and a non-directory never does. `server/main.ty` stayed at 371
> code lines. The plan's Goal ("the two known wrong answers fixed") is met — and the
> half that needed a syscall cost 3% of the lines the half that needed a type did.
>
> **Correction from `plan.md` phase 5.** "Exactly where it started" was true for four
> phases and is no longer: phase 5 replaced the startup `--root` check's one wrong
> message with four accurate ones and `server/main.ty` ended at **380** code lines,
> +9. The plan's own final number is a *growth*, not a wash.

## The score against this file, 2026-07-26

The `Option`/`Result` plan was run *because* of this file, so it owes it a tally.
Final, after all five phases:

- **Of the 12 phase-7 items below: 2 closed, 10 untouched.** The `read_head`
  reimplementation (phase 3) and the missing `stat`/`is_dir` (phase 4).
- **The work created 6 new items of its own**, in the sections that follow — and
  **phase 5 closed 1 of them** (nothing in Tycho could create a directory), leaving
  5 open. Net across the whole plan: **−2 original, +5 new.**
- **`server/main.ty`: 371 code lines before, 380 after.** It got 9 lines *longer*.

That is not a flattering ratio and it is the real one: this file's verdict that
adopting `Option`/`Result` "would remove more friction than every other item
combined" **did not hold** — it removed the item it used as its own illustration,
and cost five to do it.

The second closure deserves its own line, because it is evidence *against* the
verdict rather than for it: `stat` was closed by 4 lines of C and 10 of Tycho with
no `Option`, no `Result` and no `or_return` involved in the fix, and it is the only
one of the two closures that changed what a client receives. The error-model work
made failure *sayable*; the syscall made an answer *right*. Those are different
kinds of win and this file was conflating them.

**Phase 5's own contribution to the tally, stated plainly.** It was acknowledged
out-of-Goal cleanup, run on a user directive after the Goal was met. It closed the
directory-creation gap with `io.make_dir` / `io.remove` (35 library lines,
`Result(bool, IoErr)` both, non-recursive on purpose) and deleted the `os.system`
shell-out from `corelib/test/io` — a corelib test no longer needs `/bin/sh` to set
up a syscall test. It found **no new language friction at all**: every construct it
needed already worked, including a new payload-free variant on a shared error enum
with no call site to update. And it cost the application **+9 lines**, all of them
in one startup check, to replace a single wrong message ("`--root` is empty or not
a directory", said for an empty directory, a plain file, a missing path and an
unreadable one alike) with four accurate ones. That is the phase-5 finding worth
carrying: `Result` made the distinctions *available*; **spending them costs one
branch per cause**, and nothing about the error model makes accuracy free at the
call site.

## Phase 7 — writing the server

- **Phase 7** — `send` is a builtin, and defining `fn send(conn, r, head_only, keep) -> int` is accepted **silently**; the collision surfaces only at the call site as `error: send(ch, v) takes a channel and a value`, which points at my call and describes a channel operation I never wrote. Nothing is reported at the definition, which is where the mistake is.
- ~~**Phase 7** — `httpd.read_request` collapses EOF, idle timeout and a malformed request line into `method == ""`, so a server that must answer `400` to garbage but hang up silently on a disconnect cannot use it at all; `server/main.ty` reimplements the read loop (`read_head`) purely to keep the raw buffer and recover that one bit.~~ **CLOSED, `plan.md` phase 3.** `read_request` returns `Result(Request, ReqErr)` with five named causes, and `read_request_capped(fd, cap)` returns the raw buffer as the second element of a tuple, so the cap decision (`431`) and the log line for an unparseable request no longer need a private read loop. `read_head`, `struct Head` and `term()` are deleted.
- **Phase 7** — `httpd.reason_phrase` is a closed `if`-chain and `httpd.response()` takes no reason, so status `431` goes on the wire as `HTTP/1.1 431 Status`. The workaround is to bypass the constructor and build `httpd.Response(431, "Request Header Fields Too Large", []string, []string, body)` positionally — it works across a package boundary, which is good, but it couples the caller to the struct's field order to set a string. **Bit a second time in `plan.md` phase 3:** answering `408` needed the identical bypass, so the workaround got factored into a local `phrased_response(status, reason)` — a private reimplementation of the constructor the corelib should have had.
- **Phase 7** — no `\r` escape in string literals (`\n \t \\ \"` only), so the most common byte pair in HTTP is a function call, `httpd.crlf()`. And `const TERM = httpd.crlf() + httpd.crlf()` is rejected — `error: const value must be a literal` — so the header terminator has to be a function that reallocates two strings on every loop iteration.
- **Phase 7** — no multi-line string literal and no line continuation, so the 10-line HTML error page is 10 consecutive `s += "..."` statements. (`+=` does exist; I wrote `s = s + ...` for half the file before checking, because nothing in the corelib I had read used it.)
- ~~**Phase 7** — there is no `stat` and no `is_dir`. `io.exists` answers by listing the parent directory, and the only directory test available is `len(io.list(p)) > 0`, which reports an **empty directory as a file** — `server/main.ty`'s `resolve()` ships a documented wrong answer (a 0-byte `200`) because the question cannot be asked.~~ **CLOSED, `plan.md` phase 4** — and it was never an error-model item, which is the finding worth keeping. Phase 2 turned the 0-byte `200` into a `404` (`io.read_bytes` → `Err(io.IsDir)`); phase 3 measured the residue and left it; phase 4 wrote the syscall: `io.is_dir(p) -> Result(bool, IoErr)` over a 4-line `iox_stat_kind` in `io_shim.c`, and `resolve()` now asks the kernel. `GET /emptydir` answers `301 -> /emptydir/` instead of `404` (measured live against a `296bbc2` binary), a non-directory never redirects, and `server/main.ty` did not gain a line — 371 before, 371 after. The `Result` is house style; the fix is `stat`. **The comparison this file should remember: 14 library lines added a question and moved a status code, where ~100 added return types and moved none.**
- **Phase 7** — `args()` includes `argv[0]` but `cli.parse` requires it removed, so every program opens with the same four-line copy loop; `examples/weblog/main.ty:129` has the identical loop with a comment explaining the same thing.
- **Phase 7** — `core:cli` cannot express `--root DIR`: values must be `=`-attached by design, so any CLI wanting the conventional Unix spelling hand-rolls its parser. That is 45 of this server's lines.
- **Phase 7** — `die()` is the language's only exit and it always exits **1**, so `--help` cannot be answered with status 0 through it. The fix was to thread a `help: bool` field through the config struct so `main` could return normally — a data-flow change to work around a missing `exit(0)`.
- **Phase 7** — `net.accept` hands back a bare fd and `core:net` exposes `getsockname` (`netx_port_of`) but no `getpeername`, so an access log cannot record the client address — the single most useful field in a real access log is unreachable from Tycho.
- **Phase 7** — scrubbing control bytes out of a hostile request target (`log_safe`) has to go `string` → `[]int` → `to_bytes` → `to_str`, because a `string` cannot be rebuilt in place and `bytes` cannot be indexed. The Phase 2 `to_str`/`to_bytes` sandwich, biting exactly as predicted, in the one function where a server must be paranoid.
- **Phase 7** — `parallel for` and `spawn` are the only concurrency shapes, and neither can express "hand this connection to whoever is free". One worker owns one connection for its whole life, so N workers is a hard cap of N concurrent connections; there is no way to write an event loop or a work queue over accepted fds without a channel of ints and a hand-rolled dispatcher.

## Phase 1 of the Option/Result plan — converting `core:net`

Found while converting `core:net`'s fallible TCP surface to `Result(T, net.NetErr)` and
rewriting `server/`, `corelib/httpd`, both corelib tests and both examples against it.

- **`Option`/`Result` phase 1** — there is no `unwrap_or`, `is_ok`, `is_some` or `is_err` anywhere: searched `docs/spec/16-builtins.md`, `docs/spec/12-aggregates.md` and all of `corelib/`, zero hits. Every caller whose own return type is not a `Result` hand-writes the same three-line `match` to collapse one — `server/main.ty`'s `nwrote`, and a separate copy each in `corelib/test/net/main.ty` and `corelib/test/httpd/main.ty`. It is the single largest cost of adopting `Result`, and it is a missing three-line library function.
- **`Option`/`Result` phase 1** — there are **no nested patterns**: `Err(net.Timeout)` is rejected with `error: expected ')'`, and so is `Err(C(n))` for a local enum. Worse, `Err(A)` where `A` is a nullary variant *parses* — as a **binding named `A`**, not as a pattern — and the mistake surfaces only if a second arm exists, as `error: duplicate Err arm`. So telling two failure causes apart always costs a second `match`, which is why `net.NetErr` was given payload-free variants: `if e == net.Timeout` is one line where `match e:` is three.
- **`Option`/`Result` phase 1** — `die()` is typed `void` and the compiler does not model it as diverging, so it cannot be the tail of a value-`match` arm: `srv := match net.listen(...): Ok(fd): fd / Err(e): die("cannot bind")` is rejected with `a value if/match branch must produce a value, not void`. The statement form needs a dummy `srv := 0` first, making the `Result` version **one line longer** than the `if srv < 0: die(...)` it replaced — the only call site in `server/` where the conversion cost a line.
- **`Option`/`Result` phase 1** — `tychoc` compiles every `.ty` in the entry file's directory, not just the entry file, so two unrelated scratch programs side by side collide with `'main' is already defined` pointing at the file you asked it to build. Nothing says the sibling file is involved; it cost four compile cycles to work out that the fix was `mkdir`.
- **`Option`/`Result` phase 1** — the FFI has no way for C to return a classification alongside a `bytes` payload, and `-> Result(T, E)` is not a documented `extern` return shape (`docs/spec/14-ffi.md:20-47` lists only scalars, sized ints, `string`/`Option(string)`, `bytes`, `[int]`/`[float]`, `ptr`, handles, and numeric `inout`). Making `net.read` say *why* it read nothing needed `status: inout int` threaded ahead of the two `bytes` out-params — which works, and is undocumented as the way to do this.

## Phase 2 of the Option/Result plan — the combinators, `io.read_bytes`, `httpd.read_request`

Found while adding `core:result` and converting the two genuinely ambiguous calls.

- **`Option`/`Result` phase 2** — a **qualified name written anywhere in a generic call's argument list does not resolve**. `result.unwrap_or(net.port_of(fd), -1)` fails with `error: package 'net' has no symbol 'net__port_of'`, `result.err_or(r, net.Failed)` with `error: unknown variable 'net'`, and `result.unwrap_or(r, httpd.bad_request())` with `error: package 'httpd' has no symbol 'httpd__bad_request'` — while the identical spellings are accepted in `==` and as arguments to concretely-typed parameters, and an *unqualified* local call inline is fine. So generic instantiation loses the package qualifier, and every corelib call site pays one extra line to bind the value to a local first. It is what stops `n := result.unwrap_or(io.read_bytes(p), empty)` — the whole point of a combinator — from being the one-liner it should be.
- **`Option`/`Result` phase 2** — **two error types in one function make `or_return` unavailable again**, and nothing says so until you try. `examples/corelib/httpd/main.ty`'s `round_trip` returns `Result(int, net.NetErr)` and seven `net.*` calls short-circuit through it beautifully; the one `httpd.read_request` call in the middle returns `Result(Request, httpd.ReqErr)` and has to be collapsed by hand, because there is no `map_err` and no conversion between error enums. A function that touches two packages' fallible calls gets `or_return` for whichever one it picked as its own error type and a manual collapse for the other.
- **`Option`/`Result` phase 2** — converting a call that a big block consumes costs an **indentation level**, not just lines: `server/main.ty`'s `serve_conn` went 60 → 71 code lines almost entirely because `match httpd.parse_request(raw)` has to wrap the whole request-handling body to bind `Ok(req)`. There is no `if let`, no early-return binding form, and `or_return` is unavailable (the enclosing function returns a served count) — so the only tool re-indents 30 lines.
- **`Option`/`Result` phase 2** — the FFI trick for classifying a `bytes` result (`status: inout int` threaded ahead of the two out-params) had to be reproduced verbatim in `corelib/io/io_shim.c` from `net_shim.c`, because it is still undocumented in `docs/spec/14-ffi.md`. Two shims now depend on an ABI detail written down only in each other's comments.
- **`Option`/`Result` phase 2** — `examples/webserver/main.ty` was left **uncompilable by phase 1** (`error: ordering compares two ints ...` on `if srv < 0`, against the converted `net.listen`): it imports `core:net` but was not in that commit's file list, and no gate builds it (`make ci` skips it — see the phase 0 note below), so nothing went red for a whole phase. Fixed here because it also consumes `io.read_bytes` and `httpd.read_request`.

## Phase 3 of the Option/Result plan — acting on the cause, and deleting `read_head`

Found while moving the cap and the raw buffer into `core:httpd` so `server/` could
stop reimplementing the read loop.

- **`Option`/`Result` phase 3** — **a tuple literal will not infer a `Result` element.** `return (Err(A), "partial")` from a function declared `-> (Result(int, E), string)` is rejected with `error: tuple element 1 needs a concrete value`, pointing at the `return`; the same `Err(A)` is accepted as a bare `return` from a `-> Result(int, E)` function, and accepted inside a tuple once it has been through a typed local (`out: Result(int, E) = Err(A)`) or a helper function. So the one shape the language provides for "return a value AND a classification" costs an extra local and an extra assignment per exit, purely because inference does not reach into the tuple. It is why `httpd.read_request_capped` builds its outcome in `out` instead of returning it directly.
- **`Option`/`Result` phase 3** — tuples are the right shape for this and **nothing pointed at them**. The obvious reading of "a function returns one value" sends you to an `inout` out-param or a wrapper struct; `docs/spec/03-types.md:193` and `docs/spec/02-grammar.md:137` do document 2–8 element tuples with destructuring (`got, raw := f()`), but no corelib function in the tree returns one, so there is no example to copy. Both alternatives were written and compiled before the tuple was found: `inout string` works (§11.3) and costs the caller a dummy `raw := ""`; a `struct` with a `Result` field also works (verified — a `Result` *can* be a struct field, even though it cannot be a tuple literal element). Three shapes, one documented, none demonstrated.
- **`Option`/`Result` phase 3** — a **payload-free error enum cannot say "how much"**, so the `431` decision had to become its own variant. `Err(TooLarge)` tells the caller the cap was hit but not what the cap was or how far past it the peer got, and adding that payload would break the `==` comparison the whole design rests on (no nested patterns — see phase 1). The five-variant enum is the right call here, but the pattern does not scale: every quantitative failure needs either a variant or a second return value.

## Phase 4 of the Option/Result plan — the missing syscall

Found while writing `io.is_dir` and its test.

- ~~**`Option`/`Result` phase 4** — **nothing in Tycho can create a directory.** Verified absent, not assumed: `docs/spec/16-builtins.md` §29.10 lists five filesystem/time builtins (`read_file`, `write_file`, `list_dir`, `clock`, `now`) and none of them makes a directory, and `mkdir`/`make_dir`/`create_dir` return zero hits across `corelib/`, `src/tychoc.c` and `runtime/`. There is no remove either. So `corelib/test/io` — the test for a `stat(2)` wrapper — has to build its empty directory with `os.system("rm -rf … && mkdir -p …")`: a corelib test depending on `/bin/sh` to set up a filesystem state the corelib itself cannot reach. The asymmetry is the finding: the library can now *classify* a directory but not *make* one.~~ **CLOSED, `plan.md` phase 5.** `io.make_dir(p)` (`mkdir(2)`, no `-p`) and `io.remove(p)` (`remove(3)`, one entry, **never recursive**) both return `Result(bool, IoErr)` where `Ok(true)` is "changed it" and `Ok(false)` is "already how you asked" — `make_dir` splits `EEXIST` into `Ok(false)` (already a directory: goal met) and `Err(Exists)` (a file is in the way: goal unreachable), which is exactly the ambiguity test this plan was built on. `corelib/test/io` no longer imports `core:os` and the `rm -rf && mkdir -p` line is gone. A non-empty directory is `Err(Failed)`, which is the property that keeps `io.remove` from being `rm -rf` behind a corelib name.
- **`Option`/`Result` phase 4** — `io.exists` and `io.is_dir` now answer overlapping questions by different means, and the cheaper one is the newer one: `exists` lists the whole parent directory (O(entries), and it cannot see a `.`/`..`-only leaf) where `is_dir` is one `stat`. `resolve()` ends up calling both on the same path. A `stat`-backed `exists` is the obvious follow-on and was refused on scope, but the general shape is worth recording — a missing syscall does not just block the question it names, it leaves *neighbouring* answers implemented the long way round.
- **`Option`/`Result` phase 4** — reordering two guards to make room for a new one silently changed a security answer: hoisting `hidden_segment(path.clean(rel))` above the `index.html` append made `GET /` return **403**, because for the root target `rel` is `""` and `path.clean("")` returns `"."` (`corelib/path/path.ty:104-105`), which `hidden_segment` reads as a dotfile. Nothing in the compiler or the corelib could have caught it — `clean("")` returning `"."` is documented POSIX behaviour and both spellings type-check identically. It was caught by the live matrix (`50-request flood 0/50 200`), which is the argument for keeping that matrix.

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

**Postscript, 2026-07-26.** That last sentence was acted on and it was wrong. See
"The score against this file" above: five phases of adoption closed **two** of this
file's twelve items, created six new ones (one since closed), spent **182 library
code lines**, and left `server/main.ty` **nine lines longer** than it started —
371 → 380 — with the growth coming from the phase that made a startup message
*accurate*, not from the four that made failures *sayable*. The narrower claim
survives intact and is the one to carry forward: **where a sentinel is genuinely ambiguous, `Result` is a clear win;
where it has exactly one meaning, converting it is line-neutral at the call site
and pure cost in the package.** The first sentence of this verdict — that Tycho
cannot yet make *failure* pleasant — still holds; what changed is that the reason
is ergonomics (`unwrap_or`, nested patterns, `map_err`, inference) rather than the
absence of `Option` and `Result` from the type system.

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
