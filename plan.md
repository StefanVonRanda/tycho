# Three things nothing checks

Previous plan complete and archived at
[docs/internals/plan-json-grammar-DONE.md](docs/internals/plan-json-grammar-DONE.md).

**This plan opens with a backlog audit** rather than with its own phases, because
the backlog had reached nineteen unchecked items across four rotations, three of
its numbers collided with each other, and two of its claims were false. The audit
is below under "Backlog audit, 2026-08-01"; the three phases are what it left at
the top.

## Goal

Close the three latent breakages that no gate in this tree can see:

1. **`str(float)` corrupts its output under a comma-decimal locale.** Not
   hypothetical — measured at `1,5.0`.
2. **The hand-linked shim lists go stale**, because they track direct imports and
   the dependency that breaks them is transitive. Twice now.
3. **A new lane's golden is invisible until someone clones**, because
   `.gitignore` ignores `*.out` broadly and un-ignores per directory.

Each is a break that ships green. Done looks like: all three fixed, and for 2 and
3 a **mechanical gate** so the next one reddens instead of shipping.

## Pre-flight

- **Worst case, phase 1:** the fix formats floats through a path that is correct
  on this host and wrong on another — a locale bug replaced by a portability bug,
  with the same symptom of nobody noticing. The mitigation is the one phase 1 of
  `docs/internals/plan-json-grammar-DONE.md` used and proved: hold `LC_NUMERIC`
  hostile inside the test itself, so the check runs under the broken locale on
  every host rather than depending on the grader's environment.
- **Worst case, phases 2 and 3:** a gate that cannot fail. Both are "walk a list,
  assert each entry" checks, which is exactly the shape that passes vacuously
  when the walk finds nothing. Each must be proved red by breaking a real entry,
  and each must print what it checked, not just a verdict.
- **Reversibility:** total. `runtime/tycho_rt.c`, harness scripts, and a
  `.gitignore` line; no data is at risk and every change reverts cleanly.
- **Verified — the three claims, re-checked against this tree today, not
  inherited:**
  - `runtime/tycho_rt.c@tycho_float_to_str` formats with `%.15g`, which takes its
    separator from `LC_NUMERIC`, then scans the result for `'.'`, `'e'`, `'E'` or
    an inf/nan marker to decide whether to append `".0"`. Under a comma locale
    `1,5` contains none of those, the guard fires, and the output is `1,5.0` —
    which is neither valid Tycho float syntax nor readable by any parser in this
    tree. Measured by `docs/internals/plan-json-grammar-DONE.md` phase 1's test
    hook, not inferred.
  - `examples/fetch/run.sh:60` hard-codes
    `SHIM="corelib/http/http_shim.c corelib/io/io_shim.c corelib/strings/strings_shim.c"`.
    The third entry was added on 2026-08-01 after `make ci` reddened with
    `undefined reference to strx_parse_double`; the second was added in 2026-07
    for the same class of break. `examples/site/run.sh` greps its own source for
    `core:` imports, which finds **direct** imports only — and the 2026-08-01
    break was transitive (`examples/fetch/main.ty` imports `core:json`, which
    imports `core:strings`, which owns the shim), so copying site's loop into
    fetch would have left the lane red.
  - `grep -rn 'error-unmatch' scripts/ Makefile` returns **nothing**: the gate
    described in the backlog for six weeks does not exist.
- **Assuming — to check, not facts:**
  - That the compiler already computes the transitive shim closure on the normal
    build path (which would be why only the hand-linked `--emit-c` legs ever
    break). If it does, phase 2 exposes it; if it does not, phase 2 is bigger than
    one flag and should say so rather than growing quietly.
  - That a `"C"`-locale float formatter is reachable from `runtime/tycho_rt.c`
    without a new dependency. `snprintf_l` is the obvious route and it is a GNU
    extension, the same trap phase 1 of the previous plan hit with `strtod_l`
    (`_GNU_SOURCE`, not `_POSIX_C_SOURCE`).

## Phases

