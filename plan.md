# Odin-style loops: three-clause `for`, bare `for:`, and the end of `range()`

Previous plan complete and archived at
[docs/internals/plan-array-arith-DONE.md](docs/internals/plan-array-arith-DONE.md)
(element-wise array arithmetic, four phases, `make ci` green). Its unclosed
discoveries are carried forward at the bottom of this file.

## Goal

`new_ideas.md` item 4, as settled with the user on 2026-07-29 across three
decisions. This is the **breaking** one; the two features before it were additive
and this is not.

- **Add** the three-clause form `for i := 0; i < n; i += 1:` and the bare
  infinite form `for:`.
- **Delete** the counting form `for i in range(a, b, step):` and the `range`
  builtin with it. All **549** call sites are rewritten.
- **`parallel for` gets its own counting spelling**, `parallel for i in 0..<N:`,
  because a three-clause loop cannot be chunked (below).
- **The `tychoc0` freeze lanes are retired**, because a breaking change cannot
  coexist with a frozen compiler that must still compile the corpus.

`for C:` (the condition form) is **already the Odin form** and is untouched —
`docs/spec/10-statements.md:87`. `new_ideas.md`'s "replace `for cond`" is
mistaken about that and the note should be corrected when the file is next
touched. The foreach form `for x in xs:` (`:98`) is likewise untouched.

Done looks like: `range` is not a name the compiler knows, no `.ty` file in the
tree uses it, `for i := 0; i < n; i += 1:` and `for:` and `parallel for i in
0..<N:` all work and are specified, and `make ci` is green.

## Pre-flight

- **Worst case, and it is worse than the last two plans.** A rewrite of 549 loop
  sites that changes semantics at one of them produces a program that still
  compiles and still runs and is quietly wrong. `range(a, b, step)` has three
  spellings (`range(n)`, `range(a, b)`, `range(a, b, step)`), an **exclusive**
  bound, and **negative step** support (`docs/spec/10-statements.md:90-96`). A
  mechanical rewrite that gets the exclusive bound or a negative step wrong is
  the single most likely defect in this plan. Phase 6 is therefore gated on
  goldens, not on "it compiles", and it is the phase to be most suspicious of.
- **Reversibility:** every phase is a commit. But note that phase 7 deletes a
  language feature, and phase 1 retires a correctness gate; neither is
  *technically* hard to revert and both are decisions rather than mechanics.
- **Verified — `for C:` already exists.** `docs/spec/10-statements.md:84-88`:
  "Tycho has one loop keyword, `for`, in three shapes" and "`for C:` runs its
  body while the `bool` condition `C` holds". So one third of `new_ideas.md`
  item 4 is already shipped and this plan does not touch it.
- **Verified — `;` is not a token.** `grep -n "TK_SEMI\|SEMICOLON\|';'"
  src/tychoc.c` returns nothing, and `grep -cn semicolon src/tychoc.c` returns 0.
  The three-clause form therefore needs a **new token**, unlike the last two
  features. That is why phase 8 exists: `tools/tychofmt.ty`, `tools/lsp.ty` and
  both editor grammars each carry their own lexer and will mis-tokenise `;` and
  `..<` until taught them.
- **Verified — the population.** 549 `range()` loops, 280 condition loops, 75
  foreach loops, counted with `grep -rn --include='*.ty'` excluding the frozen
  `compiler/tychoc0.ty`. By area: `bench/` 46 files, `corelib/` 26, `compiler/`
  25, `tests/conc` 16, `examples/corelib` 4, and a long tail.
- **Verified — why `parallel for` needs its own spelling.**
  `docs/spec/13-concurrency.md:78` says `parallel for` "applies to a counting or
  foreach loop", and **17 of the 28** `parallel for` sites in the tree are the
  counting form. The runtime chunks a known iteration space across threads; a
  three-clause loop's post clause is arbitrary, so its iteration count is not
  knowable in advance and cannot be chunked the same way. `0..<N` restores a
  statically-shaped iteration space for exactly the construct that needs one.
- **Verified — and this corrects what the user was told when deciding.** The two
  freeze lanes are **not** in CI: `grep -n "fixpoint\|frontparity" Makefile`
  returns nothing, and `scripts/ci.sh`'s thirteen steps never invoke either.
  They are hand-run, exactly as the first archived plan's "by-hand sweep"
  recorded. So rewriting the 114 frozen-lane files (`tests/` 84, `examples/` 29,
  `tools/` 1) would **not** have reddened `make ci` — it would have reddened two
  scripts nobody's CI runs. Retiring the lanes costs those two scripts plus the
  `tychoc0` legs in 15 non-gated runners (`fuzz/run_*.py`, `tests/*/run.sh`,
  `examples/*/run.sh`). The decision stands regardless — the rewrite needs those
  files freed either way — but the price is smaller than it was presented as, and
  a future reader should not think CI coverage was traded away here.
- **What is genuinely lost with the freeze lanes**, stated plainly because
  `ROADMAP.md:26-32` calls the freeze finished work and the first plan of the day
  explicitly refused to reopen it: continuous proof that `tychoc0` compiles what
  `tychoc` compiles, and the differential that caught a real over-tightening
  (`scripts/frontparity.sh:15-22` records `tests/newtype_agg.ty` reddening
  `fixpoint`). `tychoc0` remains on disk as the self-hosting artifact it is; what
  ends is the claim that it is *continuously* checked. Phase 1 must write this
  into `ROADMAP.md` and `docs/architecture.md` rather than quietly deleting two
  scripts.
- **A consequence worth naming:** `tests/postfreeze/` was created this morning
  (previous-previous plan, phase 1) purely to hold fixtures the frozen compiler
  refuses. With the lanes retired it has no reason to exist, and neither does
  `tests/postfreeze/abort/`. Phase 2 folds both back. This is not waste — the
  lane is what made the last two features shippable while the freeze stood — but
  leaving it in place would mislead every future reader.
- **Assuming — `0..<N` is accepted only where chunking needs it.** Allowing
  `for i in 0..<N:` sequentially as well would make it `range()` under a new
  name, and the 549-site rewrite pointless. So: `0..<N` is legal in `parallel
  for` and refused in a sequential `for`. **Risk if wrong:** a construct whose
  legality depends on context, which is a wart. The alternative wart is two
  blessed counting forms. Phase 5 states the choice in the spec; if it proves
  unworkable when written, say so rather than quietly generalising it.
- **Assuming — the zero-step guarantee cannot survive in general, and that is a
  real loss.** `range()` rejects a literal `0` step at compile time and aborts on
  a runtime `0` (`docs/spec/10-statements.md:93-96`). A three-clause loop's post
  clause is arbitrary code, so no equivalent check exists — `for i := 0; i < n;
  i += 0:` is an infinite loop the compiler cannot diagnose. `0..<N` has an
  implicit step of 1 and so has no zero-step case at all. Phase 9 must state this
  as a deliberate trade, not omit it.

## Status — PLAN COMPLETE

All ten phases done and committed, `make ci` green at `CI_EXIT=0` in 1062s on
2026-07-30. Evidence for each phase is under its own bullet; phase 10 carries the
per-lane coverage table.

**Commits, in the order they landed** (note phase 8 landed *before* phase 7 —
the tooling had to understand `;` and `..<` before the counting form was deleted):

| commit | phase |
|---|---|
| `49ec39f` | the plan itself |
| `1b93727` | 1 — retire the `tychoc0` freeze lanes |
| `f7da4b1` | 2 — fold `tests/postfreeze/` back into `tests/` |
| `ad92444` | 3 — lex `;` and `..<` |
| `f4c0afd` | 4 — three-clause `for` and bare `for:` |
| `d66713c` | 5 — `parallel for i in 0..<N` |
| `6ca63ca` | (interstitial) repair Appendix E's dead postfreeze citations |
| `b12e356` | 6 — rewrite every `range()` loop |
| `b2db983` | 8 — `tychofmt`, LSP and both editor grammars learn `;` and `..<` |
| `3f68a00` | 7 — delete the counting form and the `range` builtin |
| `a4f2991` | 9 — the spec |
| this commit | 10 — the full sweep |

**What shipped.** `for` now has **four shapes**: the three-clause
`for i := 0; i < n; i += 1:` (new), the bare infinite `for:` (new), the condition
form `for C:` (already existed, untouched) and the foreach `for x in xs:`
(untouched). `parallel for` gets its own counting spelling
`parallel for i in 0..<N:`, legal **only** there — a sequential `for i in 0..<N:`
is refused, so there is exactly one blessed sequential counting form.
**`range()` is gone**: not a name the compiler knows, and no `.ty` file in the
tree uses it. 549 call sites were rewritten.

**Three deliberate losses**, none of them accidents, all of them written into the
tree rather than only here:

1. **Continuous `tychoc0` checking.** `scripts/frontparity.sh` and
   `compiler/fixpoint.sh` are retired; a change that silently narrows what
   `src/tychoc.c` accepts no longer has a second implementation to disagree with
   it. Nothing replaces them. Recorded in both script headers, `ROADMAP.md`,
   `docs/architecture.md` and `CLAUDE.md`.
2. **The zero-step guarantee.** `range()` refused a literal `0` step at compile
   time and aborted on a runtime `0`. A three-clause post clause is arbitrary
   code, so `for i := 0; i < n; i += 0:` is an infinite loop no compiler can
   diagnose; `0..<N` has an implicit step of 1 and so has no zero-step case at
   all. Stated as a trade in the spec by phase 9.
3. **Bounds-check elision at 223 sequential sites across 97 files.** The
   recogniser is gated on `S_FORRANGE`'s `r_start`/`r_stop`/`r_step`
   (`src/tychoc.c:10791-10801`) and `S_FOR3` has no arm, so those sites now emit
   the *checked* accessor. Correctness is unaffected — the fallback is the safe
   path — which is why every golden held and why no gate caught it. Open as
   phase 27, and phase 10 confirmed `bench-guard` still cannot see it.

**24 unchecked phases remain** in "Carried forward", none blocking. Two of them
are this plan's own follow-ups (27 bounds-check elision, 30 the dead `r_step`)
and are ordered: 27 before 30. Note also that phase 28's box is unticked while
its own last bullet says "Closed by phase 7" — filed as phase 38.

## Phases

- [x] **Phase 1 — retire the `tychoc0` freeze lanes**
  - Scope: `scripts/frontparity.sh`, `compiler/fixpoint.sh`, the `tychoc0` legs
    of the 15 non-gated runners that build it (`fuzz/run_leak.py`,
    `fuzz/run_pkg.py`, `fuzz/minimize.py`, `fuzz/run_parforparity.py`,
    `fuzz/run_reject.py`, `fuzz/run_unaryparity.py`, `fuzz/run.py`,
    `fuzz/run_eqparity.py`, `tests/recursion/run.sh`, `tests/rtparity/run.py`,
    `tests/conc/run.sh`, `tests/ffi/run.sh`, `examples/sqlite/run.sh`,
    `examples/fetch/run.sh`, `examples/site/run.sh` — re-derive this list, it was
    taken by grep), plus `ROADMAP.md`, `docs/architecture.md`,
    `docs/spec/appendix-e-conformance.md` and `CLAUDE.md`'s gate table.
  - Retire, do not silently delete. `compiler/tychoc0.ty` stays on disk as the
    self-hosting artifact. Each removed lane leaves a note saying what it proved
    and when it stopped being run.
  - Done when: no gate, runner or Makefile target builds `tychoc0`; `make ci` is
    green; `ROADMAP.md` and `docs/architecture.md` state what ended and what the
    artifact still is; `CLAUDE.md`'s gate table no longer lists the two scripts.
  - Verify: `make ci` (this phase changes what runs, so it is one of the two
    phases that earns a full sweep), then `sh scripts/check_links.sh` and
    `python3 scripts/check_citations.py` — many documents cite these scripts.

  **Evidence (2026-07-29).**

  **The runner list, re-derived — the plan's was wrong in both directions.**
  Derived by `grep -rln tychoc0 --include='*.sh' --include='*.py'
  --include='Makefile'`, which found **36** files, then reading each to separate a
  real leg from a comment. A comment is not a leg, and seven of the plan's fifteen
  were comment-only.

  - **Plan named, but comment-only — already retired on 2026-07-26, nothing to
    do:** `/home/igzo/github/tycho/fuzz/run_leak.py`,
    `/home/igzo/github/tycho/fuzz/run.py`,
    `/home/igzo/github/tycho/fuzz/run_reject.py`,
    `/home/igzo/github/tycho/tests/recursion/run.sh`,
    `/home/igzo/github/tycho/tests/conc/run.sh`,
    `/home/igzo/github/tycho/tests/ffi/run.sh`,
    `/home/igzo/github/tycho/examples/site/run.sh`. Each carries a `HISTORY:` note
    saying its tychoc0 legs were cut on 2026-07-26.
  - **Plan missed, but a real leg:** `/home/igzo/github/tycho/compiler/run.sh`
    (the Stage-1 bootstrap differential),
    `/home/igzo/github/tycho/examples/weblog/run.sh`,
    `/home/igzo/github/tycho/examples/webserver/run.sh`,
    `/home/igzo/github/tycho/fuzz/run_typeparity.py`,
    `/home/igzo/github/tycho/tools/prof/profile.sh` (its `self` emitter mode), and
    `/home/igzo/github/tycho/compiler/pkg-split.sh` (reads `tychoc0.ty` as source;
    orphaned because its only caller was `compiler/fixpoint.sh`).
  - **Plan named and correct:** `/home/igzo/github/tycho/fuzz/run_pkg.py`,
    `/home/igzo/github/tycho/fuzz/minimize.py`,
    `/home/igzo/github/tycho/fuzz/run_parforparity.py`,
    `/home/igzo/github/tycho/fuzz/run_unaryparity.py`,
    `/home/igzo/github/tycho/fuzz/run_eqparity.py`,
    `/home/igzo/github/tycho/tests/rtparity/run.py`,
    `/home/igzo/github/tycho/examples/sqlite/run.sh`,
    `/home/igzo/github/tycho/examples/fetch/run.sh`.

  Net: **16 real legs**, not 15, and only 8 of the plan's 15 were real.

  **Confirmed, as the phase brief predicted:** nothing in
  `/home/igzo/github/tycho/Makefile` and no step of
  `/home/igzo/github/tycho/scripts/ci.sh` invokes any of them. `make ci` runs
  exactly what it ran before this phase. No surprise wiring was found.

  **The retire-vs-delete rule applied.** Nothing was `git rm`'d. The tree's own
  2026-07-26 precedent (keep the lane, drop the tychoc0 leg, leave a `HISTORY:`
  header — see `/home/igzo/github/tycho/fuzz/run.py`) was followed, split three
  ways by what survives removal of the leg:

  1. **No non-tychoc0 half exists → keep the file, replace the body with a
     retirement note that prints and exits.** `/home/igzo/github/tycho/compiler/fixpoint.sh`,
     `/home/igzo/github/tycho/scripts/frontparity.sh`,
     `/home/igzo/github/tycho/compiler/run.sh`,
     `/home/igzo/github/tycho/tests/rtparity/run.py`,
     `/home/igzo/github/tycho/fuzz/minimize.py`. Each header records what the lane
     proved, why it went, and what is now uncaught.
  2. **A written-down oracle survives → keep the lane, drop only the tychoc0
     leg.** `/home/igzo/github/tycho/fuzz/run_eqparity.py`,
     `/home/igzo/github/tycho/fuzz/run_unaryparity.py`,
     `/home/igzo/github/tycho/fuzz/run_parforparity.py` keep their `expect` table
     and still gate `tychoc` against it; `FIXTURE DRIFT` was renamed
     `ORACLE DIVERGENCE` since it is now the only assertion.
     `/home/igzo/github/tycho/fuzz/run_typeparity.py` has **no** `expect` table —
     the tychoc0 verdict *was* its oracle — so what remains is strictly weaker (an
     exhaustive fail-closed sweep) and its header says exactly that rather than
     pretending otherwise. `/home/igzo/github/tycho/fuzz/run_pkg.py` keeps leg 1
     as a smoke test.
  3. **Orphaned or mode-only → keep, annotate.**
     `/home/igzo/github/tycho/compiler/pkg-split.sh` (only caller retired);
     `/home/igzo/github/tycho/tools/prof/profile.sh` (the `self` emitter now
     refuses with a pointer, the default `tychoc` emitter is untouched).

  **A coupling that would have been broken silently.** In both
  `/home/igzo/github/tycho/examples/fetch/run.sh` and
  `/home/igzo/github/tycho/examples/sqlite/run.sh` the ASan/UBSan leg sanitized the
  C that **tychoc0** emitted, so deleting the tychoc0 leg would have deleted the
  sanitizer's input with it. Both are repointed at `tychoc --emit-c` output, so the
  use-after-free coverage (transient curl body / `sqlite3_column_text` pointer
  copied into the arena) survives — now over one implementation, not two. Stated in
  both headers.

  **What was written into the docs.** `/home/igzo/github/tycho/ROADMAP.md` gained a
  section "The `tychoc0` freeze lanes were retired on 2026-07-29", and
  `/home/igzo/github/tycho/docs/architecture.md` a section "2026-07-29: the freeze
  lanes were retired — nothing builds `tychoc0` now". Both say, in plain words:
  what ended is **continuous proof that `tychoc0` accepts what `tychoc` accepts**;
  the differential caught a real over-tightening of the newtype path that made
  `tychoc0` refuse `if dup == ids:` in
  `/home/igzo/github/tycho/tests/newtype_agg.ty`, which the C compiler accepted
  without complaint; **the class of defect now uncaught is a silent narrowing of
  what the frontend accepts**, invisible to goldens because a newly-rejected
  program never reaches the comparison; and `compiler/tychoc0.ty` **stays on disk**
  as the self-hosting artifact, still the largest Tycho program in the tree, still
  fed to `tychoc` as *input* by `/home/igzo/github/tycho/scripts/asan_self.sh`.
  `/home/igzo/github/tycho/docs/architecture.md` also names two smaller losses no
  other lane covered: `fuzz/run_pkg.py`'s tychoc0 legs were the only consumers of
  the `tychoc --bundle` package stream, and `tests/rtparity/run.py` the only check
  that the embedded runtime wires up the env knobs it defines.
  `/home/igzo/github/tycho/CLAUDE.md`'s gate table no longer lists either script
  and gained a short "Two gates that used to be here" note.

  **Corrected a claim that was already false.**
  `/home/igzo/github/tycho/ROADMAP.md` said "No gate builds or runs it" as of
  2026-07-26. That was true only of `make ci`; two hand-run lanes and fourteen
  other runners built it right up to today. The wording is fixed and the
  distinction recorded. `/home/igzo/github/tycho/ROADMAP.md`'s "two independent
  compilers held to byte-identical self-hosting" is likewise gone from the
  present-tense description of the harness.

  **Verify 1 — no runner builds `tychoc0`.**
  `grep -rn "tychoc0\.ty" --exclude-dir=.git --include='*.sh' --include='*.py'
  --include='Makefile' .` with comment lines filtered leaves exactly three hits,
  none of which builds a compiler:

  ```
  compiler/pkg-split.sh:30:H="$HERE/tychoc0.ty"          # reads it as source text
  scripts/check_citations.py:137:  * `compiler/tychoc0.ty` -- the FROZEN ...   # prose
  scripts/check_citations.py:177:SRC_SKIP_CITER = ("compiler/tychoc0.ty",)     # a path constant
  ```

  `/home/igzo/github/tycho/scripts/asan_self.sh` still names
  `compiler/tychoc0.ty` on its corpus line — deliberately: it feeds it to `tychoc`
  as **input**, which is the artifact use the phase preserves.

  **Verify 2 — citations.** 49 citations pointed into the two retired scripts and
  went out of bounds. Repaired by stripping the line anchor (a line number into a
  script that is now a retirement note is meaningless) across
  `/home/igzo/github/tycho/FRICTION.md`,
  `/home/igzo/github/tycho/docs/internals/plan-friction-DONE.md`,
  `/home/igzo/github/tycho/docs/internals/plan-postfreeze-rawstring-DONE.md`,
  `/home/igzo/github/tycho/docs/spec/appendix-e-conformance.md`,
  `/home/igzo/github/tycho/tests/run.sh`,
  `/home/igzo/github/tycho/tools/lsp.ty`,
  `/home/igzo/github/tycho/tests/postfreeze/nested_pattern.ty` and
  `/home/igzo/github/tycho/corelib/test/result/main.ty` — 69 named anchors and 11
  bare `:N` continuation refs. Final:

  ```
  citation check: ok (125 anchored contain the token they name, 1943 bare in bounds,
  102 source->doc citations resolve, 96 source->source in bounds)
  ```

  **Verify 3 — links.**

  ```
  link check: ok (133 markdown files, no dead relative links)
  ```

  **Verify 4 — `make ci`.** Exceeded the 10-minute per-command cap, so it was run
  to a log with a wrapper capturing the status directly. The exit status is
  **observed, not derived** — `CI_EXIT=$?` was written by the wrapper:

  ```
   CI GREEN -- tree is good
  ================================================================
  CI_EXIT=0
  ```

  Lane detail from the same run: `ok=177 skip=23 timeout=0 FAIL=0` (fuzz),
  `accepted=31 rejected=169 FAIL=0` (fuzz-reject), `ok=131 skip=19 FAIL=0`
  (fuzz-leak). As predicted, `make ci` was unaffected by this phase.

  **Smoke-tested the rewired runners** (none is in `make ci`):
  `examples/weblog/run.sh` → `weblog: ok`; `examples/webserver/run.sh` →
  `webserver: ok`; `examples/sqlite/run.sh` → `sqlite: green`.
  `examples/fetch/run.sh` is **red, and was already red at HEAD** — see phase 20.

