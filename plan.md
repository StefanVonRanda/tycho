# Make failure pleasant: adopt `Option`/`Result` across the corelib

Follows the web-server plan (archived: `docs/internals/plan-webserver-DONE.md`, 8 phases,
head `eb42c3e`). That plan built `server/` — `tycho-httpd`, ~500 lines serving a real site
to a real browser at 79,712 req/s — and its deliverable was `FRICTION.md`, an honest
account of writing it. This plan acts on that file's headline finding.

## The finding, in one line

**The language has a good answer for fallible calls and the standard library does not use
it.** `Option` and `Result` are built-in enums (`docs/spec/03-types.md` §5.3.6) and
`or_return` is a real postfix operator that unwraps or short-circuits
(`docs/spec/10-statements.md:75`). **Exactly 1 of the corelib's 386 functions returns an
`Option`** — `io.read_line` (`corelib/io/io.ty:69`). Everything else signals failure with
a sentinel, and the sentinel differs per call:

| call | failure is… | also means |
|---|---|---|
| `net.accept` | a negative int | — |
| `net.write`, `httpd.write_response` | `-1` | — |
| `net.read` | empty `bytes` | EOF **and** idle timeout |
| `io.read_bytes` | empty `bytes` | empty file **and** missing file **and** a directory |
| `path.safe_join` | `""` | — |
| `httpd.parse_request` | `method == ""` | EOF **and** timeout **and** malformed |
| `net.set_read_timeout_ms` | `false` | — |

Six spellings of "it went wrong", three indistinguishable from success. **This is not
theoretical: two of `server/`'s real bugs came from exactly these collisions.** It cannot
tell a malformed request from a hangup, and `resolve()` ships a documented wrong answer
(a 0-byte `200` for an empty directory) because "is this a directory" cannot be asked.

`FRICTION.md`'s verdict: *"the error model is worse by choice rather than by capability …
adopting them across the IO surface would remove more friction than every other item
combined."*

## Goal

A corelib where a fallible call says so in its type, and where handling failure at the
call site is shorter than ignoring it. Done = `server/main.ty` is rewritten against the
new surface and is **measurably better** — fewer lines of error plumbing, no
sentinel-collision bugs left, and the two known wrong answers fixed.

## Anti-scope

Inherited from the previous plan and still binding:

- **A phase belongs here only if it serves the goal above.** Not "is inconsistent", not
  "could be nicer".
- Discovered defects that do not block go to `FRICTION.md` as one line, unfixed.
- **Stop condition: `server/main.ty` is better against the new API.** Not "every corelib
  function has been converted."
- **This plan must not become a 386-function mechanical sweep.** The previous-previous
  plan grew 6 → 44 phases that way. Phase 1 exists specifically to find out how much
  conversion is actually worth doing.

### GATE CONSTRAINT — user directive, 2026-07-26, still binding

**`make ci` and `make test` run AT MOST ONCE PER DAY**, across all agents. Violating it
means the gates get removed entirely. Verification is *running the thing you built* —
here that means compiling and running `server/` and the corelib's own test programs
directly with `./tychoc`, which is not a gate.

## The design question Phase 1 must answer

Do NOT convert anything before this is settled. Three real unknowns, and the wrong answer
to any of them makes the change worse than the sentinels:

1. **`Option` or `Result`?** `Option` says *something failed*; `Result(T, E)` says *why*.
   The collisions that actually bit us — EOF vs timeout, missing vs empty vs directory —
   are exactly the ones `Option` cannot fix. That argues `Result`. But `Result` needs an
   error type, and what `E` should be is itself a decision: a `string`, a corelib-wide
   error enum, or per-package enums. A wrong choice here is worse than sentinels, because
   it is a breaking API that must then be broken again.
2. **Does `or_return` actually compose at IO call sites?** It short-circuits by returning
   from the enclosing function — which requires that function to return a compatible type.
   A web-server request handler returns a `Response`, not a `Result`. **If `or_return` is
   unusable in the exact place the friction lives, the whole premise collapses** and the
   answer might instead be better sentinels or a different construct. Verify by writing
   real code, not by reading the spec.
