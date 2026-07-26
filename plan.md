# Close out `FRICTION.md`

Follows the `Option`/`Result` plan (archived: `docs/internals/plan-option-result-DONE.md`,
5 phases, head `8aac642`), which closed **2 of `FRICTION.md`'s 12 phase-7 items** and created
6 new ones. That ratio is the reason this plan exists. `FRICTION.md` is a 235-line honest
account of writing `server/` and then converting the corelib; it has never been worked as a
list. This plan works it as a list.

## Goal

**Every open item in `FRICTION.md` is either fixed or explicitly refused with a reason.**
`FRICTION.md`'s own self-score section (`FRICTION.md:79`) is the scoreboard: it currently
reads "2 closed, 10 untouched" for phase 7 plus 6 created items and the earlier-phase list
below them. Done = that section is re-scored against the tree and no item is in the
"untouched, unexplained" state.

Fixed means: the friction is gone at the call site that complained, proven by compiling and
running the thing that used to be awkward. Refused means: one line in `FRICTION.md` saying
why, with a cost estimate — not silence.

## Anti-scope

- **One item, one fix. No redesigns.** `bytes` gets operators, not a new buffer type.
  `core:cli` learns `--root DIR`, it does not become `clap`.
- **No item is fixed by moving it.** Deleting a complaint from `FRICTION.md` without a code
  change or a written refusal is the one failure mode this plan can have.
- **`compiler/tychoc0.ty` is frozen** (2026-07-26, diverging, unmaintained). The live
  compiler is `src/tychoc.c`. Never touch tychoc0 to satisfy a phase.
- Discovered defects outside the phase's item go to `FRICTION.md` as one line, unfixed —
  the same rule the last plan ran under, and it held for 5 phases.
- **This plan must not grow beyond its item list.** The item list is `FRICTION.md`. When the
  file is re-scored, the plan is done.

### GATE CONSTRAINT — user directive, 2026-07-26, still binding

**`make ci` and `make test` run AT MOST ONCE PER DAY, across all agents.** Violating it
means the gates get removed. Verification is running the thing you built: compiling the
corelib and all 13 entry points directly with `./tychoc`, diffing the 9 goldens, and running
`server/` live. The archived plan's phases 1–5 each did exactly this and each caught real
bugs that way (`plan-option-result-DONE.md`, phase 4's `403`-on-`/` regression).

**Compiler phases are the exception worth naming:** a change to `src/tychoc.c` reaches every
program in the tree. A compiler phase may spend the day's single `make ci` if it has not been
spent, and **must record in its evidence whether it did or not**. If it is already spent, the
phase compiles the corelib, all 13 entry points and all goldens by hand and says so.

## Pre-flight

- Worst case: a `src/tychoc.c` change miscompiles something no golden covers. `FRICTION.md`
  already records that `examples/webserver` went uncompilable for a whole phase because no
  gate builds it — so the by-hand entry-point sweep is not optional, it is the mitigation.
- Reversibility: every phase is one commit on `main`; `git revert` is the escape hatch. No
  destructive operations, no data migration, nothing outside the repo.
- Verified: the item list is real and quoted — `FRICTION.md:118-131` (phase 7, 10 open),
  `:133-161` (the 6 created by the last plan), `:168-169` (phase 4's two), `:219-235`
  (earlier phases). The two unfixable-in-Tycho C-level defects at `:171-179` are already
  fixed in the shims.
- Assuming: the compiler items are individually small (a lexer escape, a resolution bug, a
  divergence flag). **Not verified** — none has been costed by reading `src/tychoc.c`.
  Risk if wrong: a phase that looks like 20 lines is 300, and the honest response is to
  refuse it in `FRICTION.md` with the real number rather than half-build it.

## Phases

Ordered by leverage: the items that tax every other item come first. A phase that finds its
item is not worth fixing **refuses it in `FRICTION.md` with the cost** and still ticks — that
is a completed phase under this plan's Goal, and it is not a failure.

- [x] **Phase 1 — the resolution bug that taxes every generic call site**
  - Item: `FRICTION.md:148` — a qualified name anywhere in a *generic* call's argument list
    does not resolve. `result.unwrap_or(net.port_of(fd), -1)` → `error: package 'net' has no
    symbol 'net__port_of'`; `result.err_or(r, net.Failed)` → `error: unknown variable 'net'`.
    The identical spellings work in `==` and as arguments to concretely-typed parameters, so
    generic instantiation is losing the package qualifier.
  - This is first because it is a **bug, not a gap**, and it is the one that stops
    `core:result` — the library the last plan added — from being usable as a one-liner.
  - Scope: `src/tychoc.c` generic instantiation / name resolution. Plus a regression test.
  - Done when: all three failing spellings from `FRICTION.md:148` compile and run, and the
    corelib call sites that bound a local purely to work around it are un-worked-around
    (search for them; the last plan created several).
  - Verify: a scratch program with all three spellings compiles and prints correct values;
    corelib + 13 entry points compile; 9 goldens match; `server/` live matrix unchanged.

  #### Phase 1 — DONE. Evidence

  ##### Root cause: resolution is not single-pass, and the rewrite was not idempotent

  It is **not** generic instantiation losing the qualifier. `instantiate_generic`
  (`src/tychoc.c:6895`) resolves **every argument once** to infer `$T`:

  ```c
  for (int j = 0; j < gt->nparams; j++) {
      Type at_ = resolve_expr(e->args[j]);          /* the concrete argument type */
  ```

  and then, after `e->sval` is rewritten to the instance name, the ordinary
  concrete-signature loop resolves **the same nodes again** against the now-bound
  parameter types (`src/tychoc.c:5565`, `resolve_exp(e->args[i], s->params[i])`, whose
  job is grounding a bare `None`/`[]` argument). Both rewrites that turn a written
  `pkg.x` into a mangled name mutated the node in place and were not idempotent:

  1. `src/tychoc.c:5090-5102` (E_CALL, `e->qual` set) rewrote `e->sval` to
     `pkg_prefix_for(qual) + sval` and **kept `e->qual`**. Second pass:
     `q = "net__" + "net__port_of"`, which no lookup finds, so it died as
     `package 'net' has no symbol 'net__port_of'` — the **doubled prefix in the error
     text was the whole tell**, and it named the mangled name, not the written one.
  2. `src/tychoc.c:4827-4849` (E_FIELD `pkg.Variant`) reinterpreted the node as an
     `E_CALL` enum constructor but **left `e->lhs` pointing at the package ident**.
     Second pass entered the E_CALL arm, hit the call-on-expression branch
     (`if (e->lhs)`, `src/tychoc.c:5065`) and resolved `net` as a value →
     `unknown variable 'net'`.

  So the bug was never about generics or about variants. **Any** double-resolve site
  reproduced it: `Box(net.Failed)` on a generic *struct* literal fails identically
  (`src/tychoc.c:5173` then `:5190` — infer, then re-resolve), measured before the fix:

  ```
  r5/main.ty:8: error: unknown variable 'net'
      8 |     b := Box(net.Failed)
  ```

  Fix: one `int pkg_done` latch on `Expr` (`src/tychoc.c:1400`), set by every successful
  package rewrite and checked before doing one, plus `e->lhs = NULL` on the
  `pkg.Variant` reinterpretation. **`src/tychoc.c`: +22 / −5 lines, of which 4 are code**
  (the latch field, the `if (e->pkg_done)` guard, the two `e->lhs = NULL; e->pkg_done = 1;`
  assignments); the rest is the comment explaining why resolution runs twice. The phase's
  Pre-flight assumption that the compiler items are individually small **held here**.

  ##### The three spellings from `FRICTION.md:148`, before and after

  Each in its own directory (`FRICTION.md:141` — `tychoc` compiles every `.ty` beside the
  entry file). BEFORE is `tychoc` built from `git show HEAD:src/tychoc.c`:

  ```
  BEFORE (HEAD)                                   AFTER (this commit)
  result.unwrap_or(net.port_of(fd), -1)
    error: package 'net' has no symbol            port_ok = true
           'net__port_of'                         port_er = -1
  result.err_or(r, net.Failed)
    error: unknown variable 'net'                 err_or  = true / err_ok = true
  result.unwrap_or(r, httpd.bad_request())
    error: package 'httpd' has no symbol          req_ok  = [GET]
           'httpd__bad_request'                   req_bad = []
  ```

  Full AFTER run of the scratch program (all three inline, no bound locals):

  ```
  listen  = true   port_ok = true   port_er = -1
  err_or  = true   err_ok  = true   err_tmo = false
  req_ok  = [GET]  req_bad = []
  ```

  and the same program on the pre-fix compiler stops at its first line:
  `error: package 'net' has no symbol 'net__listen'`.

  ##### Regression test

  `tests/pkg/generic_qual_arg/` + `tests/pkg/generic_qual_arg.out`, the convention
  `tests/pkg/generic/` already established for qualified-generic resolution (a local `lib`
  package, no corelib dependency, driven by `tests/run.sh` and therefore by `make test`).
  Ten assertions: all three shapes as generic-**function** arguments, plus the generic
  **struct** literal and generic **enum** payload paths that resolve arguments twice for the
  same reason. **Reddened deliberately on the pre-fix compiler:**

  ```
  $ /tmp/prefix-tychoc tests/pkg/generic_qual_arg/main.ty
  tests/pkg/generic_qual_arg/main.ty:48: error: package 'lib' has no symbol 'lib__port_of'
  $ ./tychoc tests/pkg/generic_qual_arg/main.ty -o /tmp/gqa && /tmp/gqa | diff tests/pkg/generic_qual_arg.out -
  GOLDEN OK
  ```

  ##### Workaround locals removed (18 code lines, 7 files)

  Every site the last plan created purely for this bug, found by grepping for the
  `bind first` / `cannot be a generic call's argument` comments it left:

  | file | what went |
  |---|---|
  | `server/main.ty:282` | `isdir := io.is_dir(fsp)` → inline; 380 → **378** code lines |
  | `server/main.ty:588-591` | `pr := net.port_of(srv)` + the 3-line CAVEAT comment |
  | `corelib/test/io/main.ty` | **three** copies of `d := io.Failed` (124 → 121) |
  | `corelib/test/httpd/main.ty` | `d := httpd.bad_request()`, `d := httpd.Failed` (143 → 141) |
  | `corelib/test/result/main.ty` | `di := io.Failed`, `de := to_bytes("")` (51 → 49) |
  | `examples/corelib/result/main.ty` | `d := io.Failed`, two `empty := to_bytes("")` (36 → 33) |
  | `examples/corelib/httpd/main.ty` | `empty := httpd.bad_request()`, `d := httpd.Failed` (45 → 42) |
  | `examples/webserver/main.ty` | `empty := to_bytes("")`, `empty := httpd.bad_request()` (204 → 201) |

  `corelib/result/result.ty`'s header CAVEAT — which told every future caller to bind
  first — is rewritten as a HISTORICAL CAVEAT stating the real cause and naming the
  regression test. `err_or`'s doc comment no longer says `fallback` must be a local.
  **This is the first phase in two plans where `server/main.ty` got shorter.**

  ##### Gate spend: `make ci` WAS run (the day's single run, and it is now spent)

  plan.md's compiler-phase exception, and the budget was free (the five phases of
  `plan-option-result-DONE.md` each recorded "NOT run"). Honest account of two attempts:

  - **Attempt 1 aborted on an environment fault, not on code.** Every one of the 526
    fixtures failed `sanitizer exit 1` with `ASan runtime does not come first in initial
    library list`. Cause: this agent's shell carries
    `LD_PRELOAD=/home/igzo/phonic/tools/block-nnp.so`, which breaks **any** ASan binary —
    proven with a 1-line control (`int main(){return 0;}` under
    `-fsanitize=address,undefined` → `rc=1`, same message). Nothing was validated.
  - **Attempt 2, `env -u LD_PRELOAD make ci N=0`:** lane 1 build ok, **lane 2 `make test`
    passed 526 / failed 0, "all green"** — including `ok pkg_generic_qual_arg`. Killed by
    my own 575 s wall during lane 2b, so the remaining lanes were run individually (not
    via `make ci`/`make test`, which are now spent for the day):

  ```
  [2b] make ilp32          passed: 526  failed: 0   all green (-m32, 3m23s)
  [2c] make asan-self      compiled: 541  failed: 0  all green (the COMPILER under ASan+UBSan)
  [3]  make corelib        all green (tychoc matches goldens)
       make corelib-examples / site / raytrace / mandelbrot   all green
  [4]  make conc           passed 37  failed 0
  [5]  make ffi            green
  [6]  fuzz/run.py 200     ok=177 skip=23 timeout=0 FAIL=0
  [7]  fuzz/run_reject.py 200   accepted=31 rejected=169 FAIL=0
  [8]  fuzz/run_leak.py 60      ok=53 skip=7 FAIL=0
  [9]  tools_check.sh      formatter: 810 files, idempotence-fails=0 semantic-fails=0
                           -- lane FAILS on `bytes-rehome`, RED AT HEAD TOO (see below)
  [10] bench/guard.sh      ok (binary_trees 35% of C, maptree 23% of C, gate <60%)
  [11] make recursion      all green (fail closed on deep input)
  [12] make spec-check     grammar ok, Appendix E ok, 7 runnable examples all pass
  [13] make check-links    128 markdown files, no dead links; citations ok (see below)
  ```

  ##### By-hand sweep: 13 entry points, 9 goldens, live server

  ```
  ok compile corelib/test/{httpd,io,net,result}/main.ty
  ok compile examples/corelib/{httpd,io,net,result}/main.ty
  ok compile examples/{fetch,site,weblog,webserver}/main.ty
  ok compile server/main.ty                       -- 13 entry points, 0 failures
  same corelib/test/{httpd,io,net,result}.out
  same examples/corelib/{httpd,io,net,result}.out -- 8 goldens byte-identical
  sh examples/webserver/run.sh -> "webserver: ok (tychoc == tychoc0 == golden)"  -- 9th
  ```

  **Not one golden moved**, and the 9th is the stronger signal: the FROZEN, untouched
  `tychoc0` still agrees byte-for-byte with the patched `tychoc` on that program.

  Live server, `127.0.0.1:18099`, `--workers 4 --idle-ms 800`, driven by the previous
  plan's own raw-socket driver (reused, not rewritten). **Identical to the recorded
  phase-5 matrix, case for case:**

  ```
  GET /            200 2659 text/html      GET /style.css 200 1726 text/css
  GET /data.json   200 294  application/json   GET /nope.html 404 621
  HEAD /           200 Content-Length=2659 body=0     POST /  405 Allow: GET, HEAD
  GET /../../etc/passwd 403
  GARBAGE                    -> 400        Content-Length: 0x10 -> 400
  20 KiB head, no terminator -> 431 Request Header Fields Too Large
  (a) zero-byte hangup            -> no bytes, log lines added 0
  (b) partial head then stall     -> 408 Request Timeout, log lines added 1
  (c) connect, idle past 800 ms   -> no bytes, log lines added 0
  GET /emptydir -> 301 Location: /emptydir/ 56   GET /about -> 301   GET /img -> 301
  keep-alive 3 requests on ONE fd -> 200 200 200     50-request flood -> 50/50 200
  access log 71 lines, all 4 workers seen, format `w<id> <method> <target> <status> <bytes> <ms>`
  clean exit: server terminate -> -15 (SIGTERM)
  ```

  ##### One thing this phase caused and fixed, one it found and did not

  - **Caused and fixed:** `src/tychoc.c` grew a net 17 lines, which staled **11 anchored
    citations** into it (`docs/spec/15-program.md`,
    `docs/internals/frontend-restriction-audit-2026-07-25.md`,
    `docs/internals/plan-front-door-DONE.md`). Verified green at HEAD first (`citation check: ok`, 22 anchored / 1344
    bare, run against a pristine `git archive HEAD` tree) so the redness is provably mine,
    then shifted all five distinct anchors by the measured +17. Gate green again. **This is
    the citation gate doing exactly its job** and it is the reason a compiler phase must run
    it.
  - **Found, NOT fixed — out of scope, recorded:** `scripts/tools_check.sh`'s
    `bytes-rehome` lane is **already red at HEAD**. Its inline fixture does
    `d := io.read_bytes(p)` then `len(d)`, which stopped compiling when the archived plan's
    phase 2 gave `read_bytes` a `Result` return — and `scripts/tools_check.sh:273` throws
    the compile's exit status away, so the breakage surfaces only as
    `grep: .../brh/main.c: No such file or directory`. Proven pre-existing by running
    `tools_check.sh` inside a clean `git archive HEAD` tree: identical failure. New
    unchecked phase below.

- [x] **Phase 1b — `tools_check.sh`'s `bytes-rehome` lane is red at HEAD**
  - Found by Phase 1's gate sweep, **not caused by it** (proven against a pristine
    `git archive HEAD` tree). `scripts/tools_check.sh:272`'s fixture writes
    `d := io.read_bytes(p)` / `len(d)`, which the `Result` conversion of the archived plan's
    phase 2 made uncompilable: `error: len(...) takes an array, a string, bytes, a map, or
    a soa`. So the lane that guards a real use-after-free (`copy_into` missing `T_BYTES`)
    has been **vacuous since `eefc609`** and reports its own breakage as a missing file.
  - Scope: `scripts/tools_check.sh` only — update the fixture to the `Result` API and make
    line 273 check the compile's exit status so a stale fixture fails loudly instead of
    silently. Do not change `copy_into`; the lane's assertion is still the right one.
  - Done when: the lane passes and, with `tycho_str_copy` deliberately removed from the
    `T_BYTES` path, reddens for the right reason.
  - Verify: run `sh scripts/tools_check.sh` — green; then break it on purpose once and
    capture the failure line.

  #### Phase 1b — DONE. Evidence

  `scripts/tools_check.sh` only. **`src/tychoc.c` untouched** (`git diff --stat` after
  the sweep: `scripts/tools_check.sh | 13 ++++++++++---`, nothing else). **No `make ci`,
  no `make test`** — the day's single run was spent by phase 1. `scripts/tools_check.sh`
  was run directly, four times.

  ##### The fixture, before and after

  ```diff
  -d := io.read_bytes(p)                                    # bare bytes since eefc609 -> Result
  +import "core:result"
  +d := result.unwrap_or(io.read_bytes(p), to_bytes(""))    # the one-liner phase 1 unblocked
  ```

  Worth naming: this is the **first use of phase 1's fix outside its own regression
  test**. `FRICTION.md:148` said the blocked spelling was exactly
  `result.unwrap_or(io.read_bytes(p), empty)`; it compiles here in one line, and
  `--emit-c` still names the local `h_d`, so the lane's grep needed no change.

  ##### The exit status, before and after

  ```diff
  -TYCHO_CORELIB="$PWD/corelib" ./tychoc "$TMP/brh/main.ty" --emit-c >/dev/null 2>&1
  -if grep -q 'tycho_str_copy(_parent, h_d)' "$TMP/brh/main.c"; then
  +if ! TYCHO_CORELIB="$PWD/corelib" ./tychoc "$TMP/brh/main.ty" --emit-c >/dev/null 2>"$TMP/brh.err"; then
  +    echo "    bytes-rehome FIXTURE STALE: it no longer compiles, so this lane asserts NOTHING"
  +    sed 's/^/      /' "$TMP/brh.err"; fail=1
  +elif grep -q 'tycho_str_copy(_parent, h_d)' "$TMP/brh/main.c"; then
  ```

  Three outcomes now, where there were two: invariant held / invariant broken /
  **cannot ask**. The stderr is captured and printed indented rather than sent to
  `/dev/null`, so the STALE branch shows the compiler's own words.

  ##### Green (run 1 and run 4, run 4 being the restored tree that is committed)

  ```
  >>> bytes-rehome: a bytes field of a returned struct is deep-copied into the caller's arena
      bytes field re-homed on struct return
  tools-check: ok
  ```

  Whole-script context from run 1 (unchanged by this phase, quoted to show nothing else
  moved): `810 files checked (compilable=379) idempotence-fails=0 semantic-fails=0`, LSP
  smoke all-True, both `pkgsnip`/`pkgresolve` lanes green.

  ##### Deliberate breakage (a) — the assertion is live, not merely green

  Deleted `case T_BYTES: return sfmt("tycho_str_copy(%s, %s)", arena, val);`
  (`src/tychoc.c:7858`) so `bytes` falls through to the identity `return val`, rebuilt,
  re-ran:

  ```
  >>> bytes-rehome: a bytes field of a returned struct is deep-copied into the caller's arena
      bytes field NOT re-homed -- copy_into missing T_BYTES (dangling UAF!)
  tools-check: FAIL
  ```

  Restored with `git checkout -- src/tychoc.c`. **This is the proof the lane was worth
  un-rotting rather than deleting:** the use-after-free it was written for is still one
  deleted line away.

  ##### Deliberate breakage (b) — a stale fixture now fails loudly, in the compiler's words

  Re-injected the old spelling (`d := io.read_bytes(p)`) into the fixture:

  ```
  >>> bytes-rehome: a bytes field of a returned struct is deep-copied into the caller's arena
      bytes-rehome FIXTURE STALE: it no longer compiles, so this lane asserts NOTHING
        /tmp/tmp.0OwMLuyuhR/brh/main.ty:8: error: len(...) takes an array, a string, bytes, a map, or a soa
             8 |     if len(d) == 0:
  tools-check: FAIL
  ```

  That is the **exact error the old lane swallowed for three commits**, now on stdout
  under a message that names the failure mode. Contrast the pre-fix behaviour recorded
  by phase 1: `grep: .../brh/main.c: No such file or directory`.

  ##### Citation gate, because the file grew 7 lines

  `python3 scripts/check_citations.py` →
  `citation check: ok (22 anchored contain the token they name, 1357 bare in bounds)`.
  The one anchored citation into this file (`docs/internals/plan-front-door-DONE.md:5966`
  → `scripts/tools_check.sh:121`) is above the edit, so nothing shifted.

  ##### Out of scope, not fixed, not silently absorbed

  Nothing new found. The `scripts/tools_check.sh:273` reference inside the now-struck
  `FRICTION.md` entry is left as written: it is a claim about the pre-fix file and the
  strikethrough marks it historical.

