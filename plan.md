# The four the gates found

Previous plan complete and archived at
[docs/internals/plan-three-gates-DONE.md](docs/internals/plan-three-gates-DONE.md).

Every phase here was **found by a phase of that plan while doing something else**,
and filed rather than absorbed. That is the pattern worth naming: three fixes for
latent breakage turned up four more, and in two cases the newly found defect has a
worse blast radius than the one being fixed.

## Goal

1. **The compiler emits float literals under the ambient locale**, one layer up
   from the runtime defect just fixed — and this one corrupts C source rather than
   a printed string.
2. **Two more lanes derive shims from a direct-import grep**, the same blindness
   phase 2 deleted from the example lanes, over many more programs — and they need
   a `deps` closure the shim flag does not carry.
3. **Four lanes' goldens are tracked but sit inside ignored directories**, so
   re-recording one creates an invisible file that the new gate cannot see.
4. **A citation has now been repointed seven times**, carrying no information on
   any of them.

Done looks like: all four resolved, each with a check that is proved to fail.

## Pre-flight

- **Worst case, phase 1:** the fix is applied to the emitter and not to the lexer,
  or vice versa, leaving a program that reads `1.5` as `1` and writes it back as
  `1,5.0` — the two halves must land together or the round trip is worse than
  before. `src/tychoc.c:298` (read) and `:9496` (write) are one defect with two
  sites.
- **Worst case, phase 3:** un-ignoring four `examples/` directories exposes the
  binaries and emitted `.c` they build in place, so `git status` stops being clean
  after a build and everyone learns to ignore it. That is a worse outcome than the
  invisible golden, and it is why this is a phase and not a four-line
  `.gitignore` edit.
- **Reversibility:** total for all four. Compiler source, harness scripts, ignore
  rules and comments; no data at risk.
- **Verified — each claim re-checked when it was filed, with the command named in
  the phase.** The measurements live in
  `docs/internals/plan-three-gates-DONE.md`'s evidence blocks; each phase below
  repeats the load-bearing one.
- **Assuming — to check, not fact:**
  - That `Makefile@SKIPPED` is an acceptable citation form. `CLAUDE.md` says
    `@SYMBOL` is "for a definition, not for a region", and `SKIPPED` is a word in
    a `Makefile` echo string, which is neither. Phase 4 answers this rather than
    assuming it, and the answer decides whether the row is fixed or retired.
  - That the compiler can expose pkg-config **names** as well as resolved
    cflags. `add_pkg_deps` accumulates the resolved form into `g_pkgdeps`; the
    SKIP logic tests names with `pkg-config --exists`. If the names are not
    retained, phase 2 is bigger than a print and should say so.

## Phases