3. **What does the call site actually look like?** The current pain is `if x < 0` /
   `if len(x) == 0` / `if s == ""` scattered through every IO path. If the replacement is
   `match` on every call, that is not obviously better. Write both and compare.

## Phases

- [x] **Phase 1 — PROBE: convert ONE package, rewrite the server against it, compare**
  - Pick `core:io` or `core:net` — whichever `server/main.ty` leans on hardest for
    fallible calls; read the server first and say which and why.
  - Answer the three design questions above **by writing code**: convert that one
    package's fallible surface, then rewrite the parts of `server/main.ty` that use it,
    and put the before and after side by side in the evidence.
  - **Measure, do not assert:** lines of error handling before vs after; whether the two
    known wrong answers (malformed-vs-hangup, empty-dir-vs-file) become expressible;
    whether `or_return` worked at the call sites or had to be abandoned.
  - **A legitimate outcome is "this is not better, do not roll it out."** If the converted
    call sites read worse, say so and stop — that finding is worth more than a
    386-function sweep done on a guess. The plan's premise is testable and this is the test.
  - Done when: one package converted, the server's corresponding code rewritten, a
    side-by-side comparison recorded, and a **recommendation with a reason** on whether to
    proceed, and in what form.

#### Phase 1 evidence — 2026-07-26

**Package picked: `core:net`, not `core:io`.** `server/main.ty` makes 4 fallible `net`
calls with **4 different sentinels** — `net.accept` a negative int (old `:379`),
`net.read` empty `bytes` (old `:267`), `net.set_read_timeout_ms` `false` (old `:383`),
`net.listen` a negative int (old `:492`) — against 2 fallible `io` calls
(`io.read_bytes` old `:356`, `io.list` old `:247`/`:489`). And `net.read` is the call
behind the plan's marquee bug: the EOF-vs-timeout collision is created inside
`netx_read` (`corelib/net/net_shim.c:217` before this phase: `if (n <= 0) { free(buf);
return; }` — one branch for EOF and error alike). `io`'s bug (empty dir vs file) is not
a sentinel problem at all; it needs a `stat` that does not exist, so it could not test
the premise.

##### The three design questions, answered by compiling

**1. `Option` or `Result`? → `Result(T, E)` with `E` a per-package, payload-free enum.**

`Option` was ruled out immediately: the whole point is *which* failure, and `None`
carries none. For `E`, all three candidates were tried on paper against the collisions
that actually bit `server/`:

| `E` | verdict |
|---|---|
| `string` | rejected — distinguishing a cause means comparing strings at every call site, and it allocates on a path a server takes per request |
| corelib-wide enum | rejected — `Eof`/`Timeout` are meaningless for `path.safe_join`, and every package importing one error hub makes a dependency hub out of it |
| **per-package enum** | **chosen** — variants match reality; verified to work across a package boundary |

Cross-package use is verified, not assumed: a scratch package declaring `enum ReadErr`
and returning `Result(bytes, ReadErr)` compiled and ran from a consumer that named the
type as `probenet.ReadErr` and the variants as `probenet.Eof` / `probenet.Failed(errno)`.

The variants are **payload-free on purpose** (`corelib/net/net.ty:75-80`): `Eof`,
`Timeout`, `Failed`. That is a direct consequence of question 3 below — a payload-free
enum compares with `==`, and `==` is the only way to test one cause in one line.

**2. Does `or_return` compose at IO call sites? → YES where the enclosing function
returns the same `Result`, and it is a HARD COMPILE ERROR where it does not.**

Not read off the spec. Compiled:

```
probe1.ty:14: error: or_return requires the enclosing function to return a Result, but it returns int
    14 |     b := probenet.read(fd, 10) or_return
```

So the answer is conditional, and the condition is the finding. The place it pays best
is exactly the place the friction lived — `httpd.write_response`, whose old body was six
lines of `-1` plumbing:

```tycho
# BEFORE                                       # AFTER
fn write_response(fd, r) -> int:               fn write_response(fd, r) -> Result(int, net.NetErr):
    n := net.write(fd, to_bytes(render_head(r)))    n := net.write(fd, to_bytes(render_head(r))) or_return
    if n < 0:                                       if len(r.body) == 0:
        return -1                                       return Ok(n)
    if len(r.body) == 0:                            m := net.write(fd, r.body) or_return
        return n                                    return Ok(n + m)
    m := net.write(fd, r.body)
    if m < 0:
        return -1
    return n + m
```