- [x] **Phase 1 — `str(float)` renders in the `"C"` locale, whatever the ambient one**
  - Scope: `runtime/tycho_rt.c`, and a fixture under `tests/`.
  - Render through a `"C"`-locale conversion rather than the ambient one — the
    same shape as `corelib/strings/strings_shim.c`'s `strtod_l` fix, in the
    opposite direction. Whatever feature-test macro that needs is declared where
    the file already declares its own.
  - The `".0"` guard is a second defect hiding behind the first: it decides
    "does this look like a float?" by scanning for `'.'`. Even with the locale
    fixed, state in a comment what that guard's contract is and why the scan is
    sound once the separator is known.
  - Done when: `str(1.5)` is `1.5`, and `parse_float(str(v))` round-trips, **both
    under a hostile `LC_NUMERIC` held inside the test** and under the default.
    `-0.0`, `inf`, `nan` and a 17-significant-digit value each land on a stated,
    tested outcome.
  - Verify: `make test` — this is `runtime/`, so it is the gate, and the fixture
    count was `passed: 560 failed: 0`. Prove the check can fail by reverting the
    fix and showing the red. Not `make corelib`, which cannot see `runtime/`.

  **Evidence, 2026-08-01.** Changed: `runtime/tycho_rt.c@tycho_float_to_str`, plus
  `tests/float_str_locale.ty` and its golden `tests/float_str_locale.out`.

  **`snprintf_l` is NOT the route on glibc — the Pre-flight assumption was wrong
  in its specifics.** It is a BSD/macOS extension in `<xlocale.h>`; glibc does not
  declare it with or without `_GNU_SOURCE`. Measured, a five-line program under
  `cc -std=c11` with `_GNU_SOURCE` defined:

      lt.c:7:3: error: implicit declaration of function 'snprintf_l';
                       did you mean 'snprintf'?

  What is used instead is `uselocale()` against a `newlocale(LC_NUMERIC_MASK,
  "C", 0)` handle — POSIX 2008, present on glibc and the BSDs both, and already
  exposed by the `_DEFAULT_SOURCE` this file declares at `runtime/tycho_rt.c:34`,
  so **no new feature-test macro was needed**. It sets the calling *thread's*
  locale, which is what a runtime shared by pthread-backed Tycho tasks needs;
  `setlocale` would have been a data race. Measured on this host under
  `LC_ALL=da_DK.utf8`:

      ambient  = 1,5
      uselocale= 1.5
      restored = 1,5

  **The `".0"` guard's contract is written down** in the comment above the
  function: it answers "would this text be read back as an int?" and appends
  `.0` when it would. The scan for `'.' 'e' 'E' i/I n/N` is sound **only**
  because the separator is now known to be `'.'` — under any other separator
  every finite non-integral value looks integral to it, which is precisely how
  `1,5` became `1,5.0`. The comment says the two changes are one change.

  **Both locale runs.** `tests/float_str_locale.ty` prints the same nine values
  twice: once under the ambient environment, then again after flipping
  `LC_NUMERIC` to a comma-decimal locale through a runtime test hook it declares
  itself with `extern fn` (`corelib/test/strings/main.ty`'s pattern — the hook is
  in `runtime/tycho_rt.c` and not in any package API, because `tests/run.sh`
  builds a fixture with plain `cc … -lm` and no `--shim`, so a fixture has no
  other C to link). `hostile=1` sits between the blocks as proof the switch took;
  a host with no comma-decimal locale prints `hostile=0` and fails the golden
  loudly instead of re-testing `"C"` twice. `locale -a | grep -i da_DK` on this
  host lists `da_DK` and `da_DK.utf8`, and `da_DK.UTF-8` is the candidate that
  takes.

  The golden also matched with the hostile locale imposed from *outside* the
  process, which is the independent check that nothing leaked:

      $ LC_ALL=da_DK.utf8 ./nat | cmp - tests/float_str_locale.out   # silent
      $ LC_NUMERIC=de_DE.utf8 ./nat | cmp - tests/float_str_locale.out  # silent

  **Edge cases — each line is in the golden, identical in both blocks.** `rt=1`
  means `str(x)` re-read to a bit-identical `x`, sign included.

  | value | `str(x)` | `rt` | why that outcome |
  |---|---|---|---|
  | `1.5` | `1.5` | 1 | the case that used to be `1,5.0` |
  | `3.0` | `3.0` | 1 | integral: the `".0"` guard fires, correctly |
  | `-0.0` | `-0.0` | 1 | guard fires on `-0`; `signbit` compared, so it is not `0.0` |
  | `1e300` | `1e+300` | 1 | exponent already makes it non-integral |
  | `1e-300` | `1e-300` | 1 | as above |
  | `0.1+0.2` | `0.3` | **0** | 17 significant digits; `%.15g` is readable, not round-trip. Stated cost of the format, not a locale defect, and identical under both locales |
  | `inf` | `inf` | 1 | `'i'` in the scan set, so no `".0"` |
  | `-inf` | `-inf` | 1 | as above |
  | `nan` | `nan` | 1 | printed sign-stripped: the sign of an invalid-operation NaN is unspecified by IEEE-754 and is `-nan` on x86, `nan` on arm64. `rt=1` here means "came back a NaN" — a NaN is not equal to itself, so bitwise identity is not the contract |

  **Break and revert.** With `tycho_float_to_str` reduced to the bare ambient
  `snprintf` (both the `uselocale` leg and the `localeconv` fallback removed),
  rebuilt with `make`, the fixture goes red on exactly the two lines that carry a
  decimal separator — and reproduces the reported symptom byte for byte:

      13c13
      < 1.5         = 1.5  rt=1
      ---
      > 1.5         = 1,5.0  rt=0
      18c18
      < 0.1+0.2     = 0.3  rt=0
      ---
      > 0.1+0.2     = 0,3.0  rt=0

  Restoring the file and re-running `make` returns it to green (`cmp` silent).
  The seven other rows are unmoved by the break, which is honest: they have no
  separator to corrupt. The check is not vacuous — it fails for the real reason.

  **Gate.** `make test`: `passed: 561 failed: 0`, from `passed: 560 failed: 0`
  before. +1 is the new fixture and nothing was lost. The ASan/UBSan leg and the
  `-O2` leg produced byte-identical output with `detect_leaks=1` and no report,
  which is what makes the `arena_new`/`arena_free` pair inside the round-trip
  hook safe. `make corelib` was not run — it cannot see `runtime/`. `make ci` was
  not run — no CI step changed.

  **Citation fallout, and how it was repaired.** +113 net lines in
  `runtime/tycho_rt.c` moved every anchored ref pointing past them, and
  `python3 scripts/check_citations.py` reddened with 13 of them. Twelve are live
  prose or `> Provenance:` and were repointed, anchor kept: `:657`→`:658`,
  `:693`→`:694`, `:751`→`:752`, `:763`→`:764`, `:577`→`:578`, `:1005`→`:1006`,
  `:1184`→`:1185`, `:1284`→`:1397`, `:2427`→`:2540`, each verified by reading the
  new line and comparing it with `git show HEAD:runtime/tycho_rt.c`. Three sit in
  a before/after table in `docs/internals/plan-postfreeze-rawstring-DONE.md` —
  record lines by shape, two ref-bearing cells each — so `CLAUDE.md`'s rule
  applies literally: **the anchor was dropped and the number kept.**

  Bare refs into `runtime/tycho_rt.c` moved too and the gate cannot see them.
  They were **not** swept, which is this repo's stated position on bare refs
  rather than an oversight.

  One red was already there before this phase and is not fallout: `plan.md`'s
  backlog row 12 wrote `` `:366` `` bare, which inherits `FRICTION.md` from the
  row above and resolves to the wrong document. The intended subject is
  `Makefile:366`, confirmed by the four citing lines it describes —
  `scripts/asan_self.sh:11`, `scripts/asan_self.sh:72`,
  `scripts/editors_check.sh:29` and `scripts/check_citations.py:398`, every one
  of which spells it `Makefile:366@SKIPPED`. The row now names that path. Both
  doc gates are green: `check_citations.py` `ok`, `check_links.sh` `ok`.