- [x] **Phase 2 — fold `tests/postfreeze/` back into `tests/`**
  - Scope: `tests/postfreeze/*.ty` + goldens → `tests/`; `tests/postfreeze/abort/`
    → `tests/abort/`; the two loops in `tests/run.sh` that serve them;
    `scripts/asan_self.sh`'s glob; `.gitignore`'s `!/tests/postfreeze/*.out`
    exception; `docs/spec/appendix-e-conformance.md`'s references to the lane.
  - Done when: `tests/postfreeze/` does not exist, every fixture it held runs from
    its new home with its golden, and `make test` counts the same number of
    passes as before the move.
  - Verify: `make test` (record the count before and after — they must match).

  **Evidence (2026-07-29).**

  **The count, which is the whole proof.** `make test` **before** any change:
  `passed: 537   failed: 0`. `make test` **after** the fold: `passed: 537
  failed: 0`. Equal, so nothing was dropped on the floor. The seven fixtures run
  from their new homes under new names — the `postfreeze_` and `pfabort_`
  prefixes are gone because the loops that produced them are gone:

  ```
  ok    array_arith          ok    array_bcast_fresh
  ok    array_arith_fresh    ok    nested_pattern
  ok    array_bcast          ok    rawstring
  ok    abort_array_arith_len
  ```

  **The moves, all seven by `git mv` so history follows.** Six fixtures + six
  goldens `/home/igzo/github/tycho/tests/postfreeze/{array_arith,
  array_arith_fresh, array_bcast, array_bcast_fresh, nested_pattern,
  rawstring}.{ty,out}` → `/home/igzo/github/tycho/tests/`; and
  `/home/igzo/github/tycho/tests/postfreeze/abort/array_arith_len.ty` →
  `/home/igzo/github/tycho/tests/abort/array_arith_len.ty`. `git status` scored
  all thirteen as `R` (rename), not add+delete. Both directories then `rmdir`'d;
  `tests/postfreeze/` does not exist.

  **Collision check, run BEFORE the move, not assumed.** For each of the six
  names, `tests/<name>.{ty,out,in}` was tested with `[ -e ]`; none existed. Same
  for `tests/abort/array_arith_len.ty` against the sixteen files already in
  `/home/igzo/github/tycho/tests/abort/`. Nothing was clobbered and nothing
  needed renaming.

  **The two loops in `/home/igzo/github/tycho/tests/run.sh`, re-derived not
  guessed.** They were at `:148-153` (the `tests/postfreeze/*.ty` golden loop,
  scoring `postfreeze_<name>`) and `:172-189` (the `tests/postfreeze/abort/*.ty`
  loop, scoring `pfabort_<name>`), with 13 and 17 lines of header above each.
  Both loops and both headers were replaced by a single 10-line `HISTORY:` note
  at `/home/igzo/github/tycho/tests/run.sh:135-144`. The fixtures are now scored
  by the main golden loop at `/home/igzo/github/tycho/tests/run.sh:113-118` and
  the abort loop at `/home/igzo/github/tycho/tests/run.sh:194-211` — same
  native-vs-ASan + golden discipline, same abort contract, no new code.

  **`/home/igzo/github/tycho/.gitignore` — the general case confirmed before
  removing anything.** `!/tests/*.out` at
  `/home/igzo/github/tycho/.gitignore:94` already un-ignores the broad `*.out`
  rule at `:89` for exactly `tests/<name>.out`, which is where the six goldens
  now live; it is where they came from before the lane existed. The
  `!/tests/postfreeze/*.out` exception was therefore redundant, not load-bearing,
  and was replaced by a HISTORY note. `tests/abort/` has no golden, so no
  exception was ever needed there.

  **`/home/igzo/github/tycho/scripts/asan_self.sh` — the glob change is not
  neutral, and this is the one thing the phase brief left to judgement.**
  `tests/postfreeze/*.ty` came out of the corpus line (now
  `/home/igzo/github/tycho/scripts/asan_self.sh:146-148`) and the six fixtures
  are picked up by the `tests/*.ty` that was already there — no change. But
  `tests/postfreeze/abort/` was **never** in that glob (it is a subdirectory and
  the glob does not descend), while `tests/abort/*.ty` **is**. So the fold moves
  `array_arith_len.ty` into an ASan corpus it had never been in: the lane's
  corpus grows by one file. That is a coverage gain, not a loss, but it is a
  change to what `make ci` compiles under a sanitizer, and a fresh failure there
  would surface at phase 10 with no obvious cause. Measured rather than reasoned
  about:

  ```
  $ sh scripts/asan_self.sh
  asan-self: compiled: 552   failed: 0
  asan-self: all green (tychoc's own execution is ASan+UBSan clean over the corpus)
  ASAN_EXIT=0
  ```

  Stated plainly: 552 is the **after** number. No before-number was taken, so the
  `+1` is derived from the glob, not measured. What is measured is that the new
  corpus is clean.

  **A stale self-citation found and fixed in passing.**
  `/home/igzo/github/tycho/scripts/asan_self.sh`'s header said the sanitized
  compiler is built at `:99-100`. It was built at `:114-115` before this phase
  and at `/home/igzo/github/tycho/scripts/asan_self.sh:110-111` after — the ref
  was already 15 lines off and the bounds check cannot see it (the exact class
  carried forward as phase 13). It is corrected to `:110-111` because the phase
  rewrote the sentence around it anyway.

  **The rule applied to frozen records, stated because the brief asked.**
  *Prose in `docs/internals/plan-*-DONE.md` was not touched at all.* Those files
  describe a directory that existed when they were written; that is correct
  history, and rewriting them would make them lie about what was done. **No
  citation repair was needed either**, and the reason is mechanical rather than
  lucky: `/home/igzo/github/tycho/scripts/check_citations.py:226` skips any cited
  path that does not start with `SRC_PREFIX`
  (`/home/igzo/github/tycho/scripts/check_citations.py:153-155`), and both
  archived plans write their refs as **absolute** paths
  (`/home/igzo/github/tycho/tests/postfreeze/…`), which no relative prefix
  matches. So the ~40 archived references to the directory were never gated and
  did not go out of bounds when it disappeared. The same rule was applied to
  phase 1's own evidence block above (`plan.md:255-259`), which is equally a
  record of what was true on the day.

  **Verify 1 — `make test`, before and after.** Both `passed: 537   failed: 0`,
  `all green`. Equality is the proof this was a move, not a loss.

  **Verify 2 — citations.**

  ```
  citation check: ok (125 anchored contain the token they name, 1940 bare in bounds,
  102 source->doc citations resolve, 95 source->source in bounds)
  ```

  (Measured at the point the tree changes were complete, before this evidence
  block was written: 1943→1940 bare and 96→95 source→source. The three retired
  refs are the loop citations into tests/run.sh — lines 135-153, 148-153 and
  172-189, all naming loops that no longer exist — in
  `/home/igzo/github/tycho/docs/spec/appendix-e-conformance.md` and in the
  fixture comments. They are written here without backticks on purpose: as
  citations they would still pass the bounds check while pointing at unrelated
  lines, which is exactly the failure mode phase 13 tracks.)

  **Verify 3 — links.**

  ```
  link check: ok (133 markdown files, no dead relative links)
  ```

  **Verify 4 — no live reference to the lane remains.**

  ```
  $ grep -rn postfreeze --exclude-dir=.git . | grep -v '^./docs/internals/plan-.*-DONE\.md:'
  ```

  returns 17 lines and every one is deliberate: 10 in `plan.md` itself (the
  Pre-flight note that *asks* for this fold, phase 2's own scope, phase 21's
  cross-reference, and phase 1's frozen evidence), and 7 `HISTORY:` notes written
  by this phase in `/home/igzo/github/tycho/.gitignore:97`,
  `/home/igzo/github/tycho/tests/run.sh:135-136`,
  `/home/igzo/github/tycho/scripts/asan_self.sh:76`,`:80`,
  `/home/igzo/github/tycho/tests/nested_pattern.ty:3`,
  `/home/igzo/github/tycho/tests/rawstring.ty:3` and
  `/home/igzo/github/tycho/tests/abort/array_arith_len.ty:9`. Three live spec
  passages also still name it — `/home/igzo/github/tycho/docs/spec/12-aggregates.md:287`,
  `/home/igzo/github/tycho/docs/spec/appendix-e-conformance.md:48`,`:263`,`:331-333`
  — each rewritten to past tense with the fold recorded, because §E.2's whole
  subject is *why fixtures sit where they sit* and deleting the history would
  leave the amendments unexplained. **No hit anywhere points at a path that is
  expected to exist.**

  **Out of scope, deliberately left.** The two spec fixture tables were repointed
  (`/home/igzo/github/tycho/docs/spec/appendix-e-conformance.md:166`,`:185`) but
  the §E.2 rationale text for `corelib/test/`, `examples/corelib/` and the `\r` /
  adjacent-literal carve-outs was not — that is phase 21's job, and phase 21 is
  now the only thing standing between those fixtures and `tests/`.

  **One thing changed after the green `make test`, and re-verified rather than
  assumed.** A two-line comment in `/home/igzo/github/tycho/tests/nested_pattern.ty`
  was rewrapped after the suite ran. Comments cannot reach the golden, but the
  round-trip was re-run rather than argued:

  ```
  $ ./tychoc tests/nested_pattern.ty -o /tmp/np.bin && /tmp/np.bin </dev/null >/tmp/np.out
  run rc=0
  $ cmp /tmp/np.out tests/nested_pattern.out && echo "GOLDEN MATCH"
  GOLDEN MATCH
  ```

- [x] **Phase 3 — lex `;` and `..<`**
  - Scope: the lexer in `src/tychoc.c` only. No parser use yet.
  - `;` is a new single-character token. `..<` is a new three-character token;
    check what `.` and `..` currently do before choosing how to lex it, and say
    what you found.
  - Done when: both tokens lex, a fixture proves each is rejected by the *parser*
    with a sensible diagnostic (they have no grammar yet), and nothing else moved.
  - Verify: `make test`. **Insertions here shift `src/tychoc.c` citations** — the
    last plan's phases 1 and 2 shifted 69 and 31 anchored refs respectively and
    had to repair each through a real line map. Run `python3
    scripts/check_citations.py` and repair before committing.

  **Evidence (2026-07-29).**

  **What `.` and `..` do today, read before choosing anything.** The lexer's
  operator section is a hand-written `if / else if` chain ordered
  **longest-match-first**, `src/tychoc.c:477-513`. Three facts govern `..<`:

  1. **`...` is already a three-character token**, `TK_ELLIPSIS`, tested first at
     `src/tychoc.c:481` (variadic `...T` at `src/tychoc.c:3527`, spread `x...`
     at `src/tychoc.c:2578`).
  2. **`.` alone is `TK_DOT`**, the single-char fallback at `src/tychoc.c:504`,
     used for field/tuple access.
  3. **`..` is not a token and never was.** `grep -n "TK_DOTDOT"` returns
     nothing; `0..3` lexed as `INT DOT DOT INT` and died in the *parser*.

  **How `..<` was disambiguated, and why no care was needed against floats.**
  The plan warned about number lexing. Checked rather than assumed: the float
  branch at `src/tychoc.c:281` requires `*p == '.' && isdigit(p[1])`, so a `.`
  followed by a non-digit is **never** consumed into a numeric literal. In
  `0..<3` the second character after `0` is `.`, not a digit, so the `0` closes
  as `TK_INT` and the operator chain sees `..<` intact. No interaction, no
  lookahead hack. The one real ambiguity is `...` vs `..<`, and it is resolved
  by *following the chain's existing discipline rather than introducing a
  second style*: the new three-character test sits immediately after the
  `TK_ELLIPSIS` test and before every two-character test
  (`src/tychoc.c:482`), so maximal munch still holds by construction — `...`
  cannot be stolen by `..<` and neither can be stolen by `.`.

  **The diff is three lines and two tokens.** `TK_SEMI` appended to the existing
  enum line `src/tychoc.c:121`, `TK_DOTLT` to `src/tychoc.c:126` (no new enum
  lines — nothing in the tree compares `TokKind` by range, verified by grepping
  for `>= TK_` / `<= TK_`, which returns nothing, so placement is free and the
  citation shift is held to +2). Lexing: `src/tychoc.c:482` (`..<`) and
  `src/tychoc.c:506` (`;`). `cc -Wall -Wextra` clean.

  **The observable result: the parser refuses them now, not the lexer.**
  Measured on both compilers — the HEAD compiler was rebuilt from
  `git show HEAD:src/tychoc.c` rather than reasoned about.

  | program | before (HEAD) | after |
  |---|---|---|
  | `x := 1;` | `error: unexpected character ';'` (lexer, `src/tychoc.c:511`) | `error: expected newline` (parser, `src/tychoc.c:3349`) |
  | `for i in 0..<3:` | `error: expected a field name or tuple index after '.'` | `error: expected ':' before the block` (parser, `src/tychoc.c:3264`) |

  **A correction to the phase brief, stated because it was asserted as fact.**
  The brief said both tokens "die in the lexer with `unexpected character`".
  That is true of `;` only. `..<` **never** reached the lexer's error path — it
  lexed as `.` `.` `<` and was already a parser refusal, just at the wrong
  place with a diagnostic about field access. What phase 3 changes for `..<` is
  *which token the parser sees*, not *who does the refusing*. The fixture says
  so rather than claiming a lexer fix it did not make.

  **The two fixtures**, both chosen so they survive the phases that come next:

  - `tests/reject/semi_no_grammar.ty` — `x := 1;`. A C-style statement
    terminator is illegal today and stays illegal after phase 4, where `;`
    separates `for`-header clauses and nothing else. (The three-clause loop
    itself was deliberately **not** used as the fixture: phase 4 makes it legal
    and the fixture would have to be deleted.)
  - `tests/reject/dotlt_no_grammar.ty` — sequential `for i in 0..<3:`. Refused
    now for want of grammar, and refused *deliberately* after phase 5, which
    only has to replace the diagnostic. This is the fixture phase 5's "Done
    when" already asks for.

  Both exit **1** with a non-empty diagnostic, which is exactly what the
  `tests/reject/` lane at `tests/run.sh:158-169` asserts. Note what that lane
  does **not** do: it never compares the diagnostic text. The assertion that
  each message is the *right* one is the table above plus the message written
  into each fixture's header comment; the lane only proves the refusal is loud.

  **Verify 1 — `make test`.** `passed: 539   failed: 0`, `all green`. Phase 2
  left it at 537; +2 is exactly the two new reject fixtures and nothing else
  moved.

  **Verify 2 — citations.** The gate reddened as predicted: **85 stale anchored
  refs**, every one an anchor into `src/tychoc.c` past an insertion point.
  Repaired through a real `difflib.SequenceMatcher` line map built from
  `git show HEAD:src/tychoc.c` against the working tree (12402 → 12404 lines,
  12400 matched equal; the 2 unmatched are the two enum lines this phase
  modified in place, and no citation anchored either). Only refs the gate itself
  reported STALE were rewritten, each by `map[old]`, never by guessing from the
  diagnostic text. 85 repaired across 11 files: `docs/spec/12-aggregates.md` 17,
  `docs/internals/plan-postfreeze-rawstring-DONE.md` 27,
  `docs/spec/01-lexical.md` 10, `docs/spec/16-builtins.md` 10,
  `docs/spec/15-program.md` 5, `docs/spec/03-types.md` 4,
  `docs/spec/09-expressions.md` 3, `docs/internals/plan-front-door-DONE.md` 3,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` 2,
  `docs/spec/02-grammar.md` 2, `docs/spec/10-statements.md` 2. Every file is
  `+n -n` in `git diff --numstat`, so the documentation diff is **line-for-line
  neutral** and nothing citing *into* those files moved. Final:

  ```
  citation check: ok (125 anchored contain the token they name, 1940 bare in bounds,
  102 source->doc citations resolve, 99 source->source in bounds)
  ```

  95 → 99 source→source is the four `src/tychoc.c:N` refs in the two new
  fixture headers.

  **The bare-ref class was left alone, deliberately.** Bare `src/tychoc.c:N`
  refs past line 482 are now 1–2 lines light. The file only grew, so every one
  is still in bounds and the gate cannot see it — the same class phase 17
  already carries, and sweeping it here would have buried a three-line lexer
  change under a few hundred unrelated edits.

  **A gate hole worth knowing, found while checking my own work.** An
  **untracked** file's citations are not checked at all: `check_citations.py`
  builds its citer set from `git ls-files` (`scripts/check_citations.py:263-265`).
  Proven by deliberately corrupting a ref to line 99999 of src/tychoc.c (written
  without backticks here on purpose — backticked it would be a citation, and an
  out-of-bounds one) — silent while
  the fixture was untracked, `OUT OF BOUNDS` the moment it was `git add`ed. Both
  fixtures were therefore staged *before* the final gate run, not after. This is
  a sibling of phase 23 (an absolute path is silently unchecked) rather than a
  new defect: a file that is not in the repo is not yet the repo's problem. Left
  as a note, not a phase.

- [x] **Phase 4 — three-clause `for init; cond; post:` and bare `for:`**
  - Scope: parser, typechecker, codegen; fixtures in `tests/`.
  - `init` is a declaration or assignment, `cond` a `bool` expression, `post` an
    assignment. The loop variable is scoped to the loop, as `range()`'s was.
    `break` and `continue` must work in both new shapes
    (`docs/spec/10-statements.md:84-85` says they are valid in every shape).
    `continue` in a three-clause loop MUST run the post clause — the classic
    place this is got wrong.
  - Bare `for:` is an infinite loop, exited by `break` or `return`.
  - Done when: fixtures cover ascending, descending, a `continue` that proves the
    post clause still runs, a `break` out of `for:`, and nesting; all match
    goldens.
  - Verify: `make test`, then `python3 scripts/check_citations.py`.

  **Evidence (2026-07-29).**

  **Bare `for:` needed no new node and no new codegen.** It is parsed as the
  condition form with a literal `true` (`src/tychoc.c:3242-3255`) — the same
  `S_WHILE` + `E_BOOL` node `resolve_parfor` already builds for the channel-drain
  worker at `src/tychoc.c:6580-6583`. So `break`, `continue`, the loop arena, the
  return path and `wl_check` (which returns early on a constant condition,
  `src/tychoc.c:6800`) are all the code that was already there and already
  exercised — `tests/break_continue.ty:38` has been writing `for true:` all along.

  **The loop variable is scoped by the mechanism the brief named, not a second
  one.** `S_FORRANGE` scopes its variable with `vars_mark()` / `vars_push` /
  `resolve_block` / `vars_restore` (`src/tychoc.c:7238-7250`). `S_FOR3` does
  exactly that at `src/tychoc.c:7194-7215`, with one difference: the binding is
  made by **resolving the init statement** instead of a hand-written `vars_push`,
  so a `:=` init, a typed `i: int = 0` init and an assign-only init all behave as
  they would anywhere else in the language. Measured, not argued:

  ```
  $ cat /tmp/q.ty
  fn main():
      for i := 0; i < 3; i += 1:
          println("x")
      println(str(i))
  $ ./tychoc /tmp/q.ty -o /tmp/q.bin
  /tmp/q.ty:4: error: unknown variable 'i'
  ```

  and `tests/for3.ty`'s last block shadows an outer `i := 99` with a loop `i`
  and prints `outer_i=99` after the loop.

  **The one design decision worth a future reader's time: where init and post
  live in the AST.** `S_FOR3` reuses the generic sub-statement slots rather than
  adding `Stmt *init, *post` fields — `expr` is the condition, `els[0]` is the
  init, and the post clause is the **last element of `body`**
  (`src/tychoc.c:1556-1567`). This is not tidiness. Roughly twenty analyses in
  this file walk `body`/`els` generically and would each have needed a new line
  for two new fields: `clone_block` (`src/tychoc.c:11153-11171`, generic instantiation
  — a missed subtree there is a dropped clause in every generic function),
  `fuse_gather`/`fuse_open`, `block_mutates`, `body_defines`, `count_reads_b`,
  `body_pushcount`, `collect_escapes`, `collect_accums`, `chan_scan_body`,
  `stmt_unsafe`, `wl_scan_body`, `emit_locals`. Post goes in `body` because it
  runs **every iteration**, so every per-iteration analysis must see it; init
  goes in `els` because it runs **once**. With that split, the only places that
  needed a new case are the four that genuinely differ: the parser, the resolver,
  codegen, and `pf_scan_stmt`. `cc -Wall -Wextra` was the check on that claim —
  `pf_scan_stmt` is a `default`-less switch over `StmtKind`, so it named itself.
  The build is warning-clean. Also verified before inserting `S_FOR3` mid-enum
  that nothing compares `StmtKind` by range or serialises it (`grep -n
  "StmtKind"` returns six hits, all declarations), so placement next to
  `S_FORRANGE` is free.

  **`continue` runs the post clause, and the proof is in the emitted C.** A C
  `continue` jumps to the condition, which would skip a post clause sitting at
  the end of the loop body — the classic bug. So a `continue` whose innermost
  loop is an `S_FOR3` is emitted as `goto _post<id>` instead
  (`src/tychoc.c:10611-10617`), with the label placed immediately before the post
  clause (`src/tychoc.c:10664-10665`). `g_loop_post[]` (`src/tychoc.c:9720-9726`)
  carries the block id per enclosing loop and every other loop writes `-1` into
  its slot before `g_loop_depth++`, so a plain loop nested inside a three-clause
  one always overwrites. The label is emitted **only** when a `continue` actually
  binds to that loop (`body_continues`, `src/tychoc.c:9728-9745`, which descends
  into `if`/`match`/`select` bodies but not into a nested loop), so no unused
  label ever reaches `cc`. From `--emit-c` on the `odd=` loop of `tests/for3.ty`:

  ```c
          if (((h_i / 2LL) * 2LL) == h_i) {
              goto _post2;
          }
          ...tycho_str_append(&_scope, &h_odd, ...);
          _post2: ;
          h_i = (h_i + 1LL);
      }
  ```

  and the nested case emits `_post11` (outer) / `_post12` (inner) with each
  `continue` going to its own label. The fixture is written so this **cannot**
  fail quietly: in `tests/for3.ty`'s `odd=` loop `i` advances *only* in the post
  clause, so a `continue` that skipped it would not print a wrong answer, it
  would never terminate. `timeout 10 /tmp/for3.bin` exits 0.

  **The empty-clause decision: all three clauses are required, `for:` is the only
  degenerate form.** This is what the grammar already said rather than a rule
  imposed on it. The header is parsed by rewriting the first `;` and the block `:`
  to `NEWLINE` in the token array and then calling **`parse_stmt` itself** for the
  init and post clauses (`src/tychoc.c:3302-3312`) — which is why `:=`, a typed
  decl, `=` and every compound `op=` form work without a second copy of the
  assignment grammar that could drift from the first. This parser has no
  empty-statement production, so an empty init or post has nothing to parse, and
  an empty condition has no `bool` to check. Each is refused **by name** rather
  than by whatever `parse_stmt` said next:

  ```
  for ; i < 3; i += 1:          error: the init clause is required -- write `for:` for an infinite loop
  for i := 0; ; i += 1:         error: the condition clause is required -- write `for:` for an infinite loop
  for i := 0; i < 3; :          error: the post clause is required -- write `for cond:` for a loop that advances in its body
  for i := 0; i < 3:            error: a three-clause `for` is `for init; cond; post:` -- two ';' separating three clauses
  for i := 0; i < 3; i += 1; i: error: a three-clause `for` has exactly three clauses: `for init; cond; post:`
  for i := 0; i < 3; j := 1:    error: the post clause of a three-clause `for` is an assignment to a variable (`i += 1`)
  ```

  `tests/reject/for3_empty_clause.ty` locks the first. **Init is restricted to a
  decl or a plain assignment and post to a plain assignment** — deliberately, not
  incidentally: an index or field target (`a[f()] += 1`) routes through
  `hoist_index_calls`, whose single-eval temp is queued on `g_pending` and flushed
  by `parse_block` into the *enclosing* block, which for a post clause would
  evaluate it once instead of per iteration. Refusing the shape is smaller and
  safer than teaching the queue about loop headers.

  **How the form is recognised, and why it cannot collide with `for C:`.** A
  top-level `;` on the header line is the only discriminator, and it is
  unambiguous because `;` has no other grammar anywhere. The scan
  (`src/tychoc.c:3258-3288`) tracks `()`/`[]` depth and records the two `;` plus
  the **last** top-level `:` — last, because a typed init (`i: int = 0`) puts a
  colon of its own ahead of the block's. There are no brace tokens in this lexer,
  so `()` and `[]` are the whole nesting alphabet.

  **`parallel for` with either new shape is refused by the gate that was already
  there** (`src/tychoc.c:3234`), because neither produces an `S_FORRANGE`:

  ```
  $ ./tychoc /tmp/p.ty -o /tmp/p.bin      # parallel for i := 0; i < 3; i += 1:
  /tmp/p.ty:2: error: parallel supports 'for x in range(...)' and 'for x in collection' loops only
  ```

  **The two phase-3 reject fixtures still fail, and for the right reasons.** Both
  re-run by hand after the change:

  ```
  tests/reject/semi_no_grammar.ty  rc=1  :15: error: expected newline
  tests/reject/dotlt_no_grammar.ty rc=1  :20: error: expected ':' before the block
  ```

  `semi_no_grammar.ty` matters most: `;` now has a grammar *inside a `for`
  header*, and `x := 1;` is still refused at the decl's trailing `eat(NEWLINE)`
  because the header scan only runs after a `for`. Neither fixture was deleted or
  weakened. Their two bare `src/tychoc.c:N` refs were shifted by this phase's
  insertions and were repaired by hand through the same line map used below
  (`:3349`→`:3444`, `:3264`→`:3359`); the gate cannot see a bare ref, so this was
  a deliberate fix, not a gate response.

  **Verify 1 — `make test`.** `passed: 542   failed: 0`, `all green`. Phase 3 left
  it at 539; +3 is exactly `tests/for3.ty`, `tests/for_bare.ty` and
  `tests/reject/for3_empty_clause.ty`. Both goldens were hand-checked value by
  value before being recorded, not blessed from output:

  ```
  asc=01234        desc=54321       odd=1,3,5,7,     stop=3
  grid=00 01 10 11 20 21            tot=6            pairs=00 02 20 22 30 32
  outer_i=99       tot2=8
  n=3              hits=1,3,5,      total=6          over=21
  ```

  The golden lane runs each fixture native **and** under ASan/LSan, so the
  per-iteration `_scrN` reset and the `arena_free` after the loop are covered for
  both new shapes — including the `return` out of a bare `for:` in
  `tests/for_bare.ty`, which is the path that has to free the loop arena itself.

  **Verify 2 — citations.** The gate reddened as predicted: **81 stale anchored
  refs**, every one an anchor into `src/tychoc.c` past an insertion point.
  Repaired through a `difflib.SequenceMatcher` line map built from `git show
  HEAD:src/tychoc.c` against the working tree (12404 → 12595 lines, 12403 matched
  equal; the single unmatched old line is the `StmtKind` enum line this phase
  edited in place, and no citation anchored it). Only refs the gate itself
  reported STALE were rewritten, each by `map[old]`. 81 across 11 files:
  `docs/internals/plan-postfreeze-rawstring-DONE.md` 26,
  `docs/spec/12-aggregates.md` 16, `docs/spec/16-builtins.md` 10,
  `docs/spec/01-lexical.md` 8, `docs/spec/15-program.md` 8,
  `docs/internals/plan-front-door-DONE.md` 3,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` 2,
  `docs/spec/03-types.md` 2, `docs/spec/09-expressions.md` 2,
  `docs/spec/10-statements.md` 2, `docs/spec/02-grammar.md` 1. Every file is
  `+n -n` in `git diff --numstat`, so the documentation diff is **line-for-line
  neutral**. Final, with the new fixtures `git add`ed first (phase 3 found an
  untracked file's citations are not checked at all):

  ```
  citation check: ok (125 anchored contain the token they name, 1956 bare in bounds,
  102 source->doc citations resolve, 100 source->source in bounds)
  ```

  **Left alone, deliberately.** The bare `src/tychoc.c:N` class (phase 17) is now
  shifted by a further ~190 lines. The file only grew, so every one is still in
  bounds and the gate cannot see them; sweeping ~344 unrelated refs here would
  have buried the feature. The two refs inside the fixtures this plan owns were
  fixed by hand as above.

- [x] **Phase 5 — `parallel for i in 0..<N:`**
  - Scope: parser, the `parallel for` path (`src/tychoc.c` around the
    `S_FORRANGE` `parallel` flag at `:1558`), codegen; fixtures in `tests/conc/`.
  - `0..<N` is legal **only** in `parallel for` (see Pre-flight). Sequential `for
    i in 0..<N:` is refused, with a diagnostic naming the three-clause form.
  - The existing `parallel for x in xs:` foreach form is untouched.
  - Done when: `parallel for i in 0..<N:` runs its body over `0..N-1` across
    threads, the sequential refusal has a `tests/reject/` fixture with its
    diagnostic asserted, and `tests/conc/` fixtures pass native, ASan and TSan.
  - Verify: `make conc`, then `make test`.

  **Evidence (2026-07-29).**

  **The whole feature is 42 lines in one place, and no new node.** `0..<N` is
  parsed at `src/tychoc.c:3329-3370` into **exactly** the `S_FORRANGE` node that
  `range(0, N)` already built — `r_start` a literal `0`, `r_stop` the parsed `N`,
  `r_step` NULL. So resolve (`src/tychoc.c:7281`), `resolve_parfor`, the lifted
  `__parN` chunk proc, the fan-out at `src/tychoc.c:9932` and `gen_stmt`'s
  `S_FORRANGE` case are all untouched: the chunking machinery is the code that
  was already there and already gated. Only the spelling is new. `cc -Wall
  -Wextra` clean.

  **How parallel-only legality is enforced, and where.** The `for` parser already
  carries `int par_here = g_parallel_ctx` (`src/tychoc.c:3241`) — set by the
  `TK_PARALLEL` handler for the directly-following `for` and cleared so nested
  loops do not inherit it. That flag is the whole enforcement: `if (!par_here)
  die_at(...)` at `src/tychoc.c:3355-3357`, **inside the parser**, before any
  node exists. Nothing in the typechecker or codegen needs to know about it, and
  a sequential `0..<N` cannot reach a later pass. Verdict on the Pre-flight's
  "if it proves unworkable, say so": **it did not.** The context is a single
  local `int` already in scope for another purpose, and the check is one
  condition. The wart the Pre-flight predicted is real but it is a wart in the
  *language*, not in the implementation — nothing here argues for generalising
  `0..<N` to sequential loops, and it was not generalised.

  **The header is scanned, not peeked.** A top-level `..<` is located by the same
  depth-tracking scan the `;` header used (`src/tychoc.c:3346-3353`), so
  `for i in (a+b)..<n:` reaches a diagnostic about `..<` rather than falling
  through the foreach branch to "expected ':' before the block" — which is what
  a `peek(ps, 1) == TK_DOTLT` test would have done.

  **The sequential diagnostic, exact text.** From
  `tests/diag/dotlt_sequential.err`, recorded byte-for-byte:

  ```
  tests/diag/dotlt_sequential.ty:8: error: `0..<N` counts only in a `parallel for` -- write `for idx := 0; idx < N; idx += 1:` for a sequential count
       8 |     for idx in 0..<10:
  ```

  The loop variable is substituted into the replacement form (`%s` three times,
  `src/tychoc.c:3356-3357`), so the message is copy-pasteable rather than
  generic. There is no caret row because the site uses `t->line` with no column,
  which is the existing convention for the surrounding `for`-header errors.

  **A second refusal, not asked for but required by the shape.** `0..<N` counts
  from zero by construction (the plan's phase 6 says a non-zero start has no
  `0..<N` equivalent), so a non-zero lower bound is refused **by name** instead
  of being silently misparsed — `src/tychoc.c:3358-3359`:

  ```
  $ ./tychoc /tmp/.../nz.ty -o /tmp/.../nz.bin      # parallel for i in a..<10:
  :4: error: `parallel for` counts from zero: write `0..<N` -- a literal `0`, then `..<`, then the exclusive upper bound
  $ ./tychoc /tmp/.../paren.ty -o /tmp/.../paren.bin # parallel for i in (n+1)..<10:
  :4: error: `parallel for` counts from zero: write `0..<N` -- a literal `0`, then `..<`, then the exclusive upper bound
  ```

  This matters for phase 6: a `parallel for … in range(a, b)` with `a != 0` will
  **fail loudly** on rewrite rather than silently changing its iteration space.

  **Proof the body runs across threads rather than serially — measured, not
  argued.** The golden alone cannot tell the two apart (integer `+`/`*`
  reductions are associative and exact, which is the point). So a scratch program
  with a `parallel for i in 0..<8:` over a body of 2e9 iterations each was timed
  twice on a 16-CPU host, with `TYCHO_THREADS` pinning the chunk count
  (`runtime/tycho_rt.c:847-852`):

  ```
  TYCHO_THREADS=8   35999999994   14.54s user  791% cpu   1.838 total
  TYCHO_THREADS=1   35999999994   11.68s user   99% cpu  11.685 total
  ```

  Same answer; **791% CPU against 99%**, and 1.8s wall against 11.7s. Eight
  chunks ran concurrently on eight cores. A serial lowering cannot produce 791%
  of one CPU. (Backing source: each chunk is a real 1:1 OS thread —
  `tycho_task_start` calls `pthread_create` at `runtime/tycho_rt.c:577`, and the
  spawn loop issues `K = tycho_ncpu()` of them at `src/tychoc.c:9939-9955`.) A
  first attempt at this measurement read `Threads:` out of `/proc/<pid>/status`
  and reported **1**; that number was wrong and is recorded here because it is
  the kind of thing that gets believed — `pgrep -f threads.bin` had matched the
  *shell* running the harness, whose command line contains the string. The pid
  was wrong, not the threading.

  **The `tests/conc/` fixture and its hand-checked golden.**
  `tests/conc/parfor_dotlt.ty` covers six things, every expected value computed
  by hand **before** the golden was written, never blessed from output:

  | case | expects | why |
  |---|---|---|
  | `0..<1000`, `total += i*i` | `332833500` | `999*1000*1999/6`; a dropped or repeated chunk boundary moves it |
  | `0..<(n * 4)`, `n := 3` | `12` | N is an expression, evaluated once |
  | `0..<1` | `1` then `0` | smallest non-empty space; exclusive bound; `i == 0` |
  | `0..<0` | `0` | empty space — the body must not run |
  | `0..<6`, product | `120` | `1*2*3*4*5`, a `*` reduction over the same shape |

  Chunk-count independence was measured rather than assumed —
  `TYCHO_THREADS=1, 3, 13, 64` all produce the identical six lines, and
  `TYCHO_THREADS=7` output `cmp`s clean against `tests/conc/parfor_dotlt.out`.
  That is what makes the golden safe on a host with a different CPU count.

  **The fate of `tests/reject/dotlt_no_grammar.ty`: renamed, not deleted, not
  weakened.** `git mv` to `tests/reject/dotlt_sequential.ty` (git scores it `R`),
  because the old name is now false — `..<` *has* a grammar; only this use of it
  is illegal. Its header was rewritten to name the new diagnostic and to record
  what phase 3's refusal used to be. The reject lane
  (`tests/run.sh:158-169`) asserts only nonzero exit + non-empty diagnostic, so
  the **exact text** is locked separately by a new `tests/diag/` golden,
  `tests/diag/dotlt_sequential.ty` + `.err` — the diag lane `cmp`s compiler
  stderr byte-for-byte (`tests/run.sh:218-236`). Both fixtures were `git add`ed
  **before** the final citation run (phase 3 found an untracked file's citations
  are not checked at all).

  **Verify 1 — `make conc`** (native + ASan/UBSan + TSan, each against the
  golden, silent sanitizers required):

  ```
  conc: passed 38   failed 0
  ```

  37 before; +1 is `parfor_dotlt`. TSan clean over the new fixture means the
  chunked reduction merge is race-free for this shape too. `tests/conc/parfor.ty`
  — which uses both `parallel for i in range(...)` and `parallel for x in xs:` —
  is in that 38 and still green, so **the existing counting form and the foreach
  form both keep working**, as phase 6 requires.

  **Verify 2 — `make test`.** `passed: 543   failed: 0`, `all green`. Phase 4
  left it at 542; +1 is exactly `tests/diag/dotlt_sequential` (the reject fixture
  was renamed, not added, so that lane's count is unchanged).

  **Verify 3 — citations.** The gate reddened as predicted: **66 stale anchored
  refs**, every one an anchor into `src/tychoc.c` past the insertion point.
  Repaired through a `difflib.SequenceMatcher` line map built from `git show
  HEAD:src/tychoc.c` against the working tree (12595 → 12637 lines, **all 12595
  old lines matched equal** — this phase inserted only, and edited nothing in
  place). Only refs the gate itself reported STALE were rewritten, each by
  `map[old]`. 66 across 10 files:
  `docs/internals/plan-postfreeze-rawstring-DONE.md` 20,
  `docs/spec/12-aggregates.md` 14, `docs/spec/16-builtins.md` 10,
  `docs/spec/15-program.md` 7, `docs/spec/01-lexical.md` 4,
  `docs/internals/plan-front-door-DONE.md` 3,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` 2,
  `docs/spec/02-grammar.md` 2, `docs/spec/03-types.md` 2,
  `docs/spec/09-expressions.md` 2. Every file is `+n -n` in `git diff
  --numstat`, so the documentation diff is **line-for-line neutral**. Final:

  ```
  citation check: ok (125 anchored contain the token they name, 1971 bare in bounds,
  102 source->doc citations resolve, 105 source->source in bounds)
  ```

  1956 → 1971 bare and 100 → 105 source→source are the refs in the three new /
  rewritten fixture headers.

  **Left alone, deliberately.** The bare `src/tychoc.c:N` class (phase 17) is now
  shifted by a further 42 lines. The file only grew, so every one is still in
  bounds and the gate cannot see them; the refs inside this phase's own fixtures
  are anchored or were written after the insertion, so they are correct.

  **Not run, and why.** `make ci` is phase 10's, per `CLAUDE.md`'s gate budget.
  `sh scripts/spec_check.sh` was not run because this phase writes no spec — the
  spec is phase 9, and `docs/spec/01-lexical.md:170`'s "There is no range
  operator (`..`)" is now false and is already carried as phase 24.

- [x] **Phase 6 — rewrite all 549 `range()` sites**
  - Scope: every `.ty` in the tree except frozen `compiler/tychoc0.ty`.
    Sequential counting → three-clause; the 17 `parallel for … in range(…)` →
    `0..<N`.
  - **The dangerous phase.** `range(n)`, `range(a, b)` and `range(a, b, step)`
    map differently; the bound is **exclusive**; a **negative step** counts down
    and its three-clause form needs `i > b`, not `i < b`. Do not rewrite by
    regex alone — every site with a non-literal or negative step is read.
    Report how many sites fell into each spelling.
  - A `parallel for … in range(a, b, step)` with a non-zero start or non-unit
    step has **no** `0..<N` equivalent. Find out whether any exist; if they do,
    that is a finding that changes phase 5's scope and must be reported, not
    worked around.
  - Done when: `grep -rn "in range(" --include='*.ty' .` returns only
    `compiler/tychoc0.ty`, and every gate is green.
  - Verify: `make test`, then `make conc`, then `make corelib` and
    `make corelib-examples`. **Not `make ci` — see the sequencing correction
    below.**

  #### Sequencing correction, 2026-07-29 — phase 8 must run before this phase's sweep

  The first attempt at this phase completed the rewrite and then reddened
  `make ci` at step `[9b/13] editors-check`: `CORPUS PARSE MISMATCH`, **189 files
  newly failing** in the zed tree-sitter grammar's corpus parse. Those 189 are
  exactly the files the rewrite touched.

  **The cause is a plan error, not a defect in the rewrite.** `scripts/editors_check.sh`
  (added by the previous plan's phase 7) parses every `.ty` file in the tree with
  the zed grammar and asserts the set of files that fail to parse is exactly the
  known-bad set. The grammar does not know `;` or `..<` — teaching it is **phase 8**.
  So the moment the corpus is rewritten, that gate reddens by construction, and
  no amount of care in the rewrite avoids it.

  **The false assumption, stated plainly:** that tooling could trail the corpus,
  as it did for raw strings and for element-wise array arithmetic. It could then
  because only a handful of fixtures used the new syntax and `editors_check.sh`
  did not yet exist for the first of them. Here 189 files change at once, against
  a gate that compares the whole corpus.

  **The order from here is 6 → 8 → 7 → 9 → 10.** Phase 8 (tooling learns `;` and
  `..<`) moves ahead of phase 7 (delete `range()`), so the tree returns to green
  as soon as possible. `make ci` is run once, by phase 10, as it always was.

  **Evidence (2026-07-29).**

  **First, where the work actually is, because `git log` does not say so.** The
  rewrite is **not** in a commit of its own. The first agent left 213 dirty `.ty`
  files, and they were swept into **6ca63ca**, whose message is
  *"docs: repair Appendix E's dead postfreeze fixture citations"* and describes
  only the doc fix. That commit is 215 files, +599/-593. A future reader looking
  for the 549-site rewrite will not find it under a matching subject line; it is
  under that one. Nothing was rewritten or amended to fix this — 6ca63ca is
  already the parent of this commit and rewriting shared history is worse than a
  wrong subject line. This paragraph is the pointer instead.

  **The audit, which is what this run was for.** The whole `.ty` half of 6ca63ca
  was re-derived mechanically rather than spot-checked: `git show 6ca63ca -U0 --
  '*.ty'` parsed into (old line, new line) pairs, the `range(...)` argument list
  re-split at top-level commas, and each new three-clause header re-parsed and
  checked against the arguments it came from — init equals the start, the
  condition bound equals the stop, the condition **operator matches the sign of
  the step**, the bound is **exclusive** (`<`/`>`, never `<=`/`>=`,
  `docs/spec/10-statements.md:90-96`), the post amount equals the step, and the
  loop variable is unchanged in all three clauses. **569 sites** in **209 files**;
  after discarding 3 comment lines that merely mention `range`, **566 real
  sites**. Per spelling:

  | old spelling | sites | rewritten to |
  |---|---|---|
  | `range(n)` | 417 | `for i := 0; i < n; i += 1:` |
  | `range(a, b)` | 119 | `for i := a; i < b; i += 1:` |
  | `range(a, b, step)`, literal **positive** step | 9 | `i < b; i += step` |
  | `range(a, b, step)`, literal **negative** step | 3 | `i > b; i -= |step|` |
  | `range(a, b, step)`, **non-literal** step | 1 | *(fixture deleted, below)* |
  | `parallel for … in range(…)` | 14 | `parallel for i in 0..<N:` |
  | `parallel for … in range(0, 10, 2)` | 1 | *(fixture deleted, below)* |

  **Every one of the 566 passed.** Zero sites had an inclusive bound, zero had a
  mismatched bound or start, zero changed the loop variable, and **zero had the
  sign defect** the Pre-flight named as the most likely: all three negative-step
  sites are in `tests/range_negative_step.ty` and all three use `>`, not `<` —
  `range(3, 0, -1)` → `for i := 3; i > 0; i -= 1:`, `range(10, 0, -3)` →
  `for i := 10; i > 0; i -= 3:`, and the zero-trip
  case `range(5, 5, -1)` → `for i := 5; i > 5; i -= 1:`, which correctly runs
  **zero** times. That fixture has a golden and it passes, so the zero-trip case
  is asserted rather than merely inspected.

  **No non-literal step was invented a translation for.** The single
  `range(0, 10, z[0])` site — a step whose sign is not statically known, which
  the phase brief warns has *no* correct three-clause form — was **not**
  rewritten. Its fixture was deleted (below). So the answer to "did the first
  agent invent one?" is **no**, and there is nothing to un-invent.

  **One real oversight, found and fixed by this run.** `tests/conc/parfor.ty:12`
  was **`parallel for i in range(1, 11):`** — still live, missed by the sweep. It
  is exactly the case the phase brief asks about: a `parallel for` counting from a
  **non-zero** lower bound, which `0..<N` cannot express. **This is why phase 5's
  non-zero-lower-bound tripwire (`src/tychoc.c:3358-3359`) never fired — the site
  was skipped, not hit.** The tripwire is not broken; it was never reached. Fixed
  by moving the offset into the body, which is the only shape `0..<N` allows:

  ```
  parallel for i in 0..<10:
      s = s + (i + 1)
      if i + 1 < 6:
          p = p * (i + 1)
  ```

  The golden is **byte-identical**, which is the proof the iteration space did not
  move (`s` = 1+…+10 = 55, `p` = 1·2·3·4·5 = 120):

  ```
  $ ./tychoc tests/conc/parfor.ty -o /tmp/.../pf.bin && /tmp/.../pf.bin >/tmp/.../pf.out
  $ cmp /tmp/.../pf.out tests/conc/parfor.out && echo "GOLDEN MATCH"
  GOLDEN MATCH
  ```

  Verdict on the plan's "if any exist, that is a finding that changes phase 5's
  scope": one existed, and it does **not** change phase 5's scope. Phase 5's
  refusal is correct and the site had a faithful rewrite; what it changes is the
  claim that the sweep was complete. It was not, and only reading the diff found
  it.

  **The `parallel for` population, re-derived.** The plan said **17** counting
  sites. The real number is **15**: 14 rewritten to `0..<N` and 1 deleted. All 14
  had a lower bound of **0** already (10 were `range(n)`, 4 were `range(0, b)`),
  so none of them could have tripped the tripwire either. The tree now has 35
  `parallel for` sites, 19 of them `0..<`; the surplus over 14 is phase 5's own
  new fixtures.

  **Three fixtures were deleted, not rewritten — a coverage loss that needs
  naming.** Each tested a `range()`-only guarantee with **no** three-clause or
  `0..<N` equivalent, so none could be rewritten:

  - `tests/reject/range_step_zero_lit.ty` — a **literal** `0` step must be
    refused at compile time.
  - `tests/abort/range_step_zero.ty` — a **runtime** `0` step must abort rather
    than spin. This is the `range(0, 10, z[0])` site above.
  - `tests/conc/reject/parfor_step.ty` — `parallel for i in range(0, 10, 2)`, a
    non-unit step on a `parallel for`, must be refused.

  The first two are the zero-step guarantee the Pre-flight already records as a
  deliberate, permanent loss (a three-clause post clause is arbitrary code, so no
  equivalent check is possible) and which **phase 9** must write into the spec.
  The third becomes *unrepresentable* once `range()` is gone, since `0..<N` has no
  step syntax at all. So all three deletions are correct in the end state. What is
  not correct is the **timing**: `range()` is still in the compiler until phase 7,
  so its zero-step checks and its `parallel`-step refusal are now **untested
  between here and phase 7**. Self-resolving, but carried as **phase 28** so it is
  a decision on the record rather than an accident. The counts confirm the
  deletions exactly and nothing else moved: `make test` 543 → **541** (the two
  `tests/` fixtures) and `make conc` 38 → **37** (the one `tests/conc/` fixture).

  **The "elision loss" the first agent reported but did not explain.** Resolved.
  It is **bounds-check elision**, and it is **real, deliberate, and undocumented
  in the plan until now**.

  `src/tychoc.c:10791-10801` gates elision on fields only an `S_FORRANGE` node
  has — `s->r_start` being a literal `0`, `s->r_step == NULL`, and `s->r_stop`
  being a `len(ident)` call. When it fires it records the (loop var, array) pair
  in `g_elide[]` (`src/tychoc.c:7891-7893`) and indexing emits raw `.data[i]`
  instead of the checked accessor. **`S_FOR3` has no such arm**, so every loop
  this phase rewrote lost it. Proven on the two spellings side by side, same
  program, `--emit-c`:

  ```
  for i in range(len(a)):        ->   h_s = (h_s + (h_a).data[h_i]);
  for i := 0; i < len(a); i += 1: ->  h_s = (h_s + tycho_arr_int_get(h_a, h_i));
  ```

  **Scale: 223 sequential sites across 97 files** matched the elidable shape
  (`for X in range(len(IDENT)):`) before this phase and none does now. Counted
  from the diff, not estimated.

  **It is a performance loss only, not a correctness one** — the fallback
  `tycho_arr_int_get` is the *checked* accessor, so the tree got slower and
  safer, never wrong. That is why no golden moved and why `make test` cannot see
  it.

  **Why `bench-guard` stayed green, which is the part worth knowing.** Not because
  there is no regression — because it measures the wrong two programs.
  `bench/guard.sh:31` runs exactly `binary_trees` and `maptree`, and the four
  loops this phase rewrote in them are `range(n)`, `range(mind, maxd + 1, 2)`,
  `range(iters)` and `range(200)`. **Not one is the `range(len(A))` shape
  elision requires**, so neither workload could have observed the loss. Its gate
  is also relative and loose (healthy ~0.23x C against a 0.60x gate); this run
  measured 35% and 22% of C. So "bench-guard is ok" was true and meant nothing
  about elision. Restoring elision for `S_FOR3` is carried as **phase 27**;
  `tests/bounds_elision.ty` is annotated in place so the fixture no longer claims
  to test something it stopped testing.

  **The 4 files still matching `in range(`, all four accounted for.** Three are
  prose, one is frozen; **no live loop remains**:

  ```
  compiler/tychoc0.ty:537,1427,1432,6505,13861,13887   frozen, out of scope
  tests/bounds_elision.ty:5                            comment (the HISTORY note)
  tests/for3.ty:4                                      comment (names the mechanism)
  ```

  `tests/conc/parfor.ty` was the fourth and is the oversight fixed above.

  **Verify — six gates, each run in the foreground, one per command.**

  ```
  $ make test                       passed: 541   failed: 0        all green
  $ make conc                       conc: passed 37   failed 0
  $ make corelib                    corelib: all green (tychoc matches goldens)
  $ make corelib-examples           corelib examples: all green
  $ sh bench/guard.sh               ok binary_trees tycho=273ms C=765ms (35% of C, gate <60%)
                                    ok maptree      tycho=122ms C=545ms (22% of C, gate <60%)
                                    bench-guard: ok (tycho beats C on tree workloads)
  $ python3 scripts/check_citations.py    citation check: ok (125 anchored contain the
                                    token they name, 1985 bare in bounds, 102 source->doc
                                    citations resolve, 107 source->source in bounds)
  $ sh scripts/spec_check.sh        spec-examples: 9 runnable example(s), all pass
  ```

  Exit status was read explicitly for the two whose tail line could hide a
  failure: `spec_check exit=0`, `citations exit=0`.

  **`make ci` was NOT run, deliberately.** It reddens by construction at step
  `[9b/13] editors-check` with `CORPUS PARSE MISMATCH` over 189 files, because the
  zed tree-sitter grammar does not know `;` or `..<` — that is **phase 8**, which
  the sequencing correction above moved ahead of phase 7 for exactly this reason.
  Running it here would produce a known-red result that says nothing about this
  phase. `make ci` is run once, by phase 10. `editors/` and `tools/` were not
  touched.

  **Left alone, deliberately.** `docs/internals/diagnostic-parity-2026-07-25.md:384`
  still names `range_step_zero_lit` in a table of past diagnostics. It is history
  of what the compilers reported on that date, it is a bare fixture name rather
  than a path citation, and no gate reads it. Rewriting it would make the record
  lie.

- [x] **Phase 7 — delete the counting form and the `range` builtin**
  - Scope: parser, typechecker, codegen, builtin table; `tests/reject/` fixture
    asserting `range` is no longer a known name.
  - Done when: `for i in range(3):` is refused with a diagnostic that names the
    three-clause form as the replacement, `range` is gone from the builtin table,
    and `make test` is green.
  - Verify: `make test`, then `python3 scripts/check_citations.py`.

  **Evidence (2026-07-29).** Ran after phase 8, per the sequencing correction
  above.

  **There was no builtin table entry to delete, and this is the phase's first
  real finding.** `range` was never a procedure. It was recognised in exactly
  **one** place — by lexeme, inside a `for` header — and `grep -n '"range"'
  src/tychoc.c` returned that single hit. `parse_fn`'s own note
  (`src/tychoc.c:3634`) already said so: `range` is a *contextual identifier*
  like `soa`, `sink` and `where`, not a reserved word and not a builtin. Measured
  on the HEAD compiler **before** any change, so it is a before/after fact rather
  than a claim about the end state:

  ```
  x := range(3)   ->   error: unknown procedure 'range'      (HEAD, and still)
  ```

  So "`range` is gone from the builtin table" was satisfied by deleting one
  parser branch; nothing in the typechecker, codegen or any name table held it.

  **What is still load-bearing for `0..<N`, worked out before deleting
  anything.** `S_FORRANGE` is **kept whole**. It has five producers and only one
  of them was `range()`:

  | producer | site | r_step |
  |---|---|---|
  | `parallel for i in 0..<N:` | `src/tychoc.c:3370` | NULL |
  | `parallel for x in COLL:` (deferred, `foreach=1`) | `src/tychoc.c:3412` | NULL |
  | sequential `for x in COLL:` desugar → `for _fiN in 0 .. len(_fcN)` | `src/tychoc.c:3431` | NULL |
  | `resolve_parfor`'s channel-drain worker `__pw` over `ncpu()` | `src/tychoc.c:6603` | NULL |
  | `resolve_parfor`'s parallel-foreach index loop | `src/tychoc.c:6630` | NULL |
  | ~~`for i in range(a, b, step):`~~ | *deleted* | **the only non-NULL** |

  Deleting the node would therefore have broken not only `parallel for i in
  0..<N:` but **every foreach loop in the language**, since `for x in xs:`
  desugars into an `S_FORRANGE` index loop. The resolve arm, `gen_stmt`'s
  `S_FORRANGE` case, `resolve_parfor`, the lifted `__parN` chunk proc, the
  bounds-check elision at `src/tychoc.c:10793` and the ~12 generic
  body/expr walkers that touch `r_start`/`r_stop`/`r_step` are all untouched.

  **What was deleted: 20 lines of parser, replaced by 3 lines of refusal.** The
  branch at old `src/tychoc.c:3371-3391` (consume `range`, parse 1–3 arguments,
  fill `r_start`/`r_stop`/`r_step`) is gone. The lexeme test is **kept** so the
  refusal can name the replacements — without it the header falls through to the
  foreach branch and dies at resolve with `unknown procedure 'range'`, which
  names neither replacement. New site `src/tychoc.c:3387`, exact text, with the
  loop variable substituted four times so it is copy-pasteable:

  ```
  tests/diag/range_removed.ty:12: error: `range()` was removed: write `for idx := 0; idx < N; idx += 1:` to count, or `parallel for idx in 0..<N:` to count in parallel
      12 |     for idx in range(0, 10, 2):
  ```

  **`r_step` is now permanently NULL, and was annotated rather than removed.**
  With `range(a,b,step)` gone, every remaining producer writes NULL (table
  above), so the step codegen (`src/tychoc.c:10776-10782`, including the runtime
  `range step is zero` abort), the literal-zero refusal
  (`src/tychoc.c:7292`) and `resolve_parfor`'s step refusal
  (`src/tychoc.c:6645`) are all unreachable. They were **kept** and each carries
  a HISTORY note saying so, for two reasons: no test can exercise a deletion of
  them, so removing them would be an unverifiable change; and phase 27's elision
  recogniser is specified against `s->r_step == NULL`. The removal is filed as
  **phase 30** rather than smuggled in here.

  **Two user-facing messages that named a deleted form.** Both are the phase-26
  class — a diagnostic that would have been a lie the moment this phase landed:

  - **Phase 26, as filed.** `src/tychoc.c:3241` said *"parallel supports 'for x
    in range(...)' and 'for x in collection' loops only"*. Now: **"parallel
    supports \`for i in 0..<N\` and \`for x in collection\` loops only"**.
    Observed on `parallel for i := 0; i < 3; i += 1:`. The prose half
    (`docs/spec/13-concurrency.md:78`) is left to phase 9 as the filing says.
  - **Not filed by anyone, found here.** The `wl_check` warning at
    `src/tychoc.c:6850` suggested *"consider `for x in range(...)`"* to a user
    whose loop never progresses — advice to write the form this phase deletes.
    Now suggests `for i := 0; i < n; i += 1:`. Its golden
    `tests/warn/loop_no_progress.err` locked the old text and **reddened `make
    test`** (`passed: 542 failed: 1`); the golden was updated by hand after
    reading the diff, not blessed with `RECORD=1`.

  Also reworded, because it named a syntax the user can no longer write:
  `src/tychoc.c:7284` `range(...) arguments must be int` → `a counting `for`
  needs int bounds`, and the runtime abort string → `loop step is zero`.

  **Phase 28 — the three deleted fixtures, decided one at a time.** All three
  are **retired early on purpose**; none was restored. The plan offered
  restore-until-phase-7 as an option and it was rejected: the interval it would
  have covered is this commit's parent, the fixtures would be deleted again in
  the same phase that added them, and their guarantees are gone in the end state
  regardless.

  | fixture | guarantee | survives the language? | verdict |
  |---|---|---|---|
  | `tests/reject/range_step_zero_lit.ty` | literal `0` step refused at compile time | **no** — a three-clause post clause is arbitrary code, so `for i := 0; i < n; i += 0:` cannot be diagnosed; `0..<N` has no step to be zero | stay deleted (Pre-flight's stated loss) |
  | `tests/abort/range_step_zero.ty` | runtime `0` step aborts rather than spins | **no** — same reason; also the site's own step `z[0]` had no correct three-clause form, which is why phase 6 could not rewrite it | stay deleted |
  | `tests/conc/reject/parfor_step.ty` | a non-unit step on a `parallel for` is refused | **unrepresentable** — `0..<N` has no step syntax, so the program cannot be written to be refused | stay deleted |

  `tests/conc/reject/` still holds 22 other fixtures, so no lane was emptied.

  **What replaces them: two fixtures, because one lane cannot assert what
  matters.** `tests/reject/range_removed.ty` asserts `range` is no longer a
  known name — but that lane (`tests/run.sh:158-169`) checks only a nonzero exit
  and a non-empty stderr, so on its own it could pass on *any* diagnostic,
  including one that never names the replacement. `tests/diag/range_removed.ty`
  + `.err` locks the exact text byte-for-byte (`tests/run.sh:218-236`), which is
  what this phase's "Done when" actually asks for. Same split phase 5 used for
  the mirror-image refusal (`tests/diag/dotlt_sequential`). Both were `git
  add`ed **before** the final citation run — phase 3 established that an
  untracked file's citations are not checked at all.

  **`parallel for i in 0..<N:` still works — measured, not assumed.**

  ```
  $ ./tychoc /tmp/.../r3.ty -o /tmp/.../r3.bin && /tmp/.../r3.bin
  28
  ```

  (`parallel for i in 0..<8:` summing `i` — 0+1+…+7 = 28, computed before
  running.) The stronger evidence is `make conc` below: `tests/conc/parfor.ty`
  and `tests/conc/parfor_dotlt.ty` both run native + ASan/UBSan + TSan against
  their goldens and both are in the 37.

  **A gate the phase brief did not list, and it caught a real break.**
  `sh scripts/spec_check.sh` (~6s) compiles the runnable examples in
  `docs/spec/`. `docs/spec/03-types.md:237` was still `for i in range(len(s)):`
  — phase 6 swept `.ty` files, not fenced `tycho` blocks in Markdown — so this
  phase turned a passing spec example into a compile error:

  ```
  spec-examples: FAIL docs/spec/03-types.md:231 [tychoc] — transpile error
      ex_4_2.ty:6: error: `range()` was removed: ...
  ```

  Rewritten in place to `for i := 0; i < len(s); i += 1:` (one line for one
  line). This is a `make ci` step, so leaving it would have reddened phase 10
  with a cause four phases upstream. **23 further `in range(` sites remain in
  prose across 18 live documents** (`grep -rn 'in range(' docs/ README.md ROADMAP.md
  FRICTION.md`, excluding the archived `plan-*-DONE.md` records) — none of them runnable, none gated, and only
  `docs/spec/` is in phase 9's scope. The rest is filed as **phase 31**.

  **Verify 1 — `make test`.**

  ```
  passed: 543   failed: 0        all green
  ```

  Phase 8 left it at 541; +2 is exactly `reject_range_removed` and
  `diag_range_removed`. (The first run was `passed: 542 failed: 1` on
  `warn_loop_no_progress`, the golden described above.)

  **Verify 2 — `make conc`.**

  ```
  conc: passed 37   failed 0
  ```

  Unchanged from phase 6's 37 — this phase adds no concurrency fixture and
  removes none, and every existing `parallel for` (both the `0..<N` spelling and
  the foreach spelling) still compiles and matches its golden under TSan.

  **Verify 3 — `sh scripts/tools_check.sh`.**

  ```
  tools-check: ok
  TOOLS_EXIT=0
  ```

  **Verify 4 — citations.** The gate reddened as predicted: **77 stale anchored
  refs**. Repaired through a `difflib.SequenceMatcher` line map built from `git
  show HEAD:src/tychoc.c` against the working tree (12637 → 12642 lines, 12609
  matched equal). Only refs the gate itself reported STALE were rewritten, each
  by `map[old]`. **76 repaired mechanically** across 11 files:
  `docs/internals/plan-postfreeze-rawstring-DONE.md` 23,
  `docs/spec/12-aggregates.md` 16, `docs/spec/16-builtins.md` 10,
  `docs/spec/01-lexical.md` 7, `docs/spec/15-program.md` 6,
  `docs/internals/plan-front-door-DONE.md` 3, `docs/spec/03-types.md` 3,
  `docs/internals/frontend-restriction-audit-2026-07-25.md` 2,
  `docs/spec/02-grammar.md` 2, `docs/spec/09-expressions.md` 2,
  `docs/spec/10-statements.md` 2. Every one of those files is `+n -n` in `git
  diff --numstat`, so the citation repair is **line-for-line neutral**.

  **The 77th could not be mapped, and it is the interesting one.**
  `docs/spec/01-lexical.md:142` anchored src/tychoc.c:3371@"range" (written
  here without backticks on purpose — backticked it would be a live citation
  into a line that no longer exists, and would redden the gate again) — a
  citation into a **deleted** line, which no line map can move. §3.7 listed
  `range` as a contextual identifier *dispatched* in a `for` head; that is no
  longer what happens. Repointed by hand to `:3386@"range"` (the surviving
  lexeme test) and the §3.7 bullet reworded to say the lexeme is recognised
  **only to refuse it**. This is spec prose and the spec is phase 9's, but
  leaving the sentence would have been a statement this phase made false, so it
  was corrected rather than deferred; both edits are one line for one line, so
  `docs/spec/01-lexical.md` stays `+4 -4` and nothing citing *into* it moved.
  Final:

  ```
  citation check: ok (125 anchored contain the token they name, 2014 bare in bounds,
  102 source->doc citations resolve, 120 source->source in bounds)
  ```

  (1985 → 2014 bare and 107 → 120 source→source: the refs in the two new fixture
  headers and in this evidence block. Measured **after** this block was written,
  which is why it is not the 1994 the tree scored before it. One ref in this
  block did redden the gate on its first run — the archived
  src/tychoc.c:3371@"range" above — and is why it is written bare.)

  Exit status read explicitly (`CIT_EXIT=0`, `SPEC_EXIT=0`) for the two gates
  whose tail line can hide a failure — `spec_check.sh` prints its summary and
  exits 1 without the word "FAIL" on the last line, which is how the
  `03-types.md` break above was nearly missed.

  **Left alone, deliberately.** `editors/zed/grammars/tycho/grammar.js:49` still
  lists `range` among the highlighted builtins, and the generated
  `src/parser.c`/`grammar.json` carry an `anon_sym_range`. It is a *highlight*
  list, not a parse rule, so `scripts/editors_check.sh` is unaffected and both
  new fixtures parse; changing it needs a `tree-sitter generate` re-run, which is
  phase 8's territory. Filed as **phase 32**. The bare `src/tychoc.c:N` class
  (phase 17) is now shifted by a further ~5 lines; the file grew net, every bare
  ref is still in bounds and the gate cannot see them.

  **`make ci` was NOT run** — phase 10 owns it, per `CLAUDE.md`'s gate budget.

- [x] **Phase 8 — `tychofmt`, the LSP, and the editor grammars learn `;` and `..<`**
  - Scope: `tools/tychofmt.ty`, `tools/lsp.ty`, `editors/vscode/`,
    `editors/zed/`. Each carries its own lexer or grammar.
  - Note that with phase 1 done, `tools/*.ty` is no longer constrained by a
    frozen compiler, so unlike every previous tooling phase these files **may**
    use the new syntax themselves.
  - Done when: `tychofmt` round-trips a three-clause loop and a bare `for:`
    unchanged; the LSP tokenises `;` and `..<` without desynchronising; both
    editor grammars highlight them; `scripts/editors_check.sh` green.
  - Verify: `sh scripts/tools_check.sh`, then `make editors-check`.

  - **DONE 2026-07-29.** Four edits, one per component. Both gates that were red
    at the start of this phase are green.

    **What each component was actually doing wrong, read before patching.**

    - **`tools/tychofmt.ty` — stage 1 was already correct, stage 2 was not.** The
      lexer's operator branch ends in a 1-char catch-all, so `;` and the three
      bytes of `..<` always landed in tokens and the *lossless* round-trip was
      never at risk. What broke was the **re-spacer**: `sep_needs_space`
      suppresses the space before `)`, `]`, `,`, `:` and around `.`, and knew
      neither new lexeme. `;` got a space before it and `..<` got one after it.
    - **`tools/lsp.ty` — one scanner had a real defect.** The semantic-token
      number scanner consumed digits **and any `.`** unconditionally, so in
      `0..<N` it swallowed the first two bytes of the operator into the number
      token. `;` needed nothing: it falls to the classifier's final `else`, which
      advances one byte and emits no token, exactly as `+` and `:=` do.
      `find_occurrences` needed nothing either, for the same reason.
    - **`editors/zed/grammars/tycho/grammar.js` — only `;` was fatal.** `..<`
      already lexed, as `.` `.` `<`, because all three are in `operator`; `;` was
      in neither `operator` nor `punctuation`, so every three-clause loop failed
      to lex. `..<` is named anyway so tree-sitter's longest match makes the range
      **one** node for `highlights.scm` instead of three.
    - **`editors/vscode/syntaxes/tycho.tmLanguage.json`** — TextMate takes the
      **first** matching pattern, not the longest, so the `..<` rule is placed
      *above* the general operator rule whose `[<>]` class would otherwise claim
      the `<`.

    **The four edits.**

    | file | line | change |
    |---|---|---|
    | `tools/tychofmt.ty` | 58 | new `is_three_op`, the `..<` lexeme |
    | `tools/tychofmt.ty` | 231 | longest-match-first in the operator branch |
    | `tools/tychofmt.ty` | 327 | no space before `;` |
    | `tools/tychofmt.ty` | 331 | no space either side of `..<` |
    | `tools/lsp.ty` | 1230 | a `.` opening a `..` pair is not part of a number |
    | `editors/zed/grammars/tycho/grammar.js` | 88 | `"..<"` in `operator` |
    | `editors/zed/grammars/tycho/grammar.js` | 96 | `";"` in `punctuation` |
    | `editors/vscode/syntaxes/tycho.tmLanguage.json` | 50 | `keyword.operator.range.tycho` |
    | `editors/vscode/syntaxes/tycho.tmLanguage.json` | 56 | `punctuation.separator.tycho` |

    `editors/zed/grammars/tycho/src/` regenerated with
    `npx --yes tree-sitter-cli@0.25 generate --abi 15` per `editors/zed/README.md`
    (`parser.c`, `grammar.json`, `node-types.json`).

    **(a) `tychofmt` round-trips both constructs — proven by `cmp`, not by eye.**
    The comparison is load-bearing in both directions: the **pre-phase-8**
    `tychofmt`, rebuilt from `git show HEAD:tools/tychofmt.ty`, is run over the
    same files.

    ```
    $ for f in tests/for_bare.ty tests/for3.ty tests/conc/parfor_dotlt.ty \
               examples/mandelbrot/main.ty tools/lsp.ty tools/tychofmt.ty; do
          ./tychofmt "$f" > rt.ty; cmp -s "$f" rt.ty && echo "ROUND-TRIP UNCHANGED: $f"
      done
    ROUND-TRIP UNCHANGED: tests/for_bare.ty
    ROUND-TRIP UNCHANGED: tests/for3.ty
    ROUND-TRIP UNCHANGED: tests/conc/parfor_dotlt.ty
    ROUND-TRIP UNCHANGED: examples/mandelbrot/main.ty
    ROUND-TRIP UNCHANGED: tools/lsp.ty
    ROUND-TRIP UNCHANGED: tools/tychofmt.ty

    $ ./tychofmt_old tests/for3.ty | diff tests/for3.ty -
    12c12
    <     for i := 0; i < 5; i += 1:
    ---
    >     for i := 0 ; i < 5 ; i += 1:
    $ ./tychofmt_old tests/conc/parfor_dotlt.ty | diff tests/conc/parfor_dotlt.ty -
    24c24
    <     parallel for i in 0..<1000:
    ---
    >     parallel for i in 0..< 1000:
    ```

    `tests/for_bare.ty` (the bare `for:`) round-tripped **before** the change too
    — `for:` is a keyword and a colon, and `ct == ":"` already suppressed the
    space. It is asserted rather than assumed, and it is the file that proves the
    new `;` rule did not disturb the existing colon rule.
    `examples/mandelbrot/main.ty` covers `0..<(n * 4)`, i.e. `..<` followed by an
    opener rather than a bare identifier.

    **(b) the LSP tokenises both without desynchronising — checked by decoding
    the delta-encoded array back to source slices.** A buffer holding a
    three-clause loop, a `parallel for … 0..<n:`, a float `1.5` and a bare `for:`
    was opened over real JSON-RPC and `textDocument/semanticTokens/full`
    requested. The LSP emits `(deltaLine, deltaChar, length, type)` quintuples, so
    a single wrong length shifts **every later token in the file** — decoding the
    whole array back to `lines[ln][ch:ch+length]` and asserting each slice is the
    lexeme it claims to be is what "without desynchronising" means here, and it is
    stronger than eyeballing the two new lexemes.

    ```
    line 2 col 4  'for'  keyword    line 2 col 8  'i'  variable
    line 2 col 13 '0'    number     line 2 col 16 'i'  variable   <- after the 1st ';'
    line 2 col 20 'n'    variable   line 2 col 23 'i'  variable   <- after the 2nd ';'
    line 2 col 28 '1'    number
    line 4 col 22 '0'    number     line 4 col 26 'n'  variable   <- either side of '..<'
    line 6 col 9  '1.5'  number                                   <- float unharmed
    line 7 col 4  'for'  keyword                                  <- bare `for:`
    LSP-CHECK: ok
    ```

    No emitted token contains a `;` or a `..` byte, and every decoded slice equals
    its source text. `textDocument/references` on the `i` of the three-clause
    header returned exactly `[(2,8), (2,16), (2,23), (3,18)]` — the init
    declaration, the condition use, the post use and the body use, all four and
    nothing else, which is the check that `find_occurrences` did not need
    touching. Against the **pre-phase-8** server, rebuilt from
    `git show HEAD:tools/lsp.ty`, the same script reports
    `line 4 col 22 '0..' number` and `LSP-CHECK: FAIL` — the defect reproduced
    before the fix and gone after it.

    **(c) the known-bad set, before and after.** `scripts/editors_check.sh:86-88`
    enumerates one file. Both directions were measured over the same 825-file
    corpus, the "before" figure by regenerating the **pre-phase-8** `grammar.js`
    into a temp directory and running `tree-sitter parse -q` over it:

    | | files failing to parse | known-bad set |
    |---|---|---|
    | before | **205** | `tests/reject/rawstring_unterminated.ty` + 204 others |
    | after | **1** | `tests/reject/rawstring_unterminated.ty`, unchanged |

    The known-bad set is **byte-identical before and after**: nothing was added to
    it and nothing was removed from it. The 204 that stopped failing are exactly
    the files phase 6 rewrote plus the two fixtures phases 3-5 added.
    **205, not the 189 the phase brief quoted:** 189 was measured during phase 6's
    first attempt, before phase 6's final commit also rewrote `tools/lsp.ty` and
    before `tests/reject/semi_no_grammar.ty`, `tests/reject/for3_empty_clause.ty`
    and the rest of the phase-3-to-5 fixtures were in the tree. The number is a
    count of the same defect, not a different one.

    **Two files in that 204 deserve a sentence, because they are `tests/reject/`
    fixtures that now PARSE.** `tests/reject/semi_no_grammar.ty` (a statement
    terminator `x := 1;`) and `tests/reject/for3_empty_clause.ty` are compiler
    rejects, and the zed grammar accepts both — correctly. The grammar is **flat
    and token-level** by construction (`editors/zed/grammars/tycho/grammar.js:1-7`:
    it "lexes a .ty file into a flat stream of tokens … without modelling block
    structure"), so it has no notion of *where* a `;` is allowed and never did
    have one for any other token either. `scripts/editors_check.sh:78-85` states
    the same rule from the other side: the known-bad set is for **lexical**
    rejects only, and 239 semantic reject fixtures are expected to parse. These
    two join that majority. Their compiler-level rejection is unaffected and
    asserted by `make test` below.

    **(d) the real gate output.** Both gates that were red at the start of this
    phase, plus the two the change could reach:

    ```
    $ sh scripts/tools_check.sh
    >>> formatter: idempotence + semantic preservation
        825 files checked  (compilable=388)  idempotence-fails=0  semantic-fails=0
    >>> lsp: scripted JSON-RPC smoke
        init=True  diag(valid->[]=True invalid->diag=True loop-warn=True)  hover(local=True fn=True)  def=True
        docsym=True  completion=True  references=True  rename=True  inlay=True  fstr-rename=True  sighelp=True  wsym=True  semtok=True
    …
    tools-check: ok

    $ make editors-check
    >>> editors: JSON syntax (vscode)
        ok  editors/vscode/syntaxes/tycho.tmLanguage.json
        ok  editors/vscode/language-configuration.json
    >>> editors: zed grammar regenerated with npx --yes tree-sitter-cli@0.25 (tree-sitter 0.25.10 …)
        src/ matches grammar.js byte for byte (parser.c, grammar.json, node-types.json, tree_sitter/)
    >>> editors: zed grammar over the corpus (825 .ty files)
        825 files parsed; the only failure is the enumerated known-bad set (tests/reject/rawstring_unterminated.ty )
    editors-check: ok

    $ make test
    passed: 541   failed: 0
    all green
    ```

    `make ci` was **not** run — phase 10 owns it and phase 7 has not landed.

    **Not verified, stated because a future reader will want it.** The two editor
    grammars are checked for *lexing* (tree-sitter parses the corpus; the JSON is
    well-formed), not for the *colours* a user sees. Nothing in the tree renders a
    highlight and asserts a scope name, so `keyword.operator.range.tycho` and
    `punctuation.separator.tycho` are correct-by-convention, not by test. Risk if
    wrong: `;` or `..<` shows unstyled in VS Code. That is the same standing gap
    the previous plan's phase 7 named when it added `scripts/editors_check.sh`.

- [x] **Phase 9 — the spec**
  - Scope: `docs/spec/10-statements.md` §14.4 (the three shapes become four, and
    the counting form goes), `docs/spec/13-concurrency.md` §22 (`parallel for`'s
    counting spelling), `docs/spec/02-grammar.md` and
    `docs/spec/appendix-a-grammar.md` (**this feature adds syntax**, unlike the
    last one — the grammar genuinely changes), `docs/spec/16-builtins.md`
    (`range` removed), `docs/spec/appendix-b-keywords.md` if `range` was
    reserved, and `docs/spec/appendix-e-conformance.md`.
  - State the zero-step loss from Pre-flight as a deliberate trade.
  - Provenance discipline: single-line refs anchored `path:N@token`, ranges bare,
    tight ranges preferred — the previous plan's phase 11 found anchored ranges
    9 and 82 lines off that still passed the gate.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`,
    `sh scripts/spec_check.sh`.

  **Evidence (2026-07-29).** Phases 14, 24 and 31 were folded in, as briefed.

  **What was written, and where.**

  - **`docs/spec/10-statements.md` §14.4** — rewritten from three shapes to four.
    The condition and foreach paragraphs are unchanged; new paragraphs for the
    **infinite** form and the **three-clause** form (init/cond/post kinds, loop
    scoping, `post` resolved in the loop scope and not the body's, **all three
    clauses required**, and `continue` running the post clause), a paragraph
    saying there is no counting form and `range` is not a name the language
    knows, and a paragraph stating the **zero-step loss as a deliberate trade**
    in the words the Pre-flight asked for. A new `> Provenance:` block cites
    every one of those claims. Also repaired the chapter provenance at
    `docs/spec/10-statements.md:8-10`, whose `for` and `select` refs were stale
    by ~400 lines from phases 3–5 (`:3191-3277`→`:3245-3446`,
    `:3135-3172`→`:3189-3225`, `parse_stmt` `:3054-3408`→`:3108-3578`).
  - **`docs/spec/13-concurrency.md` §22** — the counting spelling is now stated:
    `parallel for i in 0..<N:`, the only context where `0..<N` is legal, literal
    `0` required, `N` evaluated once, exclusive bound, implicit step `1` and
    therefore **no zero-step case at all**. Also states, with the reason, that
    the three-clause and infinite shapes **cannot** be parallel — a post clause
    is arbitrary code so the iteration count is not knowable before the loop
    runs. New `> Provenance:` block.
  - **`docs/spec/02-grammar.md` §4.3.2** — the `For` production genuinely
    changed: the `range` alternative is gone, replaced by a three-clause
    alternative plus a bare `"for" ":"` alternative, with two new non-terminals
    `ForInit` / `ForPost` naming exactly the statement kinds the parser accepts.
    `ParallelFor` no longer says `"parallel" For`; it spells its own two
    alternatives, because the `0..<N` form does not exist under a bare `for`.
    Prose bullets rewritten to four shapes plus the required-clauses rule, the
    `;`-and-`:`-supply-the-NEWLINE explanation, and the removal of `range`.
  - **`docs/spec/appendix-a-grammar.md`** — kept in sync **mechanically, not by
    hand**: the GENERATED region was replaced with the output of
    `sh scripts/gen_grammar.sh` (which extracts every `::=`-bearing fence from
    §3/§4), so `spec_check.sh` check 1 compares like with like. `git diff` on
    that file is exactly the 8-line `For`/`ParallelFor` hunk and nothing else,
    which is the proof the region was regenerated rather than edited.
  - **`docs/spec/appendix-e-conformance.md`** — §14.4 row reworded to the four
    shapes; **new row** for the required-clauses rule, the `range()` removal and
    the `0..<N` refusal (`reject/for3_empty_clause`, `tests/diag/range_removed`,
    `reject/dotlt_sequential`, `tests/diag/dotlt_sequential`); `break`/`continue`
    row now also names the post-clause rule and `tests/for3`; §23.x parallel-for
    row gains `tests/conc/parfor_dotlt`. A new note explains why the two
    user-facing loop diagnostics live in `tests/diag/` and not `tests/reject/`.
    Every cited fixture was `ls`-verified before the row was written, and
    `spec_check.sh` check 2 re-proves it.
  - **`docs/spec/appendix-b-keywords.md:35`** — the `range` contextual-identifier
    row now says it is recognised **only to refuse it**.
  - **`docs/spec/16-builtins.md` — nothing to change, and this is a finding, not
    an omission.** The phase brief and the plan's own scope line both say
    "`range` removed" from this chapter. `grep -n range docs/spec/16-builtins.md`
    returns exactly two hits, `:131` ("the byte range `[a, b)`", about `substr`)
    and `:140` ("every in-range `i`"), neither of which is the counting form.
    That matches phase 7's finding that `range` never had a builtin-table entry:
    it was one lexeme test in one `for` header, so §29 never listed it and there
    was nothing to delete. The chapter's "complete, normative catalog" claim at
    `docs/spec/16-builtins.md:5` was already true and stays true.

  **Phase 24, folded in — `docs/spec/01-lexical.md` §3.8.** The false sentence at
  `:170` is gone: it now reads "The **only** range operator is `..<`; `..` alone
  is not a token, and the `range(…)` form it replaced is gone (§14.4)". The token
  inventory gained both new tokens **without adding a row**, by extending two
  existing rows the same way the table already groups same-length spellings
  (`==` `!=` `<=` `>=` share a row): `..<` joins `...` (both three characters,
  and the source tests them in that order, which is what "longest-match first"
  means here) and `;` joins `,` as a separator. The §3.8 provenance now anchors
  `:482@TK_DOTLT` and `:506@TK_SEMI`. §3.7's contextual-identifier list needed
  nothing — phase 7 had already amended `docs/spec/01-lexical.md:133` to
  "`range` (in the head of a `for … in`, **only to refuse it**)".

  **Phase 14, folded in — `docs/spec/02-grammar.md`'s escaped provenance.** All
  eight refs are rewritten with **full paths**, and every single-line ref among
  them is anchored. Why they were unchecked is now written into the block itself:
  `check_citations.py` resets its inherited path at a blank line
  (`scripts/check_citations.py:214-215`) and only polices refs whose path starts
  with `SRC_PREFIX` (`scripts/check_citations.py:226-227`), so a `> Provenance:` paragraph that
  names **no** path leaves `cur` at `None` — the refs are skipped entirely and
  the mandatory-anchor rule never fires. Every one had gone stale by ~400 lines.
  The old values are written below **without backticks on purpose** — they are
  historical numbers, not citations, and backticking them would bind each one to
  whatever path this paragraph named last, which is exactly the trap being
  described: parse_if 2338 → `src/tychoc.c:2730`; parse_match 2409 / 2723 →
  `src/tychoc.c:2836` (definition) and `src/tychoc.c:2915` (value form);
  `for`/`parallel` 3181-3277 → `src/tychoc.c:3235-3446`; select 3135-3172 →
  `src/tychoc.c:3189-3225`; and the five value-control routing sites 2655, 2858,
  2872, 2881, 2903 → `src/tychoc.c:3159`, `src/tychoc.c:3479`,
  `src/tychoc.c:3493`, `src/tychoc.c:3502`, `src/tychoc.c:3524`.
  Each target line was opened and read
  before its ref was written; every single-line ref is anchored on a token that
  line actually contains, which is what the gate now checks (`n_anchored`
  125 → 144).

  **Phase 31, folded in — the live-document sweep.** 23 prose sites across the
  18 documents phase 31 named are rewritten. Loop bodies: `docs/tutorial.md:52`,
  `docs/reference/enums-options.md:145`, `docs/reference/types.md:58`,
  `docs/reference/basics.md:51`, `docs/reference/arrays-slices.md:70`/`:76`/
  `:116`, `docs/internals/value-semantics-limits.md:67`,
  `docs/internals/sink-prototype.md:16`, `docs/guides/arrays-structs.md:107`.
  Parallel spellings → `0..<N`: `docs/reference/concurrency.md:54` (+ its "over a
  range" prose), `docs/guides/concurrency.md:66`/`:100` (+ "sizing a `range`
  yourself" and the `range(0, ncpu())` desugar),
  `docs/internals/parfor-channel-drain-design.md:10`/`:51`/`:111` — the last two
  describe the *internal* desugar, which still builds an `S_FORRANGE`, so they
  now say "an index node `0 .. len(EXPR)`" and "a synthesised `parallel for __pw
  in 0..<ncpu()`" rather than pretending a surface syntax that no longer exists.
  `docs/reference/basics.md:117-133` is the one phase 31 called sharp and it got
  the full treatment: the form list is now four shapes, and two new paragraphs
  carry the required-clauses rule, the `continue` rule, the direction-in-the-
  condition idiom that replaces a negative step, and the **lost zero-step
  diagnostic**. `docs/architecture.md:49` and `ROADMAP.md:47` needed nothing —
  phase 1 had already written them as "replace `for i in range(...)`".
  `FRICTION.md:316`'s `parallel for i in range(N)` was respelled `0..<N` (its
  claim about `min(N, ncpu())` is unaffected, same node). Left alone
  deliberately, per phase 31's archive rule: `docs/internals/spec-design-review.md:74`
  and `docs/internals/spec-plan.md:457` are dated records of defect #21 (`range`
  step 0) and rewriting them would make the record lie about what was reviewed;
  `docs/internals/diagnostic-parity-2026-07-25.md:82` likewise.

  **The snippets were compiled, not eyeballed — and one of them was already
  broken before this phase touched it.** Phase 31's "Done when" requires it: no
  gate compiles an unexecuted `docs/` fence. Every rewritten loop was collected
  into one scratch program and built and run with the real compiler; the
  expected values were computed by hand first:

  ```
  0123456789        # types.md char-append loop
  1                 # enums-options.md index_of([10,20,30], 20)
  30                # basics.md / sink-prototype.md scale2([5,5,5])
  14                # arrays-slices.md sum(make_squares(4)) = 0+1+4+9
  60                # arrays-slices.md fixed-length sum([10,20,30])
  1000              # arrays-structs.md push loop
  0 even 1 odd 2 even 3 odd 4 even    # tutorial.md control flow
  22                # basics.md descending 10,7,4,1
  23                # bare `for:` broken out by `break`
  499500            # parallel for i in 0..<1000, sum = 999*1000/2
  ```

  The first build **failed**, and the failure was pre-existing, not introduced:
  `docs/guides/arrays-structs.md:107` read `range(1_000_000)`, and Tycho has
  **no digit-group separator** (`docs/spec/01-lexical.md:192-194` says so;
  measured — `x := 1_000` gives `error: expected newline` with the caret on the
  `_`). So that snippet had never compiled in either spelling. It now reads
  `1000000` with a comment saying why. This is the class filed as **phase 33**
  below: nothing compiles the unexecuted fences, so they rot silently.

  **Line-count discipline, and where it was and was not held.** Five files were
  edited **line-for-line neutral** (`+n -n` in `git diff --numstat`):
  `docs/spec/01-lexical.md` 5/5, `docs/spec/appendix-b-keywords.md` 1/1, and the
  three internals docs. Six files genuinely grew, because the language grew and a
  new normative rule cannot be re-wrapped into zero lines:
  `docs/spec/10-statements.md` +33, `docs/spec/02-grammar.md` +25,
  `docs/spec/13-concurrency.md` +24, `docs/spec/appendix-e-conformance.md` +7,
  `docs/spec/appendix-a-grammar.md` +4, `docs/reference/basics.md` +12. **Every
  inbound citation past each insertion point was found and repaired**, by
  grepping for each target file's `:N` form and diffing old against new content
  at the mapped line before rewriting it:

  | target | shift | refs repaired |
  |---|---|---|
  | `docs/spec/10-statements.md:110` (§14.6) | +33 | `FRICTION.md`, `docs/internals/plan-friction-DONE.md` |
  | `docs/spec/02-grammar.md:361-362`, `:378` | +25 | `docs/internals/plan-array-arith-DONE.md` |
  | `docs/spec/13-concurrency.md:100`,`:109`,`:115`,`:118-125`,`:141-142` | +24 | `docs/internals/plan-front-door-DONE.md`, `docs/internals/spec-plan-audit-2026-07-24.md`, `docs/internals/spec-plan.md` |
  | `docs/spec/appendix-e-conformance.md:165`,`:183`,`:187`,`:201`,`:252-253`,`:274` | +1 | `plan.md`, `docs/internals/plan-front-door-DONE.md`, `docs/internals/plan-1.0-freeze-DONE.md`, `docs/internals/spec-plan.md`, `docs/internals/plan-array-arith-DONE.md`, `docs/internals/plan-int64-DONE.md`, `docs/internals/plan-postfreeze-rawstring-DONE.md` |
  | `docs/spec/appendix-a-grammar.md` | +4 at `:136` | none — every inbound ref is `:21`–`:98`, all above the hunk |
  | `docs/reference/basics.md` | +12 at `:117` | none — the only inbound ref is `:24-70` |

  **The rule applied to archives, stated so the next reader can disagree with
  it:** a pointer whose *target text is unchanged* was repointed (the §4.5
  precedence table, the channel-ordering paragraphs) even inside a
  `plan-*-DONE.md`, because a line number is a pointer and not a claim about the
  past. A pointer whose *target text this phase rewrote* was left where it was —
  `docs/spec/02-grammar.md:272-274` is cited four times in the archives as the
  site of the eight stale refs, and repointing those would make the archives
  appear to describe text that no longer says what they say it says. Those four
  are bare doc-refs the gate does not police at all (no `docs/` prefix, so they
  never reach `SRC_PREFIX`), and they are the same carried class as phase 17.

  **The full `git diff --numstat`** (28 files, `docs/` + `FRICTION.md` +
  `plan.md`; no source, no fixture, no script touched — this phase is Markdown
  only, which is why the gate budget allowed three cheap gates and nothing else):

  ```
  2   2   FRICTION.md                                     5   5   docs/spec/01-lexical.md
  1   1   docs/guides/arrays-structs.md                  36  11   docs/spec/02-grammar.md
  5   5   docs/guides/concurrency.md                     43  10   docs/spec/10-statements.md
  3   3   docs/internals/parfor-channel-drain-design.md  28   4   docs/spec/13-concurrency.md
  2   2   docs/internals/plan-1.0-freeze-DONE.md          8   4   docs/spec/appendix-a-grammar.md
  3   3   docs/internals/plan-array-arith-DONE.md         1   1   docs/spec/appendix-b-keywords.md
  1   1   docs/internals/plan-friction-DONE.md           10   3   docs/spec/appendix-e-conformance.md
  3   3   docs/internals/plan-front-door-DONE.md          2   2   docs/tutorial.md
  1   1   docs/internals/plan-int64-DONE.md              20   8   docs/reference/basics.md
  2   2   docs/internals/plan-postfreeze-rawstring-DONE.md  2 2   docs/reference/concurrency.md
  1   1   docs/internals/sink-prototype.md                1   1   docs/reference/enums-options.md
  2   2   docs/internals/spec-plan-audit-2026-07-24.md    1   1   docs/reference/types.md
  3   3   docs/internals/spec-plan.md                     3   3   docs/reference/arrays-slices.md
  1   1   docs/internals/value-semantics-limits.md      278   6   plan.md
  ```

  Nine of the twelve `docs/internals/` and `docs/reference/` entries are exactly
  `+n -n`, which is the check that a snippet rewrite or a citation repair moved no
  line under anything citing into it. `node-compile-cache/` is untracked and
  pre-existing (it is untracked at `3f68a00` too) and was **not** staged;
  `git status --short` was read before `git add` for exactly that reason, after a
  previous agent on this plan left ~213 files staged.

  **Verify 1 — citations.**

  ```
  citation check: ok (144 anchored contain the token they name, 2040 bare in bounds,
  102 source->doc citations resolve, 120 source->source in bounds)
  ```

  125 → 144 anchored is this phase's 19 new anchors (10-statements §14.4, §22,
  §3.8, and the nine in the repaired §4.3.2 block). Baseline before the phase was
  `125 anchored / 2014 bare`, so nothing was un-anchored to make the gate pass.
  **The gate caught one of my own refs and it is worth recording**, because it is
  the failure `CLAUDE.md` warns about and it fired on the write-up rather than on
  the spec: the phase-14 paragraph above first spelled the *old* line numbers as
  `` `:2338` `` and friends, and a bare `:N` binds to the last path named in the
  same paragraph — which there was `scripts/check_citations.py`, a 322-line file.
  `STALE plan.md:1715 :2338 -> scripts/check_citations.py has 322 lines: OUT OF
  BOUNDS`. Fixed by writing the historical numbers unbackticked and every live
  ref with its full path, not by widening the bound.

  **Verify 2 — links.**

  ```
  link check: ok (134 markdown files, no dead relative links)
  ```

  **Verify 3 — spec-check.** Exit 0.

  ```
  spec-check: Appendix A grammar matches §3/§4 (ok)
  spec-check: all Appendix E fixture citations resolve (ok)
  spec-examples: 9 runnable example(s), all pass
  ```

  **Not run, and why.** `make test` and `make ci` were not run: this phase edits
  Markdown only and cannot affect a compiled artifact, per `CLAUDE.md`'s gate
  budget. `make ci` is phase 10's, once.

- [x] **Phase 10 — the full sweep**
  - `make ci`, once. Report per-lane whether the new loop forms are actually
    exercised — the previous plan's phase 4 found the fuzz and TSan lanes
    provably never reached its new code, and `fuzz/gen.py` will have the same
    blind spot here unless it generates loops.
  - Done when: `CI GREEN`, exit 0.

  **VERIFIED 2026-07-30 — `CI GREEN`, `CI_EXIT=0`, 1062s wall (17m42s).**
  Observed, not derived: the run was wrapped
  `make ci > …/ci10.log 2>&1; echo "CI_EXIT=$?" >> …` and the log's last line is
  `CI_EXIT=0`. `LD_PRELOAD` was empty (`echo "[$LD_PRELOAD]"` → `[]`), so no
  sanitizer lane ran under the tmux shim. All thirteen numbered steps plus every
  sub-lane ran; nothing was skipped except `image` (missing `libpng`, a
  pre-existing host dependency gap, not a lane failure).

  Counts, from the log: `make test` **passed 543 failed 0**; `make ilp32`
  **passed 543 failed 0**; `make conc` **passed 37 failed 0**;
  `make fuzz N=200` **ok=200 skip=0 timeout=0 FAIL=0**;
  `make fuzz-reject N=200` **accepted=31 rejected=169 FAIL=0**;
  `make fuzz-leak N=150` **ok=150 skip=0 FAIL=0**; `editors-check` parsed the
  corpus at **827 .ty files**; every `tools-check` sub-lane true, `semtok=True`
  included.

  ### THE SWEEP FOUND A REGRESSION OF THIS CHAIN'S OWN, AND IT BLOCKED CI

  The first `make ci` of this phase was killed at step 2 once the cause was
  identified by reading, not by waiting: **`fuzz/gen.py` still emitted
  `for i in range(...)`**, at 18 statement sites and 9 more inside the fixed
  helper prelude, and phase 7 deleted `range()`. Every generated program was
  therefore rejected, and `fuzz/run.py:161-163`'s 30% skip ceiling turns that
  into a hard exit 1 — so step **[6/13]** could not have passed.

  Measured before the fix (`./tychoc --emit-c` over freshly generated seeds):
  **25/25 seeds rejected**, 270 emitted `range()` loops, every one carrying
  phase 7's own diagnostic
  `` `range()` was removed: write `for i := 0; i < N; i += 1:` to count ``.
  This is unambiguously this chain's regression: phase 6 swept `.ty` files and
  phase 7 swept the compiler, and neither swept the Python that *writes* Tycho.
  `fuzz/gen.py` is the only file this phase changed.

  Fix: two helpers, `Gen.count()` (`fuzz/gen.py:75`) and `Gen.countdown()`
  (`fuzz/gen.py:78`), plus the two `parallel for … in 0..<N` sites
  (`fuzz/gen.py:1062`, `fuzz/gen.py:1065`) and the nine prelude loops rewritten
  in place. The semantic hazard is written down at `fuzz/gen.py:71-74`:
  `range(len(a))` evaluated `len(a)` **once**, the three-clause condition
  re-evaluates it every iteration, so `count()` is only safe over a body that
  does not change the length it counts. All 27 call sites were checked
  individually against that rule; none mutates the length it counts over.

  A **second, pre-existing** fault surfaced behind the first and had to be fixed
  for the lane to be clean: `fresh("i")` produces `i8`, `i16`, `i32`, `i64` as
  the uid counter passes them, and those are reserved words
  (`src/tychoc.c:197-205`). 4 of 40 seeds died on it. Verified on 2026-07-30
  that this is **not** new with the three-clause form — `i8 := 5`,
  `for i8 in xs:` (foreach) and `parallel for i8 in 0..<3:` are all refused with
  `expected an expression`. The deleted `for i8 in range(n):` cannot be tested
  directly, but it bound its counter through the same loop-variable slot the
  foreach form still uses, so those seeds were already being counted as silent
  skips before this plan — the fault is pre-existing, merely unmasked once the
  `range()` rejections stopped hiding it. `Gen.RESERVED` (`fuzz/gen.py:54`) now
  skips the colliding uids.

  After the fix, before `make ci`: 60/60 seeds accepted; then the three fuzz
  lanes run directly — `run.py 40` → ok=40 skip=0 FAIL=0, `run_leak.py 12` →
  ok=12 skip=0 FAIL=0, `run_reject.py 40` → accepted=7 rejected=33 FAIL=0.

  **Ordering, stated so the green is not overclaimed.** Every *functional* change
  to `fuzz/gen.py` landed before `make ci` started; `git status --short` was
  checked immediately before launch and showed exactly one modified file. One
  edit was made *after* the run finished: the comment block at
  `fuzz/gen.py:43-54` originally claimed the old `for i8 in range(n):` spelling
  "tolerated" a reserved loop variable, which is an unverified assertion. It was
  measured, found false (the foreach and `parallel for` binders refuse `i8` too),
  and the comment corrected. A comment in a Python file cannot affect a compiled
  artifact, and the two gates that *can* see it were re-run green afterwards:
  `python3 scripts/check_citations.py` → 144 anchored / 2043 bare / 102
  source→doc / 121 source→source, ok; `sh scripts/check_links.sh` → 134 files,
  no dead links.

  ### Per-lane coverage: EXERCISED vs PROVABLY NOT

  Method, stated so it can be re-run and disbelieved: for each lane, the exact
  input set the lane consumes was matched line-by-line against three anchored
  patterns over **non-comment** lines only — `^\s*for\s+IDENT\s*:?=.*;.*;.*:$`
  for the three-clause form, `^\s*for:\s*$` for the bare form, and `0\.\.<` for
  the parallel counting form. Comment-only lines are excluded because a naive
  `\bfor\s*:` scan counts prose about `for:` and inflates the bare-form total
  from 5 to 14. For the fuzz lanes the measurement is **dynamic**: `fuzz/gen.py`
  was run over seeds 1..200 — the exact range `make ci` ran — and its output
  matched with the same patterns.

  | lane | input set | three-clause | bare `for:` | `0..<N` |
  |---|---|---|---|---|
  | [2] `make test` · [2b] `ilp32` · [2c] `asan-self` | `tests/` less `tests/conc/` (539 files) | **160** | **5** | **3** |
  | [4] `make conc` (native + ASan + **TSan**) | `tests/conc/` (37 files) | **12** | 0 | **16** |
  | [3] `corelib` + `corelib-examples` | `corelib/` (75 files) | **83** | 0 | 0 |
  | [3] `site`/`raytrace`/`mandelbrot` · [3b] `entrypoints` | `examples/` (70 files) | **95** | 0 | **1** |
  | [2c] `asan-self` · [3b] `entrypoints` | `compiler/` (52 files) | **38** | 0 | 0 |
  | [9] `tools-check` · [9b] `editors-check` | **every** tracked `.ty` (827 files) | **538** | **5** | **23** |
  | [10] `bench-guard` | `bench/prongB/{binary_trees,maptree}.ty` | **4** | 0 | 0 |
  | [6][8] `fuzz` / `fuzz-leak` (seeds 1..200) | generated | **2268 in 200/200** | **0 in 0/200** | **34 in 33/200** |
  | [12] `spec-check` runnable examples | ```` ```tycho ```` + ```` ```output ```` pairs in `docs/spec/` | **1** (`docs/spec/03-types.md:237`) | 0 | 0 |
  | [5] `make ffi` | `tests/ffi/main.ty` | **0** | **0** | **0** |
  | [11] `make recursion` | inputs generated inline by `tests/recursion/run.sh` | **0** | **0** | **0** |
  | [7] `fuzz-reject` | `fuzz/gen_malformed.py` token soup | **0** | **0** | **0** |
  | [13] `check-links` | Markdown links + citations | n/a | n/a | n/a |

  **Provably exercised** (input set contains the form *and* the lane runs the
  program, not merely compiles it): `make test`, `make ilp32`, `make asan-self`,
  `make conc` (including the **TSan** leg — 16 `0..<N` sites in 16 of 37 conc
  fixtures, so unlike the previous plan's phase 4 this feature *is* under the
  race detector), `corelib`, the three dogfoods, `tools-check`,
  `editors-check`, `bench-guard`, `spec-check`, and after this phase's fix
  **all three fuzz lanes**.

  **Provably NOT exercised**, and each for a reason rather than an oversight:
  `make ffi` (its one fixture `tests/ffi/main.ty` has no loop at all),
  `make recursion` (its inputs are deep paren/operator nests generated inline,
  no loops), and `fuzz-reject` (malformed token soup — it asserts fail-closed,
  not loop semantics).

  **The bare `for:` form is the thin spot, and it is thinner than the table
  suggests.** All 5 occurrences in the tree are in **one file**,
  `tests/for_bare.ty` — `grep -rncE '^[[:space:]]*for:[[:space:]]*$'` over every
  `.ty` returns exactly one path. So the bare form is covered by `make test`,
  `ilp32`, `asan-self`, `tools-check` and `editors-check` *through a single
  fixture*, and by **no** fuzz seed (0/200), no conc fixture, no runnable spec
  example, no benchmark. Filed as phase 36.

  ### Phase 19 — does the blind spot recur?

  **Partly, and the fixed half is fixed.** Phase 19 recorded 0/177 fuzz programs
  and 0/11 conc fixtures for element-wise array arithmetic. Here the fuzz lane
  reaches the three-clause form in **200/200** programs and `0..<N` in **33/200**
  — because this phase had to teach `fuzz/gen.py` the new spellings to unblock
  CI at all, which closed the fuzz half as a side effect of the regression fix
  rather than as new work. The conc half is closed too: 16 `0..<N` sites across
  `tests/conc/`, under TSan. What *does* recur, narrowly, is bare `for:` — no
  generator emits it (phase 36). Phase 19 itself stays open: it is about
  element-wise array arithmetic, a different construct, and nothing here touched
  `fuzz/gen.py`'s arithmetic generators.

  ### Phase 27 — can `bench-guard` see the elision loss? Still no.

  Confirmed against **this** run. `bench/guard.sh:27` iterates exactly
  `binary_trees maptree`. Those two files contain 4 three-clause loops between
  them (`bench/prongB/binary_trees.ty` 3, `bench/prongB/maptree.ty` 1) and
  **zero** loops of the elidable shape — a scan for
  `for IDENT := 0; IDENT < len(` over both files returns 0. So the guard
  exercises `S_FOR3` codegen but cannot observe the bounds-check elision the
  recogniser at `src/tychoc.c:10791-10801` no longer reaches, exactly as phase 27
  states. Its observed numbers this run:

      ok    binary_trees   tycho=273ms  C=725ms  (37% of C, gate <60%)
      ok    maptree        tycho=120ms  C=506ms  (23% of C, gate <60%)
      bench-guard: ok (tycho beats C on tree workloads)

  Green with margin, and green for a reason unrelated to the 223 elision sites.
  Phase 27's "add an elision-shaped workload to `bench/guard.sh`" remains the
  only way to make this class observable. Not done here — out of scope.

  ### Phase 30 — `r_step`

  Untouched, as its own filing requires (it must not land before phase 27). No
  lane in this run can distinguish the three unreachable guards from live code,
  which is precisely why phase 7 annotated rather than deleted them.

  ### What this phase did NOT do

  No feature was added. `fuzz/gen.py` gained no bare-`for:` generator, no
  elision-shaped benchmark was written, no unreachable guard was deleted, and
  the two non-CI parity runners that also still emit `range()` were left alone
  and filed (phase 37). The only file changed is `fuzz/gen.py`, and only to make
  it emit the language that now exists.

## Carried forward

Unclosed discoveries from the two previous plans; none blocking.

## Cleanup batches — how the remaining 24 phases are being run, 2026-07-30

The user asked for all remaining phases to be completed. Run one at a time they
are two dozen background agents for what is, in several cases, the same fact
recorded against different files. They are therefore grouped into **six batches**,
each independently verifiable and each committing once. Every constituent phase
keeps its own entry and its own checkbox below; a batch ticks the boxes it closes.

**Order matters in one place:** the citation-gate work (batch 1) runs before the
citation sweep (batch 2), so the sweep lands against a gate that can see the class
of drift it is repairing. Doing it the other way round is how phases 4, 6, 10 and
11 each re-created work the previous one had done.

| batch | phases | subject |
|---|---|---|
| 1 | 13, 23, 34 | **the citation gate's three blind spots** — no anchored form for source→source, an absolute path silently unchecked, a pathless `> Provenance:` block exempt from the anchor rule |
| 2 | 17, 18, 38 | **the citation sweep** — ~344 bare `src/tychoc.c:N` refs, one confirmed wrong doc→doc ref, and `plan.md`'s own malformed record around phases 28-29 |
| 3 | 12, 15, 32, 33, 35 | **documents pointing at things that no longer exist** — the hand-typed zed corpus count, dead `docs/corelib.md`, `range` still a zed builtin, ungated `tycho` fences in `docs/`, two surviving `range(len(A))` sites |
| 4 | 19, 22, 36 | **coverage the generators cannot reach** — element-wise arithmetic, bare `for:`, and `run_typeparity.py`'s lost oracle |
| 5 | 20, 21, 26, 28 | **fixtures and their gates** — `examples/fetch/run.sh` red since before this plan, fixture placement now the freeze is gone, the `parallel for` diagnostic, the three retired `range()` fixtures |
| 6 | 16, 25, 29, 30, 39, 40 | **small compiler and tooling items** — `char` has no type name, `--emit-c` strays a file, the LSP misses three keywords, dead `r_step`, the `parallel for` capture hole, and recording the `-O3` elision finding |

Phases 27 and 37 were completed individually before this batching, at the user's
request (`fc921d7`, `7a04e53`).

- [ ] **Phase 12** — `editors/zed/README.md`'s corpus count is hand-typed and
      unguarded; `scripts/editors_check.sh` already computes it.
- [ ] **Phase 13** — an anchored form for source→source citations; phase 8 of the
      first plan proved its bounds check catches none of the wrong-line class.
- [x] **Phase 14** — a `> Provenance:` block naming no path escapes the mandatory
      anchor rule by accident; 8 stale refs in `docs/spec/02-grammar.md:272-274`.
      **Note phase 9 of this plan edits that file** — worth doing together.
      **CLOSED — folded into phase 9**, which repaired all eight with full paths
      and anchors and wrote the reason for the escape into the block. The
      *general* gate hardening (make the anchor rule fire on a pathless
      Provenance block) was **not** done and is filed as phase 34.
- [ ] **Phase 15** — `docs/corelib.md` does not exist (moved to
      `docs/guides/corelib.md` by `68e5b39`); a dead backticked path in prose.
- [ ] **Phase 16** — `char` has arithmetic but no spellable type name, no
      `to_char`, and no `\xNN` escape.
- [ ] **Phase 17** — ~344 bare `src/tychoc.c:N` refs shifted by the last plan and
      were deliberately not swept; same class as the dropped phase 9.
- [ ] **Phase 18** — `docs/internals/spec-plan.md:605` cites
      `appendix-e-conformance.md:188` for a §9.5 claim; that line is the §24.2 row.
- [ ] **Phase 20** — `examples/fetch/run.sh` is red, and was red **before** this
      plan started. Two independent pre-existing faults, both found by phase 1 and
      neither caused by it. (a) `SHIM` named `corelib/http/http_shim.c` alone while
      `examples/fetch/main.ty` also imports `core:io`, so the link failed with
      `undefined reference to iox_close_lines / iox_stat_kind`. **Measured, not
      assumed:** `git show HEAD:examples/fetch/run.sh` run at HEAD fails at exactly
      that link step. Phase 1 fixed this one (it had to, to keep the ASan leg
      alive). (b) Behind it sits the real blocker — `examples/fetch/expected.out`
      records a cache filename `tycho_fetch_<hash>.json` whose hash derives from
      the URL, and the URL embeds `$PWD`, so **the golden is only reproducible in
      the directory it was recorded in** (wants `e3de3da05e1cd879`, gets
      `5124059f6a7ee320`). Making the cache key path-independent is a `core:http`
      change and was left alone. The lane is not in `make ci`, which is how it
      stayed red unnoticed. Note this is the exact blind spot
      `scripts/entrypoints.sh` was created for — that gate proves entry points
      *compile*, not that their runners *pass*.

- [ ] **Phase 21** — the freeze no longer constrains where fixtures live, and
      several files still say it does. `corelib/test/result/main.ty`,
      `examples/corelib/httpd/main.ty` and the `§E.2` rationales in
      `docs/spec/appendix-e-conformance.md` place fixtures outside `tests/`
      *because the frozen compiler would refuse them* — nested patterns, `\r`
      escapes, adjacent string literals, `Result` in a tuple literal. With no lane
      building `tychoc0` that constraint is void and those fixtures can come home.
      Phase 1 annotated the claims in place rather than moving anything; phase 2
      already folds `tests/postfreeze/` back and is the natural place to widen.

- [ ] **Phase 22** — `fuzz/run_typeparity.py` lost its oracle, not just its second
      opinion. Unlike `run_eqparity.py` / `run_unaryparity.py` / `run_parforparity.py`,
      which carry a written-down `expect` table, its only assertion *was* `tychoc ==
      tychoc0`. What survives is an exhaustive fail-closed sweep (no crash; every
      accept emits compilable C) over the scalar binop matrix, which no longer
      catches a changed type *rule*. Adding an `expect` table in the style of
      `fuzz/run_eqparity.py` would restore a real oracle. Same shape, smaller:
      `tests/rtparity/run.py` could become a single-runtime lane asserting the C
      emitted for `tests/rtparity/surface.ty` still contains each expected
      `getenv()` name, trap text and stats row against a recorded list.

- [ ] **Phase 23** — **an absolute path in a citation is silently unchecked.**
      Found by phase 2. `/home/igzo/github/tycho/scripts/check_citations.py:226`
      skips any cited path not starting with `SRC_PREFIX`
      (`/home/igzo/github/tycho/scripts/check_citations.py:153-155`, all
      relative: `src/`, `tests/`, …), so a ref written
      `` `/home/igzo/github/tycho/tests/foo.ty:12` `` matches nothing and is
      counted as nothing. Deleting `tests/postfreeze/` left roughly forty such
      refs in the two archived plans pointing at files that no longer exist, and
      the gate stayed green — the phase relied on this, but the same hole means a
      full-path evidence block is *less* checked than a relative one, which is the
      opposite of what `CLAUDE.md`'s "write full paths in evidence blocks" rule
      leads a writer to expect. Fix: strip a leading `ROOT + '/'` before the
      `SRC_PREFIX` test. Note this WILL redden on the archived plans on first run
      (they cite the deleted directory), so it needs a decision on frozen records
      first — sibling of phase 13.

- [x] **Phase 24** — **CLOSED, folded into phase 9** (§3.8's false "no range
      operator" sentence corrected; `..<` and `;` added to the inventory without
      growing the file). Original filing follows.
      **`docs/spec/01-lexical.md` is missing from phase 9's
      scope, and it is the file that goes *wrong*, not merely stale.** Found by
      phase 3. §3.8 "Operators and punctuation" (`docs/spec/01-lexical.md:144-170`)
      is the token inventory, ordered *"longest-match first"* — the exact
      discipline phase 3 followed — and it lists `...` at `:150` while listing
      neither `;` nor `..<`. Worse, `docs/spec/01-lexical.md:170` states in
      plain words: **"There is no range operator (`..`); ranges are written
      with the"** `range()` builtin — a sentence this plan makes false twice
      over (phase 5 adds `..<`, phase 7 deletes `range`). Phase 9's scope names
      `02-grammar.md`, `appendix-a-grammar.md`, `10-statements.md`,
      `13-concurrency.md`, `16-builtins.md`, `appendix-b-keywords.md` and
      `appendix-e-conformance.md` — not this one. Fold it into phase 9 rather
      than running it separately; the two new rows and the corrected sentence
      belong in the same commit as the rest of the spec.

- [ ] **Phase 25** — **`--emit-c` with no `-o` drops an untracked `.c` inside the
      tree, and `.gitignore` does not cover it.** Found by phase 4 while looking at
      the generated C for a fixture. `./tychoc --emit-c tests/for3.ty > /tmp/x.c`
      prints `wrote tests/for3.c` and writes the C **next to the source**, not to
      the redirected stdout — so the redirect captures only the status line and the
      artifact lands in `tests/`. `git check-ignore tests/for3.c` exits 1: the
      `.gitignore` entries for emitted C are a hand-listed set of specific paths
      (`/compiler/*.c`, `/tychoc0.c`, `/tychofmt.c`, …), so a `.c` beside any other
      `.ty` is untracked and a `git add -A` would commit it. Two candidate fixes,
      both small: default `--emit-c` with no `-o` to stdout, or add the emitted
      sibling to `.gitignore` by pattern. Not urgent, not this plan's subject.

- [ ] **Phase 26** — **the `parallel for` gate diagnostic will be wrong the
      moment phase 7 lands.** Found by phase 5, left alone because rewriting it
      now would make it wrong in the other direction. `src/tychoc.c:3235` refuses
      a `parallel` applied to anything that is not an `S_FORRANGE` with
      *"parallel supports 'for x in range(...)' and 'for x in collection' loops
      only"* — the message a user gets for `parallel for i := 0; i < 3; i += 1:`
      today. Phase 5 added a third accepted spelling (`0..<N`) that the message
      does not mention, and phase 7 deletes the only spelling it does mention.
      After phase 7 it should read *"parallel supports `for i in 0..<N` and
      `for x in collection` loops only"*. One line; do it inside phase 7 rather
      than as its own commit, and note `docs/spec/13-concurrency.md:78` says the
      same thing in prose (that half is phase 9's).

- [ ] **Phase 19** — no fuzz lane and no concurrency lane reaches element-wise
      array arithmetic (0/177 and 0/11); `fuzz/gen.py` has no generator for
      binary arithmetic over typed operands. **Phase 10 of this plan will hit the
      same wall for loops.**

- [x] **Phase 27** — **bounds-check elision does not reach the three-clause
      form, and phase 6 silently turned it off tree-wide.** Elision is gated on
      `S_FORRANGE`'s `r_start`/`r_stop`/`r_step` fields at
      `src/tychoc.c:10791-10801`; `S_FOR3` has no equivalent arm, so
      `for i := 0; i < len(a); i += 1:` emits the checked accessor
      (`tycho_arr_int_get(h_a, h_i)`) where `for i in range(len(a)):` emitted raw
      `(h_a).data[h_i]`. **223 sequential sites across 97 files** lost it in phase
      6. Correctness is unaffected — the fallback is the *checked* path — so this
      is purely performance, which is why every golden held.
  - Scope: `src/tychoc.c`'s `S_FOR3` codegen arm and `stmts_unsafe`; no `.ty`
    changes. The recogniser must match the same shape it matches for
    `S_FORRANGE`: init is a literal `0`, condition is `i < len(IDENT)` with `<`
    strictly, post is `i += 1` exactly, and the body must pass the existing
    `stmts_unsafe` guard (no reassign/shadow of the array or the index, no
    passing the array whole to a call). A negative or non-unit step, a `<=`
    bound, or a non-`len` bound must **not** elide.
  - Note the shape check is strictly harder here than for `S_FORRANGE`: there the
    three parts are separate AST fields, here they are an init statement, a
    condition expression and a post statement that must be *proved* to form a
    unit-stride scan. Fail closed — if the shape is not certain, do not elide.
  - Done when: `tests/bounds_elision.ty` emits `.data[i]` again for its four
    loops, its HISTORY note is updated to say elision was restored, a fixture
    proves a non-elidable three-clause loop (reassigned array, `<=` bound, step
    2) still emits the checked accessor, and `TYCHOC_NO_BOUNDS_ELISION`
    (`src/tychoc.c:7896`) still disables it.
  - Verify: `make test`, then diff `--emit-c` output for both spellings and show
    they now agree, then `sh bench/guard.sh`. **Add an elision-shaped workload to
    `bench/guard.sh`** — phase 6 established that neither `binary_trees` nor
    `maptree` contains one, so the guard cannot currently observe this class of
    regression at all.
  - **DONE 2026-07-30.** Restored, and the shape is proved part by part rather
    than pattern-matched. The recogniser is `for3_elidable_arr`,
    `src/tychoc.c:7955-8005`, called from the `S_FOR3` codegen arm at
    `src/tychoc.c:10764`; it pushes onto the same `g_elide` table `S_FORRANGE`
    uses at `src/tychoc.c:10857-10866`, so `index_in_range`
    (`src/tychoc.c:8008`) and the three accessor sites it gates
    (`src/tychoc.c:9575`, `src/tychoc.c:9906`, `src/tychoc.c:10405`) were not
    touched at all.
  - **How each shape condition is established**, against the AST as it really is
    (`S_FOR3` stores init in `els[0]` and the post clause as the LAST element of
    `body` — the note at `src/tychoc.c:1563-1573`):
    - **init is a literal `0`** — `els[0]` must be `S_DECL` with `expr->kind ==
      E_INT && ival == 0`. `init->ctrl` is rejected too, so a value-`if`/`match`
      decl (whose `expr` is NULL) cannot reach the `E_INT` test.
    - **condition is `i < len(IDENT)`, `<` strictly** — `s->expr` must be
      `E_BINOP` with `op == TK_LT` (the token, `src/tychoc.c:118`), `lhs` an
      `E_IDENT` naming the init variable, `rhs` an `E_CALL` to `len` with exactly
      one `E_IDENT` argument. `TK_LE` is a different token and simply does not
      match, which is what makes a `<=` bound fall through.
    - **post is `i += 1` exactly** — `body[nbody-1]` must be `S_ASSIGN` to the
      same name with `expr` an `E_BINOP` `TK_PLUS` over (`E_IDENT` i, `E_INT` 1).
      That is the exact node the parser builds for `i += 1`
      (`src/tychoc.c:3552-3557`), so `i += 2` fails on `ival != 1` and `i -= 1`
      fails on the operator.
    - **the body passes `stmts_unsafe`** — reused verbatim
      (`src/tychoc.c:8003`), not reimplemented, and run over
      `s->body, s->nbody - 1`. Dropping the last element is required, not a
      convenience: the post clause is `S_ASSIGN` to the index, so including it
      would make `stmt_unsafe` (`src/tychoc.c:7921`) report unsafe on every
      loop and the arm would be dead code that never fires.
    - **bounded arrays never elide** — `IS_BOUNDED` (`src/tychoc.c:755`) is
      checked on the `len()` argument's type, the same guard `S_FORRANGE` carries
      at `src/tychoc.c:10862`, because a bounded array stores in `.v` and the
      elision emits `.data[i]`.
  - **Why this is sound, and it is a different argument from `S_FORRANGE`'s.**
    `S_FORRANGE` caches `_stop = len(A)` once before the loop
    (`src/tychoc.c:10842`) and leans on the body never shrinking `A`. `S_FOR3`
    emits the condition into the C `while` header (`src/tychoc.c:10755`), so
    `i < len(A)` is re-evaluated every iteration and holds at the top of each
    body by construction — a strictly stronger position. Confirmed in the
    emitted C: `while (h_i < ((h_a).len))`, a direct field read, not a cached
    `_stopN`.
  - **Before / after, `tests/bounds_elision.ty` line 30's loop.** "Before" is a
    compiler built from `git show HEAD:src/tychoc.c` (HEAD = `7a04e53`), same
    flags, both `--emit-c`; both hit the same emitted line 2462:

    ```
    before  h_s = (h_s + tycho_arr_int_get(h_a, h_i));
    after   h_s = (h_s + (h_a).data[h_i]);
    ```

    Across the whole fixture: **0 → 5** elided accesses (the read loop, the
    in-place write `a[i] = a[i] + 1` which is two, the read under `if`, and both
    halves of the nested `a[i] * b[j]` cross), and the only surviving
    `tycho_arr_int_get` in the file is its own runtime definition at emitted line
    1324 — no call site left in `main`. `TYCHOC_NO_BOUNDS_ELISION=1`
    (`src/tychoc.c:7901`) puts all five back to the checked accessor: measured,
    `grep -c '(h_a).data\[h_i\]'` → `0`.
  - **The non-elidable fixture.** `tests/bounds_noelide.ty` (+
    `tests/bounds_noelide.out`) holds six three-clause loops, one per way of
    missing the shape: a body that reassigns the array, a `<=` bound, a step of
    2, a bound that is a variable rather than `len()`, a body that passes the
    array whole to a call, and an init of `1`. Emitted C: **0** `.data[h_i]`,
    **6** `tycho_arr_int_get(h_` — one per loop. Golden
    `r=90 s=90 t=40 u=90 v=190 w=90`, hand-computed before it was recorded.
  - **And one of them reddens at RUNTIME, not only in the emitted text.**
    `tests/abort/for3_le_bound.ty` runs a `<=` loop that indexes `a[len(a)]`. On
    the checked path it dies as it must — `tycho: index 4 out of bounds (len 4)`,
    exit 1. If `for3_elidable_arr` ever accepted `<=` it would emit
    `(h_a).data[h_i]`, read one past the end and exit 0, and `tests/run.sh`'s
    abort lane fails on "runtime abort did not fire (exit 0)". That is the one
    assertion here that does not depend on anybody re-reading generated C.
  - **How many of the 223 sites elide again: 236 of the 245 that carry the shape
    today.** Measured, not estimated, and by two independent counts that agree.
    (a) Textually, `git grep -E 'for (\w+) := 0; \1 < len\(\w+\); \1 \+= 1:' --
    '*.ty'` finds **245** sites in **110** files — more than phase 6's 223
    because later phases in this plan added loops of the same shape. (b) By the
    compiler itself: a throwaway build of `src/tychoc.c` with one `fprintf` on
    the `for3_elidable_arr` result, run over every `git ls-files '*.ty'` entry
    with `TYCHO_CORELIB` set, reports **236 elided sites in 108 files**. The
    intersection of the two lists is: **236 elide**, **8 are rejected** and **1**
    is in a proc no entry point instantiates (`corelib/sort/sort.ty:11`). All
    eight rejections are correct and conservative — five are bounded arrays
    (`tests/bounded_const_cap.ty:15`, `tests/bounded_elems.ty:29`, `:35`, `:41`,
    `:43`, the `IS_BOUNDED` guard) and three pass the indexed value's own
    container to a call (`corelib/strings/strings.ty:99`,
    `examples/site/main.ty:112` via `csv.get(rows, i, 0)`, `tools/lsp.ty:318`
    via `substr(s, i, i + 1)`).
  - **`bench/guard.sh` gained the elision workload, and its assertion is
    structural because wall time provably cannot carry it.**
    `bench/prongB/arr_pipeline.ty` is the one `prongB` program that contains the
    elidable shape — its two scan loops, `bench/prongB/arr_pipeline.ty:16` and
    `bench/prongB/arr_pipeline.ty:20`, confirmed by the instrumented compiler,
    against zero in `binary_trees` and `maptree`. The new block
    (`bench/guard.sh:41-71`) emits its C and requires `>= 2` raw `.data[h_i]` and
    **0** `tycho_arr_int_get(h_`.
  - **The measurement that forced that choice, and it is the finding of this
    phase.** At `-O3` — the level `bench/guard.sh:29` builds with and the level
    `tychoc` itself hands to `cc` (`src/tychoc.c:12695`) — gcc folds the
    per-element check away on its own, because the three-clause form puts
    `h_i < ((h_xs).len)` in the `while` header and the accessor's own
    `i >= xs.len` test is then known false. Best-of-3, same program, elision on
    vs `TYCHOC_NO_BOUNDS_ELISION=1`:

    ```
    arr_pipeline  -O3   29 vs 30 ms | 46 vs 46 ms | 47 vs 45 ms   (C moved 24->35ms
                                                                   across the same runs)
    scan micro    -O3  207 vs 208 ms   (1.00x — 1.6e9 accesses in 207ms is ~0.4
                                        cycles each: both forms vectorised)
    scan micro    -O2  2358 vs 2684 ms (1.14x)
    scan micro    -O1  2517 vs 4740 ms (1.88x)
    ```

    A nested-loop shape (`cross`, 2000x2000x60) and an `inout [int]` parameter
    shape were tried as well, looking for a case gcc could not fold: 51 vs 52 ms
    and 105 vs 105 ms. So a ratio gate on this workload would be noise dressed as
    a guard — the run-to-run spread on `arr_pipeline` (120%–191% of C) is larger
    than the entire effect being gated. The emitted C separates cleanly instead:
    **2 raw / 0 checked** with elision on, **0 raw / 2 checked** with it off.
    Proved the gate actually fires, not just that it passes:
    `TYCHOC_NO_BOUNDS_ELISION=1 sh bench/guard.sh` → `FAIL arr_pipeline 0 raw
    .data[i], 2 checked calls`, `bench-guard: FAILED`, exit **1**.
  - **Gates, all foreground, one command each.**
    - `make test` → `passed: 545   failed: 0` (543 at HEAD; +2 is
      `bounds_noelide` and `abort_for3_le_bound`).
    - `sh bench/guard.sh` → `ok binary_trees tycho=270ms C=727ms (37% of C, gate
      <60%)`, `ok maptree tycho=123ms C=498ms (24% of C, gate <60%)`, `ok
      arr_pipeline 2 raw .data[i], 0 checked calls`, `bench-guard: ok`.
    - `sh scripts/asan_self.sh` → `compiled: 561   failed: 0`, all green.
    - `python3 scripts/check_citations.py` → ok (144 anchored, 2050 bare in
      bounds, 102 source→doc, 131 source→source).
  - **On the ASan lane, honestly:** `scripts/asan_self.sh` sanitises *tychoc's
    own execution* while it compiles the corpus, so it cannot see a wrong
    elision, which would be an overread in the *emitted* program. So the emitted
    programs were also built `-fsanitize=address,undefined
    -fno-sanitize-recover=all` and run — `tests/bounds_elision.ty`,
    `tests/bounds_noelide.ty`, `bench/prongB/arr_pipeline.ty`, all exit 0 with
    empty stderr. Note the limit of even that: arrays live inside an arena block,
    so ASan would not flag a few-element overrun *within* the arena. The load
    bearing proofs of no-wrong-elision are the shape argument above,
    `tests/abort/for3_le_bound.ty`, and 545 unchanged goldens — not ASan.
  - **Repaired in passing, because this phase caused it:** inserting 51 lines at
    `src/tychoc.c:7955` and 10 more inside the `S_FOR3` arm shifted every
    anchored citation below those points. `python3 scripts/check_citations.py`
    was green at `7a04e53` and went to **25 stale**; all 25 were re-anchored
    (+51 above the `S_FOR3` arm, +61 below it) across
    `docs/internals/plan-postfreeze-rawstring-DONE.md`, `docs/spec/01-lexical.md`,
    `docs/spec/03-types.md`, `docs/spec/10-statements.md`,
    `docs/spec/12-aggregates.md`, `docs/spec/15-program.md` and
    `docs/spec/16-builtins.md`, plus one range in `docs/spec/10-statements.md:129`
    that named the `goto _post<id>` emit. This is phase 17's class arriving on
    schedule; the blanket sweep it asks for was NOT done here.
  - **Not done, deliberately:** phase 30's three unreachable `r_step` guards are
    untouched, and no `.ty` outside `tests/` was rewritten.

- [ ] **Phase 28** — **three `range()`-only fixtures were deleted in phase 6 and
      their guarantees are untested until phase 7 removes the feature.**
      `tests/reject/range_step_zero_lit.ty` (literal `0` step refused at compile
      time), `tests/abort/range_step_zero.ty` (runtime `0` step aborts rather
      than spins) and `tests/conc/reject/parfor_step.ty` (a non-unit step on a
      `parallel for` is refused). None could be rewritten — none has a
      three-clause or `0..<N` equivalent — so deleting them is correct in the end
      state. The gap is the *interval*: `range()` is still in the compiler until
      phase 7, and those three checks are now unasserted.
  - Decide, and record the decision rather than letting it lapse: either restore
    the three fixtures until phase 7 deletes them with the feature, or state in
    phase 7's evidence that they were retired early on purpose. Restoring them is
    cheap (`git show 6ca63ca^:<path>`) and would make phase 7's own deletion of
    `range()` provably complete rather than merely believed.

- [ ] **Phase 29** — **the LSP's semantic-token classifier is missing three
      keywords the other two grammars have, and `parallel for i in 0..<N:` is
      where it shows.** Found by phase 8 while decoding the token array: in
      `parallel for j in 0..<n:` the LSP classifies `parallel` as **variable**
      (type 3), not keyword (type 0). `tools/lsp.ty:1163`'s `sem_is_keyword`
      lists 30 words and omits **`parallel`, `select` and `or_return`**, all
      three of which `editors/zed/grammars/tycho/grammar.js:37-41` and
      `editors/vscode/syntaxes/tycho.tmLanguage.json:42` do list. So the same
      buffer is highlighted one way by the tree-sitter grammar and another by the
      server, and a `parallel for` header — the construct this whole plan adds a
      spelling for — is the most visible case.
  - Out of phase 8's scope on purpose: none of the three is `;` or `..<`, they
    were all missing before this plan started, and phase 8's brief says smallest
    change that satisfies "Done when".
  - Not verified: whether `handle` / `sink` / `where` / `const`, which
    `sem_is_keyword` **does** list, are still keywords in `src/tychoc.c`. The list
    may have drifted in both directions; check both before editing it.
  - Done when: the three grammars agree on the keyword set, or the divergence is
    written down as deliberate. Verify: `sh scripts/tools_check.sh` (the
    `semtok=` leg) and `make editors-check`.
  - The permanent half is already owned elsewhere: the zero-step guarantee has no
    successor in either new form and **phase 9** must state that in the spec as a
    deliberate trade. This phase is only about the fixtures.
  - Done when: `plan.md` records which of the two options was taken and why, and
    if restored, `make test` and `make conc` return to 543 and 38 until phase 7
    moves them again.
  - **Closed by phase 7**: the option taken was *retire early on purpose*.
    Nothing was restored; the per-fixture reasoning and the two replacement
    fixtures are in phase 7's evidence.

- [ ] **Phase 30** — **`Stmt.r_step` is dead and its three guards are
      unreachable.** Found by phase 7. `range(a, b, step)` was the only producer
      of a non-NULL `r_step`; with it deleted, all five surviving `S_FORRANGE`
      producers write NULL (the table in phase 7's evidence). So the step
      codegen (`src/tychoc.c:10776-10782`, including the emitted runtime
      `tycho: range step is zero` abort), the literal-zero refusal
      (`src/tychoc.c:7292`) and `resolve_parfor`'s
      `parallel for does not support a range step` (`src/tychoc.c:6645`) can
      never fire. Phase 7 annotated all three rather than deleting them, for two
      stated reasons: no test can exercise the deletion, so it would be an
      unverifiable change; and **phase 27's elision recogniser is specified
      against `s->r_step == NULL`**, so this phase must not land before it.
  - Scope: `src/tychoc.c` only — the `r_step` field, its ~10 walker mentions,
    the `_step%d` codegen and the three guards above. No `.ty` change, no
    diagnostic a user can currently see.
  - Order: **after phase 27**, or phase 27's brief must be rewritten first.
  - Done when: `grep -n r_step src/tychoc.c` is empty, `for i in 0..<N`, every
    foreach loop and every `parallel for` still emit the same C as before
    (diff `--emit-c` for a fixture of each shape), and `make test` + `make conc`
    hold at their current counts.
  - Verify: `make test`, then `make conc`, then `python3
    scripts/check_citations.py` (deletions here shift `src/tychoc.c` anchors).

- [x] **Phase 31** — **CLOSED, folded into phase 9** (all 23 sites rewritten and
      every rewritten loop compiled and run; the archives and dated review
      records were left alone as this filing required). Original filing follows.
      **23 `in range(` sites remain in prose across 18 live
      documents.** Found by phase 7, which fixed only the one that reddened a
      gate. Phase 6 swept `.ty` files; nothing swept fenced `tycho` blocks in
      Markdown, and `scripts/spec_check.sh` runs only the examples it can
      execute — `docs/spec/03-types.md:237` was one of those and broke the
      moment `range()` went, which is how the class was found. The rest are
      unexecuted snippets no gate reads, so they are wrong quietly.
  - Scope: **not** `docs/spec/` — that is phase 9's. This phase owns
    `docs/tutorial.md`, `docs/reference/` (`basics.md`, `arrays-slices.md`,
    `types.md`, `enums-options.md`, `concurrency.md`), `docs/guides/`
    (`arrays-structs.md`, `concurrency.md`), `docs/architecture.md` and the
    live `docs/internals/` design notes (`sink-prototype.md`,
    `value-semantics-limits.md`, `parfor-channel-drain-design.md`).
  - **Leave the archives alone.** `docs/internals/plan-*-DONE.md` and
    `docs/internals/diagnostic-parity-2026-07-25.md` record what was true on the
    day; rewriting them makes the record lie. Same rule phase 2 applied.
  - `docs/reference/basics.md:121` is the sharp one: it documents
    `range(a, b, step)` **including a negative step**, a semantics with no
    successor. It needs the three-clause equivalent *and* the note that the
    zero-step diagnostic is gone.
  - Done when: no live document shows a `range()` loop as current syntax, and
    every rewritten snippet has been compiled by hand (they are unexecuted — the
    gate will not catch a typo).
  - Verify: `sh scripts/spec_check.sh`, `sh scripts/check_links.sh`, `python3
    scripts/check_citations.py`.

- [ ] **Phase 32** — **the zed grammar still lists `range` as a builtin.**
      Found by phase 7. `editors/zed/grammars/tycho/grammar.js:49` has `range`
      in the builtin-highlight list, and the generated
      `editors/zed/grammars/tycho/src/grammar.json`,
      `editors/zed/grammars/tycho/src/parser.c` and `.../src/node-types.json`
      carry the corresponding `anon_sym_range`. A user typing `range` now gets
      it painted as a builtin by the editor and refused by the compiler.
  - Left out of phase 7 deliberately: it is a *highlight* list, not a parse
    rule, so `scripts/editors_check.sh` is unaffected and both of phase 7's new
    fixtures parse cleanly. Changing it needs a `tree-sitter generate` re-run,
    which is phase 8's toolchain, not phase 7's.
  - Check the VS Code grammar for the same word before assuming zed is alone —
    phase 7 grepped `editors/` for `"range"` and only zed matched, but that grep
    would miss a regex alternation that spells it without quotes.
  - Done when: `range` is not highlighted as a builtin by either editor, the
    generated files are regenerated rather than hand-edited, and
    `make editors-check` is green.
  - Verify: `make editors-check`.

- [ ] **Phase 33** — **no gate compiles the unexecuted `tycho` fences in `docs/`,
      and one of them had never compiled at all.** Found by phase 9 while
      honouring phase 31's "compiled by hand" requirement.
      `docs/guides/arrays-structs.md:107` used the digit separator `1_000_000`,
      which Tycho does not have — `docs/spec/01-lexical.md:192-194` says so, and
      `x := 1_000` gives `error: expected newline` with the caret on the `_`
      (measured on the built `./tychoc`). The snippet was therefore broken
      *before* the `range()` rewrite and in both spellings.
      `scripts/spec_examples.sh` only builds a ```` ```tycho ```` fence that is
      **paired with an ```` ```output ```` fence** (9 such examples in the whole
      spec, `scripts/spec_check.sh` check 3); every other fence in `docs/` — the
      overwhelming majority, and all of `docs/reference/`, `docs/guides/`,
      `docs/tutorial.md` — is never parsed by anything.
  - The cheap version is not "pair every fence with an output block": most are
    fragments with no `main`. It is a **parse-only** lane — a `--check`-style
    front-end pass over each fence that declares an `fn`, skipping fences marked
    as fragments. Decide the opt-out marker before writing it, or the lane will
    be red on prose and get disabled.
  - Done when: a gate parses the fences it can and names the ones it skipped, and
    `docs/guides/arrays-structs.md`'s snippet is covered by it.
  - Verify: the new gate, plus `sh scripts/spec_check.sh`.

- [ ] **Phase 34** — **the pathless-`> Provenance:` gate hole is still open in
      the tool, only closed in the one file phase 14 named.** Phase 9 repaired
      `docs/spec/02-grammar.md`'s eight refs by hand, but
      `scripts/check_citations.py` still cannot see the class: `cur` is reset at
      every blank line (`scripts/check_citations.py:214-215`) and a ref whose
      inherited path is `None` is `continue`d before the anchor rule runs
      (`scripts/check_citations.py:226-227`). So any future `> Provenance:` block
      that opens a paragraph without naming a path gets **zero** checking —
      no bounds check, no anchor requirement.
  - The fix is not "carry `cur` across paragraphs" — the comment at
    `scripts/check_citations.py:211-213` explains why that was deliberately
    removed. It is to make a `> Provenance:` block that contains a `:N` ref and
    names no path a **hard failure in its own right**: fail closed, with a
    message telling the author to write the path.
  - Done when: a pathless Provenance ref reddens the gate, the whole tree is
    swept for the class, and `python3 scripts/check_citations.py` is green.
  - Verify: `python3 scripts/check_citations.py`.

- [ ] **Phase 35** — **two `for i in range(len(A)):` sites survive outside
      phase 31's scope, and they cannot simply be respelled.** Found by phase 9.
      `bench/prongB/RESULTS.md:170` (footnote ²) and `tests/bounds_elision.ty:5`
      both describe the bounds-check-elision recogniser in terms of the deleted
      counting form. Phase 9 left them alone **on purpose**: phase 27 records
      that elision does **not** reach the three-clause loop, so rewriting the
      syntax in either place would turn a stale sentence into a false claim about
      the current compiler, and `RESULTS.md`'s 132 → 47 ms is a dated
      measurement whose provenance is the old form.
  - Do this **with or after phase 27**, not before: once elision reaches the
    three-clause loop the fixture comment and the footnote can be rewritten
    truthfully, and not until then.
  - Done when: neither file describes a deleted syntax as the trigger, and
    `RESULTS.md` says which form the recorded numbers were measured with.
  - Verify: `make test` (the fixture), `python3 scripts/check_citations.py`.

- [ ] **Phase 36** — **nothing generates the bare `for:` form, and it lives in a
      single fixture.** Found by phase 10's coverage sweep. All 5 non-comment
      occurrences of `^\s*for:\s*$` in the whole tree are in **one file**,
      `tests/for_bare.ty`; `fuzz/gen.py` emits it in **0 of 200** seeds, no
      `tests/conc/` fixture uses it, no runnable `docs/spec/` example uses it, no
      benchmark uses it. So the form is asserted by exactly one golden, and a
      codegen or arena regression specific to an unconditional loop would be
      caught only if it happened to break that one program.
  - This is the narrow recurrence of phase 19's blind spot. The *other* two forms
    are covered: phase 10 had to teach `fuzz/gen.py` the three-clause form and
    `0..<N` to unblock CI, so the fuzzer now reaches them in 200/200 and 33/200
    programs respectively. Only the bare form has no generator.
  - Scope: `fuzz/gen.py` — a `bare_loop` kind that emits `for:` with a
    guaranteed-taken `break` (an unterminated loop would hang the lane, so the
    generator must prove termination the way `loop_vars` already proves the
    counter is never written), plus at least one `tests/conc/` fixture using it.
  - Done when: a non-zero number of the 200 CI seeds contain `for:`, measured the
    way phase 10 measured it, and `make fuzz N=200` stays at skip=0.
  - Verify: `python3 fuzz/run.py 60`, then `make test`, then `make conc`.

- [x] **Phase 37** — **two hand-run fuzz lanes still emit `range()` and are dead
      on arrival.** Found by phase 10 while fixing `fuzz/gen.py`.
      `fuzz/run_parforparity.py` embeds **28** `parallel for i in range(...)`
      programs and `fuzz/run_pkg.py` one `for j in range(n):` helper; both were
      annotated by phase 1 for the `tychoc0` retirement (their headers already
      name "the breaking loop-syntax change of 2026-07-29") but neither had its
      *emitted Tycho* rewritten, so every program they feed the compiler is now
      rejected outright.
  - **They are not in `make ci`**, which is why the sweep stayed green:
    `grep -rn 'run_parforparity\|run_pkg\|run_eqparity\|run_typeparity\|run_unaryparity' Makefile scripts/ .githooks/`
    returns nothing. `scripts/ci.sh` runs only `run.py`, `run_reject.py` and
    `run_leak.py`. So this costs nothing today and everything the moment someone
    reaches for the `expect`-table oracle that phase 22 wants to extend.
  - Note `fuzz/run_parforparity.py:115` is `parallel for i in range(0, 10, 2)`,
    and `fuzz/run_parforparity.py:171` and `fuzz/run_parforparity.py:179` are
    `range(1, 8)` — a non-unit step and a non-zero start. Those
    have **no** `0..<N` equivalent, so they cannot be respelled; they are now
    tests of a syntax that does not exist and each needs a decision (delete, or
    convert into a rejection case), not a mechanical rewrite. Same class of
    decision phase 28 recorded for its three deleted fixtures.
  - Sibling of phase 22, which wants `run_typeparity.py` to grow a real oracle.
    Do them together: both are "the parity runners lost their second opinion and
    nobody re-pointed them at anything".
  - Done when: every `.py` under `fuzz/` that emits Tycho emits only spellings
    the current compiler accepts, or says in its header that it is retired.
  - Verify: run each touched runner directly; `grep -rn 'in range(' fuzz/*.py`
    shows only Python's own `range`.
  - **DONE 2026-07-30.** Site counts re-derived, not taken from this entry: the
    phase text said 28 `parallel for i in range(...)` programs in
    `fuzz/run_parforparity.py`; the real figure is **25** parallel-for headers
    (16 in `REJECT`, 9 in `ACCEPT`) plus **2** sequential `range()` loops
    (`for j in range(10):` nested inside the `nested_break` fixture, and
    `for i in range(n):` in the `feed` helper of `select_recv`) — **27** emitted
    `range()` headers total. `fuzz/run_pkg.py` had the one predicted helper,
    `for j in range(n):` inside `k_array_ret`.
  - **The lanes were dead, and each was dead in its own way — the before-state:**
    - `python3 fuzz/run_parforparity.py` → `PARFOR-PARITY FAIL: 9/25 cases
      diverge`, every one an `ORACLE DIVERGENCE  tychoc=reject expected=accept`
      (`reduction_add`, `reduction_mul`, `two_accumulators`, `capture_read`,
      `range_compound_int`, `nested_break`, `local_indexset`,
      `pass_local_as_mut`, `select_recv`). The 9 accept baselines failed loudly.
      **The 16 reject fixtures reported PASS and were worthless**: they were
      rejected at parse for saying `range()`, never reaching the soundness gate
      each exists to test. A lane that is 64% vacuous while printing 16 green
      rejections is worse than one that is simply red.
    - `python3 fuzz/run_pkg.py 20` → `DONE: ok=10 skip=10 FAIL=0`. `classify`
      (`fuzz/run_pkg.py:88`) returns `"skip"` on any non-zero tychoc exit, so a
      program that no longer parses was *counted as skipped* and the runner still
      exited 0. Half of every run was discarded behind a green `FAIL=0`.
  - **After:**
    - `python3 fuzz/run_parforparity.py` → `parfor-parity: 25/25 parallel-for gate
      cases match the oracle (accept/reject + emitted C).`
    - `python3 fuzz/run_pkg.py 200` → `DONE: ok=200 skip=0 FAIL=0` (was
      `ok=10 skip=10` at n=20). **`skip` went to zero**, which is the number that
      actually moved.
  - **25/25 is not sufficient evidence and was not treated as such.** The runner
    only compares accept-vs-reject, so a fixture rejected by a stray parse error
    scores identical to one that trips its gate — exactly the failure mode this
    phase existed to clean up. Every `REJECT` fixture was therefore re-run by hand
    and its *diagnostic* read. 14 of 16 name their own gate (`return cannot cross
    a parallel for`, `parallel for cannot mutate captured variable 'xs' in place`,
    `reduction accumulator 'acc' may only be updated, not read, inside parallel
    for`, `parallel for needs an int range`, …). Two did not; see below.
  - **The non-unit-step program (`parallel for i in range(0, 10, 2)`, the
    `range_step` fixture) — DELETED, and replaced.** `0..<N` has no step syntax at
    all, so the gate it tested is now unreachable *by construction*:
    `src/tychoc.c:6645`'s `die_at(... "parallel for does not support a range
    step")` sits under a comment at `src/tychoc.c:6643` calling itself
    "unreachable since 2026-07-29 … kept as a fail-closed assertion". No source
    text can reach it, so no fixture can test it. The other option — folding the
    stride into the body as `0..<5` with `i * 2` — was **rejected**: it converts a
    gate fixture into a second accept baseline that duplicates `reduction_add` and
    asserts nothing about a gate. In its place is `range_nonzero_start`
    (`parallel for i in 1..<10:`), which tests the constraint that actually took
    the step gate's job — the literal-`0` lower bound enforced at
    `src/tychoc.c:3364-3365`. Verified to reject with ``` `parallel for` counts
    from zero: write `0..<N` -- a literal `0` … ```.
  - **The two non-zero-start ACCEPT fixtures (`range(1, 8)`, twice) — offset
    folded into the body, not renumbered.** `reduction_mul` and
    `two_accumulators` became `0..<7` with `prod * (i + 1)` / `s + (i + 1)`,
    `p * (i + 1)`. Renumbering to `0..<8` was rejected on oracle grounds: a
    product over a zero-based space is 0, and **0 is also what a reduction that
    silently drains a private chunk copy produces**, so `0..<8` would make the
    fixture pass for the wrong reason. Folding keeps the answer 5040 and keeps the
    fixture discriminating. `range_compound_int` kept its compound bound as
    `0..<m + 2` (that fixture is about a compound *expression* as the stop, and
    `src/tychoc.c:3370` `parse_expr`s the stop, so the coverage survives intact).
  - **Found and fixed in-scope: `multiassign_capture` was never valid Tycho.** Its
    body `a, b = b, a` died at `error: expected newline` — a *parse* error, not
    its gate. The RHS of a multi-assign must be a single tuple-valued expression
    (`src/tychoc.c:7054`, `src/tychoc.c:7057`), and `b, a` is not one. The fixture
    has therefore been asserting nothing since it was written; the `range()`
    breakage merely hid it behind an earlier death. Respelled through a
    tuple-returning `swap`, it now rejects with `parallel for cannot assign to
    captured variable 'a'` — the gate it is named for.
  - **Gate as worded ("`grep -rn 'in range(' fuzz/` must return nothing") is not
    achievable and was not forced.** Every surviving hit is one of: Python's own
    `range` iterating seeds or generator counts (`fuzz/run.py:131`,
    `fuzz/run_leak.py:108`, `fuzz/run_reject.py:117`, `fuzz/run_pkg.py:75`,
    `fuzz/run_pkg.py:112`, `fuzz/gen.py`, `fuzz/gen_malformed.py`), or English
    prose in the *other* retired runners' headers recording the removal
    (`fuzz/run_typeparity.py:15`, `fuzz/run_eqparity.py:10`,
    `fuzz/run_unaryparity.py:23`, `fuzz/minimize.py:18`) — all out of this
    phase's scope. Rewriting Python's `range()` to satisfy a grep would be
    vandalism. This entry's own weaker wording (only Python's `range` remains) is
    the contract that was met. The substantive check was run instead: both
    modules were imported and **every string of Tycho they emit** was scanned —
    125 programs (25 parfor fixtures + 100 package files from 50 seeds),
    **0 containing `range`**, 25 `parallel for` headers still present.
  - `python3 scripts/check_citations.py` → `citation check: ok (144 anchored
    contain the token they name, 2043 bare in bounds, 102 source->doc citations
    resolve, 125 source->source in bounds)`.
  - **Checked, as asked: neither runner has phase 10's `fresh("i")` defect.**
    `fuzz/run_parforparity.py` is a fixed table of hand-written programs with
    literal `i`/`j` counters and no name generator at all;
    `fuzz/run_pkg.py`'s names are a fixed prefix plus a loop index
    (`Pt{i}`, `w{i}`, `gid{i}` — `fuzz/run_pkg.py:36`ff), which can never produce
    a bare `i8`/`i16`/`i32`/`i64`. Nothing to fix.
  - Both runners' headers now record that they are outside `make ci` and must be
    run by hand, so the next syntax change does not repeat this silently.

- [ ] **Phase 38** — **`plan.md`'s own record is malformed around phases 28-29,
      and one closed phase reads as open.** Found by phase 10 while counting the
      carried-forward list. Phase 28's closing bullets — "The permanent half is
      already owned elsewhere…", "Done when: `plan.md` records which of the two
      options was taken…" and "**Closed by phase 7**: the option taken was
      *retire early on purpose*" — sit underneath the **phase 29** bullet, so
      phase 29 appears to be about fixtures and test counts when it is about the
      LSP's keyword set. And phase 28's checkbox is still `- [ ]` although its own
      text says it is closed.
  - Left alone deliberately: phase 10's scope was the sweep, and silently
    re-parenting bullets in a record other phases cite is exactly the kind of edit
    that should be its own commit. Same rule phase 2 applied to the archives.
  - Done when: phase 28's bullets sit under phase 28, its box is ticked, phase 29
    reads as the LSP keyword-set phase it is, and the "Status — PLAN COMPLETE"
    unchecked count is corrected to match.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.

- [ ] **Phase 39** — **`parallel for` refuses an in-place mutation of a captured
      array through the wrong diagnostic.** Found by phase 37 while reading the
      actual rejection message of every `fuzz/run_parforparity.py` fixture rather
      than trusting its accept/reject verdict.
  - `indexset_capture` (`xs[0] = i`) and `fieldset_capture` (`p.x = i`) both get
    the dedicated gate: `parallel for cannot mutate captured variable 'xs' in
    place`. `push_capture` (`push(xs, i)`) — the same soundness violation, a
    captured array mutated inside a chunk — instead gets `cannot mutate parameter
    'xs' (it is borrowed read-only; copy it with `y := xs` first)`. That is the
    generic borrow rule catching it downstream, on the lifted chunk proc's
    parameter, after the parallel-for scan let it through.
  - It is not a soundness hole today — the program *is* rejected, fail-closed,
    and `fuzz/run_parforparity.py` scores it correct because that runner only
    compares accept-vs-reject. It is a diagnostic-quality and coverage bug: the
    message tells the user to copy an array they never wrote as a parameter, and
    the parfor capture gate has an untested hole that only the borrow checker is
    currently closing. Remove the borrow rule and the fixture likely goes green
    the wrong way.
  - Out of scope for phase 37, whose scope lock named `fuzz/run_parforparity.py`
    and `fuzz/run_pkg.py` and explicitly excluded `src/tychoc.c`. The fix belongs
    in the parallel-for capture scan (`pf_scan_body`, around
    `src/tychoc.c:6650`), not in the fixture.
  - Done when: `push(xs, i)` inside a `parallel for` over a captured `xs` is
    refused by the parallel-for capture gate with the same wording
    `indexset_capture` gets, and `fuzz/run_parforparity.py` still reports 25/25.
  - Verify: `python3 fuzz/run_parforparity.py`, plus read the diagnostic by hand —
    the runner cannot tell these two rejections apart, which is how this hid.

- [ ] **Phase 40** — **at `-O3` gcc already folds the three-clause form's bounds
      check, so the compiler-side elision restored in phase 27 buys ~0 on the
      level `tychoc` actually ships.** Measured in phase 27, not supposed:
      `src/tychoc.c:12695` hands `cc` `-O3`, and at `-O3` a scan loop runs
      207ms elided vs 208ms checked (1.00x); the same program is 1.14x at `-O2`
      and 1.88x at `-O1`. The cause is structural rather than a compiler
      accident: `S_FOR3` emits `h_i < ((h_xs).len)` into the C `while` header
      (`src/tychoc.c:10755`), which is the exact fact `tycho_arr_int_get`'s own
      `i >= xs.len` test needs, so VRP kills it. Four shapes were tried looking
      for one gcc could not fold — flat scan, in-place write, nested cross
      product, `inout [int]` parameter — and none separated.
  - So: does `for3_elidable_arr` (`src/tychoc.c:7955-8005`) earn its risk? It is
    ~50 lines whose failure mode is a memory-safety bug, buying a difference no
    shipped build can measure. The honest options are (a) keep it for `-O0`/`-O1`
    debug builds and say so in its header, (b) delete it and let gcc do the job,
    documenting that the three-clause form is *why* that became possible — the
    old `S_FORRANGE` spelling cached `_stop` (`src/tychoc.c:10842`) and broke the
    link to `len`, which is exactly why the elision was written in the first
    place. Do NOT decide this from the numbers above alone: they are one machine,
    one gcc. Re-measure on a second toolchain (clang, and a non-x86 target if one
    is reachable) before removing anything.
  - Note whichever way it goes, `bench/guard.sh:41-71` and
    `tests/bounds_noelide.ty` stay useful: they assert the emitted C, not wall
    time, so they document the decision either way.

## Out of scope

- **The two concurrency items in `FRICTION.md`** — no storable task handles, no
  way to hand a connection to whichever worker is free. They want a type-system
  answer first.
- **Unfreezing `compiler/tychoc0.ty` into maintenance.** Phase 1 retires the
  lanes that check it; it does not bring it back into the language's evolution.
  That was offered and not chosen.
- **`for x in xs:` and `for C:`.** Both stay exactly as they are.