**10 code lines → 6.** Both `-1`s gone, both `if n < 0` gone. `server/main.ty`'s `emit`
then propagates for *free* — it is a tail call onto `write_response` with the same
error type, so it spends **zero** lines on plumbing and does not even need `or_return`
(`server/main.ty:293-307`).

Where the enclosing function owns a real return type, `or_return` is unavailable and a
`match` is the tool. `accept_loop` returns a served count, so:

```tycho
# BEFORE (14 code lines)                       # AFTER (14 code lines)
conn := net.accept(srv)                        match net.accept(srv):
if conn < 0:                                       Ok(conn):
    running = false                                    armed := net.set_read_timeout_ms(conn, cfg.idle_ms)
else:                                                  ...
    armed := net.set_read_timeout_ms(...)          Err(e):
    ...                                                running = false
```

**Line-neutral.** The gain is that `conn` does not exist on the failing path; the cost
is nothing. That is the typical case, not the good case and not the bad one.

The good case generalises: introduce **one** function boundary returning
`Result(_, net.NetErr)` and every call inside becomes a one-liner. `examples/corelib/net/main.ty`
is now `roundtrip() -> Result(int, net.NetErr)` with seven `or_return`s and *no error
handling in the body at all*, plus a three-line `match` in `main()`. `or_return` also
works inside a call argument, verified:
`to_str(net.read(conn, 64) or_return)`.

**3. What does the call site actually look like? → mostly the same; better when a
boundary can be introduced, worse in two named spots.**

The honest bad news, both measured:

- **There is no `unwrap_or` / `is_ok` / `is_some` anywhere.** Searched
  `docs/spec/16-builtins.md`, `docs/spec/12-aggregates.md` and all of `corelib/` —
  zero hits. Any caller that must collapse a `Result` to a value hand-writes a
  three-line `match`. `server/main.ty:309-317` (`nwrote`) is that helper, written once
  and used five times; `corelib/test/httpd/main.ty:18-30` and
  `corelib/test/net/main.ty:20-38` each needed their own copy. **This is the single
  largest real cost of the conversion.**
- **`die()` cannot be the tail of a value-`match` arm.** The natural form
  `srv := match net.listen(...): Ok(fd): fd / Err(e): die("...")` is rejected —
  `error: a value if/match branch must produce a value, not void`
  — because `die` is typed `void` and the compiler does not model it as diverging. The
  statement form needs a dummy `srv := 0`, which is **one line worse** than the
  sentinel version it replaced (`server/main.ty:537-549`).
- **There are no nested patterns.** `Err(net.Timeout)` does not parse
  (`error: expected ')'`), and neither does `Err(C(n))` for a local enum. Worse,
  `Err(A)` where `A` is a nullary variant parses as a **binding named `A`**, not a
  pattern — surfaced only because a second arm then gives `error: duplicate Err arm`.
  So distinguishing causes always costs either a second `match` or `==` comparisons.
  **This is why `NetErr` has no payload:** `if e == net.Timeout` is one line;
  `match e:` is three.

##### Measurements

Non-comment, non-blank code lines, `git show HEAD:<f>` vs working tree:

| unit | before | after | Δ |
|---|---|---|---|
| `httpd.write_response` | 10 | 6 | **−4** (`or_return`) |
| `server/`'s `accept_loop` | 14 | 14 | 0 |
| `server/`'s `read_head` | 15 | 19 | +4 (buys the `Head` struct + the reason) |
| `examples/corelib/net/main.ty` | 14 | 19 | +5 (the `roundtrip` boundary + top `match`) |
| `server/main.ty` whole file | 371 | 381 | **+10 (+2.7%)** |
| `corelib/net/net.ty` | 34 | 64 | **+30** |

The `+30` in `net.ty` is where the honesty matters: ~10 of it is the `NetErr` enum and
`read`'s classification (real information), and ~20 is six one-line sentinel forwarders
becoming four-to-five-line `Result` constructors that add **no** information —
`net.listen`, `net.connect`, `net.accept`, `net.port_of`, `net.write` each had exactly
one failure with one meaning.

