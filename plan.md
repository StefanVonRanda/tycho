# Four of seven

Previous plan complete and archived at
[docs/internals/plan-four-found-DONE.md](docs/internals/plan-four-found-DONE.md).

That plan filed seven follow-up phases. **Each was re-checked against this tree
before this plan was written** — commands run, not filings trusted — and three
are retired without work. The audit is below; the four that survived are the
phases.

## Goal

1. **An overflowing float literal emits `inf` into generated C**, which is not a
   C identifier, so the user gets a `cc` error instead of a Tycho diagnostic.
2. **`weblog` and `webserver` have goldens no gate ever compares.** The previous
   plan spent a phase making those goldens visible; nothing reads them.
3. **Nothing in CI compiles under a hostile `LC_NUMERIC`**, so the two locale
   fixes of the last two plans can rot without a symptom.
4. **A citation in `scripts/asan_self.sh` points at the wrong recipe.**

## Pre-flight

- **Worst case, phase 2:** a new golden lane is added that compares output which
  is not deterministic — a timestamp, a port number, a path — and the gate goes
  red on an innocent machine or, worse, gets `RECORD=1`-ed into meaninglessness.
  Read what those two programs print before wiring anything, and if either is
  non-deterministic say so and gate the deterministic part only.
- **Worst case, phase 3:** a CI lane that costs nineteen minutes forever and
  cannot fail. `docs/internals/plan-four-found-DONE.md` phase 1 measured that
  `LC_ALL=<comma locale> ./tychoc` is **inert** — a C program stays in the `"C"`
  locale until something calls `setlocale`, so the obvious spelling of this lane
  proves nothing. The lane must use the `LD_PRELOAD` constructor trick that
  phase proved, and it must be shown red against a reverted fix.
- **Reversibility:** total. Compiler source, a CI step, a runner and a comment.
- **Verified — each of the four, re-checked today, with the command:**
  - `./tychoc` on a program containing `1e400`, then `grep -n inf` on the emitted
    C, gives `double h_x = inf;`. `inf` is not a C keyword or a standard macro.
  - `grep -n 'weblog\|webserver' scripts/entrypoints.sh` puts both in `MUST` at
    `scripts/entrypoints.sh:40`, and that script is **compile-only**;
    `scripts/ci.sh:104` says in as many words that the examples with their own
    runner "were outside this". Both `examples/weblog/run.sh` and
    `examples/webserver/run.sh` exist and are run by nothing in `make ci`.
  - `grep -n 'LC_ALL\|LC_NUMERIC\|LD_PRELOAD' scripts/ci.sh Makefile` returns
    **nothing**.
  - `scripts/asan_self.sh:10` cites `Makefile:103-106` as "documents that lane";
    `sed -n '103,106p' Makefile` is the **wiki sync recipe**
    (`python3 scripts/sync-wiki.py`), not the ASan lane.
- **Assuming — to check, not fact:** that `examples/weblog` and
  `examples/webserver` produce deterministic output at all. Their goldens exist
  and were being protected, which suggests their runners compare them locally;
  phase 2 confirms that before wiring, and if a runner does not compare its own
  golden, that is the finding.

## Phases

- [x] **Phase 1 — the stale citation, and nothing else**
  - `scripts/asan_self.sh:10` says `Makefile:103-106     documents that lane`.
    Those lines are `make wiki`'s recipe. Find where the ASan lane is actually
    documented in the `Makefile` and cite that, in whichever form
    `CLAUDE.md`'s citation section now sanctions — the previous plan's phase 4
    widened `@SYMBOL` to any target that **has a name of its own**, and added
    `Makefile` to the Markdown side's accepted paths.
  - A bare range is the honest form when there is no single subject token
    (`CLAUDE.md` says so explicitly, and says not to invent one). Decide which
    applies here and say why.
  - Done when: the citation names the lane it claims to, and the gate is proved
    unable to pass silently — break the thing it points at, show the red,
    restore, show the green.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.
    **Not** `sh scripts/asan_self.sh` — this changes a `#`-led comment line and
    nothing that script executes. Say so rather than running it.

  **Evidence, 2026-08-02. One line changed in `scripts/asan_self.sh`, nothing
  else.** `Makefile` was not changed — the probe below restored it
  byte-identically.

      -#   Makefile:103-106     documents that lane
      +#   Makefile@Differential  documents that lane, above the `test` target

  **WHERE THE LANE IS ACTUALLY DOCUMENTED.** The citing line is the second item
  of a three-item list, and "that lane" is the item above it — `tests/run.sh`'s
  sanitizing of the **emitted** C, i.e. `make test`. Its `Makefile`
  documentation is the comment block above the `test` target, opening
  `# Differential test suite: every examples/*.ty and tests/*.ty built both /
  native -O2 and under -fsanitize=address,undefined`. The old `:103-106` is the
  `make wiki` recipe (`python3 scripts/sync-wiki.py`), eight lines earlier and
  about nothing related.

  **THE FORM: `Makefile@Differential`, and the reason is that a bare range has
  already been measured failing on this exact citation.** `SRCCITE` checks a
  source-side range for **bounds only** (`scripts/check_citations.py:1118` —
  `if a < 1 or b < a or b > len(sl)`), so `:103-106` stayed green the whole time
  it was drifting across recipe boundaries. Re-writing it as `Makefile:111-114`
  would re-arm exactly that silent drift. The widened rule from
  `docs/internals/plan-four-found-DONE.md` phase 4 asks whether the target **has
  a name of its own**, and this one does: the lane *is* the differential test
  suite, and `Differential` is the word that opens its comment.
  **`grep -c Differential Makefile` answers 1** — the token occurs on exactly
  one line of the file, so the ref cannot resolve to some other construct. The
  `@SYMBOL` check remains deliberately weak (existence anywhere in the file, no
  uniqueness demanded); uniqueness here is a property of today's tree, checked
  rather than assumed, not a promise the gate enforces.

  **A BARE RANGE WAS THE OTHER CANDIDATE AND WAS REJECTED ON THE EVIDENCE, not
  on taste.** `CLAUDE.md` sanctions it "when there is no single subject token"
  and forbids manufacturing one. A token was not manufactured here: it was
  already the first word of the block it names.

  **THE PROOF — RED, THEN GREEN.** `git diff --stat -- Makefile` was empty
  first.

      $ sed -i 's/Differential test suite/Cross-checked test suite/' Makefile
      STALE  scripts/asan_self.sh:10  `Makefile@Differential` -> 'Differential'
             does not appear anywhere in Makefile.
      citation check: FAILED (1 stale citation(s) above)

  Restored from the backup: `cmp` silent, `git diff --stat -- Makefile` empty,
  `grep -c Differential Makefile` back to 1, gate green. The gate names
  `scripts/asan_self.sh` by file and line, so it cannot pass silently on a
  rename or a deletion of the thing it points at.

  **THE REPLACEMENT IS ONE LINE FOR ONE LINE, DELIBERATELY.** The first draft
  wrapped onto a second comment line, and that **broke a correct citation two
  screens down**: `scripts/asan_self.sh:84` carries a bare self-referential
  `:110-111` for the sanitized build, which a +2 line shift silently invalidates
  — a bare `:N-M` in a *source* file names no path, and `SRCCITE` requires one,
  so no gate would have said a word. Rewritten to a single line;
  `git show HEAD:scripts/asan_self.sh | wc -l` and `wc -l` both answer 162, and
  line 110 is still `mkdir -p build`. The self-ref's own pre-existing off-by-one
  is filed as phase 5 rather than absorbed.

  **The gate then caught this write-up itself**, which is worth recording: the
  phase-5 filing below first described "the `:358` companion", and that bare
  `:358` bound to the previously named path in this document —
  `scripts/asan_self.sh`, 163 lines — for `STALE plan.md:215 ... OUT OF BOUNDS`.
  `CLAUDE.md` warns about exactly this in as many words, and four earlier phases
  reddened the gate on their own evidence the same way. Repaired by dropping the
  number, which carried nothing the sentence did not.

  **Gates.** `python3 scripts/check_citations.py` green (`163 anchored`,
  `258 source->source in bounds`, `384 path@SYMBOL`). `sh scripts/check_links.sh`
  green, 146 markdown files. **`sh scripts/asan_self.sh` was NOT run and is not
  the gate here** — the diff is a single `#`-led comment line, confirmed with
  `git diff -- scripts/asan_self.sh`, and the script executes nothing on it.
  `make editors-check`, `make test` and `make ci` were not run for the same
  reason: no compiled artifact and no executed line changed.