- [x] **Phase 1 — the compiler emits float literals under the ambient locale too**
  - `src/tychoc.c:9496` formats a float literal into the **generated C source**
    with `snprintf(b, sizeof b, "%.17g", e->fval)`, then applies the same `'.'`
    scan at `:9498-9499` to decide whether to append `".0"`. It is the identical
    defect the previous plan's phase 1 fixed in
    `runtime/tycho_rt.c@tycho_float_to_str`, one layer up, with a larger blast
    radius: the runtime one corrupted a string a human reads, this one corrupts
    **C source a compiler reads**. Under a comma locale `f(1.5)` emits `f(1,5.0)`
    — not a syntax error but a comma expression, so `cc` accepts it and the
    program silently gets `5.0`.
  - The mirror side is `src/tychoc.c:298`: the lexer reads a float literal with
    plain `strtod`, which under a comma locale stops at the `'.'` and returns `1`
    for `1.5`. Both sites, or the round trip gets worse rather than better.
  - Latent today only because nothing in this tree calls `setlocale` — an
    unstated dependency on a process global, not a fix.
  - Scope: `src/tychoc.c`, and a fixture under `tests/`. The `"C"`-handle pattern
    and its fallback in `runtime/tycho_rt.c` are what to copy; do not share state
    across the two files, they are separate translation units. Note the previous
    plan measured that `snprintf_l` is **not** available on glibc — `uselocale`
    against a `newlocale(LC_NUMERIC_MASK, "C", 0)` handle is what worked, needing
    no new feature-test macro.
  - Done when: a fixture proves both directions under a hostile locale held
    inside the test, and the `".0"` guard carries the same written contract its
    twin got.
  - Verify: `make test` — `src/tychoc.c`, so it is the gate. Count was
    `passed: 561 failed: 0`. Prove the check can fail by reverting and showing
    the red.

  **Evidence, 2026-08-02.** Changed: `src/tychoc.c` — a new
  `src/tychoc.c@c_numeric_handle` / `src/tychoc.c@c_strtod` /
  `src/tychoc.c@c_dtoa` block, the lexer's `strtod` call, and the `E_FLOAT` arm
  of codegen — plus `tests/float_lit_locale.ty` and its golden
  `tests/float_lit_locale.out`. `#include <locale.h>` was added; **no new
  feature-test macro was needed**, because this file already declares
  `_GNU_SOURCE` at its top, which on glibc implies `_POSIX_C_SOURCE 200809L` and
  so declares `newlocale`/`uselocale`/`LC_NUMERIC_MASK`. Checked rather than
  assumed: `make` compiles it clean under `-Wall -Wextra -std=c11` with no
  implicit-declaration warning. The handle is this translation unit's own static;
  it is deliberately not shared with `runtime/tycho_rt.c@tycho_float_to_str`'s,
  and unlike that one it needs no `pthread_once` — tychoc is single-threaded,
  and `grep -n pthread src/tychoc.c` finds only the `-pthread` flag tychoc puts
  on the `cc` line it invokes.

  **THE PRE-FLIGHT'S TEST ROUTE DOES NOT WORK, AND THIS IS THE PHASE'S MAIN
  FINDING.** The brief proposed `LC_ALL=<comma locale> ./tychoc …` as "the honest
  route" for the write side. It is inert. **A C program starts in the `"C"`
  locale whatever `LC_ALL` says**, until something calls `setlocale(LC_ALL, "")`
  — and tychoc never does, which is precisely why the plan calls this defect
  latent. Measured: the first break-and-revert attempt ran the *fully broken*
  compiler under `LC_ALL=da_DK.utf8` and the fixture came out **green**, i.e. the
  check could not fail. A check that cannot fail is not a check, and this one
  would have been written into the evidence as proof.

  What does reach it is the trigger the code comment names — a linked C library
  calling `setlocale` from a load-time constructor. Reproduced with a four-line
  `LD_PRELOAD`:

      __attribute__((constructor)) static void go(void) { setlocale(LC_ALL, ""); }

      $ cc -shared -fPIC -o loc.so loc.c
      $ LC_ALL=da_DK.utf8 LD_PRELOAD=./loc.so ./tychoc tests/float_lit_locale.ty --emit-c -o w
      $ grep 'h_a = \|h_small = ' w.c
          double h_a = 1.5;
          double h_small = 0.0025000000000000001;

  `locale -a | grep -i da_DK` lists `da_DK` and `da_DK.utf8` on this host, and
  `da_DK.utf8` is the one used throughout.

  **Both locale runs, same golden.**

      $ ./tychoc tests/float_lit_locale.ty --emit-c -o v && cc -O2 -fwrapv -std=c11 -o v.bin v.c -lm
      $ ./v.bin | cmp - tests/float_lit_locale.out          # silent
      $ LC_ALL=da_DK.utf8 LD_PRELOAD=./loc.so ./tychoc tests/float_lit_locale.ty --emit-c -o w
      $ cc -O2 -fwrapv -std=c11 -o w.bin w.c -lm && ./w.bin | cmp - tests/float_lit_locale.out
                                                            # silent

  **WHAT THE FIXTURE HOLDS ITSELF, AND WHAT IT MISSES.** It holds the *runtime*
  half: `tycho_test_make_locale_hostile()` — the hook
  `tests/float_str_locale.ty` already declares — flips `LC_NUMERIC` inside the
  process and the same nine lines are printed again, so the numbers in the golden
  are the compiler's baked-in values on any grader's machine rather than an
  artifact of the printing locale. It **cannot** hold the compile-time half:
  tychoc is a separate process that has already exited before `main()` runs, and
  the environment cannot reach it either (see above). So under `make test` the
  compile side is exercised at the *grader's* compiler locale, which is `"C"` —
  the case that never broke. **The compile-side proof is the `LD_PRELOAD`
  transcript above and the break-and-reverts below, run by hand; nothing in CI
  reruns it.** Closing that would mean a harness change, which is out of this
  phase's scope. Filed as phase 5.

  **The plan's "silent comma expression" claim is right, but not everywhere —
  measured.** Of the three shapes codegen emits a float literal into, cc rejects
  one and silently miscompiles two:

  | emitted shape | `cc` | value |
  |---|---|---|
  | `double _ret = 1,5.0;` (declarator init) | **error:** expected identifier or `(` before numeric constant | — |
  | `_l0.data[0] = 1,5.0;` (assignment stmt) | accepted, no diagnostic | element becomes **1.0** |
  | `(1,5.0 + 0.0)` (parenthesised expr) | accepted, no diagnostic | **5.0** |

  So the loud case exists but covers only one of three; the blast radius the plan
  claims is real for the other two.

  **The `".0"` guard's contract is written down** above
  `src/tychoc.c@c_numeric_handle`: it answers "would `cc` read this text back as
  an **integer** constant?" and appends `.0` when it would, so `3.0 / 2.0` is not
  integer division. The scan for `'.' 'e' 'E' 'n' 'i'` is sound **only** because
  the separator is now known to be `'.'` — under any other separator the
  character is absent from the set, every finite non-integral value looks
  integral, and the guard appends `.0` to text that already had a fraction, which
  is exactly how `1,5` became `1,5.0`. The comment says the conversion and the
  guard are one change.

  **Edge cases — every line is in the golden, identical in both blocks.**

  | source literal | emitted C | printed | why that outcome |
  |---|---|---|---|
  | `1.5` | `1.5` | `1.5` | the case that used to be `1,5.0` |
  | `half(1.5)` | `h_half(&_t, 1.5)` | `0.75` | a literal in call-argument position |
  | `3.0` | `3.0` | `3.0` | `%.17g` gives `3`; the guard fires, correctly |
  | `3.0 / 2.0` | `3.0 / 2.0` | `1.5` | proves the guard keeps it out of integer division |
  | `2.5e-3` | `0.0025000000000000001` | `0.0025` | separator **and** no `e`, so it took the guard too: `0,0025…` + `.0` was worth 2.5e15 |
  | `1e300` | `1e+300` | `1e+300` | exponent form, no separator: neither half could touch it |
  | `1e-300` | `1e-300` | `1e-300` | as above |
  | `-0.5` | `-0.5` | `-0.5` | read-side breakage shows here as `-0.0` |
  | `0.30000000000000004` | same, 17 digits | `exact` | compared against `0.1 + 0.2` rather than printed, because `str()` renders `%.15g` and would show `0.3` for both |

  **Break and revert — two sites, two reverts, each rebuilt with `make` and run
  under the `LD_PRELOAD` above.**

  *Write side reverted* (`c_dtoa` → the bare `snprintf(b, sizeof b, "%.17g", …)`):

      double h_a = 1,5.0;
      double h_small = 0,0025000000000000001.0;
      cc REJECTED the emitted C:
      b.c:2564:20: error: expected identifier or ‘(’ before numeric constant

  *Read side reverted* (`c_strtod` → the bare `strtod(s, NULL)`): cc accepts, the
  program runs, and the golden diff is

      < 1.5            = 1.5          > 1.5            = 1.0
      < half(1.5)      = 0.75         > half(1.5)      = 0.5
      < 2.5e-3         = 0.0025       > 2.5e-3         = 2.0
      < -0.5           = -0.5         > -0.5           = -0.0

  *Both restored:* `double h_a = 1.5;`, no diff against the golden.

  **`make test`: `passed: 562   failed: 0`**, up from the 561 this phase started
  at — the one new fixture, nothing lost.

  **Citation churn.** The +125-line net insert near the top of `src/tychoc.c`
  moved 135 anchored refs. 118 were repointed by the diff's own old→new line map;
  **12 sat on record lines** (before/after table rows in
  `docs/internals/plan-postfreeze-rawstring-DONE.md` and its neighbours) and got
  the other treatment — number kept, anchor dropped — and 5 more were
  unbackticked refs in `fuzz/run_parforparity.py`, `scripts/asan_self.sh` and
  `scripts/check_citations.py` that the automated pass could not address and were
  repointed by hand. `python3 scripts/check_citations.py` and
  `sh scripts/check_links.sh` are both green.

- [x] **Phase 2 — the other two lanes still grep for direct imports, and they also need the `deps` closure**
  - `corelib/run.sh:32` and `examples/corelib/run.sh:30` carry the **same
    direct-import grep** the previous plan's phase 2 deleted from the example
    lanes — `for mod in $(grep … 'core:[a-z0-9_]+' …)`, mapping each to
    `corelib/<mod>/<mod>_shim.c`. Same blindness to a transitive dependency, over
    far more programs: every `corelib/test/*/main.ty` and every
    `examples/corelib/*/main.ty`.
  - **`--print-shims` alone does not close them.** Both lanes use the same loop
    for a second purpose: they read each module's sibling `<mod>/deps` file to
    decide whether to **SKIP** on a host missing a pkg-config dependency
    (`corelib/run.sh:33`, `:36-40`). A shim list does not carry that. Closing them
    wants a `--print-deps`, or one flag emitting both — and `add_pkg_deps`
    accumulates resolved *cflags/libs* into `g_pkgdeps`, not the pkg-config
    **names** the SKIP logic tests, so this is not a straight print of what
    already exists.
  - `examples/fetch/run.sh:33` has the same gap in a third form: a hard-coded
    `DEPF="$(pkg-config --cflags --libs libcurl)"`, a hand-maintained dependency
    list beside the shim list phase 2 retired, stale-prone for the same reason.
  - Done when: no `run.sh` in the tree derives shims or deps from a grep over its
    own source, and the SKIP behaviour is preserved — **proved** by forcing a
    probe where a dep is absent, not by reasoning that it still works.
  - Verify: `make corelib`, `make corelib-examples`, `make fetch`, and `make test`
    if `src/tychoc.c` gains the flag — which it will. Not `make ci`.

  **Evidence, 2026-08-02.** Changed: `src/tychoc.c` (a `--print-deps` flag and the
  name retention behind it), `corelib/run.sh`, `examples/corelib/run.sh`,
  `examples/fetch/run.sh`, and `docs/spec/15-program.md` §27.4 beside
  `--print-shims`.

  **WHAT `g_pkgdeps` ACTUALLY HOLDS — the Pre-flight's assumption, checked.** It
  holds the **resolved cflags/libs only**, exactly as the Pre-flight suspected.
  `src/tychoc.c@add_pkg_deps` parses a `deps` line into `s`, and `s` reaches
  `pkg_config_flags(s)` and the `could not resolve` error message and **nothing
  else** — the accumulator appends `fl`, the resolved string, never the name.
  The names were retained nowhere in the process. So the print alone could not
  have worked, and the fix is the one the brief authorised: a `g_pkgnames`
  accumulator beside `g_pkgdeps`, filled from the same loop.

  **The name is recorded BEFORE resolution is attempted, and that is the whole
  point.** On a host where the library is absent, `pkg_config_flags` fails and
  `g_pkgdeps` stays empty — indistinguishable from a package that declared no
  dependencies at all. That is precisely the host the SKIP exists for, so a flag
  reading the resolved form would print nothing exactly when it is needed.
  Measured, with the host's pkg-config search path emptied:

      $ PKG_CONFIG_LIBDIR=/tmp/emptypc ./tychoc examples/fetch/main.ty --print-deps
      libcurl

  `--print-deps` also sets a names-only mode, so it never forks pkg-config for an
  answer nobody reads and never prints a `could not resolve` line into the case
  the caller asked in order to handle gracefully.

  **THE DEPS BLINDNESS IS LATENT ON THIS TREE, NOT LIVE — say so rather than
  claim a catch.** Over all 68 programs in the two lanes, the old grep and the new
  flag agree on the dependency set: every deps-bearing module (`compress`,
  `crypto`, `http`, `image`, `tls`) is imported *directly* by its own test and its
  own example, so no transitive edge is in play today. The flag is still the right
  derivation, and the blindness is reachable rather than theoretical — proved by
  constructing the missing edge, a scratch `corelib/zwrap` importing `core:compress`
  and a program importing only `core:zwrap`:

      --- grep over the program's own imports (the OLD derivation) ---
      (end -- nothing above means it found no deps)
      --- ./tychoc --print-deps (the NEW derivation) ---
      zlib

  The grep finds nothing and the lane would have skipped nothing, then failed to
  link. Both scratch files were deleted after the measurement.

  **A SECOND FINDING THE PHASE DID NOT EXPECT: the `shim` and `depflags` those
  loops built were DEAD.** Both lanes assigned them and neither ever read them —
  `grep -n 'shim\|depflags'` over each file before the change returns only the
  assignments. The build in both lanes is a plain `"$TYCHOC" "$entry" -o …`, which
  discovers and links the shims and the resolved flags itself. So these two lanes
  never needed `--print-shims` at all; the only live output of that loop was the
  pkg-config names. Deleted rather than ported.

  **SKIP PROBE, BOTH DIRECTIONS, BEFORE AND AFTER — byte-identical.** The host is
  missing libpng and has the other four, so one SKIP is natural; the rest were
  forced with `PKG_CONFIG_LIBDIR=/tmp/emptypc` (an empty directory — this replaces
  the default search path, where `PKG_CONFIG_PATH` would only prepend to it).

  | probe | before | after |
  |---|---|---|
  | `sh corelib/run.sh` | `skip image (missing dependency: libpng)`, all green | identical |
  | forced, `corelib` | skips compress, crypto, http, image, tls; all green | identical |
  | forced, `examples/corelib` | same five skips; all green | identical |
  | forced, `examples/fetch` | `fetch: SKIP (libcurl not installed)` | identical |

  The fetch SKIP line is now *derived*: the name in it comes from `--print-deps`,
  and it reads the same because the derivation returns `libcurl`.

  **BREAK AND REVERT, WITH THE BREAK CONFIRMED ON DISK FIRST.** The previous
  plan's phase 2 had a break silently do nothing and nearly wrote a false proof, so
  the break was asserted unique before it was applied and `diff`ed after:

      12924c12924
      <         for (int i = 0; i < g_npkgnames; i++) printf("%s\n", g_pkgnames[i]);
      ---
      >         for (int i = 0; i < 0; i++) printf("%s\n", g_pkgnames[i]);   /* BREAK PROBE */

  Rebuilt with `make`, both lanes went red in exactly the shape the SKIP exists to
  prevent — not a cosmetic diff:

      --- corelib lane, BROKEN derivation ---
      FAIL image (tychoc compile)
      corelib: FAIL
      --- fetch lane, BROKEN derivation, forced-missing ---
      undefined reference to `curl_easy_getinfo'
      fetch: FAIL

  Restored (`diff` silent), rebuilt, and both are green again — `skip image
  (missing dependency: libpng)` / `corelib: all green`, and `fetch: SKIP (libcurl
  not installed)` under the forced probe.

  **Gates.** `make` clean under `-Wall -Wextra -std=c11`. `make corelib`,
  `make corelib-examples` and `make fetch` all green. `make test`:
  **`passed: 562   failed: 0`** — the same count as after phase 1, nothing lost and
  nothing added (this phase adds no fixture; its checks are the three lanes).
  `sh scripts/spec_check.sh` green (9 runnable examples) because `docs/spec/15-program.md`
  changed. `python3 scripts/check_citations.py` and `sh scripts/check_links.sh`
  both green.

  **Citation churn.** The insert moved 55 anchored refs across 14 files; all were
  repointed by the diff's own old→new line map. **Zero landed on a record line** —
  the one line the record-shape detector flagged, `docs/spec/12-aggregates.md:18`,
  was a **false positive**, and it is the exact failure `CLAUDE.md` predicts: it is
  a live `> Provenance:` block whose `→` sits in ordinary prose (``delete` →
  `map_del``), not a repair log joining two refs. Repointed by hand after reading
  it rather than trusted to the detector.

- [ ] **Phase 3 — four lanes' goldens are tracked but would be invisible if re-recorded**
  - `examples/mandelbrot`, `examples/raytrace`, `examples/weblog` and
    `examples/webserver` each hold a **tracked** `expected.out` inside a directory
    `.gitignore`'s `/examples/*` rule excludes outright, with no un-ignore
    beneath it. Measured with `git check-ignore -v examples/mandelbrot/__probe.out`,
    which answers `/examples/*`. They survive only because **a tracked file beats
    every ignore rule** — a property of the index, not of the tree.
  - So `RECORD=1` over a deleted golden in any of the four re-creates an
    **invisible** file, and `scripts/check_goldens.py` cannot see it: that gate
    asks whether the golden is tracked, which is a different question from whether
    `.gitignore` would let it back in.
  - **Un-ignoring also exposes the binaries and emitted `.c` those lanes build in
    place** — which is exactly why the lanes that *are* un-ignored needed a
    per-directory `.gitignore` of their own. Same treatment here, plus the
    decision whether `mandelbrot`/`raytrace` should build into a temp dir as
    `fetch` and `site` do.
  - Scope: `.gitignore`, a per-directory `.gitignore` in each of the four, and
    `scripts/check_goldens.py` to add the stricter check once it can be green.
  - Done when: `git check-ignore -v <dir>/__probe.out` answers "not ignored" for
    every lane that records a golden, the gate asserts that too and is proved red
    on a lane put back, and `git status --short` is clean after a build.
  - Verify: `make goldens-check`, then `git status --short` after `make corelib`,
    `make mandelbrot` and `make raytrace`. Not `make ci`.

- [ ] **Phase 4 — `Makefile:<N>@SKIPPED` has now been repointed seven times**
  - The previous plan's phase 3 moved it again, `:366`→`:376`, across the same
    four citing lines in `scripts/asan_self.sh` (twice),
    `scripts/check_citations.py` and `scripts/editors_check.sh`. Seven repairs,
    zero information carried by any of them.
  - `CLAUDE.md`'s own rule names the fix — "convert an old one when it next
    breaks", to `path@SYMBOL` with no line number — and the gate reports hundreds
    of refs already in that form, so the machinery exists.
  - **The open question is whether this ref qualifies, and it must be answered
    rather than assumed.** `CLAUDE.md` says to use `@SYMBOL` "for a definition,
    not for a region", and `SKIPPED` is a word inside a `Makefile` echo string —
    neither. If `Makefile@SKIPPED` is accepted, widen the rule's wording to say
    so; if it is not, retire the row as "will keep moving, and that is fine" and
    say why in `CLAUDE.md` so the eighth repair does not get filed as a defect.
  - Scope: the four citing lines, and `CLAUDE.md`'s citation section if the rule
    widens. Not `Makefile` itself.
  - Verify: `python3 scripts/check_citations.py`, then prove it cannot silently
    pass by deleting the `SKIPPED` line from `Makefile` and showing the red.

- [ ] **Phase 5 — an overflowing float literal emits `inf`, which is not C**
  - Found by phase 1 while reading the `".0"` guard's `'n'`/`'i'` cases, and
    filed rather than absorbed. `1e400` lexes fine, `strtod` returns infinity,
    and codegen writes the bare token `inf` into the generated C. Measured:

        $ printf 'fn main():\n    x := 1e400\n    println(str(x))\n' > inf.ty
        $ ./tychoc inf.ty --emit-c -o inf && cc -O2 -std=c11 -o inf.bin inf.c -lm
        inf.c:2556:18: error: ‘inf’ undeclared (first use in this function);
                              did you mean ‘ynf’?

    So the user gets a C compiler error, in generated code, naming a libm
    function they never wrote — instead of a Tycho diagnostic on their own
    source line. `nan` has the same shape, though no literal reaches it.
  - This means the `'n'` and `'i'` arms of the `".0"` guard are dead as far as
    *valid* output goes: every value that makes them fire is a value whose
    emitted C does not compile. Either the literal is rejected at lex time with
    a real diagnostic (`float literal out of range`, matching the existing
    `integer literal out of range` at the same site), or codegen emits
    `(1.0/0.0)` / `__builtin_inf()` and the guard's arms become live. Decide
    which; do not leave both.
  - Scope: `src/tychoc.c`, a `tests/reject/` fixture if the answer is rejection,
    a `tests/` fixture with its golden if it is emission.
  - Verify: `make test`, which was `passed: 562 failed: 0` after phase 1. Not
    `make ci`.

- [ ] **Phase 6 — nothing in CI compiles anything under a hostile `LC_NUMERIC`**
  - Phase 1 fixed both locale sites in `src/tychoc.c` and proved them by hand
    with an `LD_PRELOAD` whose constructor calls `setlocale(LC_ALL, "")`. That
    proof is not repeatable by any gate: `tests/run.sh` compiles every fixture in
    the grader's own environment, and the environment cannot change a compiler's
    locale anyway (a C program starts in `"C"` until something calls
    `setlocale`). So `tests/float_lit_locale.ty` locks the *values* but exercises
    the compile side only under `"C"` — the case that never broke.
  - The same hole covers the runtime twin: `tests/float_str_locale.ty` holds its
    locale in-process, so it is fine, but nothing checks that a *third* site does
    not appear. `grep -n 'strtod\|%[0-9.]*g' src/tychoc.c runtime/tycho_rt.c
    corelib/*/*.c` is the enumeration a gate would encode.
  - Cheapest shape that could work: a small lane that builds the preload shim,
    compiles two or three float-bearing fixtures under it, and golden-compares —
    the same shape `make ar-check` and `make q-check` already have. It must skip
    cleanly on a host with no comma-decimal locale and on macOS, where
    `DYLD_INSERT_LIBRARIES` is the spelling and SIP blocks it.
  - Scope: `scripts/`, `Makefile`, and a CI step. **This is the one phase in this
    plan whose brief legitimately ends in `make ci`**, because it adds a step.
  - Done when: the new lane is proved red by reverting either half of phase 1's
    fix, and green with both.

- [ ] **Phase 7 — three more lanes hand-maintain a dependency `--print-deps` cannot see**
  - Found by phase 2 while sweeping for the third form of the defect
    (`examples/fetch/run.sh`'s hard-coded `DEPF`), and filed rather than absorbed.
    `examples/sqlite/run.sh:29-30`, `bench/dbquery/run.sh:17-18` and
    `bench/fair_rest.sh:52-58` each name `sqlite3` by hand, in both a SKIP guard
    and a link line — the same hand-maintained shape phase 2 retired in three
    places.
  - **`--print-deps` does not close them, and the reason is structural rather than
    an oversight.** It reads a package's `deps` file, and these programs have no
    package: `examples/sqlite/demo.ty:15` declares `extern "sqlite3" fn
    sqlite3_open(…)`, so the dependency arrives through `extern "Lib"` (and
    `--pkg` on the CLI), which lands in `g_links` and on the `-l` part of the cc
    line. Measured: `./tychoc examples/sqlite/demo.ty --print-deps` prints nothing
    at all, which is a *correct* answer to the question that flag asks.
  - So the shape that would close these is a third read-out of the same walk —
    the `extern "Lib"` link-library names — not a widening of `--print-deps`.
    Note the two questions genuinely differ: a pkg-config name is something to
    probe with `--exists`, an `extern "Lib"` name is a bare `-l` with no probe,
    and `bench/dbquery/run.sh:13-18` already documents that macOS ships libsqlite3
    with **no** `.pc` file, so a lane must fall back to a `-lsqlite3` link probe.
    Any flag here must not push those lanes into assuming pkg-config exists.
  - Decide first whether it is worth it: three lanes, one library between them,
    versus a new compiler flag and its spec section. "Leave them hand-written and
    say why" is a legitimate answer — record it if so, the way phase 4 is prepared
    to retire its row.
  - Scope: `src/tychoc.c` if a flag is added, the three `run.sh` files, and
    `docs/spec/15-program.md` §27.4. Not `make ci`.
  - Verify: `make bench-guard` for the two bench lanes and `sh examples/sqlite/run.sh`
    for the third, plus `make test` if `src/tychoc.c` changes. The SKIP path must
    be proved on a forced-missing host as phase 2 did (`PKG_CONFIG_LIBDIR` at an
    empty directory), in both directions.

## Carried forward

The backlog audit of 2026-08-01 (in `docs/internals/plan-three-gates-DONE.md`)
verified nineteen items, retired five and ranked fourteen. Its top three are done.
These eleven remain, in its ranking, and each was confirmed open by a command
named there:

- [ ] **`decimal.div(a, b, scale, mode)`** — no `div` in
      `corelib/decimal/decimal.ty`. Blocks `select total / count`, the ordinary
      averaging query, on almost all real data. Both scale and rounding mode must
      be named by the caller; `corelib/decimal/decimal.ty@rescale` already
      truncates toward zero and a second policy must not silently disagree.
- [ ] **`core:json` does not validate UTF-8** — `corelib/test/json.out`'s
      `cb ok high` line shows a raw `0xFF` parsing. One decoder closes both this
      and `corelib/json/json.ty@esc`'s missing output side.
- [ ] **`strings.parse_int` fails open** — `corelib/strings/strings.ty:71`,
      now inconsistent with the strict `Result`-returning `parse_float` at `:164`.
      Two neighbours, opposite contracts.
- [ ] **`io.write_bytes`** — `corelib/io/io.ty:124` has `read_bytes` and no
      counterpart; writing bytes is `io.write(p, to_str(b))`, correct only by an
      accident of the runtime's length header.
- [ ] **`io.make_dirs`** — `corelib/io/io.ty:303` is one `mkdir(2)`;
      `tools/tycho-ar/main.ty@mkdir_p` is 18 lines of caller-side chain building.
- [ ] **mtime is readable, not writable** — no `utimensat`/`utimes` in
      `corelib/io/`. `tycho-ar` stores an mtime it cannot restore, and `diff -r`
      does not compare mtimes, so its gate cannot see it.
- [ ] **No incremental digest** — no `update`-shaped function in
      `corelib/sha256`, `md5`, `crypto` or `hash`. `FRICTION.md:1237` is the
      record; the code gap is real.
- [ ] **No `eprintln`** — nothing in `corelib/`, `src/tychoc.c` or
      `docs/spec/appendix-d-builtins.md`. A non-fatal warning is inexpressible,
      so it lands on stdout beside a tool's data.
- [ ] **The `image` shim is compiled by nothing here** —
      `scripts/shim_check.sh:43` skips it for missing libpng, as does
      `make corelib`. Only matters on a host that has libpng.
- [ ] **No document-reachability gate** — would have stayed green through
      `docs/bootstrap.md`'s entire outage.
- [ ] **`ParallelFor` width slot** — `FRICTION.md`'s last hard item. A language
      change and its own plan.

## Out of scope

- The eleven carried forward above. Queued and ranked, deliberately not started
  here.