**Both known wrong answers, expressibility:**

- **malformed-vs-hangup: now expressible AND measured correct.** `corelib/test/net/main.ty`
  asserts both halves and the golden records them: `eof=Eof` (peer closed cleanly) and
  `tmo=Timeout` (armed 50 ms deadline expired). These were the *same empty `bytes`*
  before this phase. `server/main.ty`'s `read_head` now returns
  `Head{raw, why: net.NetErr}` so the connection loop can ask
  (`server/main.ty:272-291`). Acting on it — a `408`, a different log line — is Phase 3.
- **empty-dir-vs-file: NOT addressed, and a `Result` cannot address it.** `io.list`
  returning `[]` for both an empty directory and a non-directory is a missing *syscall*
  (`stat`), not a missing return type. Phase 3's own text already said so.

**`set_read_timeout_ms` and all of UDP were deliberately left on sentinels**
(`corelib/net/net.ty:46-56`, `:57-66`). `set_read_timeout_ms`'s `false` has one cause
and collides with nothing, so a `Result` would add a `match` and no information.
`udp_read` is worse than a no-op: **a zero-length datagram is legal**, so its empty
result is a real success value, and changing the return type alone would move the
ambiguity rather than remove it.

##### Verify — commands run, real output

```
$ ./tychoc server/main.ty -o …/tycho-httpd
built …/tycho-httpd
$ ./tychoc corelib/test/net/main.ty   … | diff corelib/test/net.out -      → net golden OK
$ ./tychoc corelib/test/httpd/main.ty … | diff corelib/test/httpd.out -    → httpd golden OK   (UNCHANGED)
$ ./tychoc corelib/test/io/main.ty    … | diff corelib/test/io.out -       → io golden OK      (UNCHANGED)
$ ./tychoc examples/corelib/net/main.ty   … | diff examples/corelib/net.out -    → ex net golden OK
$ ./tychoc examples/corelib/httpd/main.ty … | diff examples/corelib/httpd.out -  → ex httpd golden OK
```

`corelib/test/net.out` gained exactly the three new assertion lines
(`eof=Eof`, `armed=true`, `tmo=Timeout`); the two example goldens gained one
byte-count line each. Nothing else moved. **`make ci` / `make test` were NOT run** (gate
constraint).

The rewritten server, served live on `127.0.0.1:18099`, `--workers 4`, hit with curl:

```
GET /            200 2659 text/html; charset=utf-8
GET /style.css   200 1726 text/css; charset=utf-8
HEAD /           200 0
GET /nope.html   404
GET /../../etc/passwd (--path-as-is)  403
POST /           405
printf 'GARBAGE\r\n\r\n' | nc     → HTTP/1.1 400 Bad Request     (not a silent close)
printf 'GET / HTTP/1.1\r\n' | nc  → hangup handled, no crash
keep-alive, 3 requests one connection → 200 200 200
50-request flood → 50 done, exit 0
stderr: w1/w2/w3/w4 GET / 200 2659 0.19ms   (all four workers live)
```

##### RECOMMENDATION — proceed to Phase 2, but **narrowed**, and add the missing helper first

**The premise holds, but only for half of what it claimed.** Precisely:

1. **Where a sentinel is genuinely AMBIGUOUS, `Result` is a clear win and should be
   rolled out.** `net.read` went from one value meaning three things to three variants,
   and the test measures them as distinct. This is the part worth doing.
2. **Where a sentinel has exactly ONE meaning, conversion is not worth it.** Five of
   `core:net`'s six converted calls fell in this bucket: `accept_loop` came out
   line-for-line identical, and `net.ty` grew ~20 lines to say nothing new. The
   type-safety gain (an unopened fd is unreachable) is real but small, and it is not
   what `FRICTION.md` was complaining about.
3. **`or_return` is the whole payoff and it is conditional.** It paid −40% on
   `write_response` and erased `emit`'s plumbing entirely, but only because those
   functions could be made to return `Result(_, net.NetErr)`. In a `main()` or a handler
   returning a `Response` it is a compile error, and the fallback costs a hand-written
   three-line unwrap **because the corelib has no `unwrap_or`**.