- [x] **Phase 2 — an overflowing float literal is a Tycho diagnostic, not a `cc` error**
  - `1e400` reaches codegen as an infinity and is emitted as the bare token
    `inf`, so the generated C fails to compile with `'inf' undeclared`. The user
    wrote Tycho and got an error about C.
  - Decide, and write the reason down: is an out-of-range float literal a
    **compile error in Tycho** (with the file, line and the literal named), or
    is it a legal value that must be emitted portably (`HUGE_VAL`, `INFINITY`
    from `<math.h>`, or a `1.0/0.0` construction)? Both are defensible. What is
    not defensible is the current state, where it is accepted and then explodes
    one layer down.
  - Check the same question for the underflow side (`1e-400`) and for `nan` if
    any path can produce one, rather than fixing the one case in the filing.
  - Scope: `src/tychoc.c`, and a fixture under `tests/`. If the answer is a
    diagnostic, the fixture is a `.ty` that must fail to compile — read how the
    corpus already expresses that (`tests/` has negative fixtures; find their
    convention rather than inventing one).
  - Done when: `1e400` produces a Tycho-level message naming the literal, or
    emits C that compiles and behaves; and a fixture holds whichever it is.
  - Verify: `make`, then `make test` (~8 min, timeout at least 900000 ms). It
    was `passed: 562 failed: 0`; your fixture should make it 563, and a number
    that moves DOWN is a silent loss. Prove the check can fail by reverting.

  **Evidence, 2026-08-02.** Changed: `src/tychoc.c` — one `#include`, a range
  check in the float-literal arm of the lexer, and a comment in codegen's
  `E_FLOAT` arm. Added: `tests/diag/float_lit_overflow.ty` + `.err`,
  `tests/reject/float_lit_overflow_neg.ty`, `tests/float_lit_range.ty` + `.out`.
  Nothing under `runtime/`, `corelib/` or the harness.

  **THE DECISION: (a), A TYCHO COMPILE ERROR — and the reason is that the spec's
  guarantee is about VALUES, while `1e400` is a SPELLING.**
  `docs/spec/03-types.md:59` does make infinity legal (`1.0/0.0` is `+inf`, and
  `/` does not trap), which is a real argument against rejecting anything. It is
  not an argument for this literal: refusing `1e400` removes no expressible
  value, because `1.0/0.0` is untouched and still produces `+inf` — asserted in
  `tests/float_lit_range.ty`, not assumed. What is removed is a shape nobody
  writes on purpose, in which a finite decimal the user typed silently becomes an
  infinity. That is the same class of defect as the locale bug the previous plan
  fixed one commit earlier: correct source, wrong number, nothing on stderr.

  **The decisive evidence is in-tree, not taste.** `src/tychoc.c:455` already
  does exactly this for the integer twin — `die_at(line, "integer literal out of
  range")`, eleven lines below the new check, in the same `lex` arm. An
  out-of-range integer literal has always been a Tycho diagnostic here. Option
  (b) would have left the two literal kinds answering the same question in
  opposite ways.

  **THE CHECK, AND WHY IT IS SPELLED THAT WAY.** `src/tychoc.c:444` —
  `if (dv > DBL_MAX || dv < -DBL_MAX)`, with `src/tychoc.c:30` adding
  `<float.h>`. Two choices in there were measured rather than assumed:

  - **`DBL_MAX`, not `isinf`.** `Makefile@tychoc` is the compiler's link recipe
    and carries no `-lm`, so `math.h`'s `isinf` risks an undefined reference on
    any host that does not inline it. `<float.h>` is freestanding. The
    substitution is exact: the token grammar above the check accepted only
    `[0-9.eE+-]`, so a literal can never spell `nan`, and a magnitude past
    `DBL_MAX` is therefore exactly an infinity.
  - **The value, not `errno`.** `strtod` sets `ERANGE` for overflow **and** for
    underflow, so an `errno` test would have rejected `1e-400` too.

  **THE NEIGHBOURING CASES, EACH RUN, NOT ASSUMED.** Probed with `--emit-c` on
  the built compiler; the middle column is what the emitted C or the compiler
  said before this phase.

  | Written | Before | Now | Why |
  |---|---|---|---|
  | `1e400` | `double h_x = inf;` → `cc`: `'inf' undeclared` | Tycho error quoting `` `1e400` `` | no binary64 value; `1.0/0.0` is the way to an infinity |
  | `-1e400` | `double h_x = (-inf);` → same `cc` error | same error, quoting `` `1e400` `` | the lexer sees the magnitude; unary minus is its own token, so no separate check was needed |
  | `1e-400` | `double h_x = 0.0;` | unchanged | gradual underflow: the correctly-rounded binary64 value **is** `0.0`, still finite, legal IEEE-754 |
  | `0.1` | `double h_x = 0.10000000000000001;` | unchanged | finite with no exact binary64 form — must keep working, and does |
  | `1.7976931348623157e308` | accepted | accepted | the boundary is inclusive; a `>=` check would have rejected `DBL_MAX` itself |
  | `1.0/0.0`, `0.0/0.0` | runtime division | unchanged | **not** const-folded — `src/tychoc.c:4481` bails on any non-`E_INT` operand — so no `nan`/`inf` ever reaches codegen as a literal |

  **NO PATH CAN PRODUCE A `nan` LITERAL, and that is enumerated rather than
  hoped for.** Every `E_FLOAT` `fval` is finite by construction: the lexer now
  refuses an out-of-range literal; int→float literal conversion and the
  synthesized `0.0`/`1.0` seeds start finite; and `const_fold` folds only unary
  minus on a float (negating a finite gives a finite) because its binop arm
  returns early on any non-`E_INT` operand. So the `'n'`/`'i'` arm of the `".0"`
  guard's scan is now unreachable. It is **kept**, with that fact written at
  `src/tychoc.c:9669`, because it states the guard's contract — and because
  widening `const_fold` to float arithmetic would bring `inf` straight back to
  that line as the bare token `cc` rejects. The note says so where the next
  person will meet it.

  **A PORTABLE EMISSION (`HUGE_VAL`) WAS CONSIDERED AND NOT WRITTEN.** After the
  lexer check nothing can reach codegen non-finite, so such code could not be
  exercised by any fixture — a check that cannot fail, in the same commit as a
  phase about checks that cannot fail. The enumeration above plus the comment is
  the honest form; `CLAUDE.md`'s ladder rung 1 is the rest of the reason.

  **THE FIXTURE CONVENTION, AND WHERE IT WAS READ.** Three lanes, all defined in
  `tests/run.sh` and used as they are already used there:

  - `tests/diag/<name>.ty` + `.err` — an invalid program whose **exact** compiler
    output is a golden (`tests/run.sh:229-252`). Chosen for `1e400` because this
    is the lane that can prove the message **quotes the literal**; `tests/reject/`
    asserts only "nonzero exit, non-empty diagnostic" and would pass on
    "out of range" with no number in it. Modelled on
    `tests/diag/array_arith_fixlen.ty`, whose header says in as many words that
    `tests/reject/` locks *that* and this lane locks *what*.
  - `tests/reject/<name>.ty` — the cheap companion for `-1e400`. Flat, no
    `package` header, per the guard `tests/run.sh:153-166`.
  - `tests/<name>.ty` + `.out` — the positive lane, for everything that must keep
    compiling. **Nothing in it prints an `inf` or a `nan`:**
    `docs/spec/appendix-f-impl-defined.md` records that text as
    implementation-defined, so a golden holding it would be a portability trap
    rather than a range test. Non-finites are checked structurally instead — by
    comparison against `DBL_MAX`, and by NaN's unorderedness.

  **BREAK AND REVERT — BOTH DIRECTIONS, AND THE SECOND ONE MATTERS.**
  `src/tychoc.c` was backed up first; the restore was `cmp`-silent against the
  backup before the final `make`.

  *Too narrow* — the check replaced by `if (0)`, rebuilt:

      FAIL diag_float_lit_overflow (compiler ACCEPTED an invalid program)
      FAIL reject_float_lit_overflow_neg (tychoc ACCEPTED an invalid program)
      /tmp/.../dg.c:2556:18: error: 'inf' undeclared (first use in this function)
       2556 |     double h_x = inf;

  — which is the original filing reproduced exactly, from the fixture.

  *Too wide* — the same check as `dv >= DBL_MAX || dv <= -DBL_MAX`, rebuilt:

      tests/float_lit_range.ty:42: error: float literal out of range:
      `1.7976931348623157e308` exceeds the largest float ...

  The positive fixture is therefore not decoration: it is the only thing holding
  the boundary inclusive, and it reddens on an off-by-one nobody would notice.

  **COUNT: `passed: 562` before, `passed: 565   failed: 0` after** — `+3`, one
  per fixture, in three different lanes. The brief predicted 563 for one fixture;
  the two extra are the negated form and the positive side, both named in the
  brief's own "do not fix only the one case" list, and neither could share a file
  with the first (the compiler stops at the first error). Nothing moved down.
  `make` is clean under `-Wall -Wextra -std=c11`. `make ci` and `make corelib`
  were not run, as instructed.

  **CITATION CHURN: 120 stale anchors, all repointed, none deleted.** The insert
  shifts `src/tychoc.c` by `+1` from line 30, `+28` from line 420 and `+38` from
  line 9641 (`git diff -U0 -- src/tychoc.c` hunk headers). Repointed from the
  gate's own STALE report rather than by hand, so only refs it named were
  touched; the seven it declined were finished individually after reading each
  line. **No record line was repointed** — the one candidate the scan flagged,
  `docs/spec/12-aggregates.md:18`, is ordinary provenance prose that happens to
  contain a `→`, not a before/after row, so it was shifted like the rest.
  `python3 scripts/check_citations.py` and `sh scripts/check_links.sh` both green.
  Their tallies are deliberately not copied here: every one of them moves when any
  later phase adds a citation, so a number typed into this paragraph would be
  stale by the next commit with nothing checking it — run the command.

  **ONE REPOINT IS NOT MECHANICAL, AND IT IS FLAGGED HERE.**
  `scripts/asan_self.sh:39` anchored `binds` at a line that, at `HEAD`, read
  `if (sig_find(nm)) { g_sizebinds = saved_sb; return; }` — it passed only
  because `g_sizebinds` **contains** the substring `binds`. It was already
  pointing at the wrong statement and the gate could not say so. Rather than
  shift a wrong number by 28, it now reads `src/tychoc.c:7762@gi.binds`, the
  `xmalloc` the sentence actually describes — which is the same site
  `docs/internals/plan-loops-cleanup-DONE.md:3145` independently names. Phase 7
  files the leftover inconsistency in that archive.