- [x] **Phase 2 — the compiler prints its shim closure, and the hand lists go**
  - Scope: `src/tychoc.c` (a `--print-shims` or equivalent), `examples/fetch/run.sh`,
    `examples/site/run.sh`.
  - Expose the transitive closure the normal build path already computes, then
    have every `--emit-c` lane ask the compiler for it instead of maintaining a
    list. That retires `examples/fetch/run.sh@SHIM` and `examples/site/run.sh`'s
    direct-import grep at once.
  - **Check the Pre-flight assumption first.** If the compiler does not already
    compute a closure, say so and stop before writing a flag onto something that
    is not there.
  - Done when: both lanes derive their shim list from the compiler, and deleting
    `corelib/strings/strings_shim.c` from any hand list is impossible because no
    hand list remains. Prove it by re-running the exact break: a program whose
    only route to a shim is transitive.
  - Verify: `make fetch`, `sh examples/site/run.sh`, and `make test` if
    `src/tychoc.c` changed — which it will.

  **Evidence, 2026-08-01.** Changed: `src/tychoc.c` (`--print-shims`),
  `examples/fetch/run.sh`, `examples/site/run.sh`, and `docs/spec/15-program.md`
  §27.4, which is where this tree documents its other CLI flags.

  **The Pre-flight assumption HELD — the closure exists and is transitive.** It is
  not a separate pass: `merge_pkg` adds a package's co-located shim as the DFS
  *enters* it, at `src/tychoc.c:12429`, and then recurses into that package's own
  imports at `:12456`. So `g_shims` (`src/tychoc.c@add_shim`, which dedupes) is the
  transitive closure by construction, and `:12788` splices it onto the normal cc
  line. What was missing was any way to *read* it: `--emit-c` returns at `:12775`,
  before the cc line is ever built, so a lane linking the emitted C by hand had
  nothing to ask and guessed instead.

  **The flag.** `--print-shims` parses at `:12667` alongside `--symbols`, and
  prints at `:12753-12754` — after the package merge, deliberately *before*
  `check_finite_types`/`resolve_program`, because the shim set is a property of the
  import graph and not of the program type-checking. Nothing else goes to stdout,
  so a shell splices it straight onto a cc line. A single-file program with no
  package prints nothing and exits 0 (`./tychoc tests/for3.ty --print-shims` →
  empty, `exit=0`): an empty closure is an answer. `--print-shim` (singular) is
  still `tychoc: unknown flag`, so the new spelling did not widen what is accepted.

  **The transitive break, proved both ways on the real case.**
  `examples/fetch/main.ty` imports `core:http`, `core:io`, `core:json`,
  `core:path`, `core:sha256` — **not** `core:strings`. The shim it needs is reached
  through `corelib/json/json.ty`. Measured on this tree:

      old (grep the program's own core: imports):
        corelib/http/http_shim.c
        corelib/io/io_shim.c
      new (./tychoc examples/fetch/main.ty --print-shims):
        .../corelib/http/http_shim.c
        .../corelib/strings/strings_shim.c      <-- transitive, via core:json
        .../corelib/io/io_shim.c

  That missing third line **is** the 2026-08-01 break. The grep cannot see it by
  construction, which is why copying `examples/site/run.sh`'s loop into fetch would
  have left the lane red.

  **`examples/site/run.sh` was passing by coincidence, not by mechanism.** Its grep
  and the closure agree there (io, strings, datetime) — but only because
  `examples/site/main.ty` imports `core:strings` *directly*. The lane had no
  property that would have saved it; it had a program that happened not to need
  one. Both lanes now ask the compiler, and **no hand-maintained shim list remains
  in either**: `examples/fetch/run.sh`'s `SHIM=` is a command substitution and
  site's `for mod in $(grep …)` loop is deleted.

  **Break and revert — the lanes really do depend on the derived list.** Filtering
  the transitively-reached entry back out of fetch's derivation
  (`--print-shims | grep -v strings_shim`, i.e. reproducing exactly the stale state)
  and running `make fetch`:

      FAIL: sanitizer cc
            /tmp/…/san_src.c:3524:(.text+0x1f2b0): undefined reference to `strx_parse_double'
      fetch: FAIL

  which is the historical error string byte for byte. Restoring the line returns it
  to `fetch: green`. The check is not vacuous — it fails for the real reason, and it
  fails at the link step the stale list used to break.

  Note a *first* attempt at this break silently did nothing: a `sed` whose
  replacement text contained a `|` collided with its own `s|…|…|` delimiter and
  errored, and the "broken" run was in fact the unbroken file printing green. It is
  recorded because a break-proof that quietly fails to break is indistinguishable
  from a gate that cannot fail, which is the exact hazard the Pre-flight names.

  **Gate.** `make test`: `passed: 561 failed: 0`, `all green` — the same 561 as
  after phase 1. Nothing moved, which is the expected result: `--print-shims` adds
  a flag and returns early, and no fixture passes it. `make fetch` green,
  `sh examples/site/run.sh` green. Doc gates green after repointing one anchored
  ref that the +26-line `src/tychoc.c` insert moved — `docs/spec/15-program.md`'s
  `> Provenance:` line, `` `:12765@system(cmd)` ``→`` `:12791@system(cmd)` ``, live
  prose so the anchor was kept and repointed, verified by reading the new line
  (`int rc = system(cmd);`). `sh scripts/spec_check.sh` was also run, since
  `docs/spec/` changed: `9 runnable example(s), all pass`. `make ci` was not run —
  no CI step changed.

- [ ] **Phase 3 — every golden a runner names is tracked, and a gate says so**
  - Scope: a new check (its own script, or a step in an existing one),
    `.gitignore` if a lane turns out to be untracked, `scripts/ci.sh`,
    `CLAUDE.md`'s gate table.
  - Mechanical: for every `*/run.sh` that names a golden path, `git ls-files
    --error-unmatch` it. A lane whose golden is ignored fails on a fresh clone
    with `no golden -- run RECORD=1`, and nothing catches it today —
    `tools/tycho-ar/expected.out` shipped that way once and was caught by hand.
  - **The vacuous-pass risk is the whole difficulty.** The check must print the
    list of goldens it found, and it must be proved red by un-tracking one.
  - Done when: the check runs in CI, prints every golden it checked, exits
    non-zero for an untracked one, and `CLAUDE.md`'s table has its row.
  - Verify: the check itself, both directions; `sh scripts/ci.sh` lists the new
    step; and **`make ci` once, last**, since this phase adds a CI step. Never
    `make ci` as a debugging loop.

- [ ] **Phase 4 — the compiler emits float literals under the ambient locale too**
  - Found while doing phase 1, outside its scope lock (`src/tychoc.c` was locked),
    so it is filed here rather than absorbed.
  - `src/tychoc.c:9496` formats a float literal into the **generated C source**
    with `snprintf(b, sizeof b, "%.17g", e->fval)`, then applies the *same* `'.'`
    scan at `:9498-9499` to decide whether to append `".0"`. It is the identical
    defect phase 1 just fixed in `runtime/tycho_rt.c@tycho_float_to_str`, one
    layer up, and the blast radius is larger: the runtime one corrupted a string a
    human reads, this one corrupts **C source a compiler reads**. Under a comma
    locale `f(1.5)` would emit `f(1,5.0)` — which is not a syntax error but a
    comma expression, so `cc` accepts it and the program silently gets `5.0`.
  - The mirror side is at `src/tychoc.c:298`: the lexer reads a float literal with
    plain `strtod`, which under a comma locale stops at the `'.'` and returns `1`
    for `1.5`, exactly the trap `corelib/strings/strings_shim.c` documents.
  - Latent today for the same reason phase 1's was: nothing in this tree calls
    `setlocale`. That is an unstated dependency on a process global, not a fix.
  - Scope: `src/tychoc.c` only. The `"C"` handle and the fallback in
    `runtime/tycho_rt.c` are the pattern to copy; do not share state across the
    two files, they are separate translation units.
  - Done when: a fixture proves both directions under a hostile locale, and the
    `".0"` guard at `:9498-9499` carries the same written contract phase 1 gave
    its twin.
  - Verify: `make test` — `src/tychoc.c`, so it is the gate. Count is 561 after
    phase 1.

- [ ] **Phase 5 — the other two lanes still grep for direct imports, and they also need the `deps` closure**
  - Found while doing phase 2, outside its scope lock (which named
    `examples/fetch/run.sh` and `examples/site/run.sh` only), so it is filed here
    rather than absorbed.
  - `corelib/run.sh:32` and `examples/corelib/run.sh:30` carry the **same
    direct-import grep** phase 2 just deleted from the two example lanes —
    `for mod in $(grep … 'core:[a-z0-9_]+' …)`, mapping each to
    `corelib/<mod>/<mod>_shim.c`. Same defect, same blindness to a transitive
    dependency, and these two lanes cover far more programs than the two that were
    fixed: every `corelib/test/*/main.ty` and every `examples/corelib/*/main.ty`.
  - **`--print-shims` alone does NOT close them, which is why this is a phase and
    not a five-line follow-up.** Both lanes use the same loop for a second purpose:
    they read each module's sibling `<mod>/deps` file to decide whether to **SKIP**
    on a host missing a pkg-config dependency (`corelib/run.sh:33` and
    `:36-40`). A shim list does not carry that. Closing them properly wants a
    `--print-deps` (or one flag emitting both), and the compiler already
    accumulates it in `g_pkgdeps` via `add_pkg_deps` — but it accumulates resolved
    *cflags/libs*, not the pkg-config **names** the SKIP logic tests with
    `pkg-config --exists`, so the exposure is not a straight print of what exists.
  - `examples/fetch/run.sh:33` has the same gap in its third form: it hard-codes
    `DEPF="$(pkg-config --cflags --libs libcurl)"`, a hand-maintained *dependency*
    list beside the shim list phase 2 retired. It is stale-prone for exactly the
    reason the shim list was — a transitively imported package gaining a `deps`
    file would not appear in it.
  - Done when: no `run.sh` in the tree derives shims or deps from a grep over its
    own source, and the SKIP behaviour is preserved (proved by a host, or a forced
    probe, where a dep is absent).
  - Verify: `make corelib`, `make corelib-examples`, `make fetch`, and `make test`
    if `src/tychoc.c` gains the flag — which it will. Not `make ci`.

## Backlog audit, 2026-08-01

Nineteen unchecked items were carried across four plan rotations. Each was
re-checked against this tree — commands run, not claims inherited. **Two were
false**, and the numbering had collided: `docs/internals/plan-json-grammar-DONE.md`
filed new phases 6, 9 and 10 while carrying forward *different* phases 6, 9 and 10
from `docs/internals/plan-ar-DONE.md`. Six items shared three numbers, and the
collision is why this plan renumbers from 1 rather than continuing the sequence.

### Retired — closed, or delivered as filed

| Was | Claim | Verdict |
|---|---|---|
| ar phase 7 | `corelib/test/result/main.ty` says `return (Err(why), buf)` "still fails" | **FALSE, and corrected 2026-08-01.** `corelib/httpd/httpd.ty@read_request_capped`'s own comment already recorded the opposite as MEASURED — that the rewrite compiles and its typed local is "now a style choice ... not a requirement". Two files, one construct, opposite claims, and the wrong one sat in the test that exists to document the construct. `make corelib` green after the fix |
| ar phase 8 | `README.md` documents `make bootstrap` and `make fixpoint`; neither target exists | **TRUE, and fixed 2026-08-01.** `grep -cE '^(bootstrap\|fixpoint):' Makefile` returned 0. The row is removed; `README.md:101-104` already said the tychoc0 lanes were retired on 2026-07-29, so the table contradicted its own document |
| ar phase 15 | a parameter is borrowed read-only, so streaming state needs `inout` | **Delivered as filed.** It asked for a `FRICTION.md` entry and nothing else; it is at `FRICTION.md:1249`, and the ranked finding above it is the same defect at package scale |
| ar phase 16 | a package cannot mark a top-level function internal | **Recorded, not scheduled.** At `FRICTION.md:1440`. A language feature, not a defect — promoting it is a language plan with its own design question (what does `internal` mean across a package boundary), and nothing in the tree is blocked on it today |
| ar phase 17 | `chr(n)` is the only route from a number to a byte | **Recorded, not scheduled.** At `FRICTION.md:1460`. Its own filing measured the cost as "harmless at 120 bytes per file, and it would not be in a hot loop" |

Retiring the last three does not delete anything: the findings stay in
`FRICTION.md` in full, which is where a reader looks for what the language costs.
What is retired is the expectation that a phase is owed.

### Queued — verified still open, ranked

Ranked by harm × likelihood over cost. Phases 1-3 above are the top three.

| # | Item | Was | Still open? |
|---|---|---|---|
| 1 | `str(float)` locale corruption | json-grammar phase 6 | `runtime/tycho_rt.c@tycho_float_to_str` unchanged — **phase 1 above** |
| 2 | transitive shim closure | json-grammar phase 10 | `examples/fetch/run.sh:60` still a hand list — **phase 2 above** |
| 3 | golden-tracking gate | ar phase 19 | `grep -rn error-unmatch scripts/ Makefile` finds nothing — **phase 3 above** |
| 4 | `decimal.div(a, b, scale, mode)` | ar phase 20 | `grep -n 'fn div' corelib/decimal/decimal.ty` finds nothing. Blocks `select total / count`, the ordinary averaging query, on almost all real data |
| 5 | `core:json` does not validate UTF-8 | json-grammar phase 9 | `corelib/test/json.out`'s `cb ok high` line shows a raw `0xFF` parsing. One decoder closes both this and `esc`'s missing output side |
| 6 | `strings.parse_int` fails open | ar phase 13 | `corelib/strings/strings.ty:71` unchanged — and now **inconsistent with `parse_float` at `:164`**, which is strict and `Result`-returning. Two neighbours, opposite contracts |
| 7 | `io.write_bytes` | ar phase 9 | `corelib/io/io.ty` has `read_bytes` at `:124` and no byte-writing counterpart; writing bytes is `io.write(p, to_str(b))`, correct only by an accident of the runtime's length header |
| 8 | `io.make_dirs` (`mkdir -p`) | ar phase 11 | `corelib/io/io.ty:303` is still one `mkdir(2)`; `tools/tycho-ar/main.ty@mkdir_p` is 18 lines of caller-side chain building |
| 9 | mtime is readable, not writable | ar phase 12 | no `utimensat`/`utimes` in `corelib/io/io_shim.c` or `io.ty`. `tycho-ar` stores an mtime it cannot restore, and `diff -r` does not compare mtimes, so the gate cannot see it |
| 10 | incremental digest | ar phase 14 | no `update`-shaped function in `corelib/sha256`, `md5`, `crypto` or `hash`. The `FRICTION.md:1237` entry is the record; the code gap is real |
| 11 | `eprintln` | ar phase 10 | no hit anywhere in `corelib/`, `src/tychoc.c` or `docs/spec/appendix-d-builtins.md`. A non-fatal warning is inexpressible, so it lands on stdout beside a tool's data |
| 12 | `Makefile:<N>@SKIPPED` citations | ar phase 22 | four refs still at `Makefile:366` across `scripts/asan_self.sh` (twice), `scripts/check_citations.py` and `scripts/editors_check.sh`. Six repointing edits over two phases, zero information carried |
| 13 | the `image` shim is compiled by nothing here | ar phase 5 | `scripts/shim_check.sh:43` skips it for missing libpng, as does `make corelib`. Only matters on a host that has libpng |
| 14 | no document-reachability gate | ar phase 6 | no such check in `scripts/`. Would have stayed green through `docs/bootstrap.md`'s entire outage |

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. A language
  change and its own plan.
- **Items 4-14 above.** Queued, ranked, and deliberately not started here — a
  plan that claims fourteen phases is a plan nobody finishes.