So Phase 2 should NOT be "convert `io`, `net`, `httpd`, `path` uniformly". It should be
"add the missing combinator, then convert the three genuinely ambiguous calls and stop".
Phases 2 and 3 are rewritten below to match.

- [x] **Phase 2 — NARROWED by Phase 1: add the missing combinators, then convert only the AMBIGUOUS calls**
  - **Rewritten 2026-07-26 from Phase 1's finding.** The original text said "roll out to
    the fallible IO surface" in package order. Phase 1 measured that converting an
    *unambiguous* sentinel is line-neutral at the call site and costs ~4 lines per
    function in the package, so a uniform roll-out is now explicitly out of scope.
  - **First, close the ergonomic gap Phase 1 found**, because it taxes every conversion
    after it: there is no `unwrap_or` / `is_ok` / `is_some` anywhere in the builtins or
    the corelib, so `server/main.ty`, `corelib/test/net`, `corelib/test/httpd` each
    hand-rolled the same three-line collapse. Decide where it lives (a builtin, or a
    `core:result` package) and land it with its consumers. Judge the design against the
    fact that Tycho has **no nested patterns**, so it must work with `==` on
    payload-free variants.
  - **Then convert exactly these, and nothing else** — the calls whose sentinel means
    more than one thing:
    - `io.read_bytes` — empty `bytes` means missing **and** empty **and** a directory.
    - `httpd.parse_request` / `httpd.read_request` — `method == ""` means EOF **and**
      timeout **and** malformed. Phase 1 left `read_request` collapsing all three on
      purpose (`corelib/httpd/httpd.ty`, the note above the `match`); the information
      now reaches it, so this is where it gets used.
    - `io.list` — only if a `stat`-based answer lands in Phase 3; a `Result` alone
      cannot separate an empty directory from a non-directory.
  - **Explicitly NOT converted, with the reason recorded:** `path.safe_join` (`""` is
    fail-closed with one meaning — Phase 1 measured this class as line-neutral),
    `io.write` / `io.append` / `io.write_lines` (`false`, one cause), `net.udp_*`
    (a zero-length datagram makes the empty result a real success value),
    `net.set_read_timeout_ms` (already done: left as `bool`).
  - Every consumer must land with it: `examples/*`, `corelib/test/*`, `server/`, goldens.

#### Phase 2 evidence — 2026-07-26

**(a) The combinators live in a new corelib package, `core:result`
(`corelib/result/result.ty`) — not a builtin.**

The absence was verified first, not assumed: `grep -rn 'unwrap_or\|is_ok\|is_some\|is_err'`
over `--include='*.ty' --include='*.md' --include='*.c'` across the whole tree returns
hits only in `FRICTION.md`, `plan.md`, and four `tests/*.ty` programs that define a *local*
`fn unwrap` of their own. Nothing in `corelib/`, nothing in `docs/spec/16-builtins.md`.

A package rather than a builtin because **generics already carry it**: the whole surface is
`fn unwrap_or(r: Result($T, $E), fallback: $T) -> $T` and five siblings, 25 code lines of
pure Tycho, no shim and no change to `src/tychoc.c`. A builtin would have meant editing a
14k-line C compiler to add what the language can already express — the larger change and
the larger risk, for the same call-site syntax. Proven across a package boundary before
anything was converted: a scratch package's `unwrap_or` instantiated over `int`, `string`,
`bytes`, a struct, a locally-declared enum error, and `io.IoErr` from another package.

The surface, and why each one earns its place:

| function | why |
|---|---|
| `unwrap_or(r, fallback)` | the workhorse — the four-line `match` that existed in three copies |
| `is_ok` / `is_err` | when only success matters and the payload does not |
| `err_or(r, fallback)` | **which** failure, as a value `==` can test — required because Tycho has no nested patterns, and the reason the corelib's error enums are payload-free |
| `some_or` / `is_some` | the same for `Option`, the half `io.read_line` already used |

**The three hand-rolled duplicates are gone** — `server/main.ty`'s `nwrote` (4 lines →
deleted, its five call sites now `result.unwrap_or(...)` inline), and
`corelib/test/net`'s `fd_of`/`data_of` plus `corelib/test/httpd`'s `n_of`/`data_of`
(4 lines → 2 each, now one-line forwarders onto the combinator).