- [x] **Phase 3 — the two orphan lanes get compared, not just compiled**
  - `examples/weblog/expected.out` and `examples/webserver/expected.out` are
    tracked, and as of the previous plan they are un-ignored and gate-protected —
    and **nothing in `make ci` compares them**. `scripts/entrypoints.sh` proves
    only that the programs compile.
  - **Read both runners first** and report what they do: if a runner already
    compares its golden, this phase is only about wiring it into `make ci`; if it
    does not, the runner needs the comparison too. Do not assume which.
  - Determinism is the risk. Read what each program prints. A port number, a
    timestamp, a temp path or an iteration order in the output means the golden
    is a record of the minute it was recorded — gate the deterministic part and
    say plainly what is excluded, in the runner, where the next reader meets it.
  - Scope: `examples/weblog/run.sh`, `examples/webserver/run.sh`, `Makefile`,
    `scripts/ci.sh`, `CLAUDE.md`'s gate table.
  - Done when: both goldens are compared by something `make ci` runs, each lane
    is proved red by changing its program's output and green after reverting, and
    `CLAUDE.md`'s table names the new lane and its measured cost.
  - Verify: the new lane both directions, `sh scripts/entrypoints.sh`, and
    `make goldens-check` (the new lane must not break the golden inventory).
    Hold `make ci` for phase 4, which closes the chain.

  **Evidence, 2026-08-02.** Changed: `examples/weblog/run.sh` and
  `examples/webserver/run.sh` (header comments only — not one executed line),
  `Makefile` (two targets + `.PHONY`), `scripts/ci.sh`, `CLAUDE.md`'s two gate
  tables, and two citations my own edit broke (below). No golden was re-recorded;
  no `.ty` was changed.

  **WHAT THE RUNNERS ALREADY DID — the brief said not to assume, and the answer
  is that BOTH ALREADY COMPARED THEIR GOLDEN.** Each ends in a `diff -u` of
  `$D/expected.out` against the program's stdout, sets `fail=1` on a difference
  and `exit 1`s, and each already had a `RECORD=1` re-record path. So the
  assumption in the Pre-flight — "their goldens exist and were being protected,
  which suggests their runners compare them locally" — held. **The hole was never
  a missing assertion. It was that nothing invoked the runners**, so a correct
  comparison sat unexecuted: `scripts/entrypoints.sh` proves only that the two
  `main.ty` compile, and its own line for them is in a compile-only sweep. This
  phase therefore added no comparison logic at all; it added the two `make`
  targets and the two `make -s` calls that make the existing comparison run. That
  is the whole functional diff.

  **DETERMINISM — ESTABLISHED BEFORE ANYTHING WAS WIRED, AND NOTHING IS
  EXCLUDED.** One build each into a `mktemp -d`, then the binary run three times:

      weblog: 3 runs byte-identical
      webserver: 3 runs byte-identical
      weblog == golden
      webserver == golden

  `cmp` was silent between runs 1/2 and 2/3 for each program, and between run 1
  and the tracked golden. The comparison is therefore over the **whole** of
  stdout for both lanes, with no field masked and no `grep -v` — and that is a
  property of the programs, checked rather than hoped for:

  - **weblog** parses a demo log embedded as a string literal in its own source
    when given no arguments, so every timestamp it prints is the log's
    (`2000-10-10 13`), not the clock's. No port, no hostname, no readdir order.
  - **webserver's** no-argument leg is the `else` branch of `main`, which
    dispatches a **fixed** `paths` list through the pure `route()` and prints
    status + body — `# Golden-locked (no socket needed)` in the source says so,
    and it is true: no socket is created, so no port exists to leak.
    `examples/webserver/main.ty@getenv` reads `PORT` only inside `if serve:`,
    which this runner never takes. Pages render from
    `examples/webserver/main.ty@ROOT` = `examples/webserver/content`, all eight
    files tracked, so a change there is a real golden change and should redden.

  Both binaries are built into a `mktemp -d`, but that path never reaches stdout —
  which is why nothing had to be excluded. **The exclusion statement is written
  into both runner headers**, per the brief, not only here; `tools/tycho-ar/run.sh`
  is the precedent and the wording follows it. The webserver header also says in
  as many words that it is **not** `server-check`: that lane binds `--port 0` and
  excludes the bound port, and the two are one edit away from being confused.

  **WHERE IT IS WIRED, AND WHY THERE.** `make weblog` and `make webserver`, each
  `: tychoc` running its own `run.sh` — the exact convention `fetch`, `site`,
  `raytrace` and `mandelbrot` already use — added to **step [3/13]** of
  `scripts/ci.sh` beside them. Step 3 is the right home by that step's own
  definition: `scripts/ci.sh` describes it as the lane where "everything in step 3
  compares stdout to a recorded golden", which is precisely what these two do. No
  new step number was created, so the `/13` denominator is untouched. The stale
  paragraph at the old `scripts/ci.sh:104` — which named webserver and weblog as
  examples that "were outside this file" — is now false and was rewritten rather
  than left to mislead.

  **RED, THEN GREEN — ONCE PER LANE, BY CHANGING WHAT THE PROGRAM PRINTS.** Each
  `main.ty` was backed up first and the restore was `cmp`-silent against the
  backup, with `git diff --stat` empty, before the green run.

  *weblog*, `println("status classes:")` → `println("status classes (by code):")`:

      -status classes:
      +status classes (by code):
      weblog: tychoc output differs from golden
      make: *** [Makefile:335: weblog] Error 1

  *webserver*, the footer literal `Served by Tycho.` → `Served by Tycho (dev).`:

      -<footer>Served by Tycho.</footer>
      +<footer>Served by Tycho (dev).</footer>
      webserver: tychoc output differs from golden
      make: *** [Makefile:338: webserver] Error 1

  After restoring each: `make weblog exit=0` and `make webserver exit=0`, both
  printing `ok (tychoc == golden; ...)`. Note the webserver diff landed at
  `@@ -108,7 +108,7 @@` — the change was 108 lines into a 117-line golden, so
  this is not a lane that only checks its first screen.

  **MEASURED COST, NOT ESTIMATED.** Three runs of each runner, wall clock via
  `date +%s.%N`: weblog `2.14 / 1.85 / 1.85 s`, webserver `2.07 / 2.10 / 2.10 s`
  — **~1.9s and ~2.1s, ~4s for the pair**. Nearly all of it is tychoc + `cc`; the
  programs themselves are milliseconds. `CLAUDE.md`'s gate table carries the pair
  as one row with that figure and its date, and the `make ci` step table's `[3]`
  row now names `make weblog` and `make webserver` among the targets to reach for
  instead of the sweep.

  **A CITATION MY OWN EDIT BROKE, WHICH THE GREEN GATE DID NOT CATCH.** Growing
  the two runner headers shifted their bodies down (weblog `+13`, webserver
  `+23`), and three refs elsewhere name those body lines by number:
  `.gitignore` cited `examples/weblog/run.sh:24` and
  `examples/webserver/run.sh:23` for the `mktemp -d`, and
  `scripts/check_goldens.py` cited `examples/weblog/run.sh:23` for `D`'s literal
  assignment. **`python3 scripts/check_citations.py` was green across the break**
  — `SRCCITE` bounds-checks a source→source ref and nothing more, exactly as
  phase 1 measured on `Makefile:103-106` — so all three drifted silently, and
  `examples/webserver/run.sh:23` had come to rest on a **new comment line of mine
  that contains the words `mktemp -d`**, the worst case: a ref that looks right
  and points at prose about the thing instead of the thing. Repaired by repointing
  to `:37`, `:46` and `:36`, and by rewording both new headers to say "throwaway
  temp dir" so the token `mktemp` occurs exactly once per runner and cannot
  produce that false resolution again. This is collateral of this phase's own
  edit, not a discovery — the general defect it re-demonstrates is already filed
  as phase 5, which this strengthens with a second instance.

  **GATES, each run in the foreground after the repair.** `make weblog webserver`
  green **twice in a row** (determinism at the lane level, not just the binary).
  `sh scripts/entrypoints.sh` → `entrypoints: ok (11 entry points compile with
  tychoc)`, with `ok examples/weblog/main.ty` and `ok examples/webserver/main.ty`
  in the list. `make goldens-check` → `goldens-check: ok`, `35 runners scanned, 17
  name a golden ... all tracked by git` — the new wiring did not disturb the
  inventory, which was the specific risk. `python3 scripts/check_citations.py` and
  `sh scripts/check_links.sh` both green; their tallies are deliberately not
  copied here, per `CLAUDE.md` — run the command. `sh -n` clean on `scripts/ci.sh`
  and both runners. **`make ci` and `make test` were NOT run**, as instructed:
  nothing compiled changed, and phase 4 closes the chain.

