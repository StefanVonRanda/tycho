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

- [x] **Phase 3 — every golden a runner names is tracked, and a gate says so**
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

  **Evidence, 2026-08-01.** Added `scripts/check_goldens.py`; wired as
  `make goldens-check` in the `Makefile` and as step `[1b/13]` in
  `scripts/ci.sh`; `CLAUDE.md`'s gate table and its `make ci` step→gate table
  both have a row.

  **The population, and how I know it is all of it.** 35 `run.sh` files, and the
  count is closed from both ends: `git ls-files | grep -E '(^|/)run\.sh$'`
  returns 35 and `find . -name run.sh -not -path './.git/*'` also returns 35, so
  there is no untracked runner the tracked list misses. Of the 35, **17 name a
  golden and 18 do not** — the 18 are benches comparing the three builds'
  checksums against *each other*, `compiler/run.sh` (a differential, not a
  golden), `server/run.sh` (asserts live HTTP, no recorded stdout), and
  `tests/recursion/run.sh` (asserts fail-closed).

  The extraction was designed against a dump of **every** `.out`/`.err` token on
  a non-comment line of all 35, not against a guess. That dump is what made the
  five naming forms visible, and every one is handled by reading the runner
  rather than by encoding a convention:

  | form | example | resolved by |
  |---|---|---|
  | literal path | `golden=examples/site/expected.out` | as written |
  | bare literal + self-`cd` | `life.out` after `cd "$(dirname "$0")"` | the runner's own directory |
  | `$PWD/` prefix | `golden="$PWD/tools/tycho-q/expected.out"` | strip the prefix |
  | literal-assigned var | `$D/expected.out` with `D=examples/weblog` | that assignment |
  | var in the basename | `corelib/test/$name.out` | glob the directory |
  | loop var | `${f%.ty}.err` under `for f in tests/conc/abort/*.ty` | the loop's glob |

  Scratch is excluded **mechanically**: a token rooted at a variable the same
  runner assigns from `mktemp` is a temp path. Every one of the 34 `$T/…`,
  `$TMP/…` tokens drops out by that rule and no name list was needed, so a lane
  that renames its temp variable stays classified.

  The extension set is `.out` and `.err`, and that is measured, not assumed: all
  11 `golden=`/`gold=` assignments in the tree end `.out`, the four bare-literal
  lanes are `life.out`/`mine.out`/`snake.out`/`$D/expected.out`, and the only
  other recorded outputs are the `.err` diagnostic goldens under `tests/diag`,
  `tests/warn` and `tests/conc/abort`. A golden with any other extension is
  recorded as a `# gap:` in the script's header rather than silently skipped.

  **The printed list — 21 patterns over 17 lanes, 399 files:**

      ok   corelib/run.sh:24            corelib/test/*.out                 39 files
      ok   examples/corelib/run.sh:23   examples/corelib/*.out             37 files
      ok   examples/fetch/run.sh:62     examples/fetch/expected.out         1 file
      ok   examples/life/run.sh:14      examples/life/life.out              1 file
      ok   examples/mandelbrot/run.sh:20 examples/mandelbrot/expected.out    1 file
      ok   examples/minesweeper/run.sh:13 examples/minesweeper/mine.out       1 file
      ok   examples/raytrace/run.sh:20  examples/raytrace/expected.out      1 file
      ok   examples/site/run.sh:22      examples/site/expected.out          1 file
      ok   examples/snake/run.sh:13     examples/snake/snake.out            1 file
      ok   examples/sqlite/run.sh:32    examples/sqlite/expected.out        1 file
      ok   examples/weblog/run.sh:31    examples/weblog/expected.out        1 file
      ok   examples/webserver/run.sh:31 examples/webserver/expected.out     1 file
      ok   tests/conc/run.sh:39         tests/conc/*.out                   13 files
      ok   tests/conc/run.sh:79         tests/conc/abort/*.err              3 files
      ok   tests/ffi/run.sh:21          tests/ffi/expected.out              1 file
      ok   tests/run.sh:52              tests/*.out                       252 files
      ok   tests/run.sh:237             tests/diag/*.err                   21 files
      ok   tests/run.sh:132             tests/pkg/*.out                    15 files
      ok   tests/run.sh:270             tests/warn/*.err                    6 files
      ok   tools/tycho-ar/run.sh:57     tools/tycho-ar/expected.out         1 file
      ok   tools/tycho-q/run.sh:71      tools/tycho-q/expected.out          1 file

      35 runners scanned, 17 name a golden, 18 in NO_GOLDEN, 399 golden files
      checked, all tracked by git.

  **Floor, not count, and the reason is a specific failure mode.** There is
  deliberately no global expected total. Every new fixture under `tests/` adds a
  golden, so a total moves on ordinary work — and a number that moves on ordinary
  work gets bumped reflexively, which turns the floor into a chore that reports
  its own edit history rather than a check. What this gate exists to catch is not
  "the tree has fewer goldens", it is "a lane's goldens became invisible", so the
  floor is **per lane, and it is one**: every glob must match at least one file,
  and every runner must either yield a golden or be named in `NO_GOLDEN` with a
  reason. Neither needs a number maintained.

  **The vacuous pass was real inside my own scan, and it is why the union exists.**
  The first working version collapsed every `$(…)` to a marker so the token regex
  would not split on the spaces inside `$(basename "$hi" .ty).err`. That also ate
  `tests/conc/run.sh`'s `want=$(cat "${f%.ty}.err")` whole — the whole command
  substitution became one marker and **three abort goldens vanished with the check
  still printing `ok`**. Caught only because the printed total was 396 when the
  directory listing said 399. The scan now takes the union of the collapsed and
  the raw line. This is recorded because it is exactly the hazard the Pre-flight
  names, occurring inside the gate written to prevent it.

  **Proved red four ways, each restored.** The `git rm --cached` route reproduces
  the fresh-clone state exactly: the file stays on disk, so every other lane keeps
  passing, and only the index differs.

  1. *The literal path, on the lane that historically broke.*
     `git rm --cached tools/tycho-ar/expected.out`:

         FAIL tools/tycho-ar/run.sh:57     tools/tycho-ar/expected.out         1 file
         goldens-check: FAIL
           tools/tycho-ar/run.sh:57: tools/tycho-ar/expected.out exists but is NOT
           tracked by git -- a fresh clone fails with `no golden`

     exit 1. `git add` restored it; `git diff --cached --stat` empty afterwards,
     so the index is byte-identical to before.
  2. *The glob path, which is separate code.* `git rm --cached corelib/test/json.out`
     — one file inside a 39-file glob — reddened naming that file, and the lane's
     row flipped to `FAIL` while still reporting 39. Restored the same way.
  3. *Guard 1, the runner that stops being followed.* Renaming
     `examples/site/run.sh`'s `golden=` to `GOLDEN_PATH=…expected_v2.txt`:
     `examples/site/run.sh: names no golden and is not in NO_GOLDEN`. This is the
     guard that stops a convention change from emptying the walk silently.
  4. *Guard 2, the lane whose directory moves.* Pointing `corelib/run.sh` at
     `corelib/testcases/$name.out`:
     `corelib/testcases/*.out matches nothing -- the lane's golden convention
     moved and this check went blind`.

  Green again after each restore: `35 runners scanned … all tracked by git`,
  exit 0, and `git status --short` clean.

  **Measured cost: ~0.07 s** (0.076 / 0.073 / 0.073 s over three runs), which is
  the figure in `CLAUDE.md`'s table. It needs no build product at all — it is
  `git ls-files` over a text scan — which is why it is step `[1b/13]`, ahead of
  everything that consumes a golden. Every lane below it compares against the
  copy on *this* disk, so an untracked golden leaves all of them green; reporting
  that after eighteen minutes of blind lanes is the wrong order.

  **Gate.** `make ci`: **exit 0, `CI GREEN -- tree is good`**. `make test` inside
  it: `passed: 561 failed: 0`, the same 561 as after phases 1 and 2 — nothing
  gained, nothing lost, which is right for a phase that adds no fixture. The new
  step ran and printed its list inside the sweep. The only `FAIL` strings
  anywhere in the log are the fuzz lanes' own `FAIL=0` counters.

  **Citation fallout.** The +10-line `Makefile` insert moved `Makefile:366` to
  `:376` and reddened `check_citations.py` on four refs, all live prose in
  scripts, none a record line — so the anchor was kept and the number repointed,
  verified by reading the new line and comparing it with `git show HEAD:Makefile`
  (identical text at `:366` then and `:376` now): `:366`→`:376` in
  `scripts/asan_self.sh` twice, `scripts/check_citations.py` and
  `scripts/editors_check.sh`. That is the **seventh** repointing of this one ref,
  which is precisely what the backlog's row 12 predicted; converting it to the
  `path@SYMBOL` form is filed as phase 7 rather than done here, because whether a
  `Makefile` echo string counts as a "definition" is a citation-policy question
  this phase should not answer silently. Both doc gates green afterwards:
  `check_citations.py` `ok`, `check_links.sh`
  `ok (144 markdown files, no dead relative links)`.

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

- [ ] **Phase 6 — four lanes' goldens are tracked but would be invisible if re-recorded**
  - Found while doing phase 3, outside its scope lock (which allowed a
    `.gitignore` edit only "if a lane turns out to be untracked" — these four are
    tracked), so it is filed here rather than absorbed.
  - `examples/mandelbrot`, `examples/raytrace`, `examples/weblog` and
    `examples/webserver` each hold a **tracked** `expected.out` inside a directory
    that `.gitignore`'s `/examples/*` rule excludes outright, with no
    `!/examples/<dir>/…` un-ignore beneath it. Measured with
    `git check-ignore -v examples/mandelbrot/__probe.out`, which answers
    `/examples/*`. They survive today only because **a tracked file beats every
    ignore rule** — which is a property of the index, not of the tree.
  - So `RECORD=1` over a deleted golden in any of the four re-creates an
    **invisible** file, and phase 3's gate cannot see it: that gate asks whether
    the golden is tracked, which is a different question from whether
    `.gitignore` would let it back in. The `.err` goldens are not exposed
    (`.gitignore` has no `*.err` rule); `tests/`, `corelib/test/`,
    `examples/{sqlite,life,snake,minesweeper,corelib,fetch,site}` and both
    `tools/` lanes each already carry an un-ignore line.
  - **Not a four-line fix, which is why it is a phase.** Un-ignoring those
    directories also exposes the binaries and emitted `.c` they build in place —
    that is exactly why the four lanes that *are* un-ignored needed a
    per-directory `.gitignore` of their own. The same treatment is what these
    want, plus the decision of whether `mandelbrot`/`raytrace` should build into a
    temp dir instead, as `fetch` and `site` do.
  - Scope: `.gitignore`, a per-directory `.gitignore` in each of the four, and
    `scripts/check_goldens.py` to add the stricter check once it can be green.
  - Done when: `git check-ignore -v <dir>/__probe.out` answers "not ignored" for
    every lane that records a golden, the gate asserts that too, and
    `git status --short` is still clean after `make ci`.
  - Verify: `make goldens-check`, `git status --short` after `make corelib` and
    `make mandelbrot`/`make raytrace`. Not `make ci`.

- [ ] **Phase 7 — `Makefile:<N>@SKIPPED` has now been repointed seven times**
  - Backlog row 12, re-confirmed by phase 3: the +10-line `Makefile` insert moved
    it again, `:366`→`:376`, across the same four citing lines in
    `scripts/asan_self.sh` (twice), `scripts/check_citations.py` and
    `scripts/editors_check.sh`. Seven repairs, zero information carried by any of
    them.
  - `CLAUDE.md`'s own rule names the fix — "convert an old one when it next
    breaks", to `path@SYMBOL` with no line number — and the gate reports 344 refs
    already in that form, so the machinery exists.
  - **The open question is whether this ref qualifies**, and it should be
    answered rather than assumed. `CLAUDE.md` says to use `@SYMBOL` "for a
    definition, not for a region", and `SKIPPED` is a word inside a `Makefile`
    echo string — neither a definition nor a region. If `Makefile@SKIPPED` is
    accepted, the rule's wording should be widened to say so; if it is not, this
    row should be retired as "will keep moving, and that is fine".
  - Scope: the four citing lines, and `CLAUDE.md`'s citation section if the rule
    widens. Not `Makefile` itself.
  - Verify: `python3 scripts/check_citations.py`, then prove it cannot silently
    pass by deleting the `SKIPPED` line from `Makefile` and showing the red.

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

## Status — PLAN COMPLETE

All three phases are done and committed, one commit each. Phases 4, 5, 6 and 7
are filed follow-ups discovered inside phases 1, 2 and 3 and pushed out of them
by their scope locks — they have the standing of the queued backlog rows below,
not of conditions on this plan.

**The Goal was: close the three latent breakages no gate in this tree can see,
and for the second and third leave behind a mechanical gate so the next one
reddens instead of shipping. All three are closed and both gates exist.** What
each became:

1. **`str(float)` corrupted its own output under a comma-decimal locale** —
   measured at `1,5.0`, which is neither valid Tycho float syntax nor readable by
   any parser in this tree → `runtime/tycho_rt.c@tycho_float_to_str` now renders
   through a `"C"`-locale `uselocale()` handle, so the separator is `'.'`
   whatever the ambient locale is. `snprintf_l`, the route the Pre-flight
   assumed, turned out to be a BSD extension glibc does not declare at all; the
   POSIX 2008 `uselocale`/`newlocale` pair needed **no new feature-test macro**
   and is thread-scoped, which `setlocale` would not have been. The `".0"` guard
   that scanned for a `'.'` was the second half of the same defect and now
   carries its contract in a comment: the scan is sound *only* because the
   separator is known.
2. **The hand-linked shim lists went stale twice, because they tracked direct
   imports and the dependency that breaks them is transitive** → the Pre-flight
   assumption held, the compiler's `merge_pkg` already builds the transitive
   closure as a side effect of the import DFS, and `--print-shims` exposes it.
   `examples/fetch/run.sh` and `examples/site/run.sh` now ask the compiler, and
   **no hand-maintained shim list remains in either**. The proof was the real
   case: `examples/fetch/main.ty` never imports `core:strings`, reaches its shim
   through `core:json`, and the old grep could not see it by construction.
3. **A new lane's golden was invisible until someone cloned** → `make
   goldens-check` walks all 35 runners and asserts every golden they name is in
   the index, printing all 399 of them. It is step `[1b/13]`, ahead of every lane
   that consumes a golden.

**What the two new gates catch.** `--print-shims` removes the failure mode
rather than detecting it: there is no list left to go stale, so the 2026-08-01
break is not merely caught but unrepresentable in those two lanes.
`goldens-check` catches an untracked golden on any of the 17 lanes that record
one, a runner whose golden the scan stops following, and a lane whose golden
directory moves out from under it — each proved red by breaking a real lane and
restored.

**What they cannot catch, stated plainly because a gate's edges are the part
that gets forgotten.**

- `--print-shims` closes the two example lanes only. `corelib/run.sh` and
  `examples/corelib/run.sh` still grep their own source for direct imports, over
  far more programs — that is phase 5, and it needs a `--print-deps` too, because
  those loops serve a second purpose the shim list does not carry.
- `goldens-check` asks whether a golden is **tracked**, not whether `.gitignore`
  would let it back in if it were deleted and re-recorded. Four lanes are live
  examples of the difference — phase 6.
- Neither gate follows a golden whose path is computed from something the text
  scan cannot see, nor one that does not end `.out`/`.err`. No runner in the tree
  is in that position today; the `# gap:` in `scripts/check_goldens.py`'s header
  says so and says how it was checked, so the next reader does not have to
  re-derive it.
- Phase 1's fix is in `runtime/tycho_rt.c` only. The **identical defect one layer
  up** — `src/tychoc.c` formatting float literals into generated C, where
  `f(1,5.0)` is a legal comma expression `cc` accepts silently — is phase 4, and
  it has the larger blast radius of the two.

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. A language
  change and its own plan.
- **Items 4-14 above.** Queued, ranked, and deliberately not started here — a
  plan that claims fourteen phases is a plan nobody finishes.