**A NEW compiler limitation was measured, and it taxes every call site by one line.** A
qualified name written anywhere in a *generic* call's argument list does not resolve:

```
result.unwrap_or(net.port_of(fd), -1)      error: package 'net' has no symbol 'net__port_of'
result.err_or(r, net.Failed)               error: unknown variable 'net'
result.unwrap_or(r, httpd.bad_request())   error: package 'httpd' has no symbol 'httpd__bad_request'
```

All three compile when the value is bound to a local first, and all three spellings are
accepted in `==` and as arguments to concretely-typed parameters — so it is generic
instantiation losing the qualifier. An **unqualified** call is fine inline
(`result.unwrap_or(emit(conn, r, false, false), -1)` compiles, which is why `server/`'s
five sites are one-liners). Net effect: `unwrap_or` costs 1 line for a local call and 2
for a corelib call, against the 4 a hand-written `match` costs. Recorded in
`FRICTION.md` and in the header of `corelib/result/result.ty`.

**(b) Converted, exactly the two named ambiguous calls.**

`io.read_bytes -> Result(bytes, io.IoErr)` — `NotFound` / `IsDir` / `Failed`. The
classification is real, not invented at the Tycho level: `iox_read_file` now takes a
`status: inout int` ahead of the two `bytes` out-params (the shape Phase 1 established in
`net_shim.c`) and maps `errno` — `ENOENT`/`ENOTDIR` → missing, `EISDIR` → directory, both
at `fopen` and at the first `fread`, because glibc lets `fopen("/tmp", "rb")` succeed and
fails the read. **An empty file is `Ok` with zero bytes** — the success value that used to
be indistinguishable from both failures.

`httpd.parse_request` / `httpd.read_request -> Result(Request, httpd.ReqErr)` —
`Malformed` / `Closed` / `Timeout` / `Failed`. `read_request` maps the transport cause
through `cause_of(net.NetErr)`, and the rule is: a complete header terminator means parse
it (so a truncated *body* still yields a `Request`, unchanged behaviour), no terminator
plus a failed read means report the transport cause. Phase 1's deliberate collapse in this
function is gone.

`io.list` — **SKIPPED, as the phase text permitted.** No `stat` landed, and a `Result`
alone cannot separate an empty directory from a non-directory: `[]` is a legitimate
success value for an empty directory. Converting it would move the ambiguity into an `Err`
that has to lie about one of the two cases.

**(c) Not converted, and I do not disagree with any of it.** `path.safe_join` (`""` is
fail-closed with one meaning), `io.write`/`append`/`write_lines` (`false`, one cause),
`net.udp_*` (a zero-length datagram is a real success value), `net.set_read_timeout_ms`
(already settled as `bool`). Phase 1 measured this class as line-neutral at the call site
and ~4 lines per function of pure cost in the package; nothing found in Phase 2 argues
against that. One adjacent temptation was **refused on scope**: `io.read`'s `""` is
ambiguous the same way `read_bytes`' empty `bytes` was, and the shim classification now
sitting in `io_shim.c` would convert it almost for free — but `io.read` is used by
`examples/site`, `examples/weblog`, `examples/webserver` and `examples/fetch`, none of
which this phase names, and the plan forbids a uniform sweep. Recorded here as a candidate,
not done.

##### Measurements

Non-comment, non-blank code lines, `git show HEAD:<f>` vs working tree:

| unit | before | after | Δ |
|---|---|---|---|
| `server/main.ty` — `nwrote` helper | 4 | 0 | **−4** (deleted; 5 call sites inline) |
| `corelib/test/net` — `fd_of` + `data_of` | 8 | 4 | **−4** |
| `corelib/test/httpd` — `n_of` + `data_of` | 8 | 4 | **−4** |
| `io.read_bytes` | 2 | 10 | +8 (the classification + 3 named status codes) |
| `httpd.parse_request` | 22 | 22 | **0** (two `bad_request()` → two `Err(Malformed)`) |
| `httpd.read_request` | 23 | 30 | +7 (the cause plumbing + `cause_of`) |
| `server/`'s `serve_conn` | 60 | 71 | +11 (a `match` level, and the read_bytes → 404 arm) |
| `server/main.ty` whole file | 381 | 390 | +9 (+2.4%) |
| `corelib/io/io.ty` | 51 | 66 | +15 |
| `corelib/httpd/httpd.ty` | 218 | 237 | +19 |
| `corelib/test/net/main.ty` | 56 | 53 | **−3** |
| `corelib/result/result.ty` | — | 25 | new (the whole package) |