- [x] **Phase 4 — a CI lane that compiles under a hostile locale**
  - Two plans have now fixed a locale defect — `runtime/tycho_rt.c@tycho_float_to_str`
    and both float-literal sites in `src/tychoc.c` — and **nothing in CI would
    notice either regressing**, because the defect is latent until a linked
    library calls `setlocale`.
  - The lane must use the mechanism `docs/internals/plan-four-found-DONE.md`
    phase 1 proved: a small `LD_PRELOAD` shared object whose constructor calls
    `setlocale(LC_ALL, "")`, with `LC_ALL` set to a comma-decimal locale. The
    obvious spelling without the preload is **inert** and was measured so.
  - **The lane must skip, loudly and by name, on a host with no comma-decimal
    locale** — `locale -a | grep` decides, and a silent skip here is the vacuous
    pass this chain has hit five times. It must also not leave a `.so` in the
    tree; build it in a temp dir.
  - Scope: a new script under `scripts/`, `Makefile`, `scripts/ci.sh`,
    `CLAUDE.md`'s gate table.
  - Done when: the lane compiles and runs at least the two existing locale
    fixtures under the hostile locale, is proved red by reverting either fix, and
    prints its skip reason when it cannot run.
  - Verify: the lane both directions, then **`make ci` once, last** (~19 min,
    timeout at least 1500000 ms) — this phase adds a CI step and closes the
    chain, the two conditions that earn the sweep. If it reddens, fix with the
    failing step's own gate and re-run that gate, never `make ci` as a loop.

  **Evidence, 2026-08-02.** Added: `scripts/locale_check.sh`. Changed: `Makefile`
  (a `locale-check` target, `.PHONY`, and the ilp32 CC line — see the red below),
  `scripts/ci.sh` (a new step `[2e/13]`), `CLAUDE.md`'s two tables. No `.ty`, no
  golden, no compiler and no runtime source was changed.

  **THE THREE SITES, AND WHAT EXERCISES EACH.** The lane compiles both fixtures
  with tychoc under `LC_ALL=da_DK.utf8` + an `LD_PRELOAD` constructor, then runs
  the resulting binary in both locales against the same golden.

  | site | exercised by | already gated elsewhere? |
  |---|---|---|
  | `src/tychoc.c@c_strtod` (lexer READ) | compiling either fixture under the preload | **no** |
  | `src/tychoc.c@c_dtoa` (codegen WRITE) | same, plus `cmp` of the two emitted `.c` | **no** |
  | `runtime/tycho_rt.c@tycho_float_to_str` | running the binary under the preload | **yes** — see below |

  **THE THIRD SITE WAS ALREADY COVERED, AND SAYING SO IS THE HONEST FORM.** The
  brief asked for all three; the lane does reach all three, but only two of them
  were unguarded. `tests/float_str_locale.ty` flips `LC_NUMERIC` **inside its own
  process** through a runtime test hook, so `make test` already reddens for
  `tycho_float_to_str` on any host. That is visible in the runtime break-and-
  revert below: it reddened the *default-locale* run, inside the fixture's own
  `-- hostile LC_NUMERIC --` block, not the preloaded one. The two **compiler**
  sites cannot be covered that way and that is the hole this lane closes —
  tychoc is a separate process that has exited before `main()` runs.

  **A FOURTH LOCALE SITE EXISTS AND IS ALREADY GATED — enumerated, not assumed.**
  `grep` for `%g`-family formats and `strtod` over `runtime/tycho_rt.c`,
  `src/tychoc.c` and every `corelib/*/*.c` returns exactly four float paths
  tree-wide. The fourth is `corelib/strings/strings_shim.c`'s `strtod_l`, and
  `corelib/test/strings/main.ty:40` forces a hostile locale in-process the same
  way, printing `hostile=1` into its golden, so `make corelib` is its gate. There
  is no fifth.

  **RED, THEN GREEN — THREE REVERTS, ONE PER SITE.** `src/tychoc.c` and
  `runtime/tycho_rt.c` were backed up first; every restore was `cmp`-silent
  against its backup with `git diff --stat` empty before the green run, and each
  revert was rebuilt with `make -s tychoc` (`Makefile:13-14` embeds the runtime,
  so a runtime revert needs the rebuild too).

  *Compiler WRITE side* (`c_dtoa` → the pre-fix bare `snprintf`), lane exit 1:

      FAIL: float_lit_locale -- emitted C DIFFERS between the default and the
            da_DK.utf8 locale (src/tychoc.c: c_strtod / c_dtoa)
      -    double h_a = 1.5;
      +    double h_a = 1,5.0;
      -    double h_small = 0.0025000000000000001;
      +    double h_small = 0,0025000000000000001.0;
      FAIL: float_lit_locale -- cc REJECTED the C emitted under LC_ALL=da_DK.utf8

  *Runtime side* (`tycho_float_to_str` → the pre-fix bare `snprintf`), exit 1:

      FAIL: float_lit_locale -- built under da_DK.utf8, RUN in the default
            locale: differs from tests/float_lit_locale.out
      -1.5            = 1.5
      +1.5            = 1,5.0
      -half(1.5)      = 0.75
      +half(1.5)      = 0,75.0

  *Compiler READ side* (`c_strtod` → the pre-fix bare `strtod`), exit 1 — run
  because "the lane exercises all three sites" is a claim, and two reverts prove
  two of them:

      -1.5            = 1.5        +1.5            = 1.0
      -2.5e-3         = 0.0025     +2.5e-3         = 2.0

  After each restore: `make -s tychoc`, lane exit 0.

  **A LEG THAT COULD NOT FAIL, CAUGHT IN MY OWN SCRIPT.** The first draft
  `continue`d out of the fixture loop when the two emitted `.c` files differed.
  Write-side breakage **always** moves those bytes, so the `cc REJECTED` leg
  below it was unreachable for the only defect that produces it — a check that
  cannot fail, in the gate whose entire subject is checks that cannot fail. It
  records the failure and falls through now; the write-side transcript above
  shows both legs firing, which is how it was confirmed rather than argued.

  **THE FORCED SKIP.** The decision input is overridable so the skip path is
  testable without uninstalling a locale:

      $ TYCHO_LOCALE_CHECK_LOCALES="C POSIX en_US.utf8" sh scripts/locale_check.sh
      locale-check: SKIP (no comma-decimal locale installed -- none of 3
                    candidate name(s) reports decimal_point=",")
      exit=0

  Three other skips exist and each names its reason: no `locale(1)`, no buildable
  preload, and — the one that matters — **a preload that builds but does not take
  effect**. "The `.so` compiled" and "the `.so` changed the separator" are
  different facts; the lane compiles a two-line C probe and requires
  `printf("%g", 1.5)` to give `1,5` before it will run at all. Without that, a
  host where `LD_PRELOAD` is stripped would pass vacuously, which is the failure
  mode this chain has hit five times.

  **LD_PRELOAD HYGIENE.** The lane sets its own `LD_PRELOAD` for one command at a
  time via `env -u LD_PRELOAD LC_ALL=... LD_PRELOAD=...`, so an inherited value is
  **replaced, not extended**, and it prints a note naming the inherited value when
  it finds one. Extending would drag a sanitizer or the tmux `block-nnp.so` shim
  into a lane whose whole subject is what a load-time constructor does —
  `CLAUDE.md`'s Environment section records a stale `LD_PRELOAD` scoring 251/527
  spurious failures once already. The caller's environment is not modified.

  **NOTHING IS LEFT IN THE TREE.** The `.so`, the probe, the emitted C and both
  binaries live in a `mktemp -d` removed on `EXIT`. After a run,
  `make goldens-check` is `ok` (`35 runners scanned, 17 name a golden`, unchanged
  — the script is not a `run.sh`, so the inventory does not classify it) and
  `git status --short` shows only the four files this phase edits. There is no
  `RECORD=1` path on purpose: the two goldens belong to `tests/run.sh`, and this
  lane is a second, harsher reader of them, not a second owner.

  **MEASURED COST, NOT ESTIMATED.** Three runs, wall clock via `date +%s.%N`:
  **1.73 / 1.46 / 1.44 s — ~1.5s**. Green twice in a row before wiring and again
  through `make -s locale-check`. `CLAUDE.md`'s gate table carries that figure
  with its date, and the `make ci` step table gains a `[2e]` row.

  **`make ci` REDDENED, AND IT WAS NOT THIS PHASE.** First sweep: **exit 2**, at
  `[2b/13] make ilp32`, `FAIL float_lit_range (output != golden)` —
  `passed: 564 failed: 1`. Step `[2/13] make test` was `passed: 565 failed: 0` in
  the same run, so the fixture passes at 64-bit and fails only under `-m32`.

      4c4
      < 0.1 round trip = exact
      ---
      > 0.1 round trip = LOST

  `tests/float_lit_range.ty` is **phase 2's** fixture, and phase 2's brief told it
  to run `make test` and not `make ci` — so this red has been latent since commit
  `26a59e5` and was found, not caused, by this sweep. Proved pre-existing rather
  than asserted: `tests/` and `src/` are byte-identical to `f79dca8` (nothing but
  the four gate files is modified), and the failure reproduces from
  `tychoc --emit-c` plus `gcc -m32` alone.

  **THE CAUSE IS x87 EXCESS PRECISION, AND THE SPEC DECIDES WHOSE BUG IT IS.**
  `gcc -m32` defaults to the x87 FPU, which evaluates `double` expressions in
  80-bit registers and rounds to binary64 only on store, so the binary64-exact
  identity `0.1 + 0.2 == 0.30000000000000004` comes out **false**. Two fixes were
  defensible — loosen the fixture, or fix the lane — and `docs/spec/03-types.md:55`
  settles it: `float` **is** an IEEE-754 binary64 and "its arithmetic, rounding,
  and special values ... follow IEEE-754", with
  `docs/spec/appendix-f-impl-defined.md:44` making only the *textual* form of
  NaN/inf implementation-defined and saying the values are "fully defined". x87
  evaluation is therefore not a permitted configuration for this language: the
  fixture's assertion is correct and **the ilp32 lane was compiling Tycho floats
  at a precision the spec forbids**. Fixed in the lane, in scope, one line:
  `CC="gcc -m32"` → `CC="gcc -m32 -msse2 -mfpmath=sse"`.

  | build | `0.1 round trip` |
  |---|---|
  | `gcc -m32` (before) | `LOST` |
  | `gcc -m32 -msse2 -mfpmath=sse` | `exact` |
  | `gcc -m32 -ffloat-store` | `LOST` — rounds on store, not in the comparison |
  | `gcc` (64-bit, SSE2 baseline) | `exact` |

  `-ffloat-store` is in the table because it was tried first and **did not work**;
  recording only the flag that worked would hide that the obvious fix is wrong
  here. Every host that can already run this lane is x86-64, where SSE2 is
  baseline, so nothing is narrowed. The lane's subject is integer width, not the
  FPU, and it kept its own count: `make ilp32` alone went to
  **`passed: 565 failed: 0`**, matching the 64-bit lane exactly.

  **THE DEBUG LOOP WAS NOT ENTERED.** Per `CLAUDE.md`, the red was diagnosed by
  reading the failing step's output and reproducing the single fixture by hand,
  then fixed and re-checked with **`make ilp32` alone**, not with the sweep. One
  confirming `make ci` followed, for a fix already believed: **`MAKE_CI_EXIT=0`**,
  all thirteen steps, with `[2e/13]` printing `locale-check: hostile locale
  da_DK.utf8 (decimal_point=","), preload verified` and both fixtures `ok`. Two
  sweeps total, which is the brief's allowance.

  **GATES.** `make ci` **exit 0**. `make locale-check` green twice.
  `make ilp32` `passed: 565 failed: 0`. `make goldens-check` `ok`.
  `python3 scripts/check_citations.py` and `sh scripts/check_links.sh` green
  (tallies deliberately not copied — run the command). `sh -n` clean on
  `scripts/ci.sh` and `scripts/locale_check.sh`. `git status --short` after the
  final sweep lists only this phase's four files.