- [x] **Phase 2 — `\r`, multi-line strings, and `const` folding**
  - Items: `FRICTION.md:123` — no `\r` escape in string literals, so the most common byte
    pair in HTTP is a function call (`httpd.crlf()`), and `const TERM = httpd.crlf() +
    httpd.crlf()` is rejected (`error: const value must be a literal`), making the header
    terminator reallocate two strings per loop iteration. `FRICTION.md:124` — no multi-line
    string literal and no line continuation, so a 10-line HTML error page is 10 consecutive
    `s += "..."` statements.
  - Scope: the lexer in `src/tychoc.c`, `docs/spec/` string-literal section, and the corelib
    sites that exist only because of the gap (`httpd.crlf`, `server/`'s error page builder).
    Decide whether `const` folding of a `+` over two literals is in or out — if out, say why.
  - Done when: `"\r\n"` is a literal, a multi-line string form exists (or is refused with a
    reason), `httpd.crlf()` is either gone or documented as kept deliberately, and the spec
    documents whatever landed.
  - Verify: corelib + 13 entry points compile; 9 goldens match; the HTTP wire bytes are
    unchanged (`server/` live matrix, byte-compared as phases 3–5 of the last plan did).

  #### Phase 2 — DONE. Evidence

  ##### Root cause: the escape set was small because a literal is pasted, not decoded

  All three items share one mechanism, and it is not "the lexer forgot `\r`". A string
  literal's contents are kept as **raw source text** through the whole compiler
  (`src/tychoc.c:323-325`, the comment says so) and pasted **verbatim** into the
  generated C string literal, which the runtime then interns by `strlen`:

  ```c
  /* src/tychoc.c:8671 -- one site, and it pastes e->sval straight through */
  return sfmt("({ static char *_l = 0; if (!_l) _l = tycho_str_intern(\"%s\"); _l; })", e->sval);
  /* runtime/tycho_rt.c:1005 */
  size_t n = strlen(s);
  ```

  So the escape set is exactly **the set of escapes C spells the same way and that are
  two characters wide**. `\r` qualifies, which is why it cost **one character**
  (`e != 'r'` added at `src/tychoc.c:382`). `\0` and `\xNN` do not, and that is a
  refusal with a number, not a shrug — see below.

  The same two-characters-wide property is what makes the other two items **text**
  operations rather than value operations: concatenating two literals' raw texts is
  concatenating their values, because no escape can absorb a byte across the seam.

  ##### What landed (3 edits, `src/tychoc.c`: +31 / −3 = **+28 lines**, of which 9 are code)

  | # | change | site | code lines |
  |---|---|---|---|
  | 1 | `\r` in the string escape set + the diagnostic text | `src/tychoc.c:373-382` | 2 |
  | 2 | adjacent string literals join (`"a" "b"` → `"ab"`) | `src/tychoc.c:2164-2165` | 2 |
  | 3 | `const_fold` folds `+` over two `E_STR` | `src/tychoc.c:4006-4012` | 5 |

  The rest of the +28 is the comment block explaining why the escape set is what it is.
  **Multi-line strings needed no new delimiter**, and this is the finding worth keeping:
  implicit line-joining inside `(`…`)` / `[`…`]` **already existed** — `tests/multiline_literals.ty:1-2`
  is a committed fixture for it — so item `FRICTION.md:124` was half solved before the
  phase started and only the *literal* half was missing. `("a\n"` newline `"b\n")` is the
  form; `f"a" "b"` and `"a" f"b"` still fail, deliberately, because an f-string is already
  sugar for a `+` chain.

  ##### Refused, with the number: `\0` and `\xNN`

  Not "out of scope" — mechanically blocked by the paste-through emit path, measured by
  reading the three functions that would have to change: the lexer's pass-through becomes
  a decode-to-bytes (`src/tychoc.c:319-400`); the one emit site that pastes the text needs
  a `\xNN`-emitting re-escaper next to the 10-line one already there (`:8671`, escaper at
  `:11722-11734`); and `tycho_str_intern` needs a length-carrying twin, because its
  contract is literally "a C string, `strlen`-bounded" (`runtime/tycho_rt.c:1000-1012`).
  **3 functions changed + 1 new runtime entry point, on the order of 35 lines.** A `\0`
  would silently truncate the interned length and `"\x41" "1"` would lex in C as `\x411`
  — both are corruption, not diagnostics, which is why they are refused rather than
  half-added. Both still reject cleanly:

  ```
  n1/main.ty:2: error: unsupported escape \0 (use \n \t \r \\ \")
  n2/main.ty:2: error: unsupported escape \x (use \n \t \r \\ \")
  n3/main.ty:3: error: expected ')'      <- f"a{x}" "b", an f-string never joins
  n4/main.ty:2: error: expected ')'      <- "a" f"b", same
  ```

  ##### Scratch program: `"\r\n"` vs `httpd.crlf()`, byte for byte

  Own directory (`FRICTION.md:141`). All five assertions, then `od -c` on the joined head:

  ```
  lit           = CR LF, len 2
  lit == httpd.crlf()            byte for byte
  local const fold == crlf()     byte for byte
  const TERM == crlf()+crlf()    len 4, one literal
  multi-line join == 1 literal  len 36
  joined head, CRLF every seam   == crlf() build

  0000020   .   1       2   0   0       O   K  \r  \n   C   o   n   t   e
  0000040   n   t   -   L   e   n   g   t   h   :       2  \r  \n  \r  \n
  ```

  `const TERM = "\r\n" + "\r\n"` is the exact spelling `FRICTION.md:123` recorded as
  rejected; it compiles and folds to one 4-byte literal.

  ##### THE CONSTRAINT THAT SHAPED THE PHASE: the frozen `tychoc0` owns the corelib

  `httpd.crlf()` is **kept deliberately**, and not for compatibility — because it is
  **impossible** to write the literal there. `examples/webserver/run.sh:20-27` builds
  `examples/webserver/main.ty` (which `import`s `core:httpd`) with the FROZEN `tychoc0`
  and asserts `tychoc == tychoc0 == golden`, and `compiler/tychoc0.ty:195` rejects `\r`:
  `lex: unsupported string escape (use \n \t \\ \")`. The frozen compiler's reach was
  mapped before touching anything, by reading the globs rather than guessing:

  | reached by `tychoc0` (literal FORBIDDEN) | why |
  |---|---|
  | `corelib/httpd`, `net`, `io`, `result`, `strings`, `sort`, `markdown` | imported by `examples/webserver/main.ty:17-23` |
  | `tests/*.ty`, `tests/pkg/*/main.ty`, `examples/*.ty` | `compiler/fixpoint.sh:24`,`:34` |
  | + `tests/{conc,warn,abort,diag}/*.ty`, `tools/*.ty` | `scripts/frontparity.sh:127-128` |
  | **NOT reached (literal used here)** | |
  | `server/`, `corelib/test/*/`, `examples/corelib/*/` | no runner feeds them to `tychoc0` |

  So `tools/lsp.ty:256`'s `"" + '\r' + '\n'` is kept too, with the reason written into the
  file. Verified, not assumed: **`sh scripts/frontparity.sh` → `agreed: 288 diverged: 0`**
  after the change. That number is the proof the constraint was respected rather than
  merely acknowledged.

  ##### Sites updated — the half that actually closes the items

  | file | what changed | lines |
  |---|---|---|
  | `server/main.ty` | `error_body`: 12 `s +=` → **one** parenthesized expression; `usage`: 11 `s +=` → one | 596 → **600** total, **378 → 378 code** |
  | `corelib/httpd/httpd.ty` | `crlf()` KEPT + 9-line reason; `read_request_capped` hoists `term := crlf() + crlf()` out of the read loop (`:239`) | 430 → **441** |
  | `corelib/test/httpd/main.ty` | local `fn crlf()` deleted → `nl := "\r\n"` | 201 → **198** |
  | `examples/corelib/httpd/main.ty` | same | 73 → **70** |
  | `corelib/test/csv/main.ty` | `"a,b" + chr(13) + "\nc,d"` → `"a,b\r\nc,d"` | −0 |
  | `corelib/test/strings/main.ty` | `chr(13) + chr(10)` → `"\r\nb"` inside the literal | −0 |
  | `tests/reject/string_escape.ty` | comment: the set is `\n \t \r \\ \"` now | −0 |

  **Honest on `server/main.ty`: it did not get shorter** — 378 code lines before and
  after. The win is 23 statements collapsing into 2 expressions with no mutable
  accumulator, and it is measurable in the emitted C: `tycho_str_concat` sites go
  **145 → 138**, because every run of static markup is now one interned literal instead
  of a run-time append. `usage()`'s output is byte-identical (`cmp` of `--help` from a
  binary built from `git show HEAD:server/main.ty` vs the new one: **identical, 526 bytes**).
  The `term` hoist removes two allocations and a concat **per read-loop iteration**, which
  is the cost `FRICTION.md:123` actually named.

  ##### Spec (citation-gated tree, so all four gates were run)

  `docs/spec/01-lexical.md` §3.9.4 rewritten: `StrLit ::= StrPiece StrPiece*`,
  `StrEscape` gains `"r"`, the multi-line form documented with a `tycho` example, the
  f-string non-join stated, and the raw-text soundness argument written down as the
  reason `\0`/`\xNN` are absent. §12.2 in `docs/spec/08-declarations.md` gains the string
  fold. `docs/spec/appendix-a-grammar.md` regenerated (it is GENERATED from the in-chapter
  EBNF — `make spec-check` caught the drift, which is that gate doing its job).
  `appendix-e-conformance.md` gains two rows **and a note saying why neither form has a
  `tests/` fixture**, since `tests/*.ty` is frozen-`tychoc0` territory.

  ##### Gate spend: `make ci` and `make test` — **NOT run.** The day's single run was
  spent by phase 1 (commit `5187724`). Verified by hand, every command below actually run:

  ```
  ok compile corelib/test/{httpd,io,net,result}/main.ty
  ok compile examples/corelib/{httpd,io,net,result}/main.ty
  ok compile examples/{fetch,site,weblog,webserver}/main.ty
  ok compile server/main.ty                        -- 13 entry points, 0 failures
  same corelib/test/{httpd,io,net,result}.out
  same examples/corelib/{httpd,io,net,result}.out  -- 8 goldens byte-identical
  sh examples/webserver/run.sh -> "webserver: ok (tychoc == tychoc0 == golden)"   -- 9th
  sh corelib/run.sh            -> "corelib: all green (tychoc matches goldens)"   -- incl. csv, strings
  sh examples/corelib/run.sh   -> "corelib examples: all green"
  sh scripts/tools_check.sh    -> "tools-check: ok"  (810 files, idempotence-fails=0 semantic-fails=0)
  python3 scripts/check_citations.py -> ok (22 anchored, 1367 bare)
  sh scripts/spec_check.sh     -> Appendix A matches; Appendix E resolves; 7 examples pass
  sh scripts/check_links.sh    -> ok (128 markdown files)
  sh scripts/frontparity.sh    -> agreed: 288  diverged: 0
  ```

  The formatter lane is worth naming: **810 files, 0 idempotence and 0 semantic failures
  with the new adjacent-literal syntax live in `server/main.ty`** — `tychofmt` needed no
  change, because joining happens in the parser and the formatter is token-preserving.

  ##### The wire is unchanged, byte-compared against HEAD's own binary

  Stronger than comparing to a transcript: the HEAD `server/main.ty` was built with the
  **new** compiler (all three changes are strictly additive, so HEAD's source is unaffected
  by them) and both binaries were driven over the recorded matrix on `127.0.0.1:18099`,
  `--workers 4 --idle-ms 800`, with the previous plan's own raw-socket driver reused
  unmodified. Every response captured to a file and `cmp`'d:

  ```
  identical  200_index.raw  (2846 bytes, Date masked)   identical  400.raw  (801)
  identical  404.raw        (789)                        identical  403.raw  (797)
  identical  405.raw        (842)                        identical  431.raw  (870)
  identical  301.raw        (253)
  all responses byte-identical except Date: 1 (1 = yes)
  CR bytes per response, old vs new: 404 7/7  403 7/7  405 8/8  400 7/7  431 7/7  301 8/8  200 8/8
  ```

  The only differing bytes in the raw capture were the `Date:` header (`07:32:02` vs
  `07:26:16`) — the two runs were six minutes apart; `od -c` of the 404 head confirms the
  divergence starts and ends inside that one line. The live transcripts are identical
  case for case with per-request timings masked:

  ```
  GET /            200 2659 text/html         GET /style.css 200 1726 text/css
  GET /data.json   200 294  application/json  GET /nope.html 404 621
  HEAD /           200 Content-Length=2659 body=0     POST /  405 Allow: GET, HEAD
  GET /../../etc/passwd 403
  GARBAGE -> 400        Content-Length: 0x10 -> 400
  20 KiB head, no terminator -> 431 Request Header Fields Too Large
  (a) zero-byte hangup        -> no bytes, log lines added 0
  (b) partial head then stall -> 408 Request Timeout, log lines added 1
  (c) connect, idle past 800ms -> no bytes, log lines added 0
  GET /emptydir -> 301 Location: /emptydir/ 56   /about -> 301 /about/   /img -> 301 /img/
  GET /favicon.ico -> 200 441 image/x-icon
  keep-alive 3 requests on ONE fd -> 200 200 200     50-request flood -> 50/50 200
  access log: all 4 workers seen (w1 w2 w3 w4), format `w<id> <method> <target> <status> <bytes> <ms>`
  clean exit: SIGTERM -> wait status 143 (= 128+15)
  diff of the two full transcripts, timings masked: IDENTICAL, case for case
  ```

  ##### Citation gate, because `src/tychoc.c` grew 28 lines

  Eleven anchored citations staled by exactly **+28** (`7208`→`7236`, `7233-7234`→`7261-7262`,
  `11549-11554`→`11577-11582`, `11808`→`11836`), across `docs/spec/15-program.md`,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` and
  `docs/internals/plan-front-door-DONE.md`. Proven to be **my** redness before shifting
  anything: `git show HEAD:src/tychoc.c | sed -n '7208p;7233,7234p;11549,11554p;11808p'`
  contains all four cited tokens, and `git diff --numstat` reports `31 3` = +28. Shifted by
  the measured delta; gate green again. Same failure mode phase 1 hit, same fix.

  ##### Out of scope, found, not absorbed

  - **No `tests/` fixture is possible for either new form** while `compiler/fixpoint.sh:24`
    and `scripts/frontparity.sh:127` feed `tests/*.ty` to the frozen `tychoc0`. Recorded in
    `docs/spec/appendix-e-conformance.md` and in `FRICTION.md`, and it is a **consequence**
    of the freeze this plan already lists as deliberately-kept debris — not new work. The
    coverage that does exist is golden-validated (`make corelib`, `server/`).
  - Nothing else found. `scripts/tools_check.sh`, `spec_check.sh`, `check_links.sh`,
    `check_citations.py` and `frontparity.sh` were all green at the end of the phase.

- [x] **Phase 3 — nested patterns, and `Result` in a tuple literal**
  - Items: `FRICTION.md:139` — there are **no nested patterns**: `Err(net.Timeout)` is
    rejected with `error: expected ')'`, and worse, `Err(A)` for a nullary variant *parses as
    a binding named `A`*, surfacing only as `error: duplicate Err arm` if a second arm exists.
    That silent misparse is a correctness trap, not an ergonomic one. `FRICTION.md:159` — a
    tuple literal will not infer a `Result` element: `return (Err(A), "partial")` →
    `error: tuple element 1 needs a concrete value`.
  - Both are why the last plan's error enums are payload-free and why
    `httpd.read_request_capped` builds its outcome in a local.
  - Scope: `src/tychoc.c` pattern parsing + tuple element inference; `docs/spec/` patterns
    section; a regression test per shape. **If nested patterns are genuinely large, fix the
    silent-binding misparse anyway** — a wrong parse that only shows up as a duplicate-arm
    error is the higher-severity half, and refusing it needs a very good reason.
  - Done when: `Err(net.Timeout)` matches, or the misparse is a hard error at minimum; a
    `Result` can be a tuple literal element; the workaround locals are removed.
  - Verify: corelib + 13 entry points compile; 9 goldens match; `server/` live matrix
    unchanged; new regression tests pass.

  #### Phase 3 — DONE. Evidence

  ##### Both items reproduced first, with real output, against the pre-fix compiler

  Five scratch programs, each in its own directory (`FRICTION.md:141`):

  ```
  r1  Err(net.Timeout)  -> error: expected ')'                 (col points at the `.`)
  r2  Err(C(n))         -> error: expected ')'                 (col points at the `(`)
  r3  Err(A) / Err(B)   -> error: duplicate Err arm            <- the ONLY symptom
  r4  Err(A) alone      -> BUILT, and RAN: "errA"              <- the silent misparse
  r5  return (Err(A),"partial") -> error: tuple element 1 needs a concrete value
  ```

  `r4` is the item's real content: one `Err(A)` arm **compiled and ran**, because `A`
  was a binding. Nothing was reported at the arm, which is where the mistake is.

  ##### Root cause 1: the pattern grammar had no recursion, and a bare name is ambiguous

  `MatchArm` was a flat list of *binding names*, not a pattern
  (`src/tychoc.c:1426` at HEAD: `char *binds[8]; int nbinds;`), and the parser's arm
  loop ate one bare `TK_IDENT` per slot:

  ```c
  /* src/tychoc.c:2732 at HEAD -- the whole of it */
  arm->binds[arm->nbinds++] = eat(ps, TK_IDENT, "a binding name")->text;
  ```

  So `net.Timeout` died on the `.` and `C(n)` on the `(` — "expected `')'`" was the
  arm loop refusing anything but `IDENT , IDENT`. And `A` **fit**, as a binding.
  The ambiguity is real and cannot be resolved in the parser: `Err(x)` is a binding
  and `Err(A)` is a pattern, and only the payload's *type* tells them apart. That is
  why the fix has a parse half and a resolve half, and why the resolve half is where
  the misparse dies.

  Second mechanism, in codegen: an `Option`/`Result` match was **not** an arm chain
  at all. It was a hard binary `if`, with exactly one Ok arm and one Err arm found by
  name (`src/tychoc.c:9799-9803` at HEAD, `okarm`/`errarm` single pointers). Multiple
  `Err` arms had nowhere to go, which is the structural reason the item is bigger
  than a parser tweak.

  ##### Root cause 2: `E_TUPLE` was synthesis-only, and `Ok`/`Err` synthesize a *partial*

  `Ok(v)`/`Err(e)` deliberately resolve to `T_OK_PARTIAL`/`T_ERR_PARTIAL` — half a
  `Result`, with context expected to supply the other half (`src/tychoc.c:4663-4668`).
  `resolve_exp` (checking mode) grounds them against an `IS_RES(want)`
  (`src/tychoc.c:5904-5911`), and that is why a bare `return Err(A)` and a typed local
  both work. But there was **no `E_TUPLE` arm in `resolve_exp` at all**: a tuple
  literal always fell through to the synthesis path (`:4670-4681`), which resolves
  each element with no expected type and rejects a partial:

  ```c
  /* src/tychoc.c:4676-4677 -- the error the item recorded, in the SYNTHESIS path */
  if (et == T_VOID || et == T_NONE || et == T_OK_PARTIAL || et == T_ERR_PARTIAL)
      die_at(e->line, "tuple element %d needs a concrete value", i + 1);
  ```

  The finding worth keeping: **the spec already said this should work.**
  `docs/spec/04-inference.md` §6.1 has listed "a tuple or array literal's element
  type" as a context that supplies an expected type since it was written. The
  implementation, not the specification, was out of conformance — so item 2 is a
  **conformance bug**, not a language addition, and it is recorded that way in
  `appendix-e-conformance.md`.

  ##### What landed (`src/tychoc.c`: +257 / −65 = **+192 lines**, of which **116 are code**)

  | # | change | site | code lines |
  |---|---|---|---|
  | 1 | `E_TUPLE` checking arm in `resolve_exp` — element-wise, each element resolved **once** | `src/tychoc.c:5913-5923` | 11 |
  | 2 | `Variant.raw` (the name as written) + `enum_variant_index` | `:843-847`, `:869-880` | 9 |
  | 3 | `MatchArm.sub` / `subbinds` / `sub_line` / `sub_vi` | `:1426-1443` | 3 |
  | 4 | parser: one nested pattern, `pkg.V` or `V(b,…)` | `:2761-2790` | 22 |
  | 5 | resolver: `SideCov` + `side_total` + `match_arm_payload` (promotion, ordering, coverage) | `:6496-6559` | 42 |
  | 6 | resolver: `Ok`/`Err`/`Some` arms rewritten onto it | `:6702-6795` | 12 |
  | 7 | resolver: hard error for a variant name bound in a **plain enum** arm | `:6814-6824` | 8 |
  | 8 | codegen: `gen_match_side` — the ordered chain, replacing the binary `if` | `:9297-9358` | (net) 9 |

  Item 2 is **11 code lines**, and it returns the *synthesized* element types rather
  than `want`, on purpose: a mismatch then reports through the caller's own equality
  check with the caller's own message, so there is no second visit to the node.
  Phase 1's lesson applied directly — the failure there was an in-place rewrite that
  ran twice. `match_arm_payload` is idempotent for the same reason and says so: a
  promoted arm has `sub` set and skips the promotion branch, which matters because
  `clone_block` (`:7010`) clones a generic body per instance and re-resolves it.

  The **rule that kills the misparse by construction**: inside a pattern the
  payload's enum type is already known, so a name that is a variant of that enum is
  **always a pattern, never a binding** — `Err(Timeout)` and `Err(net.Timeout)` are
  the same pattern. `enum_variant_index` matches on the mangled name **or** on
  `Variant.raw`, which is the one new field this needed.

  ##### Refused, with the number

  - **Nesting inside a plain enum arm** (`match e: Wrap(A):`). Cost measured by
    reading the two functions it would need: the enum arm loop
    (`src/tychoc.c:6795-6836`) would gain per-payload-slot coverage — `covered[]`
    becomes 2-dimensional, since `Wrap(A)` and `Wrap(B)` are two arms for one variant
    — and the enum dispatch in `gen_stmt` (`:9849-9899`) would need `gen_match_side`'s
    chain nested inside each tag test, i.e. a second level of `else if` with its own
    fallback and its own trap. **2 functions, ~70 lines**, for a shape no code in the
    tree writes. **Refused — but the trap is closed anyway**, which was the phase's
    non-negotiable half: the bare-name case there is a hard error, not a bind.
  - **Deeper than one level** (`Err(C(D(n)))`) and **non-enum nested patterns**
    (`Err(3)`, a literal). Both need `MatchArm.sub` to become a recursive `Pattern`
    node with its own resolve and codegen walk — the same ~70 lines plus recursion.
    Refused. Both reject cleanly:

  ```
  r10/main.ty:14: error: 'A' is a variant of Cause, not a binding name -- match it in its own match, or rename the binding
  r11/main.ty:12: error: expected ')' after the nested pattern's bindings
  r12/main.ty:7:  error: expected a binding name
  r13/main.ty:11: error: duplicate Err(A) arm
  ```

  ##### Scratch programs, every shape, with real output

  ```
  r1  Err(net.Timeout) / Err(net.Eof) / Err(e)   -> "ok 3"  "eof"  "timeout"
  r2  Err(C(n))            payload-carrying nested  -> "c 42"
  r3  Err(A) / Err(B)      two bare arms            -> "errA"        (was: duplicate Err arm)
  r4  Err(A) alone         -> error: match on a Result must cover both Ok and Err
                              ^ the misparse is now a HARD ERROR
  r5  return (Err(A), "partial") from -> (Result(int,E), string)     -> "partial"
  r6  VALUE-form match with a nested arm + `_`     -> "timeout" "other" "ok 2"
  r7  refined arms cover EVERY variant, no Err(e) and no `_`
                           -> "c 7 seven"  "b"  "a"  "ok 2"
  r8  Err(e) BEFORE Err(A) -> error: duplicate Err arm   (the dead arm is rejected)
  r9  Some(A) / Some(e) / None  on an Option payload -> "none" "some other" "some a"
  ```

  ##### Workarounds removed — and one KEPT, with the measurement that forced it

  | site | before | after |
  |---|---|---|
  | `server/main.ty` `serve_conn` | `Err(e)` + 5 × `e == httpd.X` + an `answer` bool | **5 arms, one per cause**; shared wire+log tail factored into `refuse()` |
  | `server/main.ty` root check | `Err(e)` + `if e == io.NotFound` + `else` | `Err(io.NotFound)` / `Err(e)` |
  | `corelib/test/httpd` `why` | `is_ok` + `err_or` + 4 × `==` | 5 arms (`Err(httpd.Malformed)` …) |
  | `corelib/test/io` `why`/`dwhy`/`fswhy` | 3 × (`is_ok` + `err_or` + `==` chain) | 3 matches, one arm per cause |
  | `examples/corelib/result` `describe` | `is_ok` + `err_or` + `==` chain | 4 arms |
  | `corelib/httpd/httpd.ty` `read_request_capped` | typed local `out` | **`out` KEPT** — see below |

  **`out` is kept, and it is not a compiler limit any more.** `return (Err(why), buf)`
  compiles under `src/tychoc.c`. It does **not** compile under the frozen
  `compiler/tychoc0.ty`, which `examples/webserver/run.sh:24-27` feeds this package
  while asserting `tychoc == tychoc0 == golden`. Measured by actually making the
  change and running the runner:

  ```
  webserver: tychoc0 BUILD FAILED
  line 540: returning (Result(,httpd__ReqErr),str) but this function returns
            (Result(httpd__Request,httpd__ReqErr),str)
                return (Err(why), buf)
  ```

  Identical in kind to phase 2's `crlf()` finding, and it generalises: **nothing in
  `corelib/`, `tools/`, `tests/` or top-level `examples/` may use a nested pattern
  either.** That is why the sites converted above are all in `server/`,
  `corelib/test/` and `examples/corelib/` — the three places phase 2 mapped as
  outside the frozen compiler's reach. The reason is written into `httpd.ty` at the
  `out` declaration, into `result.ty`, `io.ty` and `httpd.ty`'s enum headers, and
  into `docs/guides/corelib.md`.

  ##### Line deltas, honestly

  | file | before | after | note |
  |---|---|---|---|
  | `src/tychoc.c` | 11840 | **12032** | +192 total, **+116 code** (180 added − 64 removed) |
  | `server/main.ty` | 600 (378 code) | 611 (**380 code**) | **+2 code lines.** The 5 arms and `refuse()` cost 2 more lines than the `answer` bool they replace; what went is the two-step, not the line count |
  | `corelib/test/io/main.ty` | 121 code | **115 code** | −6 |
  | `corelib/test/result/main.ty` | 2 code added → 71 lines | | the regression, mostly comment |

  ##### Regression test, and why it is where it is

  `corelib/test/result` gains `why` (bare nullary, payload-carrying, and a
  three-variant exhaustive set), `io_why` (qualified `Err(io.NotFound)` + `_`) and
  `outcome` (a `Result` in a tuple literal, destructured by the caller). Golden
  recorded, 8 new lines in `corelib/test/result.out`.

  **It cannot live in `tests/`**, and that is a consequence of the freeze rather than
  a choice: `compiler/fixpoint.sh:24` and `scripts/frontparity.sh:127-128` feed every
  `tests/*.ty` and `tests/pkg/*/main.ty` to the frozen `tychoc0`. Recorded in
  `docs/spec/appendix-e-conformance.md` beside phase 2's identical note. The stronger
  coverage is incidental: `corelib/test/{io,httpd}` and `examples/corelib/result` had
  their `==` chains **rewritten as nested patterns** and their goldens still match
  byte for byte — proof the new codegen is behaviour-identical to the code it
  replaced, on four functions, not just on a fixture written to pass.

  ##### Spec

  - `docs/spec/02-grammar.md` — `Pattern` gains `VariantName` and `SubPattern`;
    `appendix-a-grammar.md` regenerated to match (it is a checked projection).
  - `docs/spec/10-statements.md` — new **§14.3.1 Nested patterns**: one level,
    `Option`/`Result` payloads only, the unqualified-variant rule, the
    "a variant name is always a pattern, never a binding" consequence, the ordered
    decision list, and the three exhaustiveness routes, with a `tycho` example.
  - `docs/spec/04-inference.md` — §6.2 gains item **7**, tuple literals checked
    element-wise.
  - `docs/spec/appendix-e-conformance.md` — two matrix rows (§14.3.1, §6.2(7)) plus
    the no-`tests/`-fixture note carrying the measured `tychoc0` error.

  ##### Gate spend: `make ci` and `make test` — **NOT run.** The day's single run was
  spent by phase 1 (commit `5187724`); phase 2 (`667f0d9`) also verified by hand.
  Every command below actually run, in the foreground:

  ```
  ok compile corelib/test/{httpd,io,net,result}/main.ty
  ok compile examples/corelib/{httpd,io,net,result}/main.ty
  ok compile examples/{fetch,site,weblog,webserver}/main.ty
  ok compile server/main.ty                    -- 13 entry points, 0 failures
  sh corelib/run.sh          -> "corelib: all green (tychoc matches goldens)"
  sh examples/corelib/run.sh -> "corelib examples: all green"
  sh examples/webserver/run.sh -> "webserver: ok (tychoc == tychoc0 == golden)"  -- 9th golden
  sh scripts/frontparity.sh  -> agreed: 288  diverged: 0   (unchanged from phase 2)
  sh scripts/tools_check.sh  -> "tools-check: ok"
                                810 files checked (compilable=379)
                                idempotence-fails=0 semantic-fails=0
  sh scripts/spec_check.sh   -> Appendix A matches; Appendix E resolves; 7 examples pass
  sh scripts/check_links.sh  -> ok (128 markdown files, no dead relative links)
  python3 scripts/check_citations.py -> ok (22 anchored, 1403 bare)
  cc -O2 -Wall -Wextra -std=c11 src/tychoc.c  -> 0 warnings
  ```

  `frontparity` at **288 / 0** is the proof the freeze was respected rather than
  merely acknowledged, and `tools_check` at **810 / 0 / 0** is the proof `tychofmt`
  needed no change: nested patterns are ordinary tokens and the formatter is
  token-preserving, exactly as phase 2 found for adjacent literals.

  ##### The `server/` live matrix, run twice and diffed

  `127.0.0.1:18099`, `--workers 4 --idle-ms 800`, raw sockets. Run once against a
  binary built from **`git show HEAD:server/main.ty`** and once against the new
  source, both with the new compiler (the compiler changes are strictly additive, so
  HEAD's source is unaffected):

  ```
  GET /            200 2659 text/html; charset=utf-8   GET /style.css 200 1726 text/css
  GET /data.json   200 294  application/json           GET /favicon.ico 200 441 image/x-icon
  GET /nope.html   404 621                             GET /../../etc/passwd 403
  POST /           405 Allow: GET, HEAD                HEAD / 200 Content-Length=2659 body=0
  GARBAGE -> 400                                       Content-Length: 0x10 -> 400
  20 KiB head, no terminator -> 431 Request Header Fields Too Large
  (a) zero-byte hangup        -> no bytes, log lines added 0
  (b) partial head then stall -> 408 Request Timeout, log lines added 1
  (c) idle past 800ms         -> 0 bytes, log lines added 0
  GET /emptydir -> 301 Location: /emptydir/ 56   /about -> 301 /about/   /img -> 301 /img/
  keep-alive 3 requests on ONE fd -> 200 200 200      50-request flood -> 50/50 200
  access log workers seen: w1 w2 w3 w4    format `w<id> <method> <target> <status> <bytes> <ms>`
  clean exit: SIGTERM -> killed by signal 15 (wait status 143)

  diff old.txt new.txt -> TRANSCRIPTS IDENTICAL, case for case (both full passes)
  access log, worker id + timings masked -> IDENTICAL
  served bytes vs disk: favicon.ico 441, index.html 2659, style.css 1726,
                        data.json 294  -- all BYTE-IDENTICAL
  ```

  **A near-miss worth recording, because it would have been a false green.** The
  first old/new pair reported "identical transcripts" while *both* runs had actually
  died at the 408 case: an orphaned server from an earlier aborted run still held
  `:18099`, so every binary failed identically at `cannot bind`. The driver now
  fails closed on a bind collision and reaps its child with `atexit`, and the exit
  line reports the real disposition (`killed by signal 15`) instead of arithmetic on
  a status it never checked. Two identical failures are not agreement — the same
  shape of mistake `FRICTION.md:169` records the live matrix catching once before.

  ##### Citation gate, because `src/tychoc.c` grew 192 lines

  Fourteen anchored citations staled. Proven to be **my** redness before shifting
  anything: `git stash push -- src/tychoc.c` → `citation check: ok (22 anchored,
  1403 bare)`. Each anchor shifted by its own measured delta, not by the file total,
  because the edits are spread through the file: `"bounded"` **+33**
  (`1767-1783`→`1800-1816`), `parse_extern_fn` **+60** (`3530-3600`→`3590-3660`),
  `no 'main' procedure` / `'main' must be` **+159** (`7236`→`7395`,
  `7261-7262`→`7420-7421`), `compile_package` / `int main(` / `system(cmd)` **+192**
  (`11577-11582`→`11769-11774`, `11698-11802`→`11890-11994`, `11836`→`12028`), across
  `docs/spec/15-program.md`, `docs/spec/03-types.md`,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` and
  `docs/internals/plan-front-door-DONE.md`. Gate green again. Third phase running,
  third time this happened.

  ##### Stale claims corrected, because the phase falsified them

  Eight live files asserted "Tycho has no nested patterns" as a fact about the
  language. Leaving them would be the most expensive kind of wrong — a reader
  designing around an absence that no longer exists. Corrected in
  `corelib/result/result.ty`, `corelib/io/io.ty`, `corelib/httpd/httpd.ty`,
  `corelib/test/{io,httpd}/main.ty`, `examples/corelib/result/main.ty` and
  `docs/guides/corelib.md` (3 places), each now stating what is true **and** why
  `corelib/` still cannot use the form. The corelib's error enums stay payload-free —
  Anti-scope forbids that redesign, and `==` remains the right tool for a caller who
  is not opening a `match`.

  One correction is **phase 1's residue, not mine**: `docs/guides/corelib.md` still
  carried "**One caveat, and it is load-bearing:** a `pkg.name` written directly in a
  generic call's argument list does not resolve", which phase 1 fixed. Removed while
  rewriting that sentence.

  ##### Out of scope, found, not absorbed

  - Nothing new. The two refusals above are costed, the frozen-`tychoc0` reach is a
    consequence of a freeze this plan already lists as deliberately-kept debris, and
    all four doc gates plus `frontparity` were green at the end of the phase.

- [x] **Phase 4 — `exit(code)`, and `die()` as a diverging call**
  - Items: `FRICTION.md:128` — `die()` is the language's only exit and always exits **1**, so
    `--help` cannot answer status 0 through it; the workaround threaded a `help: bool` field
    through `server/`'s config struct. `FRICTION.md:140` — `die()` is typed `void` and is not
    modelled as diverging, so it cannot be the tail of a value-`match` arm
    (`a value if/match branch must produce a value, not void`), which is the one call site in
    `server/` where adopting `Result` cost a line.
  - Scope: an `exit(int)` builtin (or `die`-with-status), plus divergence modelling in
    `src/tychoc.c`'s type checker; `docs/spec/16-builtins.md`; then drop the `help: bool`
    data-flow workaround in `server/main.ty` and the dummy `srv := 0`.
  - Done when: `--help` exits 0 without threading a flag, and `srv := match net.listen(...):
    Ok(fd): fd / Err(e): die(...)` compiles.
  - Verify: `./tycho-httpd --help; echo $?` prints 0; corelib + 13 entry points compile;
    9 goldens match; `server/` live matrix unchanged.

  #### Phase 4 — DONE. Evidence

  ##### Both items reproduced first, with real output, against the pre-fix compiler

  Scratch programs, each in its own directory (`FRICTION.md:145`):

  ```
  d1  srv := match net.listen(...): Ok(fd): fd / Err(e): die("cannot bind")
      -> error: a value if/match branch must produce a value, not void      (line 7, the Err arm)
  d2  exit(0)
      -> error: unknown procedure 'exit' -- core:path has `ext` -- add `import "core:path"`...
  d3  die("bye")  -> BUILT, ran, exit=1                                     <- always 1
  d6  x = if n > 0: 1 / else: die("neg")
      -> error: cannot assign void to 'x' of type int    <- the SAME gap, a different message
  ```

  `d6` is the part the item did not record: the value-`if`/`match` rule is broken in
  **four** tail positions, not one, and each reports differently because three of the
  four are desugared at parse time and only `:=` defers to the resolver.

  ##### Root cause: divergence is a property of the TAIL DESUGAR, not of a type

  There is no bottom type in Tycho, and adding one would touch every unification
  site. The value if/match desugar has exactly two halves that must agree about
  which branch carries a value:

  - `ctrl_rewrite_tails` (`src/tychoc.c:2847`) turns each branch's trailing `S_EXPR`
    into `name = tail` / `return tail` / a place-set. It runs at **parse** time for
    `x = …`, `place = …` and `return …` (`:3058`, `:3285`, `:3307`) and at **resolve**
    time for the `:=` / typed-decl form (`:6645`).
  - `ctrl_collect_tails` (`:2876`) hands the resolved tails to the unification loop in
    the `S_DECL` value-`ctrl` arm of `resolve_stmt` (`:6613-6635`), where a `T_VOID`
    tail is the error the items quote:

  ```c
  /* src/tychoc.c:6629-6630 -- unchanged; a diverging tail simply never reaches it */
  if (ti == T_VOID)
      die_at(tails[i]->line, "a value if/match branch must produce a value, not void");
  ```

  **Both now skip a diverging tail**, so the branch keeps the plain statement it
  already was: no destination, no contributed type. Because the skip went into the
  shared desugar rather than into the `:=` case, all four tail positions were fixed
  at once — which is *smaller* than special-casing one, not larger.

  ##### The predicate is syntactic, and that is sound rather than convenient

  ```c
  /* src/tychoc.c:2838 */
  static int expr_diverges(Expr *e) {
      return e && e->kind == E_CALL && !e->qual && !e->lhs && e->sval &&
             (!strcmp(e->sval, "die") || !strcmp(e->sval, "exit"));
  }
  ```

  Three facts make the name-match safe, each verified rather than assumed:

  1. **A program cannot define either name.** Measured: `fn die(s: string) -> int` →
     `error: 'die' is already defined`, and with the new `Sig` in place
     `fn exit(n: int) -> int` → `error: 'exit' is already defined`. This is the
     distinction `FRICTION.md:120`'s `send` item is about — `send` is a **magic**
     builtin, special-cased in `resolve_expr` (`src/tychoc.c:5382`) and absent from
     `g_sigs`, which is precisely why *it* can be shadowed silently. `die`/`exit` are
     `Sig` builtins and go through the duplicate check.
  2. **`e->sval` is the written name before AND after resolution**, because builtins
     are never mangled — which is why codegen has always matched `die` the same way
     (`:8665`). One predicate therefore serves both the parse-time rewrite and the
     resolve-time unification, and cannot disagree with itself between the two.
  3. `!e->qual` excludes `pkg.die`; `!e->lhs` excludes a call through a function
     *value*.

  ##### Fail-closed half: every branch diverging is a hard error

  `t` starts at `T_VOID` as the loop's "unset" sentinel, so an all-diverging
  if/match would have pushed a **void local**. Rejected with the fix in the message:

  ```
  d8/main.ty:4: error: every branch of this value if/match diverges, so there is no
                       value to bind to 'x' -- write the if/match as a plain statement
  ```

  ##### The `exit` spelling, and why not `die(msg, code)`

  A new `Sig` builtin `exit(int) -> void` (`src/tychoc.c:4302`), emitting **C's
  `exit(3)` directly** (`:8672-8674`) with no `tycho_exit` wrapper. Three reasons,
  in order of weight:

  1. The builtin `Sig` table is **fixed-arity** (`.nparams`, `register_builtins`
     `:4288`), so `die(msg)` / `die(msg, code)` would need overload handling in the
     resolver that no other builtin has.
  2. The two calls want different **streams**: `die` writes stderr, an answered
     `--help` writes stdout. Folding them would make the status the only difference
     between two things that differ in more than status.
  3. Emitting C's `exit` rather than adding a runtime function means
     `runtime/tycho_rt.c` is **untouched**, so the runtime text embedded in every
     emitted `.c` does not move. There is also nothing to wrap: `exit()` flushes
     stdio itself (verified — `d2` prints `before` then exits 0), and only the low 8
     bits reach the parent, same as C.

  `tycho_die` is deliberately **not** given `noreturn`: `runtime/tycho_rt.c:1190-1192`
  documents the defensive `return (T){0}` that a dying branch still gets, and
  `block_ends_in_return` (`src/tychoc.c:9220`) drives both that codegen decision and
  the fall-off-the-end lint. So a `-> int` function whose `else` branch dies still
  warns — **measured as pre-existing, not introduced**: the identical warning fires on
  the plain statement form `if n > 0: return n * 2 / else: die("neg")`. Left alone on
  purpose; changing it would hand C a real fall-off-the-end path.

  ##### What landed (`src/tychoc.c`: +48 / −4 = **+44 lines**, of which **14 are code**)

  | # | change | site | code lines |
  |---|---|---|---|
  | 1 | `expr_diverges` | `src/tychoc.c:2838-2841` | 4 |
  | 2 | `ctrl_rewrite_tails` skips a diverging tail | `:2860` | 1 |
  | 3 | `ctrl_tail_push` + its 3 call sites in `ctrl_collect_tails` | `:2873-2884` | 3 (+3 rewritten) |
  | 4 | `exit` `Sig` | `:4302` | 1 |
  | 5 | all-branches-diverge rejection | `:6622-6623` | 2 |
  | 6 | `exit` codegen | `:8672-8674` | 3 |

  ##### `--help` exit status, and the workarounds removed

  ```
  ./tycho-httpd --help ; echo $?   ->  0        (usage on stdout, 20 lines)
  ./tycho-httpd -h     ; echo $?   ->  0
  ./tycho-httpd --nope ; echo $?   ->  1        ("unknown option: --nope" + usage, stderr)
  diff old --help vs new --help    ->  BYTE-IDENTICAL
  diff old --nope vs new --nope    ->  BYTE-IDENTICAL
  ```

  where *old* is a binary built from `git show HEAD:server/main.ty`. Removed:

  - `struct Config`'s **`help: bool`** field, and with it the 7th positional argument
    at both `Config(...)` sites — the `--help` arm's `return Config("", "", 0, 1, 1,
    false, true)` is now `print(usage())` + `exit(0)`.
  - `main`'s **`if cfg.help: print(usage()); return`** block.
  - the **dummy `srv := 0`** and its statement `match`, now the item's own spelling:

  ```tycho
  srv := match net.listen(cfg.host, cfg.port):
      Ok(fd): fd
      Err(e): die("tycho-httpd: cannot bind " + cfg.host + ":" + str(cfg.port) + "\n")
  ```

  Proven to still fire: with `:18131` held by another process, `d1` printed nothing on
  stdout, `cannot bind` on stderr, and exited **1**.

  **Line delta for `server/main.ty`: 611 → 606 total, 380 → 376 code lines (−4).**
  It is the second time in this plan the application got *shorter* (phase 1 was
  380 → 378), and the first time a `Result` call site got shorter than the sentinel
  code it replaced — which is the specific claim `FRICTION.md:145` disputed.

  ##### Gate spend: `make ci` and `make test` — **NOT run.** The day's single run was
  spent by phase 1 (commit `5187724`); phases 2 and 3 also verified by hand. Every
  command below actually run, in the foreground, on this phase's tree:

  ```
  ok compile corelib/test/{httpd,io,net,result}/main.ty
  ok compile examples/corelib/{httpd,io,net,result}/main.ty
  ok compile examples/{fetch,site,weblog,webserver}/main.ty
  ok compile server/main.ty                    -- 13 entry points, 0 failures
  same corelib/test/{httpd,io,net,result}.out
  same examples/corelib/{httpd,io,net,result}.out  -- 8 goldens byte-identical
  sh corelib/run.sh          -> "corelib: all green (tychoc matches goldens)"
  sh examples/corelib/run.sh -> "corelib examples: all green"
  sh examples/webserver/run.sh -> "webserver: ok (tychoc == tychoc0 == golden)"  -- 9th golden
  sh scripts/frontparity.sh  -> agreed: 288  diverged: 0   (unchanged from phases 2-3)
  sh scripts/tools_check.sh  -> "tools-check: ok"
                                810 files checked (compilable=379)
                                idempotence-fails=0 semantic-fails=0
  sh scripts/spec_check.sh   -> Appendix A matches; Appendix E resolves; 7 examples pass
  sh scripts/check_links.sh  -> ok (128 markdown files, no dead relative links)
  python3 scripts/check_citations.py -> ok (22 anchored, 1432 bare)
  cc -O2 -Wall -Wextra -std=c11 src/tychoc.c  -> 0 warnings
  ```

  `frontparity` at **288 / 0** is the proof the freeze was respected: a `tests/`
  fixture for `exit` or for a diverging arm would be a program `tychoc` accepts and
  the frozen `tychoc0` refuses, which is exactly what that script reports as a
  divergence. Recorded in `docs/spec/appendix-e-conformance.md`; the witness is
  `server/main.ty`, which no runner feeds to `tychoc0`. **Third phase, third time
  this constraint bound, and the first time it bound a new BUILTIN rather than new
  syntax** — checked before writing the fixture, not at verify time.

  ##### The `server/` live matrix, run twice and diffed

  `127.0.0.1:18099`, `--workers 4 --idle-ms 800`, raw sockets, driven by **phase 3's
  own driver reused** (the one that fails closed on a bind collision and reaps with
  `atexit`; the port was checked free before each run). Once against a binary built
  from `git show HEAD:server/main.ty`, once against the new source, both with the new
  compiler:

  ```
  GET /            200 2659 text/html; charset=utf-8   GET /style.css 200 1726 text/css
  GET /data.json   200 294  application/json           GET /favicon.ico 200 441 image/x-icon
  GET /nope.html   404 621                             GET /../../etc/passwd 403
  POST /           405 Allow: GET, HEAD                HEAD / 200 Content-Length=2659 body=0
  GARBAGE -> 400                                       Content-Length: 0x10 -> 400
  20 KiB head, no terminator -> 431 Request Header Fields Too Large
  (a) zero-byte hangup        -> no bytes, log lines added 0
  (b) partial head then stall -> 408 Request Timeout, log lines added 1
  (c) idle past 800ms         -> 0 bytes, log lines added 0
  GET /emptydir -> 301 Location: /emptydir/ 56   /about -> 301 /about/   /img -> 301 /img/
  keep-alive 3 requests on ONE fd -> 200 200 200      50-request flood -> 50/50 200
  access log workers seen: w1 w2 w3 w4
  clean exit: SIGTERM -> killed by signal 15 (wait status 143)

  diff old.txt new.txt                          -> TRANSCRIPTS IDENTICAL, case for case
  access log, worker id + timings masked         -> IDENTICAL, line for line (69 / 69)
  served bytes vs disk: index.html 2659, style.css 1726, data.json 294,
                        favicon.ico 441          -- all BYTE-IDENTICAL
  ```

  Both runs reported `MATRIX OK: every assertion passed`, and both came up on a port
  verified free first — the near-miss phase 3 recorded (two identical *failures*
  reading as agreement) cannot recur silently, but it was checked anyway.

  ##### Citation gate, because `src/tychoc.c` grew 44 lines

  Eleven anchored citations staled. Proven to be **my** redness before shifting
  anything: `git stash push -- src/tychoc.c` → `citation check: ok (22 anchored,
  1432 bare)`. Shifted by **two** measured deltas, not one, because the edits
  straddle the anchors: `no 'main' procedure` / `'main' must be` **+37** (the five
  resolver-side edits above them: `7395`→`7432`, `7420-7421`→`7457-7458`), and
  `compile_package` / `system(cmd)` **+44** = the file total (`:11769-11774`→
  `:11813-11818`, `:12028`→`:12072`), the `exit` codegen arm sitting between them.
  Across `docs/spec/15-program.md`,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` and
  `docs/internals/plan-front-door-DONE.md`. Gate green again. Fourth phase, fourth
  time.

  ##### Documented

  - `docs/spec/16-builtins.md` — §29.3 table gains the `exit(code)` row ("all eight"
    → "all nine"); §29.12 rewritten as a two-terminator table with the low-8-bits
    and stdout-flush contract; **new §29.12.1** states the divergence rule, its
    all-diverging rejection, and why name-matching is sound.
  - `docs/spec/04-inference.md` §6.5 — the diverging-tail exemption to branch
    unification (this is the section that states the rule the item hit).
  - `docs/spec/10-statements.md` §14.8 — retitled `die`, `exit`, and termination;
    `die` is still the only *abort*, `exit` is the answered exit.
  - `docs/spec/02-grammar.md` §4.3.2 — `ValueCtrl`'s branch shape now admits a
    diverging call, and requires at least one non-diverging branch.
  - `docs/spec/appendix-e-conformance.md` — two §29.12 rows plus the
    no-`tests/`-fixture note carrying the `frontparity` reason.

  ##### Stale claim corrected

  `server/main.ty:575` still read "die() always exits 1, which is the wrong status
  for a judgement call about someone else's directory" — a *reason* that no longer
  holds, attached to a decision that still does. Rewritten to say the decision
  (warn, never terminate, because serving an empty tree is legal) without asserting
  the absence. `docs/internals/plan-option-result-DONE.md`'s two copies are left
  alone: that file is an **archived** plan and its value is being the record of what
  was true when it ran.

  ##### Out of scope, found, not absorbed

  - Nothing new. The `send`-shadowing item (`FRICTION.md:120`) is *adjacent* — this
    phase's soundness argument rests on `Sig` builtins being duplicate-checked while
    magic builtins are not — but it is a different item with its own phase, and the
    `Sig`/magic asymmetry it describes was confirmed here (`src/tychoc.c:5382`), not
    changed.

- [x] **Phase 5 — the corelib's small honest wins**
  - Items, all small and all measured as biting real code:
    - `FRICTION.md:122` — `httpd.reason_phrase` is a closed `if`-chain and `httpd.response()`
      takes no reason, so `431` went on the wire as `HTTP/1.1 431 Status`. **Bit twice**: the
      `408` in phase 3 needed the same positional-struct bypass, which got factored into a
      private `phrased_response()` — a reimplementation of the constructor the corelib should
      have. Estimated ~5 lines.
    - `FRICTION.md:129` — `core:net` exposes `getsockname` but no `getpeername`, so an access
      log cannot record the client address: the single most useful field in a real access log
      is unreachable. Then use it in `server/`'s log line.
    - `FRICTION.md:168` — `io.exists` lists the whole parent directory (O(entries), and blind
      to a `.`/`..`-only leaf) where the new `io.is_dir` is one `stat`. Make `exists`
      `stat`-backed; `resolve()` currently calls both on the same path.
    - `FRICTION.md:227` — `to_bytes("")` is the only spelling for an empty `bytes`.
  - Scope: `corelib/httpd/`, `corelib/net/` (+ shim), `corelib/io/`, and their consumers.
  - Done when: `431` and `408` carry real reason phrases with no positional bypass and
    `phrased_response()` is deleted; the access log records the peer address; `io.exists` is
    one syscall; the `bytes` zero value is decided (landed or refused).
  - Verify: 9 goldens match or their change is justified; `server/` live matrix shows the
    real reason phrases on the `431`/`408` lines and a client address in the log; corelib +
    13 entry points compile.

  #### Phase 5 — DONE. Evidence

  ##### Gate spend: `make ci` / `make test` — **NOT run.** The day's single run was spent
  by phase 1 (`5187724`); phases 2, 3 and 4 also verified by hand and this is the
  fourth. No `src/tychoc.c` change in this phase, so the compiler is byte-identical
  to `241c159` and the exception at the top of this file does not apply.

  ##### Item 1 — reason phrases (`FRICTION.md:122`). Landed. **+6 corelib code lines,
  −4 in the application.**

  The fix is two halves, and the second is the one the item was actually asking for:

  1. `408` and `431` joined `reason_phrase`'s table (`corelib/httpd/httpd.ty:336-339`),
     so the *existing* constructor now renders them.
  2. `response_reason(status, reason, body)` (`:352-353`) — the constructor that takes a
     reason. `response()` is now one line on top of it (`:357-358`), so there is exactly
     one place that builds a `Response` positionally and it is inside the package that
     owns the struct. That is what closes the item for the NEXT status nobody thought of;
     adding two table rows alone would have left the gap.

  **Wire proof, from the constructor alone** — one probe program, `render_head` of
  `httpd.response(s, …)`, compiled against a `git archive HEAD` tree (HEAD corelib) and
  against this one, both with the same `./tychoc`:

  ```
  --- HEAD corelib ---            --- NEW corelib ---
  HTTP/1.1 408 Status             HTTP/1.1 408 Request Timeout
  HTTP/1.1 431 Status             HTTP/1.1 431 Request Header Fields Too Large
  HTTP/1.1 200 OK                 HTTP/1.1 200 OK
  HTTP/1.1 404 Not Found          HTTP/1.1 404 Not Found
  ```

  `phrased_response()` is **deleted**, and with it `oversize_response()` and
  `timeout_response()` — the two named wrappers existed only to hold the two literal
  phrases. Both call sites are now `error_response(431)` / `error_response(408)`, the same
  constructor the other four statuses already used. `server/main.ty` **376 → 372 code
  lines**; three functions became zero.

  **The live wire is UNCHANGED for `431`/`408`, and that is the expected result, not a
  miss.** The bypass produced the correct bytes — that was its whole point. What moved is
  *where* the phrase comes from: the response bodies are still 680 and 661 bytes, so the
  `<title>431 Request Header Fields Too Large</title>` the bypass wrote by hand is now
  written by the library, byte for byte. The regression the item recorded
  (`HTTP/1.1 431 Status`) is only reachable through `httpd.response()`, which is exactly
  what the probe above tests, and it is the spelling every *other* caller of `core:httpd`
  would have used.

  ##### Item 2 — `getpeername` (`FRICTION.md:129`). Landed, shim **and** consumer.

  - `netx_peer_addr(fd)` in `corelib/net/net_shim.c:204-219` (17 code lines + the
    `<arpa/inet.h>` include): `getpeername` into a `sockaddr_storage`, then `inet_ntop`
    for `AF_INET` **and** `AF_INET6`; `""` for an unconnected fd, an unknown family or a
    failed call.
  - `net.peer_addr(fd) -> Result(string, NetErr)` (`corelib/net/net.ty:143-147`): `""` is
    never an `Ok` — an access log that cannot name the client must say so, not print a
    blank column.
  - **The buffer is `__thread`, not `static`.** N workers are N pthreads sharing one
    listening fd, so a shared buffer would be a data race on precisely the field this
    item exists to add. `corelib/crypto/crypto_shim.c:43` set that precedent. The borrow
    is safe because an extern `-> string` return is copied at the call site
    (`src/tychoc.c:8387`, `is_extern_str_call` → `tycho_str_copy`), so the Tycho value
    outlives the next request's overwrite. Verified live under the 50-request flood
    across 4 workers: every line carries the address, none is truncated or empty.
  - Consumer: `log_req` gained a `peer` column, asked **once
    per connection** rather than once per request — the peer of an accepted fd cannot
    change, and keep-alive would otherwise repeat the syscall per request
    (`server/main.ty:334`, `:351`, `:360`).

  **The access log line, before and after** (same case, same matrix, same port):

  ```
  before:  w1 GET / 200 2659 0.286ms
  after:   w1 127.0.0.1 GET / 200 2659 0.210ms
  before:  w3 - GET / HTTP/1.1. 431 680 0.114ms
  after:   w3 127.0.0.1 - GET / HTTP/1.1. 431 680 0.111ms
  ```

  69 lines before, 69 after; with the new column and the timings masked the two logs are
  **identical line for line (69 / 69)**, so the peer column is the *only* change to the
  log. `cc -O2 -Wall -Wextra` on `net_shim.c`: **0 warnings**.

  ##### Item 3 — `io.exists` is one `stat` (`FRICTION.md:168`), and the redundancy DOES go away

  `exists` is now `iox_stat_kind(p)` and two comparisons (`corelib/io/io.ty:252-254`) —
  the same shim call `is_dir` uses. It **fails closed**: `false` means "stat could not say
  yes", folding an unstattable path in with a missing one, which is what the old
  list-the-parent version did too (an unlistable parent yielded no entries) and the safe
  direction for the one shape that consumes it. A caller needing the distinction wants
  `is_dir`, whose `Err(Failed)` says exactly that.

  Two consequences the item did not predict:

  - **`core:io` lost a dependency.** `path.base`/`path.dir` were needed *only* by the old
    `exists`, so `import "core:path"` is gone from `corelib/io/io.ty`. The module that was
    written up as "the first corelib module to COMPOSE other core modules" now composes
    one, not two. `corelib/io/io.ty` **98 → 93 code lines**.
  - **`resolve()`'s double call is gone, and it collapsed further than a call count.**
    `is_dir` then `exists` on the same path was two syscalls asking one question; making
    the second a `stat` is what made that visibly redundant rather than merely ugly. The
    pair is now ONE `match io.is_dir(fsp)` reading all three answers off the Result
    (`server/main.ty:293-302`): `Ok(true)` → `301` (or `404` when `dir_form` already
    appended `index.html`, i.e. a directory *named* `index.html`), `Ok(false)` → `200`,
    `Err(_)` → `404`. That last corner was a `200` → `read_bytes` → `Err(IsDir)` → `404`
    before: same status, one syscall fewer, and no wrong intermediate. Per request for a
    real file the path went **2 syscalls (1 opendir/readdir walk + 1 stat) → 1 stat**.
  - The arm binds a bool (`Ok(isdir)`) rather than matching `Ok(true)`, because a literal
    nested pattern is refused — `error: expected a binding name`, measured here, exactly
    the refusal phase 3 costed at ~70 lines and declined. Written at the site.

  `corelib/test/io.out` is **byte-identical** before and after, which is the proof the
  swap changed the means and not the meaning: the golden records `exists` on a file, a
  missing path, `Makefile`, and a directory that was just removed — four answers, all
  unchanged.

  ##### Item 4 — the empty `bytes` (`FRICTION.md:227`). **REFUSED, with the number.**

  Costed by reading the emit path rather than guessing, and the measurement changed the
  answer: **the status quo costs nothing at run time.** `T_BYTES` lowers to `char *` —
  "the same length-headered buffer as string" (`src/tychoc.c:1289`) — and `to_bytes` on a
  string is a **zero-cost reinterpret**, not a conversion (`:8702`, the `to_str`/`to_bool`/
  `to_bytes` newtype-unwrap arm). So `to_bytes("")` emits the same interned `""` a literal
  would, and the complaint is 11 characters of spelling at **10 sites in the whole tree**
  (counted: 4 in `corelib/test/`, 2 in `examples/corelib/result`, 1 each in
  `corelib/httpd/httpd.ty:142`, `corelib/image/image.ty:37`, `corelib/test/compress`,
  `server/main.ty`).

  Two candidate spellings measured against the current compiler:

  ```
  b: bytes = ""    -> error: declared type bytes but value is string
  b: bytes = []    -> error: cannot type a bare [] here -- no expected type
                             (write []T, or use it where the element type is known)
  ```

  The cheapest landing is therefore a checking-mode arm in `resolve_exp` grounding a
  string *literal* against an expected `T_BYTES` — **~6 code lines**, structurally the
  same shape as phase 3's `E_TUPLE` arm (11 lines), needing **no codegen and no runtime**
  because the representation is already identical. Refused anyway, on three counts:

  1. It puts an **implicit string→bytes conversion** into the type system. That is a
     language change with spec text in `04-inference.md` §6.1 and `03-types.md` plus an
     Appendix E row — call it ~25 lines of docs for 6 of code — and this plan's
     Anti-scope forbids redesigns.
  2. **It cannot be used at the site the item names.** "Every struct default" is
     `corelib/httpd/httpd.ty:142`'s `Request("", "", "", []string, []string, to_bytes(""))`,
     and `core:httpd` is compiled by the frozen `compiler/tychoc0.ty` through
     `examples/webserver/run.sh`. Fourth phase in a row this constraint has bound
     (`\r`, `exit`, the tuple `Result`, now this).
  3. A **real** `bytes` literal — byte-exact, `\xNN`-capable — is already costed at
     **~35 lines across 3 functions plus a new runtime entry point** in the `\r` item
     (`FRICTION.md:123`), and it belongs to phase 7 (`bytes` operators), not here.

  **Net: 0 lines, 0 bytes of emitted code, and one number written down.** The item is
  settled, not fixed.

  ##### Line deltas

  | file | change |
  |---|---|
  | `corelib/httpd/httpd.ty` | code **249 → 255** (+6): 4 table rows, `response_reason`, `response` rewritten |
  | `corelib/net/net.ty` | code **64 → 70** (+6): 1 extern, `peer_addr` |
  | `corelib/net/net_shim.c` | **332 → 361** lines (+29; 17 code, 12 comment) |
  | `corelib/io/io.ty` | code **98 → 93** (−5): `exists` is 2 lines, `import "core:path"` gone |
  | `server/main.ty` | code **376 → 372** (−4): 3 functions deleted, `resolve` tail collapsed, `peer` threaded |
  | `corelib/test/io/main.ty`, `docs/guides/corelib.md`, `docs/spec/18-library.md` | comment/doc only |

  Library +7 Tycho code lines and +17 C; application −4. The first phase of this plan
  where the corelib grew and the application shrank at the same time.

  ##### Verify — every command actually run, in the foreground, on this tree

  ```
  ok compile corelib/test/{httpd,io,net,result}/main.ty
  ok compile examples/corelib/{httpd,io,net,result}/main.ty
  ok compile examples/{fetch,site,weblog,webserver}/main.ty
  ok compile server/main.ty                        -- 13 entry points, 0 failures
  same corelib/test/{httpd,io,net,result}.out
  same examples/corelib/{httpd,io,net,result}.out  -- 8 goldens byte-identical
  sh corelib/run.sh            -> "corelib: all green (tychoc matches goldens)"
  sh examples/corelib/run.sh   -> "corelib examples: all green"
  sh examples/webserver/run.sh -> "webserver: ok (tychoc == tychoc0 == golden)"  -- 9th golden
  sh scripts/frontparity.sh    -> agreed: 288  diverged: 0  (unchanged, phases 2-4)
  sh scripts/tools_check.sh    -> "tools-check: ok"
  sh scripts/spec_check.sh     -> 7 runnable examples, all pass
  sh scripts/check_links.sh    -> ok (128 markdown files, no dead relative links)
  python3 scripts/check_citations.py -> ok (22 anchored, 1459 bare)
  cc -O2 -Wall -Wextra corelib/net/net_shim.c -> 0 warnings
  ```

  **All 9 goldens diffed and all 9 match.** The one change that was *expected* to move a
  golden did not have to: the access-log format change is in `server/`, which no golden
  covers, and it is recorded above as a before/after instead.

  The **9th golden is the load-bearing one this phase**: `examples/webserver/run.sh`
  asserts `tychoc == tychoc0 == golden` over `core:httpd`, `core:net` and `core:io` — the
  three packages edited here. It is green, so the frozen compiler accepts every line
  added, and `frontparity` at **288 / 0** confirms nothing new-syntax slipped in. That was
  designed for, not discovered: every addition is a plain function, a plain `if` arm and
  an `extern` of an already-legal shape.

  `check_citations` reports 1459 bare with the code change in (was 1432 at phase 4): the
  27 new in-source `path:line` references in these comments. Re-run after this evidence
  and the `FRICTION.md` entries were written: **1487, still ok, all in bounds** — the
  gate was run again on the prose, not only on the code.

  ##### The `server/` live matrix, run twice and diffed

  `127.0.0.1:18099`, `--workers 4 --idle-ms 800`, raw sockets, **phase 3's driver reused**
  (fails closed on a bind collision, reaps with `atexit`). The port was checked free
  before each run (`ss -ltn | grep -c 18099` → `0`, both times). The "before" binary is
  built from a **`git archive HEAD` tree with its own corelib**, not just
  `HEAD:server/main.ty` — the corelib changed this phase, so a HEAD server against the new
  corelib would have compared the wrong thing. Confirmed by grepping the emitted C for
  `httpd__response_reason` / `netx_peer_addr`: **0 hits**, i.e. the HEAD corelib really was
  the one compiled.

  ```
  GET /            200 2659 text/html; charset=utf-8   GET /style.css 200 1726 text/css
  GET /data.json   200 294  application/json           GET /favicon.ico 200 441 image/x-icon
  GET /nope.html   404 621                             GET /../../etc/passwd 403
  POST /           405 Allow: GET, HEAD                HEAD / 200 Content-Length=2659 body=0
  GARBAGE -> 400                                       Content-Length: 0x10 -> 400
  20 KiB head, no terminator -> HTTP/1.1 431 Request Header Fields Too Large
  (a) zero-byte hangup        -> no bytes, log lines added 0
  (b) partial head then stall -> HTTP/1.1 408 Request Timeout, log lines added 1
  (c) idle past 800ms         -> 0 bytes, log lines added 0
  GET /emptydir -> 301 Location: /emptydir/ 56   /about -> 301 /about/   /img -> 301 /img/
  keep-alive 3 requests on ONE fd -> 200 200 200      50-request flood -> 50/50 200
  access log workers seen: w1 w2 w3 w4
  clean exit: SIGTERM -> killed by signal 15 (wait status 143)

  diff old.txt new.txt                     -> TRANSCRIPTS IDENTICAL, case for case
  access log, peer column + timings masked -> IDENTICAL, line for line (69 / 69)
  ```

  Both runs reported `MATRIX OK: every assertion passed`. Every case moved by this phase
  is in there: `/emptydir` → `301` and `/nope.html` → `404` exercise the collapsed
  `resolve()` tail, the `431`/`408` cases carry the real phrases with no bypass in the
  source, and all 68 request lines carry `127.0.0.1`.

  ##### Out of scope, found, not absorbed

  - `corelib/net/net_shim.c` does not compile standalone under `-std=c11`: `getaddrinfo`
    and `struct addrinfo` need `_POSIX_C_SOURCE`/`_DEFAULT_SOURCE`, and strict ISO mode
    hides them (`resolve4`, `:84-89`). **Pre-existing, not mine** — the `git archive HEAD`
    copy fails identically with the same 4 errors — and invisible in practice because
    `tychoc` invokes plain `cc` (`src/tychoc.c:11976`), whose default is `gnu17`. Filed as
    one line in `FRICTION.md`; no phase.

- [x] **Phase 6 — `core:cli` and `args()`**
  - Items: `FRICTION.md:126` — `args()` includes `argv[0]` but `cli.parse` requires it
    removed, so every program opens with the same four-line copy loop (`server/main.ty` and
    `examples/weblog/main.ty:129` carry identical copies, the second with a comment
    explaining it). `FRICTION.md:127` — `core:cli` cannot express `--root DIR`: values must be
    `=`-attached by design, so **45 of `server/`'s lines** are a hand-rolled parser.
  - Biggest single line-count win in the whole file, and it is library-only.
  - Scope: `corelib/cli/`, then delete `server/main.ty`'s hand-rolled parser and both copy
    loops. Keep `=`-attached spellings working — this is an addition, not a migration.
  - Done when: `--root DIR` and `--root=DIR` both parse through `core:cli`, `args()` is
    usable without the copy loop, and `server/main.ty`'s 45-line parser is gone.
  - Verify: every existing CLI spelling in the tree still works (`server/` and
    `examples/weblog` both driven with real flags, output captured); 9 goldens match;
    `server/main.ty`'s line count recorded before and after.

  #### Phase 6 — DONE. Evidence

  ##### The `=`-attached design decision, quoted, and how it was preserved

  It is deliberate and the file says so in its own header, at HEAD
  (`corelib/cli/cli.ty:9-11`):

  ```
  # Values are ALWAYS attached with `=` (`--out=file`), so the parser needs no
  # schema of which options take a value: `--verbose` is unambiguously a flag and
  # `--out=x` unambiguously an option.
  ```

  So the decision is not "`=` is prettier" — it is **schema-freedom**: `parse` can sort
  any vector with no declaration from the caller, and that property is exactly what
  `--root DIR` cannot have. You cannot know whether `DIR` is `--root`'s value or a
  positional without being told that `--root` takes one. The two are therefore not in
  conflict and there was nothing to reverse; the fix is an **addition** that asks for the
  schema only from callers that want the second spelling:

  - `parse(av)` is unchanged, still schema-free, and is now one line on top of the shared
    loop (`corelib/cli/cli.ty`, `parse_into(av, []string, []string, false)`).
  - `parse_spec(av, valued, boolean)` is the same loop with `strict = true`.
  - One loop, not two, so the two entry points cannot drift on the spellings they share.

  **Proof that `parse` did not move**: `corelib/test/cli.out`'s pre-existing 16 lines are
  byte-identical (`git diff` on the golden is **+26 / −0**, a pure append), and
  `examples/corelib/cli.out` — which only ever calls `parse` — is untouched. The new test
  also asserts it from the inside: fed the *schema* vector, `parse` still reports
  `has(root) = no` / `flag(root) = yes` (a bare `--root` is a flag to it) and
  `missing`/`unknown` both empty, because it cannot have those conditions.

  The rule table is written down in three places: `corelib/cli/cli.ty`'s `parse_spec`
  doc comment (the authority), `docs/guides/corelib.md:353`, and as executable assertions
  in `corelib/test/cli/main.ty`.

  ##### Decisions the item did not specify, made and documented

  | Question | Decision | Why |
  |---|---|---|
  | how a flag declares it takes a value | two `[string]` lists, `valued` + `boolean`, names **without** dashes | same spelling `get`/`flag` already take; no new struct, no builder |
  | `--root --port` | root **=** `"--port"`, consumed as-is | getopt's rule, and it is **bit-for-bit what the hand-rolled parser did** (`server/main.ty:545-549` at HEAD consumed `argv[i+1]` unconditionally). Guessing would make `--root -1` unspellable |
  | `--root` with nothing after it | recorded in `missing(c)`, as written | the caller owns the message; `server/` still prints `--root needs a value` |
  | `--quiet=1` on a boolean | flag set, value **dropped** | preserves HEAD's behaviour exactly (verified, case 14 of 47) |
  | unrecognised name / bad short cluster | recorded in `unknown(c)`, **as written** | lets `server/` reproduce its old message verbatim, `--bogus=1` and `-qx` included |
  | `--` | ends option parsing and vanishes | unchanged from HEAD `parse`, documented since the file was written (`:12-13`) |
  | bare `-` | positional | unchanged, the stdin convention |
  | short option with a value (`-p 80`) | **refused** | it would make `-abc` ambiguous. Use `--port 80` |

  ##### `args()`/`argv[0]`: `cli.argv()`, and why not a skip inside `parse`

  `fn argv() -> [string]: a := args(); return a[1:len(a)]` — 3 code lines. Array slicing
  and calling the `args()` builtin from inside a package both already worked; measured
  with a scratch package before writing anything (`n=3` from `prog aa bb cc`).

  **Why not make `parse` skip element 0**, which was the other option offered: two
  consumers in this tree build a *synthetic* argv with no program name in it
  (`corelib/test/cli/main.ty:14`, `examples/corelib/cli/main.ty:14`), so a `parse` that
  dropped element 0 would have silently eaten `--out=build/app` — the first real option —
  in both. That is a wrong answer, not an error, and `examples/corelib/cli.out` would have
  had to be re-recorded to bless it. `parse` stays a pure function over a vector; the
  argv[0] convention lives in the function whose name is about argv. The new test asserts
  the mismatch the wrapper absorbs: **`len(args()) = 1`, `len(cli.argv()) = 0`.**

  Both copy loops are gone. `server/main.ty` is now `cfg := parse_args(cli.argv())` and
  `examples/weblog/main.ty:129` is `c := cli.parse(cli.argv())` — its 4-line loop and the
  3-line comment explaining it deleted together, which is the real tell that this was
  friction and not a design.

  ##### Flag-by-flag, all 47 spellings, HEAD binary vs phase-6 binary

  Driven with `--port 0` so no two cases can collide on a port; the **stderr banner is the
  parse result** (it prints root/host/port/workers/idle), exit `124` = still serving when
  `timeout` killed it. Both binaries built from the same tree state via `git stash` /
  `stash pop`, confirmed distinct by their emitted C: `httpd_before.c` has
  `opt_name`/`wants_value` and **0** `parse_spec`; `httpd_after.c` has **4** `parse_spec`
  and **0** `opt_name`.

  | # | spelling | HEAD | phase 6 |
  |---|---|---|---|
  | 1 | *(no args)* | `serving . … workers=8 idle=5000ms` | same |
  | 2-3 | `--root=DIR` / **`--root DIR`** | `serving …/server/www` | same |
  | 4-5 | `--host=A` / **`--host A`** | ok | same |
  | 6-7 | `--port=N` / **`--port N`** | ok | same |
  | 8-9 | `--workers=3` / **`--workers 3`** | `workers=3` | same |
  | 10-11 | `--idle-ms=250` / **`--idle-ms 250`** | `idle=250ms` | same |
  | 12-13 | `--quiet` / `-q` | ok | same |
  | 14 | `--quiet=1` (value on a boolean) | serves | same |
  | 15-17 | all-attached / all-spaced / mixed | `workers=2 idle=300ms` | same |
  | 18-20 | repeated `--root` (last wins) / trailing slash / `--root=` | ok | same |
  | 21-22 | `--help` / `-h` | **exit 0** | **exit 0** |
  | 23-25 | `--bogus` / `--bogus=1` / `-x` | exit 1, `unknown option: <as written>` | same |
  | 26 | `-qh` | exit 1 `unknown option: -qh` | **exit 0 (help)** — widened |
  | 27 | bare `--` | exit 1 `unknown option: --` | **serves** — widened |
  | 28-30 | bare `-` / positional / `--=x` | exit 1, `unknown option: <as written>` | same |
  | 31-35 | `--root`/`--port`/`--host`/`--workers`/`--idle-ms` with no value | exit 1 `<name> needs a value` | same |
  | 36-37 | `--port abc` / `--port=abc` | exit 1 `wants a non-negative integer, got: abc` | same |
  | 38-41 | `--port 70000` / `--workers 0` / `--workers 300` / `--idle-ms 0` | exit 1, range message | same |
  | 42 | `--port -1` | exit 1 `…got: -1` | same |
  | 43-44 | `--root /no/such/dir` / `--root <a file>` | warning + serves | same |
  | 45 | `--root --port` | root=`--port`, then `cannot bind` | same |
  | 46 | `--help --bogus` | exit 0 | same |
  | 47 | `--bogus --help` | exit 1 | **exit 0** — widened |

  ```
  diff <condensed before> <condensed after>  -> 44 of 47 IDENTICAL, 3 diverge
  ```

  **Every one of the 3 divergences turns an error into a success on a spelling that never
  worked**, so nothing that worked before stopped working — which is the constraint the
  phase set. Named, not buried:

  - **`-qh`** — HEAD's parser had no short-cluster rule at all (it string-compared whole
    tokens, `server/main.ty:560-562`), so `-qh` was simply unknown. `core:cli` has had
    `-abc → a,b,c` since it was written, `q` and `h` are both declared boolean, so the
    cluster now resolves and the `h` answers `--help`.
  - **bare `--`** — HEAD rejected the POSIX end-of-options marker because `opt_name("--")`
    returned `"--"` and fell through to the unknown arm; an accident, not a decision.
    Proven directly with a free port, since case 27's exit stayed `1` only because
    `:8080` is occupied on this host:

  ```
  httpd_before --port 0 --   -> exit=1  tycho-httpd: unknown option: --
  httpd_after  --port 0 --   -> exit=124 serving . on http://127.0.0.1:PORT/
  ```

  - **`--bogus --help`** — HEAD's loop was argv-*ordered*, so the first of the two won:
    `--bogus --help` exited 1 and `--help --bogus` exited 0. A parsed `Cli` carries no
    order, so one of the two had to be picked; "`--help` always answers" is the invariant
    worth keeping, and it is the one Phase 4 (`c8be42b`) landed `exit(0)` for. Recorded in
    the source at the check itself, not only here.

  ##### `examples/weblog`, driven with real flags

  Six invocations × 2 binaries, output hashed: `(none)`, `--top=2`, `--top=3 <log>`,
  `<log>`, `--top=1 -- <log>`, `--verbose <log>`.

  ```
  md5 pairs: 218477721317 e5c66b0594f0 6615b2a9c34e f4ff539a5131 0a963cd0ac8d f4ff539a5131
  before == after on all six, exit 0 on all twelve
  ```

  The `--` case and the `--verbose` (unknown-to-weblog flag) case are in there on purpose:
  `weblog` calls plain `parse`, so those are the spellings that would have moved if the
  shared loop had changed `parse`'s behaviour. They did not.

  ##### Line deltas — the number is the point of this phase

  | file | code lines | delta |
  |---|---|---|
  | `server/main.ty` | **372 → 341** | **−31** |
  | `examples/weblog/main.ty` | **170 → 163** | **−7** |
  | `corelib/cli/cli.ty` | **57 → 102** | **+45** |
  | `corelib/test/cli/main.ty` | 37 → 101 | +64 (coverage) |
  | `docs/guides/corelib.md` | doc only | — |

  What was deleted from `server/main.ty`, measured region by region at HEAD:

  ```
  opt_name + opt_inline + wants_value   12 code lines   -> deleted outright
  parse_args (the stepping loop)        43              -> 32
  main()'s argv[0] copy loop             4              -> 0
                                        --                 --
                                        59                 32   (+1 for `import "core:cli"`)
  ```

  So **`FRICTION.md:127`'s "45 of this server's lines" was an UNDER-count**: the
  hand-rolled parser plus its copy loop was **59** code lines, and what replaces it is
  **32** — of which 2 are the schema itself (`valued := [...]`, `boolean := [...]`) and
  the rest is the part that is genuinely this program's: which names exist, their
  defaults, and what counts as out of range. Nothing left in `server/main.ty` splits a
  token or counts an index.

  A tightening worth recording because it moved the headline number by 9: the schema was
  first written as 11 `push(valued, "root")` statements, then collapsed to two array
  literals after checking that `["root", "host"]` — an array literal **with elements** —
  compiles. It does; `server/main.ty` went 350 → 341, and the full 47-case matrix was
  re-run on the tightened binary to confirm nothing moved.

  **Library +45, application −38.** The library line is spent once and every future
  consumer of `core:cli` gets `--flag VALUE`, `missing()` and `unknown()` for nothing;
  `+45` also buys `parse`'s replacement, since `parse_into` (54 lines) subsumes HEAD's
  27-line `parse` rather than sitting beside it. This is the inverse of the shape the
  archived plan's own tally complained about (`FRICTION.md:79`: *"`server/main.ty`: 371
  code lines before, 380 after. It got 9 lines longer."*) — here the application shrank
  by 38 and no gate moved.

  ##### Verify — every command actually run, in the foreground, on this tree

  **`make ci` / `make test` NOT run — the day's single run was spent by Phase 1
  (`5187724`).** The by-hand list Phases 2-5 used, reused verbatim:

  ```
  ok compile corelib/test/{httpd,io,net,result}/main.ty
  ok compile examples/corelib/{httpd,io,net,result}/main.ty
  ok compile examples/{fetch,site,weblog,webserver}/main.ty
  ok compile server/main.ty                    -- 13 entry points, 0 failures
  ok compile corelib/test/cli/main.ty, examples/corelib/cli/main.ty
                                               -- +2, the packages THIS phase touched
  same corelib/test/{httpd,io,net,result}.out
  same examples/corelib/{httpd,io,net,result}.out      -- 8 goldens byte-identical
  same corelib/test/cli.out  same examples/corelib/cli.out
  sh corelib/run.sh            -> "corelib: all green"  (ok cli; 37 ok, 0 fail)
  sh examples/corelib/run.sh   -> "corelib examples: all green" (ok cli; 36 ok, 0 fail)
  sh examples/webserver/run.sh -> "webserver: ok (tychoc == tychoc0 == golden)"  -- 9th golden
  sh scripts/frontparity.sh    -> agreed: 288  diverged: 0   (unchanged, phases 2-5)
  sh scripts/tools_check.sh    -> "tools-check: ok"
  sh scripts/spec_check.sh     -> 7 runnable examples, all pass
  sh scripts/check_links.sh    -> ok (128 markdown files, no dead relative links)
  python3 scripts/check_citations.py -> ok (22 anchored, 1487 bare)
  ```

  **The one golden that moved is `corelib/test/cli.out`, and it is +26 / −0.** A pure
  append: the 16 pre-existing lines are byte-identical, which is the phase's proof that
  `parse` is unchanged. The 26 new lines are the `parse_spec` assertions — both spellings
  agreeing (`both same = yes`), `--quiet=1` setting the flag, `missing[0] = --port`, the
  four `unknown` entries as written (`--bogus`, `--bogus=1`, `-x`, `-qx`), the getopt
  swallow (`swallow = --port`), `parse`'s unchanged verdicts, and
  `nargs = 1` / `nargv = 0`. Re-recorded with `RECORD=1 sh corelib/run.sh`, and
  `git diff --stat corelib/test/` confirms **only** that one golden moved — every other
  corelib golden re-recorded byte-identically, which is the check that a blanket RECORD
  did not paper over a regression.

  > **CORRECTED BY PHASE 8, 2026-07-26 — this claim is WRONG.** `core:cli` **is**
  > inside the frozen compiler's reach, via `examples/weblog/run.sh:24`. The
  > enumeration below is right about *where* `core:cli` is imported (it names
  > `examples/weblog/`) and wrong about that not being a tychoc0 input: that runner
  > feeds `examples/weblog/main.ty` to a freshly built `tychoc0`, and
  > `scripts/frontparity.sh` never saw `examples/<dir>/main.ty`, so its 288 / 0
  > could not have confirmed it either way. Measured in phase 8: a `tychoc0` built
  > at that commit, fed `examples/weblog/main.ty`, emits **81 `cli__` symbols**, and
  > giving `corelib/cli/cli.ty` a `\r` escape reddens the (now-extended) frontparity
  > at `examples/weblog/main.ty` while the pre-phase-8 script stays green at 288 / 0
  > on the identical tree. Phase 6 was harmless only because it added no syntax
  > `tychoc0` rejects — `cli.argv`/`parse_spec` use nothing new. Phase 7 recorded the
  > same finding (`plan.md:2160-2167`); the authoritative statement is
  > `docs/spec/appendix-e-conformance.md` §E.2's `bytes`-operator note, and the
  > 13-blocked / 24-free split there is what frontparity now enforces.

  **`core:cli` is out of frozen `tychoc0`'s reach, checked before designing** (as the
  brief required), so unlike phases 2, 3 and 5 nothing had to be left un-modernised:
  `corelib/run.sh:6-11` and `examples/corelib/run.sh:6-8` both record that their tychoc0
  legs were cut on 2026-07-26; `examples/webserver/run.sh` feeds tychoc0 only
  `examples/webserver/main.ty`, which imports `core:httpd`/`net`/`io` and not `core:cli`;
  `scripts/frontparity.sh:126-127` and `compiler/fixpoint.sh:24` feed `examples/*.ty`,
  `tests/*.ty`, `tools/*.ty` and `tests/pkg/*/`, and a tree-wide grep for `core:cli`
  returns only `corelib/cli/`, `corelib/test/cli/`, `examples/corelib/cli/`,
  `examples/weblog/` and now `server/` — none of them a tychoc0 input. `frontparity` at
  288 / 0 confirms it after the fact. **`compiler/tychoc0.ty` was not touched.**

  ##### `server/` live matrix, 4 workers on `:18099`, diffed against a HEAD binary

  Phase 3's driver (`matrix.py`, fail-closed on a bind collision at `:26-30`, `atexit`
  reaper at `:20`), port confirmed free before each run. Diffed against a **HEAD-built
  binary**, not a recorded transcript, because Phase 5 added the peer-address column.

  ```
  GET /            200 2659 text/html; charset=utf-8   GET /style.css 200 1726 text/css
  GET /data.json   200 294  application/json           GET /favicon.ico 200 441 image/x-icon
  GET /nope.html   404 621                             GET /../../etc/passwd 403
  POST /           405 Allow: GET, HEAD                HEAD / 200 Content-Length=2659 body=0
  GARBAGE -> 400                                       Content-Length: 0x10 -> 400
  20 KiB head, no terminator -> HTTP/1.1 431 Request Header Fields Too Large
  (a) zero-byte hangup        -> no bytes, log lines added 0
  (b) partial head then stall -> HTTP/1.1 408 Request Timeout, log lines added 1
  (c) idle past 800ms         -> 0 bytes, log lines added 0
  GET /emptydir -> 301 Location: /emptydir/ 56   /about -> 301 /about/   /img -> 301 /img/
  keep-alive 3 requests on ONE fd -> 200 200 200      50-request flood -> 50/50 200
  access log workers seen: w1 w2 w3 w4
  clean exit: SIGTERM -> killed by signal 15 (wait status 143)

  both runs                                -> MATRIX OK: every assertion passed
  diff old.txt new.txt                     -> TRANSCRIPTS IDENTICAL, case for case
  access log, timings masked               -> IDENTICAL, line for line (69 / 69)
  ```

  Expected to be identical and it is: this phase changed only how the *arguments* are
  read, and the matrix drives the server with the same `--root server/www --port 18099
  --workers 4 --idle-ms 800` in both — through the hand-rolled parser in one binary and
  through `cli.parse_spec` in the other. The identical transcript **is** the assertion
  that the two parsers agree on the spelling the driver uses, on top of the 47-case
  matrix that covers the ones it does not.

  ##### Out of scope, found, not absorbed

  - Nothing found that needed a new phase. The one incidental discovery —
    `["a", "b"]`, an array literal with elements, compiles — is not a defect; it was used
    to tighten `server/main.ty` by 9 lines and is recorded above.
  - `examples/corelib/cli/main.ty` still shows only `parse`. Left deliberately: its golden
    is this phase's proof that `parse` did not move, and re-recording it would spend that
    proof to demonstrate something `corelib/test/cli/main.ty` already asserts. One line in
    `FRICTION.md`.

