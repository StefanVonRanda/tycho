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
  (`/home/igzo/github/tycho/docs/spec/appendix-e-conformance.md:165`,`:185`) but
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

- [ ] **Phase 5 — `parallel for i in 0..<N:`**
  - Scope: parser, the `parallel for` path (`src/tychoc.c` around the
    `S_FORRANGE` `parallel` flag at `:1558`), codegen; fixtures in `tests/conc/`.
  - `0..<N` is legal **only** in `parallel for` (see Pre-flight). Sequential `for
    i in 0..<N:` is refused, with a diagnostic naming the three-clause form.
  - The existing `parallel for x in xs:` foreach form is untouched.
  - Done when: `parallel for i in 0..<N:` runs its body over `0..N-1` across
    threads, the sequential refusal has a `tests/reject/` fixture with its
    diagnostic asserted, and `tests/conc/` fixtures pass native, ASan and TSan.
  - Verify: `make conc`, then `make test`.

- [ ] **Phase 6 — rewrite all 549 `range()` sites**
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
  - Verify: `make test`, then `make conc`, then `make corelib`, then
    `make ci`. This is the second phase that earns a full sweep, because it
    touches every area of the tree at once.

- [ ] **Phase 7 — delete the counting form and the `range` builtin**
  - Scope: parser, typechecker, codegen, builtin table; `tests/reject/` fixture
    asserting `range` is no longer a known name.
  - Done when: `for i in range(3):` is refused with a diagnostic that names the
    three-clause form as the replacement, `range` is gone from the builtin table,
    and `make test` is green.
  - Verify: `make test`, then `python3 scripts/check_citations.py`.

- [ ] **Phase 8 — `tychofmt`, the LSP, and the editor grammars learn `;` and `..<`**
  - Scope: `tools/tychofmt.ty`, `tools/lsp.ty`, `editors/vscode/`,
    `editors/zed/`. Each carries its own lexer or grammar.
  - Note that with phase 1 done, `tools/*.ty` is no longer constrained by a
    frozen compiler, so unlike every previous tooling phase these files **may**
    use the new syntax themselves.
  - Done when: `tychofmt` round-trips a three-clause loop and a bare `for:`
    unchanged; the LSP tokenises `;` and `..<` without desynchronising; both
    editor grammars highlight them; `scripts/editors_check.sh` green.
  - Verify: `sh scripts/tools_check.sh`, then `make editors-check`.

- [ ] **Phase 9 — the spec**
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

- [ ] **Phase 10 — the full sweep**
  - `make ci`, once. Report per-lane whether the new loop forms are actually
    exercised — the previous plan's phase 4 found the fuzz and TSan lanes
    provably never reached its new code, and `fuzz/gen.py` will have the same
    blind spot here unless it generates loops.
  - Done when: `CI GREEN`, exit 0.

## Carried forward

Unclosed discoveries from the two previous plans; none blocking.

- [ ] **Phase 12** — `editors/zed/README.md`'s corpus count is hand-typed and
      unguarded; `scripts/editors_check.sh` already computes it.
- [ ] **Phase 13** — an anchored form for source→source citations; phase 8 of the
      first plan proved its bounds check catches none of the wrong-line class.
- [ ] **Phase 14** — a `> Provenance:` block naming no path escapes the mandatory
      anchor rule by accident; 8 stale refs in `docs/spec/02-grammar.md:272-274`.
      **Note phase 9 of this plan edits that file** — worth doing together.
- [ ] **Phase 15** — `docs/corelib.md` does not exist (moved to
      `docs/guides/corelib.md` by `68e5b39`); a dead backticked path in prose.
- [ ] **Phase 16** — `char` has arithmetic but no spellable type name, no
      `to_char`, and no `\xNN` escape.
- [ ] **Phase 17** — ~344 bare `src/tychoc.c:N` refs shifted by the last plan and
      were deliberately not swept; same class as the dropped phase 9.
- [ ] **Phase 18** — `docs/internals/spec-plan.md:605` cites
      `appendix-e-conformance.md:187` for a §9.5 claim; that line is the §24.2 row.
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

- [ ] **Phase 24** — **`docs/spec/01-lexical.md` is missing from phase 9's
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

- [ ] **Phase 19** — no fuzz lane and no concurrency lane reaches element-wise
      array arithmetic (0/177 and 0/11); `fuzz/gen.py` has no generator for
      binary arithmetic over typed operands. **Phase 10 of this plan will hit the
      same wall for loops.**

## Out of scope

- **The two concurrency items in `FRICTION.md`** — no storable task handles, no
  way to hand a connection to whichever worker is free. They want a type-system
  answer first.
- **Unfreezing `compiler/tychoc0.ty` into maintenance.** Phase 1 retires the
  lanes that check it; it does not bring it back into the language's evolution.
  That was offered and not chosen.
- **`for x in xs:` and `for C:`.** Both stay exactly as they are.
