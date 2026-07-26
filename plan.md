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

- [ ] **Phase 4 — `exit(code)`, and `die()` as a diverging call**
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

- [ ] **Phase 5 — the corelib's small honest wins**
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

- [ ] **Phase 6 — `core:cli` and `args()`**
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

- [ ] **Phase 7 — `bytes` gets operators**
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

- [ ] **Phase 8 — tooling and doc debt, where each item is a trap for the next person**
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

- [ ] **Phase 9 — the two items that need a decision, not a patch**
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