- [x] **Phase 7 — `bytes` gets operators**
  - Items: `FRICTION.md:224` — `bytes` supports **only** `len()`, `to_str()` and crossing the
    FFI: `a + b`, `b[i]` and `b[i:j]` are all rejected, so every non-trivial manipulation
    detours through `to_str`, works in `string`, and `to_bytes` back. `FRICTION.md:130` —
    that detour lands in `log_safe`, the one function where a server must be paranoid about a
    hostile request target: `string` → `[]int` → `to_bytes` → `to_str`. `FRICTION.md:225` —
    and the error message for `bytes + bytes` suggests `to_float`/`to_int`, neither of which
    applies to a buffer.
  - Scope: `src/tychoc.c` — concat, index, slice for `bytes`, plus the diagnostic text.
    `docs/spec/` bytes section. Then simplify `log_safe`.
  - Note the recorded fact that makes this cheap to get right: `string` is already fully
    byte-safe (interior `0x00` survives concat, index, slice and `len` — measured,
    `FRICTION.md:226`), so this is about intent and FFI shape, not new binary machinery.
  - Done when: `a + b`, `b[i]`, `b[i:j]` work on `bytes`; the misleading diagnostic is
    replaced; `log_safe` no longer round-trips through `[]int`.
  - Verify: a scratch program exercising all three operations against known bytes, including
    an interior `0x00`; `server/` still serves the PNG/ICO/TTF byte-exact (`cmp` against
    disk, as the last plan's phase-2 evidence did); 9 goldens match.

  #### Phase 7 — DONE. Evidence

  ##### All three rejections plus the misleading diagnostic, reproduced first

  Against `./tychoc` built from `HEAD`, each in its own directory
  (`FRICTION.md:141`):

  ```
  b1  c := a + b     -> error: arithmetic requires two ints or two floats (got bytes, bytes)
                               -- convert one side, e.g. to_float(x) to compute in floats,
                                  or to_int(x) in ints            <- the misleading advice
  b2  a[1]           -> error: can only index an array, a string, or a map (as a place)
  b3  a[1:3]         -> error: can only slice an array, soa, or string
  ```

  Verbatim matches for `FRICTION.md:225` and `:226` (now `:225`/`:226` after the
  strike-throughs). The diagnostic is the item's whole point: `to_float(x)` on a
  buffer is not advice, it is noise.

  ##### Root cause: there was nothing to build — `bytes` IS `string`'s buffer

  `src/tychoc.c:498` declares `T_BYTES` as "the same length-headered `char*` repr as
  string (so all string runtime ops apply)", and `:1289` lowers it to `char *`. The
  string runtime is **entirely header-driven**, never NUL-driven:
  `tycho_str_len` reads `((const tycho_int *)s)[-1]` (`runtime/tycho_rt.c:1023`),
  `tycho_str_get` bounds-checks against that header (`:1037`), `tycho_str_substr`
  clamps against it (`:1059`), `tycho_str_concat` sizes from both headers (`:874`).
  So `FRICTION.md:227`'s measured fact — `string` is already fully byte-safe — is
  not merely *related* to this item, it **is** the implementation. **Zero new
  runtime code, zero new C.**

  What landed (`src/tychoc.c`: **+56 / −13 = +43 lines, of which 19 are code**):

  | # | change | site | code lines |
  |---|---|---|---|
  | 1 | `E_INDEX`: `bt == T_BYTES` yields `T_INT` | `src/tychoc.c:4937` | 1 |
  | 2 | `E_SLICE`: `bt == T_BYTES` yields `T_BYTES` | `:4947` | 1 |
  | 3 | `TK_PLUS`: `lt == T_BYTES` with `bytes` or `char` rhs | `:5869-5874` | 5 |
  | 4 | the `bytes`-aware arithmetic diagnostic | `:5931-5933` | 4 |
  | 5 | codegen: `tycho_str_get` / `tycho_str_substr` / `tycho_str_concat{,_char}` predicates widened | `:9038`, `:9045`, `:9204` | 3 (edits) |
  | 6 | in-place accumulator widened to `bytes` (`is_self_append`, the sidecar decl, the rebind resync) | `:8037-8038`, `:9587`, `:9813` | 3 (edits) |
  | 7 | **`is_reinterpret_of_place`** — the use-after-free, below | `:8147-8168`, `:8182` | 8 |

  Two diagnostics also gained `bytes` where they enumerate what is indexable /
  sliceable / index-assignable (`:4938`, `:4951`, `:6979`) — the same courtesy
  `len(...)`'s message already extended.

  ##### The one design decision: `b[i]` returns `int`

  A byte **value** in `0..255`, not a 1-length `bytes`. Three reasons, in order of
  weight:

  1. **It is what the motivating function wants.** `log_safe` asks `is_ctl(b[i])`,
     i.e. `c < 32 or c == 127`. A 1-length `bytes` would have to be compared
     against constructed 1-length buffers, or unwrapped, to ask a numeric question.
  2. **`s[i]` already yields `int`** (`src/tychoc.c:4937`, normative at
     `docs/spec/03-types.md` §5.2.5 and deliberately *not* `char`). One
     representation must not have two indexing meanings, or `to_str`/`to_bytes` —
     which are casts, not conversions — would silently change what `[i]` means.
  3. **It allocates nothing.** `tycho_str_get` is a bounds check and a byte load; a
     1-length `bytes` is an arena allocation per index, i.e. O(n) allocations to
     walk a buffer.

  The cost is the same one `string` pays: `b[i] == 'c'` does not type-check, and
  appending a byte back needs `b[i:i+1]` (a 1-length sub-buffer) or a `char`
  literal. `b + 'c'` was added for exactly that, mirroring `string + char`.

  ##### The use-after-free this uncovered — pre-existing, and it blocked the item

  Writing `log_safe` in `bytes` produced a server that **died**: `w1 127.0.0.1 w1
  200 2659` in the access log (the method field showing `w1`), then
  `tycho: out of memory`. Root cause, read rather than guessed:

  ```c
  /* src/tychoc.c:9565 -- the decl re-home test */
  if (is_place(s->expr) && type_is_heap(s->decl_type) && !can_move_from(s->expr, owner))
      v = copy_into(s->decl_type, owner, v);
  ```

  `is_place` covered `E_IDENT`/`E_FIELD`/`E_INDEX`/`E_TUPIDX`/`E_SLICE` — not
  `E_CALL`. But `to_str`/`to_bytes`/`to_under` are **zero-cost reinterprets**
  (`:8745`, the fact `FRICTION.md`'s `to_bytes("")` refusal already measured): they
  return their argument's pointer. So `out := to_str(b)` over a `_scope`-owned
  `bytes` was treated as a freshly-owned value, the copy was skipped, and
  `arena_free(&_scope); return _ret;` returned freed memory. **`ret_must_copy` had
  the same blind spot**, so `return to_str(acc)` was dangling too.

  Isolated, with **no phase-7 syntax in the program at all** (`to_bytes([int])` is
  the pre-existing way to get a scope-owned `bytes`):

  ```tycho
  fn scrub(cs: [int]) -> string:
      b := to_bytes(cs)
      out := to_str(b)
      return out
  ```
  ```
  tychoc built from `git show HEAD:src/tychoc.c` : got=[8] len=8
  tychoc at this commit                          : got=[ABCDEFGH] len=8
  ```

  Emitted C, before and after:

  ```diff
  -char *h_out = h_b;                              /* alias into _scope */
  +char *h_out = tycho_str_copy(_parent, h_b);     /* re-homed */
   { char *_ret = h_out; arena_free(&_scope); return _ret; }
  ```

  Fix: `is_reinterpret_of_place` (8 code lines) makes a zero-cost reinterpret of a
  place a place, with `to_bytes([int])` excluded because that one really does
  allocate (`:8743`); `ret_must_copy` recurses through it so a `_parent`-owned local
  still needs no copy. **Fixed in-phase rather than deferred because it blocked the
  item**: every `bytes`-domain spelling of `log_safe` hits it, and two of the four
  candidate shapes measured (`return to_str(b)` and a returned `bytes` accumulator)
  printed the right answer while emitting a dangling return — a false green that
  would have shipped. Same class as the `copy_into` `T_BYTES` gap phase 1b
  un-rotted: a missing re-home, one line from a UAF. Pinned by `reinterp_ret` in
  `corelib/test/io` with an arena-churn loop between the call and the read.

  ##### `log_safe`, before and after

  ```diff
  -    cs := []int
  -    for i in range(n):
  -        c := s[i]
  -        if is_ctl(c):
  -            push(cs, 46)
  -        else:
  -            push(cs, c)
  -    out := to_str(to_bytes(cs))
  +    b := to_bytes(s)
  +    scrubbed := to_bytes("")
  +    for i in range(n):
  +        if is_ctl(b[i]):
  +            scrubbed = scrubbed + '.'
  +        else:
  +            scrubbed = scrubbed + b[i:i + 1]
  +    out := to_str(scrubbed)
  ```

  `string` → `bytes` → `string`, where both ends are zero-cost reinterprets
  (`src/tychoc.c:8745`) — so the `[]int` **and** the one real allocation in the old
  path (`to_bytes([int])`, `:8743`) are both gone. Emitted C confirms it:
  `char *h_b = h_s;` is the whole of `to_bytes`, and the two branches are
  `tycho_str_append_char(&_scope, &h_scrubbed, …, 46LL)` and
  `tycho_str_append(&_scope, &h_scrubbed, …, tycho_str_substr(…))` — the **in-place**
  accumulator, so the loop is O(n) as the old `push` loop was, not the O(n²) a naive
  per-byte concat would be.

  **Honest line count: 0.** `server/main.ty` is **341 code lines before and after**,
  and `log_safe` is **17 code lines before and after**. The win is that the function
  works in one domain instead of three, and that the one allocation is gone — not
  that it got shorter. Same shape as phase 3's `+2`.

  ##### The scrubber re-verified against a HEAD-built binary — this is the paranoid function

  The live matrix never sends a hostile target, so `log_safe` was driven directly
  (`scrub.py`, 2 workers) and the **access log** read back:

  ```
  w1 127.0.0.1 - GET /x......... 400 631 0.132ms      <- control bytes 1..31 + 0x7f
  w2 127.0.0.1 GET /AAAA…(160 A's)... 200 …          <- 300-byte target -> 160 + "..."
  w1 127.0.0.1 GE.T / 405 647 0.051ms                 <- control byte in the METHOD
  w2 127.0.0.1 - GET /a. 400 631 0.102ms              <- "\r\nw9 INJECTED …" injection attempt
  total log lines: 4        control bytes surviving anywhere in the log: 0
  diff old new -> SCRUBBER OUTPUT IDENTICAL, old vs new
  ```

  The injection attempt is the one that matters: 4 requests, 4 log lines, no
  `w9 INJECTED` line, and `\r` rendered as `.`.

  ##### Byte-exactness of the binary assets, `cmp`-equivalent against disk

  Added to phase 3's fail-closed driver (`matrix.py`), so both binaries run it:

  ```
  GET  /img/logo.png                200 cl=7883  disk=7883  served=7883  BYTE-IDENTICAL=True  ct=image/png
  HEAD /img/logo.png                200 Content-Length=7883  body=0
  GET  /favicon.ico                 200 cl=441   disk=441   served=441   BYTE-IDENTICAL=True  ct=image/x-icon
  HEAD /favicon.ico                 200 Content-Length=441   body=0
  GET  /fonts/quicksand-regular.ttf 200 cl=95440 disk=95440 served=95440 BYTE-IDENTICAL=True  ct=font/ttf
  HEAD /fonts/quicksand-regular.ttf 200 Content-Length=95440 body=0
  ```

  Body compared against `open(path,'rb').read()` in full, not by length;
  `Content-Length` asserted equal to the file size; every `HEAD` reports that same
  length and sends **0** body bytes.

  ##### The scratch program: all three operators across an interior `0x00`

  Own directory, `bytesop/`. `"ab" + chr(0) + "cd"` (there is no `bytes` literal and
  no `\xNN`, so `chr(0)` is the only spelling), plus the real 7883-byte PNG:

  ```
  len       = 5          dump      = 97 98 00 99 100
  b[0] = 97   b[2] = 0   b[4] = 100                  <- the interior NUL reads as 0
  cat len   = 10         cat dump  = 97 98 00 99 100 97 98 00 99 100   cat b[7] = 0
  plus char = 6  97 98 00 99 100 90                  <- bytes + 'Z'
  slice len = 3          slice dump= 98 00 99         <- a slice ACROSS the NUL
  open lo   = 97 98      open hi   = 99 100           empty = 0        <- b[:2] b[3:] b[2:2]
  roundtrip = 4 0        eq = true    neq = false     <- to_str(mid+…); == is byte-wise
  png len   = 7883       png sig  = 137 80 78 71 013 010 26 010
                         png ihdr = 00 00 00 013 73 72 68 82   <- three interior NULs
  rebuilt   = 7883 same=true          <- png[0:half] + png[half:len]
  bytewise  = 7883 same=true          <- 7883 iterations of acc = acc + png[i:i+1]
  ```

  `png ihdr` is the point: the IHDR chunk length is `00 00 00 0D`, three NUL bytes
  read individually and one length of 13, from a slice taken past two other NULs.

  ##### Regression fixtures, and why they are where they are

  `corelib/test/io` gains `byte_index` / `byte_slice` / `byte_cat` / `byte_rebuild`
  over the buffer `io.read_bytes` returns (`"AB" NUL "CD"`, interior `0x00` at index
  2) plus `reinterp_ret` for the use-after-free. **5 new golden lines, a pure
  append** — the 18 pre-existing lines are byte-identical, which is the proof
  nothing else in `core:io` moved.

  **It cannot live in `tests/`, for the fourth time in this plan**, and this time
  the constraint was *enumerated* instead of assumed. Measured: a `tychoc0` built at
  this commit refuses `println(str(b[2]))` with `line 3: str(x) can't stringify a
  yte`, and `compiler/fixpoint.sh:24` + `scripts/frontparity.sh:127` feed every
  `tests/*.ty` and `tests/pkg/*/main.ty` to it. Closing the import graph from every
  file a `tychoc0` runner compiles reaches **13** corelib packages that may not use
  these operators (`cli` `datetime` `http` `httpd` `io` `json` `markdown` `net`
  `path` `result` `sha256` `sort` `strings`) and leaves **24** free (`base64`
  `compress` `crypto` `hash` `hex` `image` `md5` `raster` `tls` among them — the
  packages that would most want them). Recorded in
  `docs/spec/appendix-e-conformance.md`.

  One workaround was removed outside `server/`: `corelib/test/image` indexed a
  `to_str(png)` view because "`bytes` itself is not indexable"; it now indexes `png`
  directly. That test is **skipped** without libpng, so it was verified by diffing
  the emitted C — the only delta is `char *h_sig = h_png;` disappearing and
  `tycho_str_get(h_sig, i)` becoming `tycho_str_get(h_png, i)`: the identical call
  on the identical pointer. One `FRICTION.md` line records that it was not run.

  ##### Spec and guides

  - `docs/spec/03-types.md` §5.2.6 — a normative operator table (`len`, `b[i]`→`int`
    and not a place, `b[i:j]`→`bytes` and clamping, `a+b`, `b+'c'`, `==`), the "no
    implicit `string` mixing" rule, the explicit "no iteration, no `in`" limit, and
    a runnable example extended with all three (`scripts/spec_check.sh` compiles and
    runs it: `ok docs/spec/03-types.md:142`).
  - `docs/spec/09-expressions.md` §13.2 — a `bytes` concatenation paragraph beside
    the string one, including what the failure diagnostic must name.
  - `docs/spec/12-aggregates.md` §16.2 / §16.6 — `b[i]` is not a place; a `bytes`
    slice is a string slice that yields `bytes`.
  - `docs/spec/17-runtime.md` §30.2 / §30.3 / §30.4 — the same abort for `b[i]`, the
    same compile error for `b[i] = v`, slices named in the clamp clause, and `b[i]`
    added to the byte-safety guarantee.
  - `docs/spec/appendix-e-conformance.md` — a §5.2.6 matrix row and the fourth
    freeze note, this one carrying the enumerated 13/24 split.
  - `docs/guides/ffi.md` — the "`len(b)` and `==` work as for strings" sentence now
    names all five operators; the archived `docs/internals/plan-webserver-DONE.md:428`
    claim ("`bytes` supports exactly three things") is annotated as superseded rather
    than rewritten, since it is a record of what was measured then.

  ##### Citation gate, because `src/tychoc.c` grew 43 lines

  Twelve anchored citations staled across `docs/spec/15-program.md`,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` and
  `plan-front-door-DONE.md`. Proven to be **my** redness before shifting anything —
  `git show HEAD:src/tychoc.c | sed -n '7432p;11813p;12072p'` contains all three
  single-line tokens — and shifted by the delta *computed* with `difflib` rather
  than eyeballed: `+21` below line 7432 and `+43` above it (`7432`→`7453`,
  `7457-7458`→`7478-7479`, `11813-11818`→`11856-11861`,
  `11890-11994`→`11933-12037`, `12072`→`12115`). Gate green:
  `ok (22 anchored, 1505 bare)`.

  ##### Verify — every command actually run, in the foreground, on this tree

  **`make ci` / `make test` NOT run — the day's single run was spent by Phase 1
  (`5187724`).** The by-hand list phases 2–6 used, reused verbatim, after the final
  compiler build:

  ```
  cc -O2 -fwrapv -Wall -Wextra -std=c11 src/tychoc.c   -> 0 warnings
  ok compile corelib/test/{httpd,io,net,result,cli}/main.ty
  ok compile examples/corelib/{httpd,io,net,result,cli}/main.ty
  ok compile examples/{fetch,site,weblog,webserver}/main.ty
  ok compile server/main.ty                       -- 15 entry points, 0 failures
  sh corelib/run.sh            -> "corelib: all green (tychoc matches goldens)"
                                  (skip image -- libpng absent in this environment)
  sh examples/corelib/run.sh   -> "corelib examples: all green"
  sh examples/webserver/run.sh -> "webserver: ok (tychoc == tychoc0 == golden)"  -- 9th golden
  sh scripts/frontparity.sh    -> agreed: 288  diverged: 0   (unchanged, phases 2-6)
  sh scripts/tools_check.sh    -> "tools-check: ok"
                                  810 files checked (compilable=379)
                                  idempotence-fails=0 semantic-fails=0
                                  bytes-rehome: "bytes field re-homed on struct return"
  sh scripts/spec_check.sh     -> Appendix A matches; Appendix E resolves;
                                  7 runnable examples, all pass (incl. 03-types.md:142)
  sh scripts/check_links.sh    -> ok (128 markdown files, no dead relative links)
  python3 scripts/check_citations.py -> ok (22 anchored, 1505 bare)
  git diff --stat corelib/ examples/  -> ONLY corelib/test/io.out moved (+5, pure append)
  ```

  `tools_check`'s **`bytes-rehome` lane is directly relevant and it is green**: this
  phase touched `bytes` and `copy_into`'s neighbourhood, and that lane asserts the
  `T_BYTES` deep-copy on struct return is still emitted (phase 1b proved the lane is
  live by deleting the line and watching it redden). `frontparity` at 288 / 0 is the
  proof the freeze was respected — **`compiler/tychoc0.ty` was not touched.**

  ##### The `server/` live matrix, run twice and diffed

  `127.0.0.1:18099`, `--workers 4 --idle-ms 800`, raw sockets, phase 3's driver
  (fail-closed on a bind collision, `atexit` reaper), port confirmed free first.
  Diffed against a binary built from **`git show HEAD:server/main.ty`** with the new
  compiler, since the compiler changes are additive:

  ```
  GET /            200 2659 text/html; charset=utf-8   GET /style.css 200 1726 text/css
  GET /data.json   200 294  application/json           GET /favicon.ico 200 441 image/x-icon
  GET /nope.html   404 621                             GET /../../etc/passwd 403
  POST /           405 Allow: GET, HEAD                HEAD / 200 Content-Length=2659 body=0
  GET/HEAD png+ico+ttf -> BYTE-IDENTICAL x3, Content-Length = file size, HEAD body 0
  GARBAGE -> 400                                       Content-Length: 0x10 -> 400
  20 KiB head, no terminator -> HTTP/1.1 431 Request Header Fields Too Large
  (a) zero-byte hangup        -> no bytes, log lines added 0
  (b) partial head then stall -> HTTP/1.1 408 Request Timeout, log lines added 1
  (c) idle past 800ms         -> 0 bytes, log lines added 0
  GET /emptydir -> 301 Location: /emptydir/ 56   /about -> 301 /about/   /img -> 301 /img/
  keep-alive 3 requests on ONE fd -> 200 200 200      50-request flood -> 50/50 200
  access log workers seen: w1 w2 w3 w4
  clean exit: SIGTERM -> killed by signal 15 (wait status 143)

  both runs                          -> MATRIX OK: every assertion passed
  diff old.txt new.txt               -> TRANSCRIPTS IDENTICAL, case for case
  access log, worker id + timings masked -> IDENTICAL, line for line (75 / 75)
  ```

  **The first attempt at this matrix FAILED, and that is the phase's most valuable
  evidence.** The new binary served six requests, logged them with a garbled method
  field, and died of `tycho: out of memory` at the seventh — the use-after-free
  above. A phase that had trusted "the operators compile and the scratch program
  prints the right numbers" would have shipped a server that crashes on its seventh
  request.

  ##### Out of scope, found, not absorbed

  - **`scripts/frontparity.sh` cannot see the whole frozen-`tychoc0` reach.** It
    feeds `examples/*.ty` but never `examples/<dir>/main.ty`, while
    `examples/{webserver,weblog,fetch,sqlite}/run.sh` each feed theirs to `tychoc0`.
    So `core:cli` **is** inside the freeze (via `examples/weblog/run.sh:24`), which
    phase 6 recorded as outside it — harmless only because phase 6 added no new
    syntax there. None of the four runners is in `make ci`, so **no gate can catch a
    corelib package adopting new syntax.** One `FRICTION.md` line; making it
    checkable wants a runner, which is not this plan's business.
  - **`corelib/test/image` is skipped without libpng**, so its golden asserts
    nothing in this environment. One `FRICTION.md` line, with the emitted-C diff
    that stands in for running it.
  - Nothing else. The `bytes` **literal** (`\xNN`-capable), already costed at ~35
    lines in `FRICTION.md`'s `to_bytes("")` refusal, was **not** built: the three
    operators close the item without it, and Anti-scope says `bytes` gets operators,
    not a new buffer type.

- [x] **Phase 8 — tooling and doc debt, where each item is a trap for the next person**
  - Items: `FRICTION.md:141` — `tychoc` compiles every `.ty` in the entry file's directory,
    so two scratch programs side by side collide with `'main' is already defined` pointing at
    the file you asked it to build; it cost four compile cycles to work out.
    `FRICTION.md:152` — `examples/webserver/main.ty` was left uncompilable by a whole phase
    because **no gate builds it** (`make ci` skips it). `FRICTION.md:142` and `:151` — the FFI
    has no documented shape for returning a classification alongside a `bytes` payload; the
    `status: inout int` trick is now reproduced verbatim in two shims and written down only in
    each other's comments, while `docs/spec/14-ffi.md:20-47` lists neither.
    `FRICTION.md:160` — tuples are the right shape for "value AND classification" and
    **nothing pointed at them**: documented at `docs/spec/03-types.md:193` but demonstrated by
    no corelib function. `FRICTION.md:235` — `docs/bootstrap.md` is cited by two live files
    and does not exist; the citation gate missed it because it only checks Markdown→Markdown.
  - Scope: `src/tychoc.c` entry-file selection (or a clear diagnostic naming the sibling
    file); the gate/runner list so `examples/webserver` cannot rot again — **without** adding
    to `make ci`'s runtime if that would tempt anyone to run it more than once a day;
    `docs/spec/14-ffi.md`; the missing `docs/bootstrap.md` (write it or remove the citations);
    the citation gate's source→doc direction.
  - Done when: a sibling `.ty` no longer breaks a build (or the error names it), a gate or
    runner covers `examples/webserver`, the FFI classification shape is in the spec, and no
    live file cites a document that does not exist.
  - Verify: two scratch programs in one directory, one built, real output captured; the new
    coverage reddened deliberately once (break `examples/webserver`, see it fail, fix it);
    the citation gate run over the tree.

  #### Phase 8 — DONE. Evidence

  ##### Item 1 (`FRICTION.md:144`) — the sibling collision is a DIAGNOSTIC bug, not a scan bug

  Three facts the item did not have, all read out of the source before anything was
  written:

  1. **The directory scan is conditional.** `src/tychoc.c:12069-12071` picks
     `compile_package(input, pkg)` over single-file `parse_program(toks.v)` only when
     `detect_package` finds a `package` header. Measured: two *headerless* scratch
     files side by side build fine (the sibling is never read); two `package main`
     files collide. So the trap is armed **exactly** when a scratch program imports
     the corelib — which is what every probe in this plan is.
  2. **A package may legally span files.** `tests/pkg/multifile/{main,util}.ty` is
     the committed fixture. "Compile only the entry file" would delete a documented
     feature to improve a message, so it was not on the table (Anti-scope).
  3. **Which file gets blamed depends on sort order.** `scan_pkg_files` qsorts
     (`src/tychoc.c:11759`) and the check fires at the *second* definition. Measured
     both ways on the pre-fix compiler:

     ```
     sibling named aprobe.ty (sorts FIRST):
       sib5/main.ty:5: error: 'main' is already defined      <- blames the file you asked for
     sibling named probe2.ty (sorts LAST):
       sib4/probe2.ty:5: error: 'main' is already defined    <- blames the sibling, unexplained
     ```

  Fix: `die_dup_proc` + `dup_other_file` (`src/tychoc.c`, **13 code lines**, +37/−2 with
  comments) scan the ProcVec for a same-named proc in a **different** file and name it.
  After:

  ```
  .../sib5/main.ty:5: error: 'main' is already defined -- also at .../sib5/aprobe.ty:5,
      a DIFFERENT file in the same package: tychoc compiles every .ty beside the entry
      file, so two unrelated programs cannot share one directory
       5 | fn main():
  ```

  A same-file duplicate keeps the plain message (`sib6/main.ty:6: error: 'f' is already
  defined`) — self-evident, and lengthening it would be noise. `tests/pkg/multifile`
  still builds and prints `area=42`.

  **Proof it touched nothing but an error path:** `--emit-c` output compared against a
  compiler built from `git show HEAD:src/tychoc.c`, over all 15 entry points →
  `emitted C compared for 15 entry points, differing = 0`.

  ##### Item 2 (`FRICTION.md:155` + Phase 7's out-of-scope finding) — two holes, two fixes

  **(a) No gate compiled `examples/webserver`.** `scripts/ci.sh` step 3 builds corelib,
  corelib-examples, site, raytrace, mandelbrot — and nothing else with an entry point.
  New `scripts/entrypoints.sh` (`make entrypoints`, CI step **3b**) compiles all 11
  entry points under `examples/` plus `server/main.ty` with `--emit-c`, which stops
  before `cc`: no link, no libcurl/sqlite3/libpng, ~7 ms per program, so it is not a
  reason to run `make ci` less often (the phase's stated constraint).

  Reddened deliberately **twice**, with the runner, never `make ci`:

  ```
  restore `if lr < 0` in examples/webserver/main.ty (this item's own breakage):
    FAIL    examples/webserver/main.ty
            examples/webserver/main.ty:199: error: ordering compares two ints, two
            floats, two strings, or two values of the same numeric newtype
    entrypoints: FAILED (1 of 11 entry points do not compile)
  rename examples/webserver/main.ty away (the VACUITY case, phase 1b's lesson):
    entrypoints: MUST-COVER FILE GONE: examples/webserver/main.ty -- this lane asserts
    LESS than it claims; fix the list or restore the file
  restored: entrypoints: ok (11 entry points compile with tychoc)
  ```

  **(b) The freeze-enforcement story, and the Phase 6 / Phase 7 contradiction —
  RESOLVED IN PHASE 7's FAVOUR, measured.** The question is whether `core:cli` is
  inside the frozen `tychoc0`'s reach. It is:

  ```
  ./tychoc compiler/tychoc0.ty -o T/tychoc0        (as the four runners do)
  T/tychoc0 examples/weblog/main.ty > T/wl0.c      -> exit 0
  grep -c 'cli__' T/wl0.c                          -> 81
  ```

  The frozen compiler compiles `corelib/cli/cli.ty`. **Phase 6's own evidence contains
  the error** (`plan.md:1745-1754`): it enumerated `examples/weblog/` as a `core:cli`
  consumer and then wrote "none of them a tychoc0 input", while
  `examples/weblog/run.sh:24` feeds `examples/weblog/main.ty` to a freshly built
  `tychoc0`. Its `frontparity` 288 / 0 could not have contradicted it, because that
  script never fed `examples/<dir>/main.ty` — the blind spot *is* the reason the wrong
  conclusion looked confirmed. **Where a future reader will hit both claims:**
  `plan.md:1729-1744` is now an in-place `CORRECTED BY PHASE 8` block quoting this
  measurement and pointing at the authoritative statement; `plan.md:2160-2167` (Phase
  7's finding) needed no change; `docs/spec/appendix-e-conformance.md` §E.2 already
  had it right and is now updated to say the split is *checked* rather than argued;
  `FRICTION.md`'s phase-7 line is struck with the same numbers. Phase 6 was harmless
  in fact — `cli.argv`/`parse_spec` use no post-freeze syntax — and that is luck, not
  method.

  **Then the blind spot itself was closed, because it is 6 lines of glob.**
  `scripts/frontparity.sh` now also feeds the four per-example entry points its own
  runners feed (`examples/{fetch/main,sqlite/demo,weblog/main,webserver/main}.ty`),
  plus the `TYCHO_CORELIB` export they need, and is fail-closed if one is gone.
  **288 → 292 agreed, 0 diverged.** Reddened deliberately in the shape Phase 7 said
  no gate could catch — a freeze-blocked corelib package adopting new syntax:

  ```
  give corelib/cli/cli.ty a `\r` escape (tychoc accepts it; tychoc0:195 rejects it):
    EXTENDED script:  FAIL examples/weblog/main.ty (tychoc ACCEPTED it, tychoc0 REFUSED it)
                        lex: unsupported string escape (use \n \t \\ \")
                          43 |     return "\r\n"
                      agreed: 291   diverged: 1
    HEAD's script, IDENTICAL TREE:
                      agreed: 288   diverged: 0
                      frontparity: all green
  restored: agreed: 292   diverged: 0
  ```

  `server/` and `examples/corelib/{result,httpd}` are excluded **by name, with the
  measured reason** (`server/` → `parse: line 2348: unexpected token`;
  `examples/corelib/result` → `parse: line 571: unexpected token`;
  `examples/corelib/httpd` → `lex: unsupported string escape`): those are the
  witnesses deliberately written outside the freeze, so globbing them would redden the
  lane at intended state. `examples/{life,minesweeper,raytrace,site,snake,mandelbrot}`
  are excluded because no `tychoc0` runner feeds them — including them would make the
  lane assert **more** than the freeze requires and turn a free program into a blocked
  one. Residual, stated plainly: `frontparity` is still not in `make ci` (it is a
  removed gate's harness, kept on disk on purpose), so enforcement is a runner you
  invoke — but it is now **one** runner that sees the whole reach instead of four that
  each see a slice.

  ##### Item 3 (`FRICTION.md:145`, `:154`) — the FFI classification shape is now §24.1.1

  `docs/spec/14-ffi.md` gains **§24.1.1 "Returning a payload and a classification"**:
  the `inout` classification alongside the payload return, `-> Result(T, E)` stated as
  absent **by decision** (a Tycho aggregate has no flat C ABI; the wrapper on the Tycho
  side is what makes the `Result`), and the ordering rule neither shim's comment states
  as a *rule* — written params in written order, an `inout` becoming `T*`, then a
  `bytes`/array return **appending** two trailing out-params, so the classification
  pointer sits ahead of the payload's although written last. Read out of
  `gen_extern_proto` (`src/tychoc.c:10385-10397`) and confirmed against emitted C:

  ```
  extern void netx_read(tycho_int , tycho_int , tycho_int *, unsigned char **, tycho_int *);
  extern void iox_read_file(char *, tycho_int *, unsigned char **, tycho_int *);
  ```

  Plus the four rules that make it safe (numeric-scalar-only, set the failure code
  first, leave `*out = NULL`, the `Result` is built on the Tycho side) and the half
  neither shim says: **when not to use it** — `iox_stat_kind(path) -> int` carries the
  same four codes with no `inout`, because there the kind is the whole answer. Both
  shims are cited as worked examples, so the third one copies a spec, not a sibling.

  ##### Item 4 (`FRICTION.md:163`) — the premise was false, and that is the finding

  "No corelib function in the tree returns one" was wrong **by five**:
  `strings.split_once` (`corelib/strings/strings.ty:193`) and `path.split_path`
  (`corelib/path/path.ty:95`) date to `39d75be`, `datetime.parse_offset -> (int, bool)`
  — the value-and-verdict shape the item needed, exactly — to `4c7f8a5`, plus
  `bignum.divmod` and `datetime.civil_from_days`. So the three alternatives were not
  written for want of a demonstration; they were written because **§5.3.3 was four
  sentences with no example, no citation and no statement of what the shape is for.**
  §5.3.3 now says a tuple is the shape for "a value AND a classification", why the two
  alternatives cost more, that a `Result` element may be written inline since §6.2(7),
  and carries a **runnable, gated** example plus all five corelib functions by
  `path:line` and a pointer to §24.1.1 for the C boundary:

  ```
  sh scripts/spec_check.sh -> spec-examples: ok docs/spec/03-types.md:231 (tychoc)
                              spec-examples: 8 runnable example(s), all pass   (was 7)
  ```

  ##### Item 5 (`FRICTION.md:238`) — write it, and gate the direction that missed it

  Two corrections to the item: the `Makefile` **no longer mentions `bootstrap`**
  (`grep -c bootstrap Makefile` → `0`; phase 0 removed the target), and the live
  citations are **three**, not two — `compiler/tychoc0.ty:617`, `compiler/run.sh:3`,
  `compiler/fixpoint.sh:2`. Since `compiler/tychoc0.ty` is **frozen**, "remove the
  citations" was never an available option: writing the document was the only way to
  satisfy "no live file cites a document that does not exist."

  `docs/bootstrap.md` names the stages those headers cite by number — Stage 1
  (`compiler/run.sh`'s differential over 51 `compiler/tests/` fixtures), Stages 2–3
  (the A/B/C self-emission chain), Stage 4 (`fixpoint.sh`'s byte-identical `B == C`),
  Stage D (package programs both ways), Stage E (`pkg-split.sh`) — states that **none
  is a gate any more**, and carries the two consequences that keep costing time: the
  freeze reaching 13 corelib packages, and tychoc0's own `:N` self-citations being off
  by −50.

  **The general form of the bug is now gated.** `scripts/check_citations.py` grew a
  second direction: every tracked **non-Markdown** file under a wider prefix set
  (`DOC_SCAN_PREFIX`, which adds `Makefile`, `bench/`, `fuzz/`, `server/`, `editors/`,
  `.githooks/`) is scanned for `docs/<...>.md` mentions; the document must exist, with
  line bounds checked when a `:N` is present. `SRC_PREFIX` was **not** touched — moving
  it would change the md→src pass. Proven against the pre-fix state:

  ```
  mv docs/bootstrap.md aside; python3 scripts/check_citations.py
    STALE  compiler/fixpoint.sh:2   `docs/bootstrap.md` -> NO SUCH DOCUMENT
    STALE  compiler/run.sh:3        `docs/bootstrap.md` -> NO SUCH DOCUMENT
    STALE  compiler/tychoc0.ty:617  `docs/bootstrap.md` -> NO SUCH DOCUMENT
  ```

  **On its first real run it found four more instances of the same bug**, none of them
  `bootstrap`: `docs/memory-model.md`, `docs/ffi.md` and `docs/map-mutation.md` (twice)
  cited by `bench/prongB/iter_transform.ty:10`, `corelib/crypto/crypto_shim.c:19`,
  `src/tychoc.c:4523` and `tests/map_mutation.ty:1` — all three documents had moved
  into `docs/guides/` and no gate could see it. Four one-path fixes. The lane is
  deliberately narrow, and its header says what it cannot catch (a document that
  exists but does not say what the comment claims; a directory or suffix-less mention;
  a bare prose mention inside another Markdown file, because the archived internals
  docs quote paths that were true when written and are a record, not a claim).

  ##### Citation gate, because `src/tychoc.c` grew 35 lines

  Twelve anchored citations staled by the uniform `+35` shift below the insertion at
  `:7368`, across `docs/spec/15-program.md`,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` and
  `plan-front-door-DONE.md`. Proven to be **this phase's** redness before shifting
  anything (each token verified present at the same range in `git show HEAD:src/tychoc.c`)
  and shifted by the computed delta: `7453`→`7488`, `7478-7479`→`7513-7514`,
  `11856-11861`→`11891-11896`, `11933-12037`→`11968-12072`, `12115`→`12150`.

  ##### Verify — every command actually run, in the foreground, on this tree

  **`make ci` / `make test` NOT run — the day's single run was spent by Phase 1
  (`5187724`).** `src/tychoc.c` WAS touched, so the compiler-phase mitigation applies;
  in place of a full `server/` live matrix the change was proven to be error-path-only
  by byte-comparing emitted C against a HEAD-built compiler over all 15 entry points
  (0 differences), plus a live 4-worker smoke on `127.0.0.1:18099`. The by-hand list
  phases 2–7 used, reused verbatim:

  ```
  cc -O2 -fwrapv -Wall -Wextra -std=c11 -Ibuild src/tychoc.c  -> 0 warnings
  ok compile corelib/test/{httpd,io,net,result,cli}/main.ty
  ok compile examples/corelib/{httpd,io,net,result,cli}/main.ty
  ok compile examples/{fetch,site,weblog,webserver}/main.ty
  ok compile server/main.ty                       -- 15 entry points, 0 failures
  emitted C vs HEAD-built tychoc, same 15 entry points -> differing = 0
  sh corelib/run.sh            -> "corelib: all green (tychoc matches goldens)"
                                  (skip image -- libpng absent in this environment)
  sh examples/corelib/run.sh   -> "corelib examples: all green"
  sh examples/webserver/run.sh -> "webserver: ok (tychoc == tychoc0 == golden)"  -- 9th golden
  sh scripts/entrypoints.sh    -> "entrypoints: ok (11 entry points compile with tychoc)"  NEW
  sh scripts/frontparity.sh    -> agreed: 292  diverged: 0   (was 288 / 0; +4 entry points)
  sh scripts/tools_check.sh    -> "tools-check: ok"  (pkgresolve + bytes-rehome lanes green)
  sh scripts/spec_check.sh     -> Appendix A matches; Appendix E resolves;
                                  8 runnable examples, all pass (incl. 03-types.md:231)
  sh scripts/check_links.sh    -> ok (129 markdown files, no dead relative links)
  python3 scripts/check_citations.py --stats
                               -> 22 anchored, 1549 bare, 76 source->doc : ok
  live: ./tycho-httpd --workers 4 --idle-ms 800 on :18099
        GET /            200 (2846 B)   GET /style.css       200 (1912 B)
        GET /nope.html   404 (789 B)    GET /../../etc/passwd 403 (797 B)
        GARBAGE          400            SIGTERM -> clean exit
  git diff --stat corelib/ examples/ server/ -> ONLY corelib/crypto/crypto_shim.c
                                  (+1/-1, the docs/guides/ffi.md path fix). NO golden moved.
  ```

  ##### Out of scope, found, not absorbed

  - Nothing new beyond the four stale doc paths, which were fixed because the gate this
    phase added cannot be green while they exist (and a gate left red is worse than no
    gate — phase 1b). `docs/bootstrap.md` is **not linked from `docs/README.md`**; the
    link checker checks links, not orphans, so this is a one-line note rather than a
    defect: a future docs-index pass should list it.

- [x] **Phase 9 — the two items that need a decision, not a patch**
  - Item A: `FRICTION.md:131` — `parallel for` and `spawn` are the only concurrency shapes
    and neither expresses "hand this connection to whoever is free", so one worker owns one
    connection for its whole life and **N workers is a hard cap of N concurrent
    connections**. There is no way to write an event loop or a work queue over accepted fds
    without a channel of ints and a hand-rolled dispatcher. This is the largest open item in
    the file and the only one that limits what the server can *be*.
  - Item B: `FRICTION.md:149`/`:150` — two error types in one function make `or_return`
    unavailable again (no `map_err`, no conversion between error enums), and converting a
    call a big block consumes costs an **indentation level**: `serve_conn` went 60 → 71 lines
    because `match httpd.parse_request(raw)` wraps the whole body. There is no `if let` and no
    early-return binding form.
  - Scope: **cost both by reading `src/tychoc.c` and `runtime/tycho_rt.c`, then decide.** A
    channel-of-fds work queue may be writable today with what exists (the file says a
    hand-rolled dispatcher is possible) — if so, write it in `server/` and measure against
    the recorded 79,712 req/s. `map_err` may be a library function once Phase 1 lands. An
    `if let` is a language addition and may be the right refusal.
  - Done when: each item is either landed with a measurement or refused in `FRICTION.md`
    with a real cost estimate from reading the source. **A refusal with a number is a pass.**
  - Verify: whatever lands, measured against the recorded baseline (79,712 req/s, 8 clients);
    whatever is refused, the estimate cites `path:line`.

  #### Phase 9 — DONE. Evidence

  Item A: **REFUSED, with the numbers, and the item's premise CORRECTED by measurement.**
  Item B: **half LANDED (`result.map_err`), half REFUSED with a number (the binding form).**
  `make ci` / `make test` were **NOT** run — the day's single run was spent by Phase 1
  (`5187724`). Everything below is a by-hand runner or a live socket.

  ##### The baseline, reproduced on THIS machine first

  Same-machine comparison, so the numbers below mean something. 16 cores
  (`nproc` = 16), `./tycho-httpd --root server/www --port 18099 --workers 8 --quiet`,
  `GET /data.json` (294 B — the recorded body size), N client **processes** (not
  threads), one keep-alive connection each, 800 requests on it:

  ```
                          recorded (plan-webserver-DONE.md:814-816)   this machine
    1 client  x 800 req                12,456 req/s                    14,829 req/s
    4 clients x 800 req                41,046 req/s                    50,150 req/s
    8 clients x 800 req                79,712 req/s                    93,441 req/s
  ```

  This box is ~1.17× the recorded one and the shape is unchanged (linear in client
  count). A second shape was added because it is the one a work queue actually
  changes — **connection churn**, one request per connection (`Connection: close`),
  so accept+dispatch is paid 800 times instead of once:

  ```
    1 / 4 / 8 clients x 800 churned connections    4,696 / 23,613 / 40,292 req/s
  ```

  And the **concurrency cap**, reproducing `plan-webserver-DONE.md:803-808`: S peers
  that send a partial head and go silent each pin one worker; then time a real request.

  ```
    workers=4  silent peers=3  ->  HTTP/1.1 200 OK  in     1 ms   served at once
    workers=4  silent peers=4  ->  HTTP/1.1 200 OK  in  4753 ms   waited for a worker
    workers=8  silent peers=7  ->  HTTP/1.1 200 OK  in     1 ms   served at once
    workers=8  silent peers=8  ->  HTTP/1.1 200 OK  in  4829 ms   waited for a worker
  ```

  ##### Item A (`FRICTION.md:134`) — the work queue IS writable today, and it does not
  ##### lift the cap. Both numbers, side by side.

  **The premise is wrong and that is the finding.** The item says there is no way to
  express "hand this connection to whoever is free" without "a channel of ints and a
  hand-rolled dispatcher". There is: **the channel of ints and the ten-line dispatcher
  compile today, with no compiler change**, and the dispatcher is not a workaround —
  it is the shape. Verified from the source first: `Channel(T)` has type syntax and a
  spawned worker may take one as a parameter (`src/tychoc.c:612-616`, and the guards
  at `:1095-1097` / `:7450` forbid only *storing* or *returning* a handle), `send`/
  `recv`/`close` are builtins over it (`:5392`, `:5401`, `:5412`), and a channel is
  the language's one deliberately-shared object with a documented MPMC contract —
  "with multiple receivers, each value is delivered to exactly one receiver"
  (`docs/spec/13-concurrency.md` §23.1). Then measured with a scratch program: one
  channel, four recursively-fanned-out workers, 1000 sends → `total = 1000`.

  So it was written in `server/` (**+39 / −1 lines**: `queue_loop`, `queue_worker`, and
  a `main` that owns the accept loop and sends fds into `channel(int, 64)`), built, and
  run against the same drivers. `--workers N` now means N servers **plus** the acceptor.

  ```
                              HEAD (N independent accept loops)   work queue (1 acceptor + N)
    keep-alive, 1 client              14,829 req/s                    15,309 req/s
    keep-alive, 4 clients             50,150 req/s                    50,647 req/s
    keep-alive, 8 clients             93,441 req/s                    91,667 req/s   (-1.9%)
    churn,      1 client               4,696 req/s                     5,984 req/s
    churn,      4 clients             23,613 req/s                    21,669 req/s
    churn,      8 clients             40,292 req/s                    47,205 req/s   (+17.2%)

    cap: workers=4  silent=3            1 ms                            1 ms
    cap: workers=4  silent=4         4753 ms                         4824 ms
    cap: workers=8  silent=7            1 ms                            1 ms
    cap: workers=8  silent=8         4829 ms                         4830 ms
  ```

  **Throughput: a wash.** −1.9% on the recorded keep-alive shape (inside run-to-run
  noise), +17.2% on connection churn — one dedicated acceptor beats eight threads
  contending on `accept(2)`, which is a real result and the only thing in the work
  queue's favour.

  **The cap: UNCHANGED, to the millisecond.** This is the answer the phase exists to
  give. N workers is still a hard cap of N concurrent connections, because the cap was
  never a property of *dispatch* — it is a property of a worker **blocking in
  `recv(2)`**. A work queue chooses which idle worker gets the next fd; it cannot make
  a busy worker idle. Worse, it moves the backlog from the kernel to userspace and so
  makes an unserved connection *look* served:

  ```
    8 workers, all 8 pinned by silent peers, then 40 COMPLETE requests:
      40 complete requests handed to the acceptor in 3 ms (all connected)
      first queued request answered within 1s: False  -- accepted, queued, UNSERVED
  ```

  The kernel's backlog at least applies `listen()`'s limit and lets `connect()` block;
  the channel accepts, ACKs and then says nothing. **Not adopted** — reverted, and
  `server/main.ty` is byte-identical to `HEAD`. The full diff is preserved as the
  reproduction recipe: two functions (`queue_loop` = the `accept_loop` body with
  `match recv(ch)` in place of `match net.accept(srv)` and a `None` arm for
  closed-and-drained; `queue_worker` = the identical recursive fan-out) plus a `main`
  that does `ch := channel(int, 64)` / `pool := spawn queue_worker(cfg, ch, 1,
  cfg.workers)` / `for dispatching: match net.accept(srv): Ok(conn): send(ch, conn) /
  Err(e): dispatching = false` / `close(ch)` / `total := wait(pool)`.

  ###### What WOULD lift the cap, costed, and refused with the number

  Readiness notification — one thread holding many connections. Read out of the source:

  1. **`core:net` has no readiness call and no non-blocking mode.**
     `corelib/net/net_shim.c` is 361 lines with 12 exported entry points
     (`:110`, `:162`, `:170`, `:185`, `:204`, `:225`, `:271`, `:300`, `:316`, `:324`,
     `:339`, `:350`) and not one is `poll`/`select`/`epoll` or `O_NONBLOCK`; the only
     timeout control is `SO_RCVTIMEO` (`netx_set_read_timeout`, `:300`). The syscall
     itself is the CHEAP part: `[int]` crosses the FFI in both directions as a
     `(const long*, long)` pair (`docs/spec/14-ffi.md` §24.1), so
     `netx_poll(fds, n, ms, out, outlen)` is ~45 C lines and `netx_set_nonblocking`
     ~8, plus ~20 in `corelib/net/net.ty` (189 lines) for `poll_ready`,
     `set_nonblocking` and an `Again` variant on `NetErr`.
  2. **`httpd.read_request_capped` is a BLOCKING accumulator and cannot be reused.**
     `corelib/httpd/httpd.ty:242-303` holds `buf`, `need`, `reading`, `why`, `cut` as
     **locals** (`:243-247`) and loops `net.read(fd, want)` (`:271`) until the head is
     complete. An event loop needs that state to survive between poll wakeups, keyed
     by fd — so it must be split into a `struct Pending` plus `feed(state, chunk)` and
     `finish(state)`, ~60 lines, **additive rather than replacing**: three consumers
     call the blocking form, and this package is compiled by the frozen
     `compiler/tychoc0.ty` (`examples/webserver/run.sh:24`), so the new struct and
     functions must also parse there.
  3. **`serve_conn` is one blocking loop over one fd, and so is the write path.**
     `server/main.ty:363-473` is **70 code lines**; `netx_write` loops `send()`
     internally (`corelib/net/net_shim.c:225`), so a slow reader would stall the event
     loop unless writes are buffered per fd too — ~150 lines replacing 70.

  **~283 lines across 4 files, ~80 of them inside the frozen-`tychoc0` reach**, and it
  is a redesign of `core:httpd`'s read surface, which this plan's Anti-scope forbids in
  as many words ("One item, one fix. No redesigns"). **Refused, with that number.**
  Two things a future reader should weigh against it: the cap is already **tunable** —
  `--workers` accepts 1..256 (`server/main.ty:564`) and the live-task ceiling is 1024
  (`runtime/tycho_rt.c:557-562`) — and the item's own headline claim, that neither
  concurrency shape can express work dispatch, is now measured false.

  ##### Item B, first half (`FRICTION.md:152`) — `map_err` LANDED, 4 code lines

  Writable in Tycho today as a plain library function, and Phase 1's fix is what made
  it usable: **three** type params, with `$T` passing through untouched and only the
  error type moving.

  ```tycho
  fn map_err(r: Result($T, $E), replacement: $F) -> Result($T, $F):
      match r:
          Ok(v): return Ok(v)
          Err(e): return Err(replacement)
  ```

  **4 code lines** in `corelib/result/result.ty` (+35 with the header explaining the
  choice). The recorded call site is `examples/corelib/httpd/main.ty:22`, whose
  `round_trip` returns `Result(int, net.NetErr)` while `httpd.read_request` returns
  `Result(Request, httpd.ReqErr)`:

  ```
  before:  served := result.unwrap_or(httpd.read_request(conn_in), httpd.bad_request())
  after:   served := result.map_err(httpd.read_request(conn_in), net.Failed) or_return
  ```

  Same line count, different behaviour: the old spelling **continued with a dummy
  `Request` nobody sent**; the new one ends the function. The example's golden is
  **byte-identical** (the exchange succeeds, so the changed path is not taken) — which
  is the proof this touched only the failure path. Regression: `two_types` in
  `corelib/test/result`, whose golden is **+4 / −0**, a pure append:

  ```
    map_ok    = 21        # 7 * 3 -- the Ok payload passed through map_err untouched
    map_err   = -1        # or_return carried the re-labelled failure out
    map_why   = 1         # ... as io.Failed, the caller's own error type
    map_thru  = hi        # a string payload: $T is genuinely generic
  ```

  **The freeze is satisfied, measured not assumed:** `core:result` is inside the frozen
  `tychoc0`'s reach (`core:httpd` imports it and `examples/webserver/run.sh:24` feeds
  the package to a freshly built `tychoc0`), and the three-type-param generic compiles
  there — `webserver: ok (tychoc == tychoc0 == golden)`.

  **Why a value and not a function, recorded because the alternative was built and
  measured, not assumed.** The function-value form compiles too —
  `fn map_err(r: Result($T, $E), f: fn($E) -> $F) -> Result($T, $F)` with
  `Err(e): return Err(f(e))` builds and runs, mapping `A2 -> B2` through a named
  `conv`. It was still not adopted: the caller who needs this has one target variant in
  mind, so the function form charges every call site a **named two-line helper to
  convert an enum** — which is the hand-written collapse this item exists to delete —
  and nothing else in `core:result` takes a callable (`unwrap_or`/`err_or`/`some_or`
  all take a plain fallback). The cost `map_err` does charge is written at the
  declaration: **the original cause is gone.**

  ##### Item B, second half (`FRICTION.md:153`) — the binding form, REFUSED with a number

  **`if let` is not what the item needs, and that is the first finding.**
  `if let Ok(req) := httpd.parse_request(raw):` still puts the whole body inside its own
  block at the same depth — it saves an arm, not an indentation level. Only an
  **early-return binding form** (`x := e or_else: <diverging block>`) flattens anything.
  Costed both:

  - **`if let` as parser-only sugar: ~45 lines.** The machinery exists. `parse_match`
    (`src/tychoc.c:2734`) already parses arm patterns into a `MatchArm` carrying
    `variant`, `binds[8]`, and Phase 3's `sub`/`subbinds`/`sub_vi` (`:1456-1458`); the
    resolver (`:6799`) and codegen (`:9304`, `:10113`) already handle an ordered
    Ok/Err decision chain. So it is: a contextual `let` after `TK_IF`, the arm-pattern
    parser factored out of `parse_match`, and a synthetic two-arm `S_MATCH`. **It
    would not close the item**, so it was not built.
  - **The early-return binding form: ~105 code lines in `src/tychoc.c`.** A token
    beside `TK_ORRETURN` (`:123`); a parse hook on the decl path where
    `x := if/match` already lives (`:3260`); a resolver unwrap in the `S_DECL` arm that
    grounds `x` to the Ok payload; a divergence check reusing `expr_diverges`
    (`:2847`) and `block_ends_in_return` (`:9298`); codegen; and diagnostics beside
    the existing `or_return requires the enclosing function to return a Result, but it
    returns %s` (`:4795-4796`). Plus a spec section beside §14.6 `or_return`
    (`docs/spec/10-statements.md:110`), plus a fixture that — being **new syntax** —
    can live only in `corelib/test/` or `server/`, never in `tests/` or `examples/`
    (frozen `tychoc0`; `scripts/frontparity.sh:127` reports it as a divergence).

  **And the payoff, measured rather than assumed.** `serve_conn`
  (`server/main.ty:363-473`) is **70 code lines** with **six** arms: five `Err` causes
  that each answer differently (`:390-414` — silent close, silent close, 408, 431, 400)
  and `Ok(req)`, which holds **45 of the 70**. A binding form moves those 45 out one
  level and pushes the five causes into the `or_else` block, where they are still the
  same five-way match. **Net line change ≈ 0**, one indentation level, in **one**
  function in the whole tree, unusable in `corelib/`. **105 compiler lines for that:
  refused.**

  The item's own number needs a footnote a future reader should have: the recorded
  60 → 71 growth was attributed to "no `if let`", but plan.md phase 3 has since turned
  what was one `Err(e)` plus five `==` tests into five real arms **on purpose**,
  because acting on the cause is the whole point of the conversion. A happy-path
  binding form pushes them back into one block. The 11 lines were not bought by a
  missing keyword; they were bought by a function that has six outcomes.

  ##### Verify — the by-hand sweep, real output

  ```
    scripts/entrypoints.sh    entrypoints: ok (11 entry points compile with tychoc)
    scripts/frontparity.sh    agreed: 292   diverged: 0   (skipped, tychoc refused: 15)
                              all green (tychoc0's frontend accepts every program tychoc accepts)
    corelib/run.sh            corelib: all green (tychoc matches goldens)   [ok result]
    examples/corelib/run.sh   corelib examples: all green
    examples/webserver/run.sh webserver: ok (tychoc == tychoc0 == golden)
    scripts/tools_check.sh    tools-check: ok
    scripts/spec_check.sh     spec-examples: 8 runnable example(s), all pass
    scripts/check_links.sh    link check: ok (129 markdown files, no dead relative links)
    scripts/check_citations.py citation check: ok (22 anchored ..., 1585 bare in bounds,
                              77 source->doc citations resolve)
  ```

  Live matrix on `127.0.0.1:18099`, 4 workers, `--idle-ms 800`, phase 3's fail-closed
  driver — run even though `server/main.ty` is unchanged, because `core:result` is one
  of its imports: 200 / 404 / 403 / 405, `HEAD` with a real `Content-Length` and no
  body, three byte-identical binaries (PNG 7883, ICO 441, TTF 95440), `GARBAGE` → 400,
  `Content-Length: 0x10` → 400, 20 KiB head → 431, zero-byte hangup → no bytes and
  **0 log lines**, partial head then stall → 408 and 1 log line, idle past 800 ms → 0
  bytes and 0 log lines, `GET /emptydir` → `301 Location: /emptydir/`, keep-alive
  `200 200 200` on one fd, 50-request flood `50/50 200`, all four workers seen in the
  access log, `SIGTERM` → status 143. **`MATRIX OK: every assertion passed`.**

- [ ] **Phase 10 — re-score `FRICTION.md`, and settle this plan's Goal**
  - Walk every item in the file against the tree — phase 7 (`:118-131`), the created ones
    (`:133-161`, `:168-169`), and the earlier-phase list (`:219-235`, which includes the
    frozen-tychoc0 debris items that are *deliberately* kept and should be marked as such,
    not counted as open).
  - Rewrite `FRICTION.md:79`'s score section: closed, refused-with-reason, and deliberately
    kept — with no item left in "untouched, unexplained".
  - Then answer this plan's Goal honestly, to the standard the last plan was held to: what
    the fixes cost in library and compiler lines, whether `server/main.ty` got shorter this
    time (it went 371 → 380 last time and the plan said so), and which refusals a future
    reader should treat as the real remaining debt.
  - Done when: the score section matches the tree item for item, and the Goal verdict is
    written with numbers.
  - Verify: every "CLOSED" claim in `FRICTION.md` is checked against the code it names —
    spot-check by `path:line`, not by memory. A stale CLOSED is the one thing this phase
    exists to prevent.

## Out of scope

- **`compiler/tychoc0.ty`** — frozen 2026-07-26. `FRICTION.md:231-234` records four items
  about its debris (non-gated runners still building it, orphaned goldens, the `+50` citation
  correction). Those are **deliberately kept**, not open work; Phase 10 marks them as such.
- The two C-level defects at `FRICTION.md:171-179` (`SIGPIPE` killing the server, Nagle
  costing 620×) are **already fixed** in the shims and recorded as things a Tycho program
  could not have fixed. Nothing to do; do not re-open them.
- `FRICTION.md`'s "What was good" section (`:181-194`). It is the other half of an honest
  account and it stays exactly as written.