- [ ] **Phase 5 — a source file's bare self-reference is checked by nothing, and this one is already wrong**
  - Found by phase 1, which nearly displaced it by two lines and shortened its
    own edit to avoid doing so. Filed rather than absorbed.
  - `scripts/asan_self.sh:84` reads "the LIVE compiler: `:110-111` builds
    src/tychoc.c with -fsanitize". Today line 110 is `mkdir -p build` and the
    `src/tychoc.c` token it names is on line 112 — the build statement is
    111-112. Off by one at both ends, and **no gate can see it**: a bare `:N-M`
    in a source file names no path, `SRCCITE` requires one, so the ref is
    checked by nothing at all. This is the same blind spot
    `docs/internals/plan-four-found-DONE.md` phase 4 measured on the unanchored
    companion ref beside it, which had drifted onto a blank line of `Makefile`
    through twelve edits without one gate noticing.
  - **The open question is what a self-reference should look like, and it must
    be answered rather than assumed.** Three candidates, none obviously right:
    repoint the numbers (a repair carrying no information, which will drift
    again the next time a comment line is added above it); write the path out
    so `SRCCITE` at least bounds-checks it; or name the construct with the
    `@SYMBOL` form the previous plan widened, which for a self-reference means
    a file citing itself — a shape nothing in the tree uses yet.
  - Whichever is chosen, decide whether **every** bare in-file `:N` deserves it
    or only this one, and write the answer into `CLAUDE.md` so the next reader
    is not left guessing. `CLAUDE.md` is explicit that the bare-ref count is not
    a backlog and the reachable ones stay bare — that argument was made about
    *Markdown* prose, and whether it transfers to a source file whose refs are
    checked by nothing is exactly the question.
  - Scope: `scripts/asan_self.sh`'s line 84, and `CLAUDE.md` if the rule gains a
    sentence. **Not** `scripts/check_citations.py` unless the chosen form needs
    it, and not a sweep of other files.
  - Verify: `python3 scripts/check_citations.py`, and — if the chosen form is
    checkable — a break-and-restore proving it red. If it is not checkable,
    say so plainly instead of claiming a proof.