**Did the combinator shorten the sites `or_return` could not reach? Yes, measurably: 12
lines of hand-written `match` became 4 lines of forwarder plus 6 inline one-liners, for a
25-line library paid once.** The honest caveat is the +11 on `serve_conn`: converting
`parse_request` cost an indentation level there, because `match` is the only way to bind
`Ok(req)` and the whole request-handling block lives inside it. That is the same shape
Phase 1 measured — a `Result` pays when a function boundary can absorb it and costs a level
when it cannot.

**malformed vs EOF vs timeout, proven in a golden** (`corelib/test/httpd.out`, four new
lines; each of these was `method == ""` before this phase):

```
bad_why        = Malformed      # parse_request on "garbage\r\n\r\n"
garbage_why    = Malformed      # a complete head off a real socket: this one deserves 400
hangup_why     = Closed         # peer connected and closed without a byte
partial_why    = Closed         # "GET / HTTP/1.1\r\n" then closed -- NOT Malformed
timeout_why    = Timeout        # 200 ms SO_RCVTIMEO expired with the peer silent
```

**The empty-file / missing / directory split, proven in a golden**
(`corelib/test/io.out`, one line became four):

```
read_bytes len=5 why=Ok        read_missing len=0 why=NotFound
read_empty  len=0 why=Ok       read_dir     len=0 why=IsDir
```

##### One of Phase 3's two wrong answers fell out of this, measured before and after

`server/`'s documented 0-byte `200` for an empty directory **is fixed**, without a `stat`.
`resolve()` is unchanged and still cannot ask "is this a directory", but the read that
follows it now answers `Err(io.IsDir)`, and `serve_conn` turns that into a `404`. Both
binaries, same document root containing an empty `emptydir/`:

```
$ ./tychoc <git archive HEAD>/server/main.ty ...   # pre-phase-2 binary
HEAD-of-repo  GET /emptydir -> 200 0 bytes         # log: w1 GET /emptydir 200 0 0.226ms
$ ./tychoc server/main.ty ...                      # this phase
              GET /emptydir -> 404 621 bytes       # log: w3 GET /emptydir 404 621 0.195ms
```

What is still wrong is narrower: a directory *with* content gets the correct `301` to
`<path>/` while an empty one gets a `404`, because the `len(io.list(p)) > 0` test still
cannot see it. Phase 3 is rewritten below to match.

##### Verify — commands run, real output

Gate constraint honoured: **`make ci` / `make test` / `make corelib` were NOT run.** Every
program was compiled and run directly with `./tychoc`, exactly as Phase 1 did.

A compile sweep over every program in the tree that imports `core:io`, `core:net`,
`core:httpd` or `core:result` — 13 entry points, all green:

```
ok corelib/test/{httpd,io,net,result}/main.ty
ok examples/corelib/{httpd,io,net,result}/main.ty
ok examples/{fetch,site,weblog,webserver}/main.ty
ok server/main.ty
```

Goldens (run, then `cmp` against the recorded file):

```
CHANGED  corelib/test/io.out          1 line -> 4   (the three-way split above)
CHANGED  corelib/test/httpd.out       +4 lines, 1 changed (the four causes above)
CHANGED  examples/corelib/httpd.out   +1 line  (garbage= true)
NEW      corelib/test/result.out      16 lines
NEW      examples/corelib/result.out   8 lines
same     corelib/test/net.out
same     examples/corelib/{io,net}.out
same     examples/webserver/expected.out
```

The live server, `127.0.0.1:18099`, `--workers 4`, document root a copy of `server/www`
with an added empty directory:

```
GET /            200 2659 text/html; charset=utf-8
GET /style.css   200 1726 text/css; charset=utf-8
HEAD /           200 0
GET /nope.html   404
GET /../../etc/passwd (--path-as-is)  403
POST /           405
GET /emptydir    404 621                      <- was 200 0 before this phase
printf 'GARBAGE\r\n\r\n'          | nc  -> HTTP/1.1 400 Bad Request
printf 'GET / HTTP/1.1\r\n'       | nc  -> HTTP/1.1 400 Bad Request  (truncated head; see below)
Content-Length: 0x10                    -> HTTP/1.1 400 Bad Request  (smuggling primitive)
keep-alive, 3 requests one connection   -> 200 200 200
50-request flood                        -> 50/50 200
stderr: w1 w2 w3 w4 present (all four workers served)
```

A note on that truncated-head line, because it is the one place the new information is
*not* acted on yet: `server/` still uses its own `read_head`, whose 400/431 branch fires
for any buffer with no terminator, so a peer that sends half a request line and hangs up
gets a `400`. `httpd.read_request` now calls that same case `Closed` (proven in the golden
above) — acting on it is Phase 3, which is what Phase 3 is now scoped to.

##### Discovered, out of this phase's scope

- **`examples/webserver/main.ty` did not compile at HEAD.** Phase 1 converted `core:net`
  without updating it (it is not in `556119e`'s file list), so
  `./tychoc examples/webserver/main.ty` failed with `examples/webserver/main.ty:193:
  error: ordering compares two ints ...` on `if srv < 0`. It is a direct consumer of
  `io.read_bytes` and `httpd.read_request`, so it was fixed here rather than left broken —
  ~10 lines of `net` Result plumbing on top of this phase's own changes. Its golden
  (`examples/webserver/expected.out`) now matches for the first time since Phase 1.

- [ ] **Phase 3 — REWRITTEN by Phase 2: act on the reasons, and drop `read_head`**
  - **Both halves have moved.** Phase 1 made the transport reason *expressible*; Phase 2
    put it in `httpd.read_request` (`Malformed` / `Closed` / `Timeout` / `Failed`, all four
    measured in `corelib/test/httpd.out`) and, as a side effect of `io.read_bytes`
    returning a `Result`, already turned `server/`'s empty-directory 0-byte `200` into a
    `404` (measured before and after against a binary built from `556119e`).
  - What is left, and it is now one coherent job: **`server/main.ty` should delete
    `read_head` and its `Head` struct and call `httpd.read_request` instead.** That is the
    workaround `FRICTION.md` phase 7 exists to complain about — 15 lines reimplementing the
    corelib because the corelib could not say which failure it was. The corelib can now say
    it, so:
    - `Err(httpd.Timeout)` → answer `408 Request Timeout` (or close silently, but the
      *choice* becomes available) instead of the current unconditional close.
    - `Err(httpd.Closed)` → close silently, no response, no log line.
    - `Err(httpd.Malformed)` → `400`, which is what it does today.
    - The one thing `read_head` still has that `read_request` does not is the **raw
      buffer**, used for two things: the `431` decision (`len(raw) >= MAX_HEAD`, a 16 KiB
      cap against a peer that grows a string forever) and `first_line(raw)` in the log line
      for a request that would not parse. `read_request`'s own cap is 1 MiB and it does not
      hand the buffer back, so this phase must decide: a configurable cap plus a raw-head
      accessor on the corelib side, or keep a thin local read for that one case. **Measure
      both; do not assume the corelib call is automatically better** — the same standard
      Phase 1 and 2 were held to.
  - Still genuinely absent, and still not a return-type problem: `resolve()` cannot ask
    "is this a directory". A directory with content gets a correct `301`; an empty one now
    gets a `404` instead of a redirect. Closing that needs a real `io.stat` / `io.is_dir`
    shim call — **independent of the `Option`/`Result` work, and a legitimate thing to
    leave undone** if the phase's own measurements say the 404 is good enough.
  - Done when `server/main.ty` no longer reimplements the read loop, the `408`/silent-close
    distinction is exercised against a live server with captured output, and every golden
    still matches.

## Out of scope

- The rest of `FRICTION.md`. It is a deliverable, not a queue: `\r` escapes, multi-line
  strings, `cli` argument spelling, `die()` always exiting 1, `getpeername`, `bytes`
  having no operators — all real, none of them this plan's subject. They stay recorded.
- `compiler/tychoc0.ty` — frozen 2026-07-26, diverging, unmaintained.
