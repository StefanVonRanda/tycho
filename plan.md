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

- [ ] **Phase 3 — the two orphan lanes get compared, not just compiled**
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

- [ ] **Phase 4 — a CI lane that compiles under a hostile locale**
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

## Carried forward

The eleven backlog items ranked by the audit in
`docs/internals/plan-three-gates-DONE.md`, unchanged and unstarted:
`decimal.div`, JSON UTF-8 validation, `strings.parse_int` failing open beside a
strict `parse_float`, `io.write_bytes`, `io.make_dirs`, writable mtime, an
incremental digest, `eprintln`, the `image` shim nothing compiles here, a
document-reachability gate, and the `ParallelFor` width slot.