- [ ] **Phase 6 — the same silent overflow, one width down: an `f32` literal**
  - Found by phase 2 while enumerating the neighbouring cases, and filed rather
    than absorbed: it is a different type, a different site and a different
    verdict, and phase 2's scope was `float`.
  - Measured on the built compiler: `a: f32 = 3.5e38` **compiles clean and prints
    `inf`**. `3.5e38` is a perfectly good binary64, so phase 2's `DBL_MAX` check
    passes it; codegen then emits `float h_a = 3.5e+38;` and the C narrowing to
    binary32 — whose maximum is about `3.4e38` — turns it into an infinity. No
    diagnostic from tychoc, none from `cc`, and unlike the `float` case the
    program builds and runs, so nothing anywhere says a word.
  - **This is not simply "apply phase 2 again", and the phase must decide, not
    assume.** Phase 2 rejected a literal with no representable value at all;
    here the literal is representable as a `float` and is only out of range for
    the **annotated** target width, which is a narrowing question. `src/tychoc.c`
    already rewrites an `E_INT`/`E_FLOAT` literal in place when `want == T_F32`
    (`src/tychoc.c:6490` and the arms around it) — that rewrite is the natural
    place for a range check, and it is also the place a check would catch
    coercions the user did not write. Check whether the integer side already
    answers this for `u32`/`i32` narrowing and follow whatever it does, rather
    than inventing a third convention.
  - Scope: `src/tychoc.c`, and a fixture. Whether `f32` arithmetic that overflows
    at **runtime** should say anything is explicitly **out** of scope — that is
    IEEE-754 doing its job, exactly as `1.0/0.0` is.
  - Verify: `make`, then `make test` (~8 min), which was `passed: 565` after
    phase 2. Prove the check can fail both ways, as phase 2 did: a literal that
    must be refused, and the `f32` maximum itself, which must stay legal.

- [ ] **Phase 7 — a frozen archive names two different "real sites" for the same ref**
  - Found by phase 2's citation churn. Not urgent and possibly not work at all —
    filed so the next reader of that archive is not left to rediscover it.
  - `docs/internals/plan-loops-cleanup-DONE.md:3145` says the real site of
    `scripts/asan_self.sh`'s generic-bind-vector reference is the `gi.binds`
    `xmalloc`; `docs/internals/plan-loops-cleanup-DONE.md:3461` says it is a
    different line, and quotes the same `xmalloc` **beside a number that is not
    it**. The two cannot both be right, and they were written into the same
    archive.
  - Phase 2 repaired the live citation (`scripts/asan_self.sh:39` now points at
    the `xmalloc`, agreeing with the first of the two) and left both archive
    lines shifted mechanically, because `CLAUDE.md` protects a frozen record's
    numbers and this phase had no mandate to decide which of them was the record.
  - **The question is whether either line is a record line at all**, in the shape
    `CLAUDE.md` defines — neither is a repair log and neither is a table row, so
    on the letter of the rule they are ordinary prose in a frozen file, which the
    rule does not protect and the shape does not mark. That reading is worth
    testing before anything is edited: if it holds, the second line is simply
    wrong and can be corrected; if it does not, the honest move is to leave both
    and say why here.
  - **Read the whole `binds`/`gi.binds` history before touching either.** The
    ref has been repointed across several phases and the accidental
    `g_sizebinds` ⊃ `binds` substring match — which is why the gate never
    reddened on it — is itself part of the record.
  - Scope: `docs/internals/plan-loops-cleanup-DONE.md`, and only if the answer is
    "correct it". **Not** `scripts/check_citations.py`, and not a sweep.
  - Verify: `python3 scripts/check_citations.py` and `sh scripts/check_links.sh`.
    **Not** `make test` — no compiled artifact is involved, so it can tell you
    nothing the doc gates did not.

- [ ] **Phase 8 — three MORE orphan goldens, found while closing the first two**
  - Found by phase 3 and filed rather than absorbed: its scope was the two lanes
    the plan named, and this is three different programs with a different reason
    for being uncovered.
  - `ls examples/*/run.sh` returns eleven runners. After phase 3, step [3/13]
    covers `corelib`, `site`, `raytrace`, `mandelbrot`, `fetch`, `weblog` and
    `webserver`. Of the four left, **`examples/sqlite` is deliberate** — it needs
    `sqlite3`, and the audit table above already retired the phase about it. The
    other three are not deliberate and were never argued for: `examples/life`,
    `examples/minesweeper` and `examples/snake` each ship a tracked golden
    (`life.out`, `mine.out`, `snake.out`) and `grep -n 'life\|snake\|minesweeper'
    Makefile scripts/ci.sh` returns **no target and no CI call** for any of them.
    They are exactly the state weblog and webserver were in one commit ago:
    goldens visible, protected by `make goldens-check`, compared by nothing.
  - **The open question is whether these three CAN be gated, and it must be
    answered by reading them, not assumed.** All three are terminal games, so
    unlike weblog they may want a TTY, may read a clock for a frame delay, may
    seed a PRNG, or may print ANSI cursor control whose bytes depend on terminal
    size. Any one of those makes the golden a record of the minute it was
    recorded. Phase 3's method transfers: run each binary three times from one
    build and `cmp` before wiring anything.
  - If a program is deterministic, wire it exactly as phase 3 did (a `make`
    target beside the others, a `make -s` call in step [3/13], the exclusion
    statement in the runner). **If one is not, do not mask fields to force it
    green** — say so in the runner and in the evidence, and consider whether the
    golden should exist at all rather than being asserted by nothing while
    looking asserted. That is a real possible outcome here and is not a failure
    of the phase.
  - Scope: `examples/life/run.sh`, `examples/minesweeper/run.sh`,
    `examples/snake/run.sh`, `Makefile`, `scripts/ci.sh`, `CLAUDE.md`'s gate
    table. **Not** the three `main.ty` files — if a program must change to be
    gateable, that is a further phase, not this one.
  - Verify: the new lane both directions per program (red by changing what it
    prints, green after reverting), `make goldens-check`, and the two doc gates.
    **Not** `make test` and **not** `make ci` — no compiled artifact changes, and
    `scripts/ci.sh:117` already records this filing so the next reader of that
    comment meets it.

- [ ] **Phase 9 — the ilp32 lane ran non-conforming for its whole life, and only an accident found it**
  - Found by phase 4's `make ci`, filed rather than absorbed: phase 4 fixed the
    **lane** (one flag on one `CC` line, in its scope) and deliberately did not
    answer the general question below.
  - `make ilp32` compiled every fixture under `gcc -m32`, which defaults to x87
    and evaluates `double` in 80-bit registers. `docs/spec/03-types.md:55` says
    Tycho `float` arithmetic follows IEEE-754 binary64, so that lane was testing
    a configuration the language does not permit — for as long as it has existed.
  - **Nothing could see it**, and that is the part worth a phase. It surfaced
    only because phase 2 happened to write a fixture asserting an FP identity
    that is exact in binary64 and false at 80-bit. No gate asks whether a lane's
    compiler flags match the spec's arithmetic; the question is whether one
    should, or whether the answer is a sentence in `docs/spec` and a comment.
  - **Open questions, none to be assumed.** Does any *other* lane compile the
    emitted C with flags that change FP semantics (`make conc`, `make ffi`,
    `scripts/asan_self.sh`, `tests/rtparity`, the fuzz lanes, `bench/`)? Grep
    their `cc` lines for `-ffast-math`, `-Ofast`, `-mfpmath`, `-m32`. And should
    the spec say in as many words that evaluation carries **no excess precision**
    (C's `FLT_EVAL_METHOD == 0`)? It currently says the *values* are binary64,
    which a reader can satisfy while evaluating wider.
  - Scope: `docs/spec/` if the answer is a sentence, and other lanes' `cc` lines
    if the grep finds one. **Not** `src/tychoc.c` — the compiler emits C and does
    not choose the host's FPU mode — and **not** `tests/float_lit_range.ty`,
    whose assertion phase 4 established is correct.
  - Verify: `python3 scripts/check_citations.py` and `sh scripts/check_links.sh`
    for a spec sentence; `make ilp32` only if a `cc` line actually changes.
    **Not** `make ci`.

- [ ] **Phase 10 — a golden-consuming script that is not named `run.sh` is outside the golden inventory by construction**
  - Found by phase 4 while checking that its new script would not disturb
    `make goldens-check`. It does not — and the reason is the finding.
  - `scripts/check_goldens.py` walks **every tracked `run.sh`** and requires each
    to either yield a golden or be listed in `NO_GOLDEN` with a reason. A script
    named anything else is never classified, so it is neither checked nor
    reported missing. Measured: staging `scripts/locale_check.sh`, which reads
    `tests/float_lit_locale.out` and `tests/float_str_locale.out`, left the count
    at `35 runners scanned` — unchanged, and silently so.
  - No hole exists **today**: both goldens it reads are already inventoried
    through `tests/run.sh`, and `scripts/asan_self.sh` (the other such script)
    consumes none. So this is a shape, not a live defect, and it may well be
    correct as it stands — `run.sh` is a real convention and widening the walk to
    every `*.sh` would sweep in dozens of scripts that own nothing.
  - **The question to answer, not assume:** is the guard's contract "every
    *runner*" or "every *golden consumer*"? If the first, say so in the
    docstring, because the current wording reads as the second. If the second,
    the walk needs to widen and `NO_GOLDEN` needs a much larger exemption list —
    at which point the cost may exceed the value, which is a legitimate outcome.
  - Scope: `scripts/check_goldens.py`, and its docstring at minimum. **Not** a
    rename of any existing script.
  - Verify: `make goldens-check` both directions — the count must move if the
    walk widens, and a deliberately un-inventoried golden must redden it.
    **Not** `make test`.

## Audit of the seven filed phases, 2026-08-02

Each was re-checked against the tree with a command before this plan was
written. Four survived and are the phases above. Three are retired:

| Filed as | Claim | Verdict |
|---|---|---|
| phase 7 | `examples/sqlite/run.sh`, `bench/dbquery/run.sh` and `bench/fair_rest.sh` hand-name `sqlite3`, which `--print-deps` cannot see | **Retired — not a defect.** Confirmed they hand-name it (`examples/sqlite/run.sh:29-30`), and confirmed the filing's own reason: the dependency arrives through `extern "Lib"`, not a `deps` file, so `--print-deps` correctly prints nothing. The hand-naming is therefore **necessary and correct**, not stale-prone in the way the shim lists were. Closing it means teaching the compiler to surface `extern "Lib"` dependencies — a design change with no demonstrated breakage behind it. Ladder rung 1: it does not need to exist |
| phase 10 | the citation gate's docstring still spells a live line ref as a grammar example | **Retired — already done.** `grep -n 'Makefile:[0-9]' scripts/check_citations.py` returns nothing. The previous plan's phase 4 retired that example when it converted the refs; the follow-up was filed against a state that no longer existed by the time the commit landed |
| phase 11 | 67 Markdown `Makefile:N` refs are checked by nothing | **Retired — against stated policy, and it would redden frozen archives.** The count is 96 occurrences today, and the anchored ones live in `docs/internals/plan-loops-cleanup-DONE.md` and `docs/internals/plan-three-gates-DONE.md` — frozen records whose numbers are *data*. `CLAUDE.md` is explicit on both halves: the bare-ref count "is not a backlog", the reachable ones "stay bare", and a widening that puts gate pressure on a record line is the failure mode it warns about by name (it happened once already when `SRC_PREFIX` widened). Enforcing this would produce exactly the hand sweep this repo has declined three times with measurements each time |

Retiring these deletes nothing. Their filings stay in
`docs/internals/plan-four-found-DONE.md` with the reasoning that produced them.

## Status — PLAN COMPLETE

All four phases are done and committed, one commit each. Phases 5 through 10 are
filed follow-ups discovered **inside** phases 1 through 4 and pushed out of them
by their scope locks — they have the standing of the queued backlog rows below,
not of conditions on this plan.

**The Goal was: close four defects the previous plan's phases found while doing
something else — after re-checking all seven of its filings against this tree and
retiring three. All four are closed and every one has a break-and-restore
transcript under its phase.** What each was, and is now:

1. **A citation pointed at the wrong recipe** — `scripts/asan_self.sh:10` cited
   `Makefile:103-106` as documenting the ASan lane; those lines are `make wiki`'s
   recipe → it now reads `Makefile@Differential`, naming the differential test
   suite's own comment block. The phase's finding was *why* a bare range was the
   wrong repair rather than a matter of taste: `SRCCITE` bounds-checks a
   source-side range and nothing more, so `:103-106` stayed green the entire time
   it drifted across recipe boundaries, and re-writing it as a number would have
   re-armed exactly that.
2. **An overflowing float literal emitted `inf` into generated C**, so the user
   wrote Tycho and got `'inf' undeclared` from `cc` → a Tycho diagnostic quoting
   the literal, decided on in-tree evidence rather than preference: the integer
   twin eleven lines away has always been a Tycho error. `DBL_MAX` not `isinf`
   (no `-lm` on the compiler's link line), the value not `errno` (which cannot
   separate overflow from underflow, and `1e-400` must stay legal). Three
   fixtures in three lanes, `562 → 565`, and the positive one is the only thing
   holding the boundary inclusive.
3. **Two goldens no gate ever compared** — `weblog` and `webserver` → both are
   compared by `make ci` step `[3/13]`. The finding was that **both runners
   already did the comparison**; the hole was that nothing invoked them, so a
   correct assertion sat unexecuted. The phase therefore added no assertion at
   all, only two `make` targets and two calls — and determinism was established
   by three byte-identical runs each *before* anything was wired.
4. **Nothing in CI compiled under a hostile `LC_NUMERIC`** → `make locale-check`,
   step `[2e/13]`, ~1.5s. The load-bearing fact, inherited from the previous
   plan and re-confirmed here, is that the obvious spelling is **inert**: a C
   program stays in the `"C"` locale whatever `LC_ALL` says, so only an
   `LD_PRELOAD` constructor calling `setlocale` reaches the two compiler sites.
   Two of the three sites were genuinely unguarded; the runtime's third was
   already covered by a fixture that flips the locale in-process, and the phase
   says so rather than claiming three.

**Which of the seven filed phases were retired, and why.** Three, each re-checked
with a command before this plan was written, none deleted — their filings remain
in `docs/internals/plan-four-found-DONE.md` with the reasoning that produced them.
The hand-named `sqlite3` dependency was **not a defect**: the dependency arrives
through `extern "Lib"`, which `--print-deps` correctly cannot see, so the
hand-naming is necessary. The citation gate's docstring example was **already
done** — the filing was written against a state that no longer existed when the
commit landed. And gating 96 Markdown `Makefile:N` refs was **against stated
policy and would redden frozen archives**, which is the failure mode `CLAUDE.md`
warns about by name and which had already happened once when `SRC_PREFIX` widened.

**This plan filed three new phases: 8, 9 and 10.** Phase 3 found three *more*
orphan goldens (`life`, `minesweeper`, `snake`) whose gateability is an open
question because all three are terminal games. Phase 4 found the ilp32 lane
compiling Tycho floats at x87 precision the spec forbids — fixed in the lane,
with the general question of whether any *other* lane's flags change FP semantics
filed rather than assumed — and that a golden-consuming script not named `run.sh`
is invisible to `make goldens-check` by construction.

**The discovery rate keeps falling, and that is worth recording against the
previous plan's reading of it.** Three fixes found four; four fixes found seven;
these four found three. `docs/internals/plan-four-found-DONE.md` called the ratio
"not falling" and read it as a property of a heavily gated tree. One plan later
it has halved. Two readings survive and this plan cannot separate them: the
backlog of unguarded things is genuinely being worked down, or these four phases
simply touched less surface than the previous four. What did not change is the
rule that produced the number — none of the three was absorbed into the phase
that found it, and phase 4's ilp32 red is the sharpest case, because fixing the
lane was in scope while deciding the language question was not.

## Carried forward

The eleven backlog items ranked by the audit in
`docs/internals/plan-three-gates-DONE.md`, unchanged and unstarted:
`decimal.div`, JSON UTF-8 validation, `strings.parse_int` failing open beside a
strict `parse_float`, `io.write_bytes`, `io.make_dirs`, writable mtime, an
incremental digest, `eprintln`, the `image` shim nothing compiles here, a
document-reachability gate, and the `ParallelFor` width slot.
