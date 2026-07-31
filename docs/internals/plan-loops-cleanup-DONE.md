# Odin-style loops: three-clause `for`, bare `for:`, and the end of `range()`

Previous plan complete and archived at
[plan-array-arith-DONE.md](plan-array-arith-DONE.md)
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
touched. The foreach form `for x in xs:` (`docs/spec/10-statements.md:98`) is likewise untouched.

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

**20 unchecked phases remain** in "Carried forward", none blocking. Two of them
are this plan's own follow-ups (27 bounds-check elision, 30 the dead `r_step`)
and are ordered: 27 before 30. The count was 24 until batch 2 closed 18, 38 and
41 and ticked phase 28, whose own last bullet had said "Closed by phase 7" while
its box stayed unticked — the malformed record phase 38 repaired. **Phase 17 is
deliberately still open**: batch 2 swept the live normative documents and left
the design records; its entry says exactly what remains.

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
  at `tests/run.sh:135-144`. The fixtures are now scored
  by the main golden loop at `tests/run.sh:113-118` and
  the abort loop at `tests/run.sh:194-211` — same
  native-vs-ASan + golden discipline, same abort contract, no new code.

  **`/home/igzo/github/tycho/.gitignore` — the general case confirmed before
  removing anything.** `!/tests/*.out` at
  `.gitignore:94` already un-ignores the broad `*.out`
  rule at `:89` for exactly `tests/<name>.out`, which is where the six goldens
  now live; it is where they came from before the lane existed. The
  `!/tests/postfreeze/*.out` exception was therefore redundant, not load-bearing,
  and was replaced by a HISTORY note. `tests/abort/` has no golden, so no
  exception was ever needed there.

  **`/home/igzo/github/tycho/scripts/asan_self.sh` — the glob change is not
  neutral, and this is the one thing the phase brief left to judgement.**
  `tests/postfreeze/*.ty` came out of the corpus line (now
  `scripts/asan_self.sh:146-148`) and the six fixtures
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
  and at `scripts/asan_self.sh:110-111` after — the ref
  was already 15 lines off and the bounds check cannot see it (the exact class
  carried forward as phase 13). It is corrected to `:110-111` because the phase
  rewrote the sentence around it anyway.

  **The rule applied to frozen records, stated because the brief asked.**
  *Prose in `docs/internals/plan-*-DONE.md` was not touched at all.* Those files
  describe a directory that existed when they were written; that is correct
  history, and rewriting them would make them lie about what was done. **No
  citation repair was needed either**, and the reason is mechanical rather than
  lucky: `scripts/check_citations.py:250` skips any cited
  path that does not start with `SRC_PREFIX`
  (`scripts/check_citations.py:177-179`), and both
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
  by this phase in `.gitignore:97`,
  `tests/run.sh:135-136`,
  `scripts/asan_self.sh:76`,`:80`,
  `tests/nested_pattern.ty:3`,
  `tests/rawstring.ty:3` and
  `tests/abort/array_arith_len.ty:9`. Three live spec
  passages also still name it — `docs/spec/12-aggregates.md:287`,
  `docs/spec/appendix-e-conformance.md:48`,`:263`,`:331-333`
  — each rewritten to past tense with the fold recorded, because §E.2's whole
  subject is *why fixtures sit where they sit* and deleting the history would
  leave the amendments unexplained. **No hit anywhere points at a path that is
  expected to exist.**

  **Out of scope, deliberately left.** The two spec fixture tables were repointed
  (`docs/spec/appendix-e-conformance.md:166`,`:185`) but
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
  builds its citer set from `git ls-files` (`scripts/check_citations.py:287-289`).
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
  longer what happens. Repointed by hand to src/tychoc.c:3386@"range" (the surviving
  lexeme test; de-backticked — the anchor has since drifted) and the §3.7 bullet reworded to say the lexeme is recognised
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
    by ~400 lines from phases 3–5 (`src/tychoc.c:3191-3277`→`:3245-3446`,
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
    returns exactly two hits, `docs/spec/16-builtins.md:131` ("the byte range `[a, b)`", about `substr`)
    and `docs/spec/16-builtins.md:140` ("every in-range `i`"), neither of which is the counting form.
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
  (`scripts/check_citations.py:238-239`) and only polices refs whose path starts
  with `SRC_PREFIX` (`scripts/check_citations.py:250-251`), so a `> Provenance:` paragraph that
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
  `docs/reference/arrays-slices.md:116`, `docs/internals/value-semantics-limits.md:67`,
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
  | `docs/spec/appendix-a-grammar.md` | +4 at `docs/spec/appendix-e-conformance.md:136` | none — every inbound ref is `docs/spec/appendix-e-conformance.md:21`–`docs/spec/appendix-e-conformance.md:98`, all above the hunk |
  | `docs/reference/basics.md` | +12 at `docs/spec/appendix-e-conformance.md:117` | none — the only inbound ref is `docs/spec/appendix-e-conformance.md:24-70` |

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

- [x] **Phase 12** — `editors/zed/README.md`'s corpus count is hand-typed and
      unguarded; `scripts/editors_check.sh` already computes it.
      **CLOSED by batch 3** — the count was 813 in the README and 829 in the
      tree; a README lane now recomputes it and fails on disagreement.
- [x] **Phase 13** — an anchored form for source→source citations; phase 8 of the
      first plan proved its bounds check catches none of the wrong-line class.
      **CLOSED by batch 1** — evidence under "Batch 1 evidence" below.
- [x] **Phase 14** — a `> Provenance:` block naming no path escapes the mandatory
      anchor rule by accident; 8 stale refs in `docs/spec/02-grammar.md:272-274`.
      **Note phase 9 of this plan edits that file** — worth doing together.
      **CLOSED — folded into phase 9**, which repaired all eight with full paths
      and anchors and wrote the reason for the escape into the block. The
      *general* gate hardening (make the anchor rule fire on a pathless
      Provenance block) was **not** done and is filed as phase 34.
- [x] **Phase 15** — `docs/corelib.md` does not exist (moved to
      `docs/guides/corelib.md` by `68e5b39`); a dead backticked path in prose.
      **CLOSED by batch 3, and it was not one reference but 31.** 26 in
      `docs/spec/18-library.md`, 3 in `docs/spec/appendix-h-differences.md`, 2 in
      `docs/internals/spec-plan.md`; 5 more in frozen `-DONE.md` archives, left
      alone. **17 of the 25 ranged ones had also drifted in line number**, so a
      path-only rewrite would have manufactured 17 false citations — see the
      batch 3 evidence for how each was re-derived.
- [x] **Phase 16** — `char` has arithmetic but no spellable type name, no
      `to_char`, and no `\xNN` escape.
      **CLOSED by batch 6 as "this needs its own plan", not as a fix.** All four
      claims re-verified against the built compiler (commands and output in the
      batch 6 evidence); the scope decision is that they are three separate
      *language* changes, not cleanup:
  - **A spellable type name** is a parser change with two distinct failure sites,
    not one: in a parameter or return position `fn f(c: char)` dies
    `unknown type 'char'` (`src/tychoc.c:2141`), while in an element position
    `cs: []char` dies with the *element* list instead —
    `expected a type (int, float, bool, string, [int], or a struct)`. Adding the
    name means adding `char` to both, and then deciding what `[]char` *is*: the
    tree already has `bytes` for a byte sequence, and `T_CHAR` is `tycho_int` in
    C (`src/tychoc.c:1361`), so `[]char` and `bytes` would overlap. That is a
    language design question with a spec section attached (§12, §16), not a
    line to add to a switch.
  - **`to_char`** is a new builtin (`unknown procedure 'to_char'`): a `Sig` row
    beside `char_at` (`src/tychoc.c:4526`), a codegen case, its own bounds
    contract for an out-of-range int, spec text and fixtures.
  - **`\xNN`** is a lexer change in two literal forms — the char escape table
    (`src/tychoc.c:464`, which today rejects it with
    *"unsupported char escape (use \n \t \r \0 \\ \'"*) and the string one
    (`src/tychoc.c:384`) — plus a decision about what `\xNN` means above 0x7F in
    a string that claims to be UTF-8.
  - **Why it is not this batch's:** every other batch-6 item is a fix or a
    recorded decision inside existing behaviour. This one *adds language*, and
    the cleanup batches were explicitly scoped to the leftovers of the
    loop-syntax plan. Filed as **phase 51** below with what a real plan for it
    would have to settle. The testability complaint that motivated the phase
    stands and is recorded there: `char` is the one element type whose narrower
    operator set (`+`/`-` only, `src/tychoc.c:1029`) is also the hardest to
    write a fixture for.
- [x] **Phase 17** — **PARTIAL after batch 2.** The population is not ~344: a
      re-derivation against the repaired gate found **568 live** bare
      `src/tychoc.c:N` refs (plus 621 more inside the frozen
      `docs/internals/plan-*-DONE.md` archives, which stay untouched). Batch 2
      swept the **227** in `docs/spec/*.md` and `FRICTION.md` — the set phases 6
      and 11 established as the live, repointable one — reading every citation
      against the line it names. **What remains, and why it was not done:**
  - **167 refs in `docs/internals/*.md` (non-archived) and `docs/rfc/*.md`** —
    `generics-stage2-body-cloning.md` (52), `generics-gap-fixes-plan.md` (44),
    `ffi-threading-design-review.md` (26), `frontend-restriction-audit-2026-07-25.md`
    (14), `value-lifetime-regions.md` (9) and nine smaller files. Every prior
    repointing phase (`782af20` phase 6, `1b772c6` phase 11) touched
    `docs/spec/*`, `FRICTION.md` and `plan.md` and **never** these — they are
    dated design studies and audits, records of the tree as it was, and the
    archive rule's reasoning applies to them even though the gate's `ARCHIVED`
    prefix does not. Repointing them is a decision to make explicitly, not a
    side effect of a sweep.
  - **38 refs inside `plan.md`'s own completed-phase evidence blocks** (phases 3,
    4, 5, 6, 7, 10, 27). Same reason, more sharply: an evidence block records what
    a phase verified *at its commit*. Renumbering it makes the record claim
    something the phase never checked. Only three sit in still-open entries
    (phases 26 and 30) and the Status paragraph.
  - Done when: someone decides, in writing, whether design records get repointed
    or annotated-and-frozen, then applies that decision to the 167 — and repairs
    the three live-entry refs in `plan.md` either way.
  - **BATCH 10 MADE THE DECISION AND CLOSED THIS BOX. The decision is: do NOT
    repoint the design records.** A dated study — `frontend-restriction-audit-2026-07-25.md`
    names its date in its own filename — is a photograph of the tree on that day.
    Repointing its citations produces a document whose prose is dated and whose
    citations are current, and a reader has no way to tell that the two halves
    disagree. A stale ref in a dated record is legible (the title dates it); a
    fresh ref in a dated record is a lie the reader cannot detect. That is
    strictly worse, so the answer is annotate-and-freeze, and the annotation is
    this paragraph plus the `FRICTION.md` entry it is retired into.
  - **Re-derived against the tree at `b5c8406`, because batch 2's split is eight
    batches old.** 1457 refs name `src/tychoc.c`: 660 archived, 797 live. The
    design-record class is now **127** — 90 in non-archived `docs/internals/*.md`,
    37 in `docs/rfc/*.md` — not 167. `plan.md` holds 240, and batch 2's "three in
    still-open entries (phases 26 and 30)" is **moot**: both phases are `[x]` as
    of a later batch, so those refs are completed-phase evidence like the rest.
  - **What sweeping would and would not buy, measured rather than argued.** All
    **139 anchored** refs to `src/tychoc.c` in the tree contain the token they
    name — checked here, 0 bad. So the anchored population is already correct and
    a sweep would not move it. The other 1318 are bare, which is bounds-only: the
    gate cannot tell a bare ref that drifted onto a plausible line from one that
    did not, so "sweeping" them means re-verifying each by hand and anchoring it.
    That is 1318 hand-verified citations, and the phase-44 sweep in this batch is
    what a 42-ref version of that costs. It is not a phase, it is a project.
  - **Retired to `FRICTION.md`** ("The bare `src/tychoc.c:N` citation population")
    as recorded-not-actioned, with the number, the reason, and the only mechanism
    that would actually fix it. The box is ticked on the decision, not on a sweep.
- [x] **Phase 18** — `docs/internals/spec-plan.md:605` cites
      `appendix-e-conformance.md:188` for a §9.5 claim; that line is the §24.2 row.
- [x] **Phase 20** — `examples/fetch/run.sh` is red, and was red **before** this
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
      **CLOSED by batch 5 — but (b) above is WRONG and that is the finding.** The
      hash is body-derived, not path-derived; the real fault was a stale golden.
      Golden re-recorded, runner green. Details in the batch 5 evidence.

- [x] **Phase 21** — the freeze no longer constrains where fixtures live, and
      several files still say it does. `corelib/test/result/main.ty`,
      `examples/corelib/httpd/main.ty` and the `§E.2` rationales in
      `docs/spec/appendix-e-conformance.md` place fixtures outside `tests/`
      *because the frozen compiler would refuse them* — nested patterns, `\r`
      escapes, adjacent string literals, `Result` in a tuple literal. With no lane
      building `tychoc0` that constraint is void and those fixtures can come home.
      Phase 1 annotated the claims in place rather than moving anything; phase 2
      already folds `tests/postfreeze/` back and is the natural place to widen.
      **CLOSED by batch 5.** The fold-back closed only part of it. Nothing was
      *moved* — the answer is that the placements were over-determined and the
      freeze cost the `tests/` witness, not the corelib one. Two new fixtures
      restore it. Details in the batch 5 evidence.

- [x] **Phase 22** — `fuzz/run_typeparity.py` lost its oracle, not just its second
      opinion. Unlike `run_eqparity.py` / `run_unaryparity.py` / `run_parforparity.py`,
      which carry a written-down `expect` table, its only assertion *was* `tychoc ==
      tychoc0`. What survives is an exhaustive fail-closed sweep (no crash; every
      accept emits compilable C) over the scalar binop matrix, which no longer
      catches a changed type *rule*. Adding an `expect` table in the style of
      `fuzz/run_eqparity.py` would restore a real oracle. Same shape, smaller:
      `tests/rtparity/run.py` could become a single-runtime lane asserting the C
      emitted for `tests/rtparity/surface.ty` still contains each expected
      `getenv()` name, trap text and stats row against a recorded list.
      **CLOSED by batch 4** — the `expect` oracle was chosen over retirement and
      over a property check; 4608/4608 cases now match it, 640 accept / 3968
      reject. Justification and the negative control are in the batch 4 evidence.
      The `tests/rtparity/run.py` half of this entry was **not** done and is
      filed as phase 46.

- [x] **Phase 23** — **an absolute path in a citation is silently unchecked.**
      Found by phase 2. `scripts/check_citations.py:250`
      skips any cited path not starting with `SRC_PREFIX`
      (`scripts/check_citations.py:177-179`, all
      relative: `src/`, `tests/`, …), so a ref written as an absolute path —
      the repo root spelled out in full, then tests/foo.ty and a line number —
      matched nothing and was
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
      discipline phase 3 followed — and it lists `...` at `docs/spec/01-lexical.md:150` while listing
      neither `;` nor `..<`. Worse, `docs/spec/01-lexical.md:170` states in
      plain words: **"There is no range operator (`..`); ranges are written
      with the"** `range()` builtin — a sentence this plan makes false twice
      over (phase 5 adds `..<`, phase 7 deletes `range`). Phase 9's scope names
      `02-grammar.md`, `appendix-a-grammar.md`, `10-statements.md`,
      `13-concurrency.md`, `16-builtins.md`, `appendix-b-keywords.md` and
      `appendix-e-conformance.md` — not this one. Fold it into phase 9 rather
      than running it separately; the two new rows and the corrected sentence
      belong in the same commit as the rest of the spec.

- [x] **Phase 25** — **`--emit-c` with no `-o` drops an untracked `.c` inside the
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
      **CLOSED by batch 6 — the default output path, not the ignore rule.**
      The `.gitignore` route was checked and rejected on evidence: **31
      directories hold both `.ty` sources and hand-written, tracked `.c` shims**
      (`corelib/http`, `corelib/image`, `bench/*`, `examples/life`, `tests/ffi`,
      `tools`, …), so any by-pattern ignore broad enough to catch the strays
      would also silently swallow a newly added shim — a worse failure than the
      one it fixes, and one no gate would catch. `--emit-c` with no `-o` now
      writes the C to **stdout** (`src/tychoc.c:12708`), which is what the
      phase's own repro (`> /tmp/x.c`) already expected; `-o` behaviour is byte
      for byte unchanged, and the `wrote <path>` status line survives only on
      the `-o` path. `README.md:190` and `:220` say so.
      **One in-tree caller did NOT pass `-o`, and the gate found it — this is
      the interesting part.** The claim "every caller passes `-o`" was written
      from a `grep` and was **wrong**: the bytes-rehome lane in
      `scripts/tools_check.sh:283` emitted with no `-o` and grepped the sibling
      `$TMP/brh/main.c`. With the C going to stdout and the lane's `>/dev/null`
      swallowing it, `sh scripts/tools_check.sh` failed with exactly the message
      its own header (`scripts/tools_check.sh:275`) predicts for a rotted
      fixture: `grep: .../brh/main.c: No such file or directory` →
      *"bytes field NOT re-homed -- copy_into missing T_BYTES (dangling UAF!)"*.
      That lane was written to fail loudly rather than silently assert nothing,
      and it did. Fixed by giving it an explicit `-o`; re-run green. Everything
      else — the six `fuzz/run*.py` runners, `scripts/entrypoints.sh:63`,
      `bench/guard.sh:28` and `:63`, and the `examples/sqlite`,
      `examples/fetch`, `examples/site` sanitizer legs — was already explicit.
      **Out of scope, filed as phase 52:** the *default* (non-`--emit-c`) build
      leaves `<base>.c` beside the source too (`src/tychoc.c:12757` hands it to
      `cc` and nothing removes it) — same stray-file class, different flag, and
      phase 25 named only `--emit-c`.

- [x] **Phase 26** — **the `parallel for` gate diagnostic will be wrong the
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
      **TICKED by batch 5 — nothing remained; both halves had already landed and
      only the box was open.** Verified by blame, not by assumption. Details in
      the batch 5 evidence.

- [x] **Phase 19** — no fuzz lane and no concurrency lane reaches element-wise
      array arithmetic (0/177 and 0/11); `fuzz/gen.py` has no generator for
      binary arithmetic over typed operands. **Phase 10 of this plan will hit the
      same wall for loops.**
      **CLOSED by batch 4.** Re-measured against the current `fuzz/gen.py` first:
      still **0 of 200** generated programs and **0 of 12** `tests/conc` fixtures.
      Now 67/200 and 1/13. Numbers and commands in the batch 4 evidence.

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

- [x] **Phase 28** — **three `range()`-only fixtures were deleted in phase 6 and
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
  - The permanent half is already owned elsewhere: the zero-step guarantee has no
    successor in either new form and **phase 9** must state that in the spec as a
    deliberate trade. This phase is only about the fixtures.
  - Done when: `plan.md` records which of the two options was taken and why, and
    if restored, `make test` and `make conc` return to 543 and 38 until phase 7
    moves them again.
  - **Closed by phase 7**: the option taken was *retire early on purpose*.
    Nothing was restored; the per-fixture reasoning and the two replacement
    fixtures are in phase 7's evidence.

- [x] **Phase 29** — **the LSP's semantic-token classifier is missing three
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
  - **CLOSED by batch 6 — the three were added, and the "check both directions"
    caveat was checked and cleared.** `sem_is_keyword` (`tools/lsp.ty:1175`) now
    lists `parallel`, `select` and `or_return`. The other direction found **no**
    drift: `handle` is a hard lexer token (`src/tychoc.c:187`), and `const`,
    `sink`, `where` and `soa` are soft keywords — `TK_IDENT` compared by text in
    the parser (`src/tychoc.c:3116`, `:3661`, `:3695`, `:1911`) — which is a
    lexer implementation detail, not evidence they stopped being keywords, and
    both editor grammars highlight them. So nothing was removed. The reason is
    written into the function's header so the next reader does not re-derive it.

- [x] **Phase 30** — **`Stmt.r_step` is dead and its three guards are
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
  - **CLOSED by batch 6 as "the guards stay, annotated" — and the stated blocker
    was wrong, so here is the real one.** Batch 6 re-checked phase 7's reasoning
    against what phase 27 actually built, as its brief required:
  - **Phase 7's blocker does not hold.** It said phase 27's elision recogniser
    is specified against `s->r_step == NULL`. Phase 27's recogniser is
    `for3_elidable_arr` (`src/tychoc.c:8023-8048`) and it **never mentions
    `r_step`** — it matches S_FOR3's init/cond/post triple, which has no step
    field at all. The recogniser that *does* test `s->r_step == NULL` is the
    older **S_FORRANGE** one (`src/tychoc.c:10902`), and `git blame` puts it in
    the pre-plan commit *"hierc bounds-check elision for monotone loop indices"*,
    not in phase 27. So "must not land before phase 27" was never the reason;
    phase 27 landing changes nothing about this phase.
  - **The real blocker is this phase's own "Done when", which is unsatisfiable.**
    It requires that every foreach loop and `parallel for` "still emit the same
    C as before". They cannot: `r_step` is not only an AST field, it is *emitted
    C text*. A two-element foreach emits
    `tycho_int _stop1 = ((h__fc0).len), _step1 = 1L;`, then
    `if (_step1 == 0) { ... "tycho: range step is zero" ... }`, then
    `for (... ; _step1 > 0 ? h__fi0 < _stop1 : h__fi0 > _stop1; h__fi0 += _step1)`
    (`src/tychoc.c:10885-10889`; the emitted text is in the batch 6 evidence).
    Any honest deletion removes all three lines from **every** S_FORRANGE loop in
    the corpus. The binary is unchanged — gcc folds a constant `1L` — but the C
    is not, so the acceptance test as written fails by construction. A phase
    cannot be verified against a criterion it must violate.
  - **And the cost is real.** `src/tychoc.c` carries **485 live `path:N`
    citations at lines ≥ 1553** (plus 515 in the frozen archives). A deletion
    spread from the field declaration to the codegen shifts essentially all of
    them; only the anchored subset reddens the gate, so the rest would rot
    silently — which is exactly the population phase 17 is still open on.
  - **Decision: the guards stay, and they stay annotated.** They are already
    marked unreachable at `src/tychoc.c:1555-1559`, `:6668-6670` and
    `:7310-7316`, which is what a reader needs. The deletion is worth doing, but
    as its own commit with a **corrected** Done-when — "the emitted C loses the
    `_stopN`/`_stepN` pair and the zero-step abort, every fixture's *behaviour*
    is unchanged, `make test` and `make conc` hold at their counts" — and after
    phase 17 settles what happens to the bare `src/tychoc.c:N` population. Filed
    that way as **phase 53**.

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

- [x] **Phase 32** — **the zed grammar still lists `range` as a builtin.**
      **CLOSED by batch 3 — and zed was NOT alone.** The VS Code grammar spelled
      it in a regex alternation
      (`editors/vscode/syntaxes/tycho.tmLanguage.json:47`), exactly the form the
      entry below predicted phase 7's quoted-string grep would miss.
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

- [x] **Phase 33** — **no gate compiles the unexecuted `tycho` fences in `docs/`,
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
  - **BATCH 3 BUILT THE GATE. The box stays unchecked because the coverage is
    partial, and the honest number is small.** `scripts/docs_fences.sh` +
    `make docs-fences` now runs `tychoc --emit-c` over the ```` ```tycho ````
    fences in tracked `docs/*.md` and is a step in `scripts/ci.sh`. The
    `arrays-structs.md` snippet is covered (its fence was **bare** ```` ``` ````,
    not tagged `tycho` — retagging it is what opted it in) and the gate reddens
    on the original `1_000_000` with `error: expected ';' after the condition`.
  - **What is checked: 10 fences.** Of the 40 ```` ```tycho ```` fences in
    `docs/`: 10 CHECKed, 19 FRAGMENT (no `fn` at all), 6 MARKED
    `<!-- fence-skip: … -->` with a printed reason, 5 FROZEN (in
    `docs/internals/plan-*-DONE.md`, the same exemption as
    `scripts/check_citations.py:474@ARCHIVED`).
  - **What remains unchecked, precisely:**
    1. **~155 fences opened with a bare ```` ``` ```` and no language tag**, of
       which an unknown subset is Tycho. `docs/reference/` has **48** and
       `docs/guides/` **33** — and `docs/reference/` and `docs/tutorial.md`
       contain **zero** `tycho`-tagged fences, so the original claim above that
       they are "never parsed by anything" is right while the implied reason is
       wrong: they are not untagged-and-skipped, they are untagged entirely.
       Nothing can distinguish shell from C from Tycho in them. Tagging one
       `tycho` opts it in; that is the intended growth path, one reviewed fence
       at a time, and it is filed as **phase 41**.
    2. **The 19 FRAGMENT and 6 MARKED fences are not compiled at all.** A
       fragment can still be wrong. The gate proves only that what claims to be a
       whole program is one.
    3. **Nothing is run.** Output correctness stays `scripts/spec_examples.sh`'s
       9 pairs.
    4. The 3 MARKED fences in `docs/internals/generics-stage2-body-cloning.md`
       document programs the compiler **must reject**. They are skipped, not
       asserted-red — a negative lane would be strictly better and is not built.
  - Close this box when (1) is resolved, or re-scope it to say the bare-fence
    population is permanently out of reach and close it then.
  - **BATCH 10 CLOSED THIS BOX.** (1) is resolved for every population it named:
    `docs/reference/` (48), `docs/guides/` (33) and `docs/tutorial.md` (10) now
    carry a language tag on every fence, and so do `docs/spec/` and the rest of
    the reader-facing tree — 90 fences tagged in phase 43, below. **The gate went
    from 40 `tycho` fences / 10 CHECKed to 119 / 39.** What is left is 64 bare
    fences in `docs/internals/` (56), `docs/rfc/` (4) and `docs/` (4) — a
    different and much smaller population, mostly inside the same dated design
    records phase 17 decided not to touch, and it is filed as **phase 61** rather
    than held against this box.
  - **Two things were fixed in the gate itself, and one of them was a real bug.**
    (a) A **no-main retry**: a fence that declares whole `fn`s but no `main` used
    to be un-checkable, because `--emit-c` needs an entry point. It is now
    retried with an *empty* `fn main()` appended, which typechecks exactly the
    declarations the document contains and invents nothing — 6 fences moved from
    skip to checked. This is not the synthetic-main wrapper the bullet above
    rejects: that one would have invented a body for loose statements. (b) The
    carved temp files were named `f_<closing-line>_<n>.ty` with `n` restarting in
    every document, so **two documents closing their first fence on the same line
    collided and one silently overwrote the other** — the gate then compiled one
    document's fence while printing the other's path. It was live at `b5c8406`
    (`docs/spec/12-aggregates.md` vs `docs/spec/15-program.md`, both at `:43`),
    so one of those two was never actually compiled by batch 3's gate; at 119
    fences there were 8 collisions. The document path is now part of the name.
  - **Items 2, 3 and 4 of "what remains unchecked" are unchanged and still true**:
    the 58 FRAGMENT and 17 MARKED fences are not compiled, nothing is run, and
    the three `generics-stage2-body-cloning.md` must-reject fences are skipped
    rather than asserted-red. A negative lane is still not built.

- [x] **Phase 34** — **the pathless-`> Provenance:` gate hole is still open in
      the tool, only closed in the one file phase 14 named.** Phase 9 repaired
      `docs/spec/02-grammar.md`'s eight refs by hand, but
      `scripts/check_citations.py` still cannot see the class: `cur` is reset at
      every blank line (`scripts/check_citations.py:238-239`) and a ref whose
      inherited path is `None` is `continue`d before the anchor rule runs
      (`scripts/check_citations.py:250-251`). So any future `> Provenance:` block
      that opens a paragraph without naming a path gets **zero** checking —
      no bounds check, no anchor requirement.
  - The fix is not "carry `cur` across paragraphs" — the comment at
    `scripts/check_citations.py:235-237` explains why that was deliberately
    removed. It is to make a `> Provenance:` block that contains a `:N` ref and
    names no path a **hard failure in its own right**: fail closed, with a
    message telling the author to write the path.
  - Done when: a pathless Provenance ref reddens the gate, the whole tree is
    swept for the class, and `python3 scripts/check_citations.py` is green.
  - Verify: `python3 scripts/check_citations.py`.

- [x] **Phase 35** — **two `for i in range(len(A)):` sites survive outside
      phase 31's scope.** **CLOSED by batch 3 — only one of the two was still
      open.** Phase 27 had already rewritten `tests/bounds_elision.ty:5` to the
      three-clause form when it restored elision; the file's only remaining
      mention (`tests/bounds_elision.ty:11`) is HISTORY prose recording the old
      spelling, which is true and stays. So no `.ty` file changed and `make test`
      was not required. `bench/prongB/RESULTS.md` was the real work.
      *(original entry follows)*
- **Phase 35 (original)** — **two `for i in range(len(A)):` sites survive outside
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

- [x] **Phase 36** — **nothing generates the bare `for:` form, and it lives in a
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
  - **CLOSED by batch 4** — 69/200 seeds contain a bare `for:` (was 0/200),
    `python3 fuzz/run.py 200` is `ok=200 skip=0 timeout=0 FAIL=0`, and
    `tests/conc/bare_for_arrarith.ty` is the second fixture in the tree to use
    the form. The termination argument for a *generated* `for:` is in the batch 4
    evidence and in the `bare_loop` comment in `fuzz/gen.py`.

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

- [x] **Phase 38** — **`plan.md`'s own record is malformed around phases 28-29,
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

- [x] **Phase 39** — **`parallel for` refuses an in-place mutation of a captured
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
  - **CLOSED by batch 6 — the hole was closed, not argued away.** `pf_scan_expr`
    (`src/tychoc.c:6456-6466`) now recognises an in-place mutating builtin
    applied to a captured root and issues the same message its S_INDEXSET /
    S_FIELDSET sibling does (`src/tychoc.c:6549`). `push`/`pop` is the pair the
    tree already treats as mutating argument 0 — the while-loop mutation scan
    uses exactly that test (`src/tychoc.c:6830`), so the gate now agrees with an
    analysis that already existed rather than inventing a second list. The root
    is walked through `.f`/`[i]`/`.0` the same way S_INDEXSET walks its target,
    so `push(p.xs, i)` is caught too.
  - **Why close it rather than lean on the borrow checker:** the borrow rule is
    in another pass, on a *different* program (the lifted chunk proc), and its
    message names a parameter the user never wrote. Leaving it as the only
    thing standing meant this gate's coverage depended on a rule nobody would
    think to preserve — the phase's own "remove the borrow rule and the fixture
    goes green the wrong way".
  - Verified by hand, both messages read (output in the batch 6 evidence): the
    `push_capture` and `indexset_capture` fixture bodies now produce the
    identical `parallel for cannot mutate captured variable 'xs' in place`, and
    a **chunk-local** `push(ys, i)` where `ys` is declared inside the loop still
    compiles — the gate keys on `pf_local`, so it did not become a blanket ban
    on `push` in a parallel body.

- [x] **Phase 40** — **at `-O3` gcc already folds the three-clause form's bounds
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
  - **CLOSED by batch 6 — recorded in `for3_elidable_arr`'s own header, and
    option (a) taken.** Where the finding goes was the actual question, and half
    of it was already answered: `bench/guard.sh:49-62` **already** carries the
    measurement (the -O2 1.14x / -O1 1.88x pair, the VRP explanation, and the
    counted `.data[h_i]` vs `tycho_arr_int_get(h_` assertion). That is the right
    home for the reader asking *"why does this lane assert C text instead of a
    ratio?"* — and phase 27 put it there when it wrote the lane.
  - **The reader it did not serve is the one standing in front of the risk.**
    Someone editing `for3_elidable_arr` is looking at ~50 lines whose failure
    mode is a memory-safety bug; nothing at that site said what they buy. The
    finding now leads that function's header (`src/tychoc.c:8006-8022`), with
    the numbers, the four shapes that failed to separate, the pointer to
    `bench/guard.sh:49-62`, and the historical note that the old `S_FORRANGE`
    spelling cached `_stop` (`src/tychoc.c:10885`) and broke the `len` link —
    which is why the elision had to be hand-written at all. Not the spec: this
    is a codegen implementation trade-off with no observable language semantics,
    and the spec describes the language. Not a new `docs/internals/` file
    either: a file nobody opens is where a finding goes to die, and this one has
    exactly two audiences, both already in code that exists.
  - **Option (a), keep it, and the header says why:** it is the only thing that
    elides at `-O0`/`-O1`, which is what `tychoc -g` builds
    (`src/tychoc.c:12754`) and what a debugger actually steps. Option (b),
    deletion, stays open and is explicitly gated on re-measuring with a second
    toolchain, as this phase required — that instruction is now in the header
    rather than only in `plan.md`, so it survives this plan.

- [x] **Phase 41** — **two bare source→source citations are drifted, and the
      anchored form batch 1 added is what would have caught them.** Found by
      batch 1 while looking for citations correct enough to anchor; NOT repaired
      there, because verifying and repointing the other 121 bare source→source
      refs is a sweep and belongs with the sweep phases, not with the phase that
      added the grammar.
  - `scripts/asan_self.sh:38` says the generic bind vector is xmalloc'd at
    `src/tychoc.c:6870`. That line is `static const char *discarded_map_get`,
    an unrelated function. The real site is `src/tychoc.c:7585@gi.binds` — off
    by ~690 lines, in bounds the whole time, and therefore green.
  - `tests/rtparity/run.py:67` cites `src/tychoc.c:10343` as "the loop codegen".
    That line is a bare closing brace.
  - Both are the exact class phase 8 of the first plan repaired by hand and
    reported its bounds check would catch none of. The other 121 bare
    source→source refs have never been audited against their content.
  - Done when: every source→source ref that names a distinctive line carries
    `@token`, ranges stay bare, and the anchored/bare split in `--stats` is
    recorded here so the next reader knows what was audited.
  - Verify: `python3 scripts/check_citations.py --stats`.

- [x] **Phase 42** — **§16.7 says `[bool]` MUST be rejected and the compiler
      accepts it.** Found by batch 2 while repointing the two citations that
      §16.7 hangs on — a behaviour divergence, not a line-number one, which is
      why it is filed rather than fixed under a citations scope lock.
  - `docs/spec/12-aggregates.md:196-199` states "`void` and `bool` MUST be
    rejected as a bracket-array element type… both `[bool]` and `[3]bool` are
    diagnosed at type-parse", and `docs/spec/03-types.md:203-204` repeats it.
  - The **fixed-size** half is true: the `[N]T` site tests
    `felem == T_VOID || felem == T_BOOL`. The **dynamic** half is not: the `[T]`
    site tests `elem == T_VOID` alone, and its own diagnostic lists `bool` as a
    permitted element — the two messages in the same function now disagree about
    whether `bool` is legal in an array.
  - So either `[bool]` was deliberately allowed and §16.7 plus the fixed-size
    site are stale, or the dynamic site lost its `T_BOOL` test in a refactor and
    is a real hole. The `[N]bool` comment blames tychoc0's missing fixarr-bool
    codegen, which the freeze retirement makes moot — that is evidence for the
    first reading, but it is a guess until someone checks.
  - Done when: a `.ty` fixture pins whichever answer is chosen, the two spec
    sites agree with it, and the two compiler sites agree with each other.
  - Verify: `make test` (this one does reach a compiled artifact), plus the two
    doc gates.

## Batch 1 evidence — phases 13, 23, 34, the citation gate's three blind spots

Ran 2026-07-30 against `cd9893d`. All three phases are changes to
`scripts/check_citations.py` plus the minimum set of citations needed to keep the
tree green. **Batch 2's sweep was not touched**: the ~344 bare `src/tychoc.c:N`
refs are phase 17 and none of the three new checks fires on a bare relative ref.

### What each check does, and where the line was drawn

**Phase 13 — anchored source→source (`scripts/check_citations.py:268`, and the
content check at `scripts/check_citations.py:432-434`).** `SRCCITE` gained an
optional `@token` suffix; when present, the cited lines must literally contain
the token. Two deliberate differences from the Markdown anchor, both forced by
the medium: the token is `[A-Za-z0-9_]+` with no spaces, because a source
citation is bare comment prose with no closing delimiter and a space-permitting
token would swallow the sentence; and there is no construct where it is
mandatory, because nothing on the source side marks a comment as load-bearing
the way `> Provenance:` does.

*Line against batch 2:* **opt-in by construction.** Zero of the 133 existing
source→source refs carried an `@`, so the grammar change alone could not redden
anything. It was adopted on **8 sites verified by reading the target line**, all
in shell and Python comments — `Makefile:245@SKIPPED` from `scripts/asan_self.sh`
(twice), `scripts/editors_check.sh` (once) ; `scripts/tools_check.sh:25@editors`
from `scripts/editors_check.sh` and `scripts/ci.sh` ; `src/tychoc.c:6668@r_step`
and `src/tychoc.c:3366@i_dotlt` (twice) from `fuzz/run_parforparity.py`. No
`.ty` file was touched on purpose: a `.ty` edit is `make test` and
`scripts/tools_check.sh` territory, and this batch's gate budget is the two doc
gates. Sites whose target line did **not** support the claim were left bare and
filed as phase 41 rather than silently repaired.

**Phase 23 — an absolute path is a failure (`scripts/check_citations.py:321`).**
Not resolved against `ROOT`, per the phase: reported instead, naming the
repo-relative spelling. `cur` is cleared afterwards so a following bare `:N`
cannot inherit an unspellable path.

*Line against batch 2 — the frozen-record decision phase 23 asked for:* the
tree holds **187** absolute refs, **161** of them in `docs/internals/plan-*-DONE.md`,
and ~40 of those name the deleted `tests/postfreeze/`. Enforcing there would
demand an edit to a frozen record and redden the gate permanently, so the
archived set is exempt — the same rule and the same reason as the existing
mandatory-anchor exemption. They were unchecked before and are unchecked now;
nothing regressed. The remaining **20** live ones were all in `plan.md` and were
rewritten repo-relative. **Measured, not assumed:** that rewrite moved
`plan.md`'s refs reaching the bounds check from 133 to 149, +16, which is exactly
the 2082 → 2098 rise in the tree-wide bare count. The other four resolve to
`.gitignore` and `docs/…`, outside `SRC_PREFIX`, and stay skipped as
cross-document links. Untouched and out of scope: 66 backticked absolute paths in
`plan.md` that carry no `:N` — a mention is not a citation and the rule does not
see them.

*Left for a human to decide:* `CLAUDE.md`'s Citations section still says "Write
full paths in evidence blocks", which is the instruction that produced these 187
absolute refs in the first place — a writer reading it can reasonably spell a
"full path" as `/home/igzo/…`. The gate now answers that in one run, by name, so
the loop self-corrects; but the wording and the gate now disagree in tone.
`CLAUDE.md` was **deliberately not edited** here: it is the instruction file, and
this batch had no mandate to rewrite it.

**Phase 34 — a pathless `> Provenance:` ref is a failure
(`scripts/check_citations.py:346`).** Fail-closed, with a message naming the
missing path. Not "carry `cur` across paragraphs" — that was deliberately
removed and the comment above the loop says why.

*Line against batch 2:* the population is **0**. All 60 `> Provenance:` blocks in
the tree already name a path; the 12 that did not were repaired by hand earlier
(ten by the postfreeze plan's phase 11, two ranges held path-less *on purpose to
avoid reddening a gate that could not see them* — the workaround that should not
have needed to exist). So the tree is green with no sweep, and this check is a
guard against the next one, exactly as the archived-plans exclusion was when it
landed. Note the rule catches **ranges too**: a range is exempt from the *anchor*
requirement, never from naming a path.

### The three deliberate-break proofs

Each was run against the **final** version of the script, broken and restored,
with the gate output read both ways. Backticks are stripped from the quoted
failures below **on purpose**: this gate does not track code fences, so a quoted
citation inside one is re-parsed as a live citation and would redden the very
evidence block that reports it.

```
A. phase 13 — scripts/asan_self.sh:72, Makefile:245@SKIPPED -> @SKIPPEDX
   STALE  scripts/asan_self.sh:72  Makefile:245@SKIPPEDX -> lines 245-245 of
          Makefile do NOT contain 'SKIPPEDX' (token absent from the whole file)
   citation check: FAILED (1 stale citation(s) above)          exit=1
   restored -> citation check: ok (... 10 source->source anchored)   exit=0

B. phase 23 — a throwaway tracked docs/ file citing an absolute path
   STALE  docs/_break_probe.md:5  /home/igzo/github/tycho/src/tychoc.c:402 ->
          ABSOLUTE PATH, which this gate cannot check: write it repo-relative,
          as src/tychoc.c:402
   citation check: FAILED (1 stale citation(s) above)          exit=1
   rewritten repo-relative -> ok, and the BARE COUNT ROSE 2098 -> 2099: the
   repaired ref is now actually checked, where the absolute one counted as
   nothing.                                                    exit=0

C. phase 34 — the same file, a Provenance block opening a paragraph with no path
   STALE  docs/_break_probe.md:6  :3364@i_dotlt -> a > Provenance: ref that
          names no path and inherits none from its paragraph; nothing about it
          is checked. Write the path: <path>:3364@i_dotlt
   STALE  docs/_break_probe.md:7  :3360-3370 -> (same message)
   citation check: FAILED (2 stale citation(s) above)          exit=1
   path written in -> ok, anchored count 144 -> 145, and the range :3360-3370
   inherits the path and stays bare: the range exemption is intact.  exit=0
```

The probe file was removed with `git rm --cached` and deleted; `git status
--short` was read before staging and held only the six intended files.

While writing this block the gate caught **its own author twice**, which is the
best evidence the checks are live: the docstring's shape-table example was a
false anchor into `src/tychoc.c` (its two lines contain no `main`), and the
sentence explaining that fact quoted the bad form as a live citation. The shape
table now names `src/example.c`, a path deliberately not in the tree, with the
reason written above it — a grammar of shapes is not a claim about the compiler,
and binding it to real line numbers would put the gate's own docstring into the
population every renumbering sweep has to repair.

### Gate output — the real runs

The first run below is the state **before this block was appended**; the second
is the committed tree. This block is itself full of citations, so writing it
moved the numbers — which is the gate working on its own evidence, and the
reason the baseline table gives the committed figures.

```
python3 scripts/check_citations.py          # before this evidence block
citation check: ok (144 anchored contain the token they name, 2098 bare in
bounds, 103 source->doc citations resolve, 123 source->source in bounds,
10 source->source anchored)

python3 scripts/check_citations.py          # committed tree
citation check: ok (148 anchored contain the token they name, 2106 bare in
bounds, 103 source->doc citations resolve, 123 source->source in bounds,
10 source->source anchored)

sh scripts/check_links.sh
link check: ok (134 markdown files, no dead relative links)

sh scripts/spec_check.sh
spec-examples: 9 runnable example(s), all pass
```

`make ci` and `make test` were **not** run: nothing here reaches a compiled
artifact. Every edit is a comment, a Markdown line, or the checker itself.
`python3 -c "import ast"` and `sh -n` were run on all six edited files.

### Baseline for batch 2

| metric | before (`cd9893d`) | after (committed) |
|---|---|---|
| md anchored (content-checked) | 144 | 148 |
| …of them mandatory `> Provenance:` | 85 | 85 |
| md bare (bounds only) | 2082 | 2106 |
| source→doc (existence) | 102 | 103 |
| source→source **bare** (bounds) | 133 | 123 |
| source→source **anchored** (content) | — | 10 |

The md columns rose by 4 anchored and 24 bare **because of this evidence block**,
not because of the gate change; the gate-change contribution to the bare column
is the +16 measured above.

The source→source total is unchanged at 133; ten moved from the bare column to
the anchored one. `source→doc` rose by one because the new docstring names
`docs/bootstrap.md:106`. Batch 2 owns the ~344 bare `src/tychoc.c:N` refs inside
the 2098, and phase 41 owns the 123 bare source→source refs.

## Batch 2 evidence — phases 18, 38, 41, and phase 17 partially

### The population, re-derived rather than inherited

Phase 17 said "~344". Re-running the gate's own Markdown walk (same `CITE`
regex, same paragraph-scoped inheritance of a bare `:N`, same absolute-path
clearing) over every tracked `*.md` gives:

| bucket | count |
|---|---|
| every bare citation the gate resolves | 2107 |
| …of them into `src/tychoc.c` | 1189 |
| …**live** (outside the frozen archives) | **568** |
| …frozen, in `docs/internals/plan-*-DONE.md` | 621 |

So the real live population is **568**, 65% larger than the phase estimated, and
"~344" was probably a count of one document set rather than the class. The 621
archived refs were **left alone**, on the rule phase 4 settled and batch 1
re-affirmed: they are true accounts of the tree as it stood, and renumbering them
falsifies the record. `check_citations.py` encodes the same exemption.

### How each citation was checked

Not by arithmetic. For every one of the 568, the citation's authoring commit was
recovered with `git blame --line-porcelain -w`, the cited lines were read **as
they were at that commit**, and compared against the same lines today:

| result | count |
|---|---|
| cited text unchanged since written | 140 |
| cited text changed — drifted | 428 |
| …with a single exact relocation of the whole cited block | 340 |
| …ambiguous (block occurs more than once) | 34 |
| …block no longer exists verbatim | 54 |

The 428 were then read individually — prose claim on the left, the text the
author was pointing at on the right — and only repointed where the two agreed.
That is the load-bearing step: the relocation is exact evidence of *where the
author's referent went*, never evidence that the author was right.

**Two earlier findings were reproduced, and they are why the arithmetic alone is
not enough.** A blame-and-shift pass would have scored both of these green:

- `docs/spec/16-builtins.md:145` cited `char_at`'s codegen at `src/tychoc.c:8641-8648`;
  that block's referent was a **`sink`-parameter diagnostic**, and had been since
  the line was written at `3f68a00`. Repointed to `:9000-9007`, the real
  `char_at` codegen, not to where the wrong block moved.
- `docs/spec/16-builtins.md:332` cited `die`'s codegen at `src/tychoc.c:8791-8792`, which was
  a comment about `tycho_streq` group-stripping. Repointed to `:9150-9151`. Its
  two siblings at `docs/spec/16-builtins.md:20` and `:86` cited the *same* numbers and were genuinely
  right when written, so they moved with the drift instead — the same numbers
  needed two different repairs.

**The "unchanged since written" bucket is not safe either**, which was the
sharpest finding. `docs/spec/12-aggregates.md:15` cites `reserve` at
`src/tychoc.c:5657-5683` and `:8693-8700`; those are `to_int` and a `sink` diagnostic. The
line was last written **today** (`fc921d7`, phase 27) — phase 27 shifted
`src/tychoc.c` by 170 lines, repaired the **anchored** refs on that line, and
left the bare ones pointing 170 lines short. That is phase 17's premise
demonstrated on a single line: src/tychoc.c:11797@'pop from an empty array' (de-backticked: the anchor has drifted) green,
`` `src/tychoc.c:5657-5683` `` silently 170 off. The same pattern was found and repaired on
`16-builtins.md:85`, `:116`, `:143`, `:145`, `:218`, `:243` and
`12-aggregates.md:18`.

### What was repaired

**227 citations across 17 files**, all in the live normative set:

| file | refs repaired |
|---|---|
| `FRICTION.md` | 42 |
| `docs/spec/16-builtins.md` | 43 |
| `docs/spec/15-program.md` | 40 |
| `docs/spec/12-aggregates.md` | 30 |
| `docs/spec/02-grammar.md` | 19 |
| `docs/spec/03-types.md` | 16 |
| the other 11 spec chapters | 37 |

194 were exact relocations applied by an offset-safe rewriter (right-to-left
within each line, so several citations on one line cannot corrupt each other);
33 were resolved by hand — wide ranges whose function had grown (`lex`
`:211-530`, `parse_stmt` `:3108-3578`, `resolve_expr_inner` `:4885-6257`,
`merge_pkg` `:12372-12436`, `register_builtins` `:4506-4538`), ambiguous blocks
disambiguated against the authoring commit's surrounding lines, and the
wrong-when-written set above.

### Where a mechanism was annotated instead of repointed

`docs/spec/12-aggregates.md:503` claimed `keys(m)`'s insertion order came from
"an insertion-ordered link chain" and cited two lines of the map codegen. That
chain — a `nxt` field threaded through the slot table — **does not exist
anywhere in the compiler any more**; `keys` now walks the append-ordered entries
array and filters on `elive`. Repointing those two refs at whatever now sits at
those numbers would have preserved a false claim behind a true-looking line
number. The refs were retired, the prose was corrected to the mechanism that is
actually there, and the disappearance is recorded in place. Same instinct as
phase 41's second ref below.

### Phase 18 — the confirmed-stale doc→doc ref

`docs/internals/spec-plan.md:605` cited `appendix-e-conformance.md:188` for the
claim that §9.5 is evidenced by the whole differential suite. Read: `:188` is
`| §17.3 | recursion only through a container | ... |`, a fixture row (it was the
§24.2 row when the drift was first noticed — the appendix has moved again since).
The flagged-clause list that actually carries §9.5 is `:235-241`. Repointed
there. Reading the target also caught a second staleness in the same sentence:
it cited `make fixpoint` as evidence, and `make fixpoint` was retired with the
`tychoc0` freeze — which the target paragraph itself now says. Both fixed; the
old spelling and why it was wrong are written into the line.

### Phase 41 — two drifted source→source refs, now anchored

- `scripts/asan_self.sh:38` said the generic bind vector is xmalloc'd at
  `src/tychoc.c:6870`. That line is `static const char *discarded_map_get`. The
  real site is `src/tychoc.c:7585@binds` (`gi.binds = (Type *)xmalloc(...)`),
  ~690 lines away and in bounds the whole time. Now **anchored**, so the next
  shift reddens the gate instead of rotting.
- `tests/rtparity/run.py:67` cited `src/tychoc.c:10343` for the inline
  `tycho: range step is zero` trap; that line is a bare `}`. The trap survived
  the removal of `range()` — it is at `:10843` — so this one *is* repointable,
  and is now `src/tychoc.c:10886@_step`. Its sibling `compiler/tychoc0.ty:9513`
  was read and is correct.

The anchor token is `[A-Za-z0-9_]+` on the source side, so `@binds` is the
spellable form of `gi.binds` — the `.` would terminate the token.

### Phase 38 — the malformed record, diagnosed before it was edited

Read, not assumed. Three bullets — "The permanent half is already owned
elsewhere…", "Done when: `plan.md` records which of the two options was taken…"
and "**Closed by phase 7**: the option taken was *retire early on purpose*" —
were physically under the **phase 29** entry while being about phase 28's three
deleted `range()` fixtures. The mis-parenting made phase 29 read as a fixtures
phase when it is about the LSP's `sem_is_keyword` set, and it stranded phase 28's
closure notice where nobody counting boxes would find it. Consequences repaired:

1. the three bullets moved under phase 28, after its own "Decide, and record…"
   bullet, so the entry now reads decision → constraint → outcome;
2. phase 28's box ticked, because its own last bullet says it was closed by
   phase 7 and phase 7's evidence carries the per-fixture reasoning;
3. phase 29 now ends at its `tools_check.sh` / `make editors-check` line and is
   unambiguously the LSP keyword-set phase;
4. the "Status — PLAN COMPLETE" count corrected from 24 to 20, with the arithmetic
   named (18, 38, 41 closed here, 28 ticked here) so the next reader can audit it.

**Note for batch 5**, which lists phase 28: it is now ticked and needs no work.

### What was deliberately left, and why

| left alone | count | reason |
|---|---|---|
| `docs/internals/plan-*-DONE.md` | 621 | frozen records; the rule phase 4 settled |
| `docs/internals/*` (live) + `docs/rfc/*` | 167 | dated design studies; **no** prior repointing phase has ever touched them |
| `plan.md` completed-phase evidence blocks | 38 | an evidence block records what a phase verified at its commit |
| unrelocatable single-line refs in `FRICTION.md` | 7 | their cited text is `}` or `Type b = base_of(rt);` — no distinctive content to track, all in-bounds, all inside struck-through resolved entries |

The middle row is the reason phase 17 stays open rather than being ticked with a
footnote. 621 + 167 + 38 + 7 = 833 refs untouched, against 227 repaired.

### Gate output — the real runs

```
$ python3 scripts/check_citations.py --stats
citation check: 150 anchored (content-checked, 85 of them the mandatory
`> Provenance:` single-line refs), 2112 bare (bounds only), 103 source->doc
(existence), 121 source->source (bounds), 12 source->source anchored
(content-checked)
citation check: ok (150 anchored contain the token they name, 2112 bare in bounds,
103 source->doc citations resolve, 121 source->source in bounds,
12 source->source anchored)

$ sh scripts/check_links.sh
link check: ok (134 markdown files, no dead relative links)

$ sh scripts/spec_check.sh
spec-check: Appendix A grammar matches §3/§4 (ok)
spec-check: all Appendix E fixture citations resolve (ok)
spec-examples: 9 runnable example(s), all pass
```

| metric | batch 1 (`fd361e9`) | before this block | after batch 2 |
|---|---|---|---|
| md anchored (content-checked) | 148 | 148 | 150 |
| …of them mandatory `> Provenance:` | 85 | 85 | 85 |
| md bare (bounds only) | 2106 | 2106 | 2112 |
| source→doc (existence) | 103 | 103 | 103 |
| source→source **bare** (bounds) | 123 | 121 | 121 |
| source→source **anchored** (content) | 10 | 12 | 12 |

Read the columns in that order and the numbers say three separate things.

The **source→source** move is phase 41 and nothing else: two bare refs left the
bare column and arrived in the anchored one, so the total holds at 133.

The **md** columns did not move at all for the sweep itself — 227 citations were
repointed, and repointing changes a number inside a citation without creating or
destroying one. That is the honest reading of "2106 → 2106" in the middle column,
measured after every repair and before this write-up existed.

They then rise by **+2 anchored and +6 bare because of this evidence block**,
which quotes the refs it repaired. The gate does not exempt fenced code, so the
quoted forms are live citations and are checked like any other — which is the
intended behaviour, not an accounting nuisance: batch 1's own commit reddened on
a citation written as a counter-example inside prose.

`make test` and `make ci` were **not** run, per `CLAUDE.md`'s gate ladder:
every edit is a Markdown line or a comment, and none can reach a compiled
artifact. `sh -n scripts/asan_self.sh` and `python3 -c "import ast; ast.parse(open('tests/rtparity/run.py').read())"`
were run on the two source files edited.

## Out of scope

- **The two concurrency items in `FRICTION.md`** — no storable task handles, no
  way to hand a connection to whichever worker is free. They want a type-system
  answer first.
- **Unfreezing `compiler/tychoc0.ty` into maintenance.** Phase 1 retires the
  lanes that check it; it does not bring it back into the language's evolution.
  That was offered and not chosen.
- **`for x in xs:` and `for C:`.** Both stay exactly as they are.

---

## Batch 3 evidence — documents pointing at things that no longer exist

Phases 12, 15, 32 and 35 closed; **phase 33 built its gate but stays open**, see
its entry for the exact remaining scope. HEAD at start: `2e6e698`.

### What each phase actually needed

**Phase 12 — the zed corpus count.** The README read 813; the tree has **829**
(`git ls-files '*.ty' | wc -l` and `scripts/editors_check.sh`'s `find` agree, and
`editors/` itself holds zero `.ty` files, so the README's old "excluding
`editors/`" was describing an exclusion the script never made — dropped).
`scripts/editors_check.sh` already computed the number as `nfiles` but only for
its own corpus lane. The `find` moved **above** the tree-sitter availability
check so the README lane runs offline, and the CORPUS lane now reuses the same
file list — deliberately, so the number the README is checked against is the
number the parser actually ran on.

**Phase 15 — `docs/corelib.md`.** Not "a dead backticked path" but **31 live
refs**, and the interesting part is that the line numbers had drifted too.
`68e5b39` was a pure rename (`git diff --numstat`: `1 1 docs/{ => guides}/corelib.md`),
but 10 later commits edited `docs/guides/corelib.md`. Spot-check that caught it:
`docs/spec/18-library.md`'s `io` section cited docs/corelib.md:204-210 (de-backticked: the path it names is the pre-rename one), and
line 204-210 of the current `docs/guides/corelib.md` is the **`hash`** package,
not `io`. Repair method: parse the `- **\`pkg\`**` bullets
in `docs/guides/corelib.md` into per-package line extents, parse
`docs/spec/18-library.md`'s `### 32.N \`pkg\`` headings into a section map, and
retarget each citation to its own package's current extent. 25 ranged citations
rewritten; **8 were unchanged (math…sort), 17 had moved.** Old range in
`docs/corelib.md` → new range in `docs/guides/corelib.md`, for three of the
seventeen: `rand` 96-101 → 108-113, `io` 204-210 → 266-297, `crypto` 192-203 →
232-243. A path-only `sed` would have left all 17 pointing at the wrong package
while *looking* repaired.

**Phase 32 — `range` as a builtin.** The entry's own warning paid off:
`editors/vscode/syntaxes/tycho.tmLanguage.json:47` spelled `range` inside a regex
alternation, which is exactly what phase 7's grep for the quoted string could not
see. Both grammars fixed; `editors/zed/grammars/tycho/src/` regenerated with
`npx tree-sitter-cli@0.25 generate --abi 15` (not hand-edited), leaving zero
`anon_sym_range` in the generated tree.

**Phase 35 — the two `range(len(A))` sites.** Only **one** was still open. Phase
27 had already rewritten `tests/bounds_elision.ty:5` when it restored elision;
that file's surviving mention at `tests/bounds_elision.ty:11` is HISTORY prose
about the old spelling and is true. `bench/prongB/RESULTS.md` footnote ² was
rewritten: the 132 → 47 ms is now labelled as measured on the deleted counting
form, with the current three-clause behaviour stated from source
(`src/tychoc.c:7955-8005`: the condition goes into the C `while` header and is
re-evaluated per iteration rather than cached, a *stronger* basis than
`S_FORRANGE`'s cached `_stop`) and the note that **47 ms was never re-measured**
on the three-clause form — at `-O3` gcc folds the check anyway, which is why
`bench/guard.sh` asserts emitted C instead (`bench/guard.sh:58-62`).
**No `.ty` file changed, so `make test` was correctly not run.**

### Phase 33 — the scope decision, and why

The population is smaller than the entry assumed: **40** ```` ```tycho ````
fences in all of tracked `docs/`, not a majority of the documentation.
`docs/reference/` and `docs/tutorial.md` have **zero**. Measured before deciding.

A "declares an `fn` ⇒ must compile" rule was tried and **rejected**: it reddens
on six legitimate fences — `docs/spec/12-aggregates.md:408` shows a function
beside its call-site binding (Tycho has no top-level statements, so the pair is
not a program), `docs/guides/ffi.md:152` is an `extern` block against a real
`libsqlite3`, and three in `docs/internals/generics-stage2-body-cloning.md`
document programs the compiler **must reject**, one containing a literal `...`.
That is precisely the "red on prose, then disabled" failure the entry warned of.

Settled design: **opt-out with a named reason**, `<!-- fence-skip: … -->`, and
the reason is printed on every run so the skip list cannot grow quietly. Frozen
`docs/internals/plan-*-DONE.md` fences are exempt on the same grounds as
`scripts/check_citations.py:474@ARCHIVED`. Fences with no `fn` are classed
FRAGMENT rather than wrapped in a synthetic `main`, because wrapping would
typecheck a program the document does not contain.

**Result: 10 CHECK, 19 FRAGMENT, 6 MARKED, 5 FROZEN.** Ten is small and it is
the honest number. The value is that a *new* `tycho` fence is checked by
default. The `arrays-structs.md` snippet was covered only after **retagging its
fence** — it was opened with a bare ```` ``` ````, so even a perfect tycho-fence
gate would have missed it. That is why the remaining bare-fence population is
filed as phase 41 rather than waved at.

### Break proofs — both directions

**Phase 33 gate**, reverting `docs/guides/arrays-structs.md` to the original bug:

```
--- BROKEN RUN ---
docs-fences: FAIL docs/guides/arrays-structs.md:104 -- does not compile
      <fence>:3: error: expected ';' after the condition
           3 |     for i := 0; i < 1_000_000; i += 1:   # no digit separators
             |                      ^
exit=1
--- RESTORED RUN ---
docs-fences: 10 fence(s) compiled, 30 skipped (reasons above), 0 failure(s)
exit=0
```

**Phase 12 README lane**, README set back to the historical wrong value:

```
>>> editors: zed README corpus count
    STALE: editors/zed/README.md claims 462 committed .ty files, tree has 829.
    Fix the README to say 829.
editors-check: FAIL
```

restored:

```
>>> editors: zed README corpus count
    ok  README says 829 committed .ty files, and so does the tree
```

### Gate output

```
$ make editors-check
    ok  editors/vscode/syntaxes/tycho.tmLanguage.json
    ok  editors/vscode/language-configuration.json
>>> editors: zed README corpus count
    ok  README says 829 committed .ty files, and so does the tree
>>> editors: zed grammar regenerated with npx --yes tree-sitter-cli@0.25 (tree-sitter 0.25.10)
    src/ matches grammar.js byte for byte (parser.c, grammar.json, node-types.json, tree_sitter/)
>>> editors: zed grammar over the corpus (829 .ty files)
    829 files parsed; the only failure is the enumerated known-bad set (tests/reject/rawstring_unterminated.ty )
editors-check: ok

$ sh scripts/docs_fences.sh
docs-fences: 10 fence(s) compiled, 30 skipped (reasons above), 0 failure(s)

$ python3 scripts/check_citations.py
citation check: ok (150 anchored contain the token they name, 2113 bare in bounds,
103 source->doc citations resolve, 121 source->source in bounds, 12 source->source anchored)

$ sh scripts/check_links.sh
link check: ok (134 markdown files, no dead relative links)

$ sh scripts/spec_check.sh
spec-examples: 9 runnable example(s), all pass
```

`make test` not run: no `.ty` file changed (`git status --short` confirmed).
`make ci` not run, per the batch instruction — **so the new `[12b/13]` step added
to `scripts/ci.sh` has not been exercised inside a full `ci` run.** It is one
line, `make -s docs-fences`, and the target was verified standalone above; the
residual risk is a step-ordering or numbering surprise, not a gate failure.

**Self-inflicted breakage, caught and repaired.** Adding the `docs-fences` target
to the `Makefile` shifted the `ilp32` ASan line from 245 to 253, reddening four
`Makefile:245@SKIPPED` citations in `scripts/asan_self.sh`,
`scripts/check_citations.py` and `scripts/editors_check.sh`. All four retargeted
to `Makefile:253@SKIPPED` and re-verified. Worth noting for the next phase that
edits the `Makefile`: the citation gate does catch this, but only if you run it.

---

## Phases discovered by batch 3

- [x] **Phase 43** — **~155 fences in `docs/` carry no language tag, so nothing
      can check them.** Found by batch 3 while building `scripts/docs_fences.sh`.
      (Filed as "41" by batch 3, renumbered — 41 and 42 were already taken by
      batch 2's filings. Batches file phases without seeing each other's work, so
      collisions are a property of the process, not a mistake by either.)
      The gate keys off ```` ```tycho ````, of which there are 40. The bare
      ```` ``` ```` population is **48 in `docs/reference/`, 33 in
      `docs/guides/`, 56 in `docs/internals/`, 14 in `docs/`, 4 in `docs/rfc/`**
      — a mix of shell, C, emitted output and Tycho that no heuristic can safely
      separate. `docs/reference/` and `docs/tutorial.md` contain **zero** tagged
      Tycho fences, so the reader-facing reference is entirely uncovered.
  - The work is a human pass per file: tag the Tycho ones `tycho`, tag the others
    `sh`/`c`/`text`, and let `make docs-fences` sort out which then compile.
    Expect some to fail — that is the point, and it is how
    `docs/guides/arrays-structs.md`'s `1_000_000` would have surfaced years
    earlier.
  - Do NOT automate the tagging by guessing the language. A fence mistagged
    `tycho` makes the gate red on prose and the gate gets disabled, which is the
    failure mode phase 33 was written to avoid.
  - Done when: every fence in `docs/reference/` and `docs/guides/` carries a
    language tag, and `make docs-fences` is green over the enlarged set.
  - Verify: `make docs-fences`, `sh scripts/check_links.sh`.
  - **BATCH 10 DID THE PASS. 90 fences tagged**, one file at a time, by reading
    each: **79 `tycho`, 6 `text`, 4 `sh`, 1 `ebnf`** across `docs/reference/`
    (48), `docs/guides/` (32) and `docs/tutorial.md` (10). Nothing was guessed —
    the non-Tycho eleven are named in the commit: three gdb/lldb/`tychoc`
    invocations and one `$ ./tychoc f.ty` (`sh`); the `TYCHO_ARENA_STATS` dump, a
    directory tree, the inference pattern/arg table, a quoted compiler warning,
    the `subscript <name>(<recv>…)` syntax template and a program's stdout
    (`text`); the file grammar (`ebnf`).
  - **11 of the 79 needed a `fence-skip` marker** — the convention phase 33
    established, not a second one — because they declare an `fn` and still cannot
    compile alone: three one-file-of-a-multi-file-package examples, three showing
    statements at top level, two ellipsis-body placeholders, one importing
    `core:math` without the `package main` line the compiler requires, and two
    calling a helper the prose names but does not define (`bump`, `parse_digit`).
    Each marker states which of those it is, and the reason prints on every run.
  - **The pass found two real documentation bugs, which is the point.** Tycho has
    no one-line suite — `if c: stmt` and `for x in xs: stmt` are both
    `error: expected newline`, verified on the built `./tychoc` in both spellings
    against a two-line control that compiles. `docs/guides/arrays-structs.md`
    showed `if len(xs) > 0: return Some(xs[0])` and `docs/reference/functions.md`
    showed `for x in xs: acc = acc + x`. Same class as the `1_000_000` of phase
    33, in the two most-read files in the tree, and neither had ever been parsed
    by anything. Both split across lines.
  - Residual: 64 bare fences in `docs/internals/`, `docs/rfc/` and `docs/` —
    filed as **phase 61**, not silently absorbed.

- [x] **Phase 44** — **doc→doc `path:N` citations are checked by nothing, and
      there are 103 stale ones.** Found by batch 3 while fixing phase 15.
      (Filed as "42" by batch 3, renumbered for the same reason as phase 43.)
      `SRC_PREFIX` (`scripts/check_citations.py:224-225`) lists `src/`,
      `compiler/`, `runtime/`, `corelib/`, `tests/`, `scripts/`, `tools/`,
      `examples/` — **not `docs/`** — so a ref whose path starts `docs/` is
      `continue`d at `scripts/check_citations.py:328-329` before any bounds or
      anchor check runs. That is why 31 refs to a file deleted eight months ago
      sat green, and it is the *general* form of the hole phase 15 only patched
      by hand.
  - **Measured, not estimated.** Adding `"docs/"` to `SRC_PREFIX` and running the
    gate yields **103 failures: 51 `NO SUCH FILE` and 52 `OUT OF BOUNDS`**. Batch
    3 deliberately did **not** ship this — it is a sweep the size of batch 2, not
    a line in a batch about dead references, and shipping the widened gate
    without the sweep would leave the tree red.
  - Note the population is concentrated in `plan.md`'s own bare `:N` refs
    inheriting a doc path from the previous sentence, which is the exact class
    `scripts/check_citations.py:235-237` documents as deliberately not carried
    across paragraphs. Read that comment before deciding the fix shape; the
    answer may be to require an explicit path in `docs/`-targeted refs rather
    than to widen the bounds check.
  - Done when: `docs/`-targeted citations are checked, the 103 are resolved, and
    `python3 scripts/check_citations.py` is green.
  - Verify: `python3 scripts/check_citations.py`.
  - **BATCH 10 SHIPPED THE GATE AND THE SWEEP. It is 77, not 103** — batches 4–9
    repaired 26 of them incidentally — split **25 `NO SUCH FILE` / 52
    `OUT OF BOUNDS`**, and the split is the finding. **Every one of the 52 is a
    bare `:N` that inherited a `docs/` path from its sentence while meaning
    `src/tychoc.c`**; not one is a genuine doc→doc citation that merely drifted.
    So the note above is right that the population is concentrated there, and the
    fix it floated — "require an explicit path in `docs/`-targeted refs" — turned
    out to be unnecessary machinery: the ordinary bounds check already catches
    every one of them loudly, because a compiler line number read against a
    386-line chapter is out of bounds by two orders of magnitude. `docs/` is
    simply added to `SRC_PREFIX` (`scripts/check_citations.py:248`) and the
    doc→doc direction becomes ordinary: bounds, anchors, and the mandatory
    `> Provenance:` rule, same as doc→source.
  - **One new exemption, and it is the one already settled.** 35 of the 52 sit in
    the frozen `plan-*-DONE.md` archives, where the ARCHIVED rule forbids
    demanding an edit; a `docs/`-targeted ref in a frozen file is therefore
    skipped and **counted in `--stats`** (242 of them) so the hole is declared
    rather than silent (`scripts/check_citations.py:352`).
  - **What the other 42 got, per class.** 22 in `docs/rfc/ffi-threading-design-review.md`
    named `docs/ffi.md` and `docs/concurrency.md`, both moved into `docs/guides/`
    by `68e5b39`; repointed by building a difflib equal-block map from
    `68e5b39^` to today and **comparing the cited text old-vs-new for all 22** —
    identical in every case, so the repoint is evidence of where the referent
    went, not a guess. One (`:73`) was blank when written and its sentence has
    always been on the next line; corrected to `:74` and said so. 2 in
    `docs/spec/appendix-h-differences.md` row H5 named `docs/generics.md`, same
    rename, mapped `:11`→`:13` and `:205-208`→`:207-210`. 18 in `plan.md`:
    15 mis-inherited refs given the explicit `src/tychoc.c` path they meant (one
    of which then correctly re-anchors the two *siblings* on the next line back
    to `16-builtins.md`, which the naive fix would have silently mis-bound), and
    3 whose anchor token has since drifted or whose path is quoted *because* it
    is dead — de-backticked, the convention `plan.md:1450-1452` established for
    exactly this and states in words.
  - **Editing the gate's docstring staled 18 live citations into it**, all bare
    and all still in bounds — the silent class this whole batch is about, caught
    only by mapping HEAD→working and re-verifying each pair (18/18 text-identical
    at the new line). Repaired, plus one in `scripts/docs_fences.sh:21` promoted
    to the anchored form `@ARCHIVED` so it cannot rot silently again.
  - Two placeholder paths written into the new docstring were themselves parsed
    as live citations by the pass being documented, on two separate runs. The
    docstring already warns about this for the doc→source table; it now does the
    same for its own doc→doc example.

## Batch 4 evidence — phases 19, 22, 36, coverage the generators cannot reach

Three lanes that were green while never touching what they exist to exercise.
Nothing below is asserted from reading the generator; every fraction is a corpus
that was emitted, then counted.

### How the fractions were measured

A grep for element-wise array arithmetic cannot be a grep for a marker the
generator writes — that measures the marker, not the coverage. The count is
therefore taken by a **grammar-shaped** detector (kept in the run's scratch dir,
not the repo) that flags a program iff an arithmetic operator has a **whole
array** on one side: an array-typed local used un-indexed, or an array literal.
It answers the same way before and after the generator change, which is what
makes the before/after pair mean anything.

Two things had to be got right before the number was trustworthy:

- **It was validated on known answers first.** `tests/array_arith.ty` and
  `tests/array_bcast.ty` → 2/2. `tests/fixed_array.ty`, `tests/option_arrays.ty`,
  `tests/float_arrays.ty`, `tests/for_bare.ty` (arrays, no array arithmetic) →
  0/4.
- **Its first version scored the baseline 200/200 and was wrong.** A whole-file
  scan matched `mkarr`'s `r := []int` against `fz_join`'s `r = r + "x"` — two
  different functions, same name. Scoping the scan to `fn main():` onward (where
  `fuzz/gen.py` puts every generated statement — `gen_block` is called only from
  `generate`) drops it to 0/200. A detector that says "already covered" is the
  one failure mode that would have made this whole batch a no-op, so it is
  recorded rather than quietly fixed. The same limitation is why the hand-written
  `tests/conc` fixtures were counted whole-file and then checked one by one.

Commands:

```
for i in $(seq 1 200); do python3 fuzz/gen.py $i > $D/p$i.ty; done
python3 detect.py --main-only $D                 # generated corpus
python3 detect.py tests/conc                     # hand-written fixtures
grep -lE '^[[:space:]]*for:[[:space:]]*$' $D/*.ty | wc -l
```

### The numbers

| construct | before (`74a6eeb`) | after | how |
|---|---|---|---|
| element-wise array arithmetic, 200 fuzz seeds | **0/200** | **67/200** | detector, `--main-only` |
| element-wise array arithmetic, `tests/conc` | **0/12** | **1/13** | detector, whole-file, then per file |
| bare `for:`, 200 fuzz seeds | **0/200** | **69/200** | detector **and** plain `grep -lE '^[[:space:]]*for:[[:space:]]*$'`, same answer |
| bare `for:`, `tests/conc` | **0/12** | **1/13** | same grep |
| three-clause `for` (already covered) | 200/200 | 200/200 | unchanged |
| `0..<N` (already covered) | 33/200 | 29/200 | unchanged mechanism; the two new kinds shift the per-seed draw |

Phase 19's recorded 0/177 and phase 36's 0/200 were **re-measured, not
inherited** — `fuzz/gen.py` had been edited by a later phase since those numbers
were taken. Both still held at `74a6eeb`; the array count is 0/200 here only
because this run used 200 seeds where phase 19 used 177.

### Phase 19 — what the generator now emits

`fuzz/gen.py:680` (`arr_arith`, registered at `fuzz/gen.py:401`) has three
variants: growable `[int]` over all five operators, growable `[float]` over the
four it has, and fixed `[3]int` — both array kinds, per
`docs/spec/12-aggregates.md:246-253`. Each variant also emits a broadcast with
the scalar on the **left** (`20 - a`), because `-`, `/` and `%` are not
commutative and an implementation that normalised `s OP a` into `a OP s` would
still typecheck and still return an array of the right length
(`docs/spec/12-aggregates.md:263-266`). Results fold into `acc` **by index**, so a
wrong element changes the number rather than merely compiling.

Three hazards, closed by construction rather than by low probability:

- a `[T]` length mismatch **aborts** at runtime, so both operands are literals
  written with the same literal length — no length is ever derived from another;
- `/` and `%` by zero abort, so every divisor array element and every broadcast
  divisor is drawn from `1..9`;
- elements stay in `1..9`, so a product is ≤ 81 and the checksum cannot overflow
  into a UBSan report.

The fixed `[3]int` locals are deliberately **not** put in `env`: a `[N]T` is not a
`[T]`, and the kinds that pick array variables (`push`/`pop`/`slice`/`vscheck`)
would emit a growable-only operation on one and the program would be rejected —
a skip, i.e. a silent loss of the seed.

### Phase 36 — how a generated `for:` is made to terminate

An unterminated generated loop does not fail loudly: `fuzz/run.py`'s
`RUN_TIMEOUT` fires on **both** builds, which is the "timeout" verdict, not a
FAIL. So termination is proved, not hoped for. `fuzz/gen.py:736` (`bare_loop`)
gives all three variants the same five properties:

1. the counter is a **fresh** name, initialised to `0` on the line before `for:`;
2. it is incremented **unconditionally as the first statement of the body**, so
   no `continue` can skip the increment;
3. the `break` test is the next statement, against a small **literal** bound;
4. the body is **fixed** — `gen_block` is never called inside a `bare_loop`, so
   no generated statement can be interleaved that writes the counter;
5. the counter is added to `self.loop_vars`, so the `compound` and `inout_str`
   kinds — which pick a write target out of the environment — cannot take it
   either. That is the same guarantee `loop_vars` already gives the three-clause
   counter (`fuzz/gen.py:37-41`).

Property 4 is the load-bearing one: the three-clause counter is safe because the
header owns the increment, and a bare `for:` has no header. The observed result
is `timeout=0` over 200 seeds with 69 of them containing the form.

### Phase 22 — the decision, and why it was not one of the other two

The three options were an `expect` oracle, a property check needing no second
implementation, or honest retirement.

**Retirement was wrong** because the lane still does real work: over the 4608
cases it proves tychoc never crashes and that every accept emits compilable C.
That half was never the tychoc0 half.

**A property check was wrong** because there is no property here to check. The
sweep's subject is a *decision table* — accept or reject per (type, form) ×
operator × (type, form) — and the only property such a table has is "it says what
the rules say", which is an oracle by another name.

**The `expect` oracle was chosen**, in the shape the three sibling runners use.
Note what "table" has to mean at this arity: `fuzz/run_eqparity.py:138` is a
one-line **rule** (`accept iff the two operands have the same nominal type`), not
an enumerated table, and 4608 enumerated rows could only have been machine-
recorded off the compiler — a photograph, not an oracle. So `expect` at
`fuzz/run_typeparity.py:108` is a rule, derived from the spec:

- arithmetic, string concat, the `char` byte domain — `docs/spec/09-expressions.md:24-40`
- comparison and ordering — `docs/spec/03-types.md:436-457`
- bitwise and shift — `docs/spec/09-expressions.md:83-92`
- literal adaptation — `docs/spec/06-conversions.md:11-27`

and then **reconciled arm by arm against the resolver**, each clause citing the
line it encodes. The reconciliation ran the rule against all 4608 cases and
started at **18 disagreement classes / 48 cases**. Every one was read in
`src/tychoc.c` before the rule moved:

- **`2.5 == 7` is a type error but `2.5 < 7` is not.** Not a bug and not an
  accident: `src/tychoc.c:6050` demands `lt == rt` on the equality path, while
  ordering carries its own int-literal-to-float adaptation at
  `src/tychoc.c:6065-6068` and says why in the comment above it. The oracle
  encodes the asymmetry and names it.
- **`int ± char` is accepted, not only `char ± int`.** `src/tychoc.c:6227-6232`
  is symmetric — "char±int, int±char, char±char". `docs/spec/09-expressions.md:32`
  names only `char ± int`; under-documentation, not divergence.
- **Mixed-width shifts are accepted.** `src/tychoc.c:6101-6107` takes any integer
  value shifted by any integer count and gives the result the **left** operand's
  width. `docs/spec/09-expressions.md:83` says bitwise **and** shift "Operands
  MUST be the same integer type", which is true of `& | ^`
  (`src/tychoc.c:6211`) and false of `<< >>`. The compiler is the sane one — a
  shift count has no reason to share the shifted value's type — so the oracle
  encodes the implemented rule, says so at the clause, and the spec sentence is
  filed as phase 45 rather than being quietly encoded either way.

**What this does not buy, stated because the whole batch is about false
confidence:** the rule was reconciled against the compiler it gates, so a
fail-open the two now share is invisible to it. A *changed* rule reddens; a rule
that was always wrong does not. That is the residue of losing tychoc0 and no
single-implementation oracle removes it. The file's header says this in the same
words.

**Negative control — the oracle bites.** Deleting one clause (`if False and` on
the char arm) in a copy run out of a scratch tree:

```
TYPE-PARITY FAIL: 24/4608 cases bad
  [ORACLE DIVERGENCE]  7 + 'A'   (int + char)   tychoc=accept expected=reject
  [ORACLE DIVERGENCE]  vi + vc   (int + char)   tychoc=accept expected=reject
  ... 24 total, rc=1
```

Before this change the same perturbation was undetectable, because the lane
asserted nothing at all about accept/reject.

### Gate output — the real runs

```
$ python3 fuzz/run.py 200
fuzz: 200 seeds x 2 builds (native -O2, ASan/UBSan -O1), 14 workers
... 200/200  ok=200 skip=0 timeout=0 FAIL=0
DONE: ok=200 skip=0 timeout=0 FAIL=0  (findings in fuzz/findings/)

$ python3 fuzz/run_typeparity.py
type-parity: 4608/4608 scalar binop cases match the `expect` oracle (640 accept / 3968 reject;
             every accept emits compilable C, no crash on any case).

$ make test
passed: 545   failed: 0
all green

$ make conc
conc: passed 38   failed 0

$ python3 scripts/check_citations.py
citation check: ok (152 anchored contain the token they name, 2121 bare in bounds,
115 source->doc citations resolve, 135 source->source in bounds, 12 source->source anchored)
```

`make conc` is outside the batch's stated verify list and was run anyway: this
batch adds `tests/conc/bare_for_arrarith.ty`, and `make test` does not run
`tests/conc` (`Makefile:115-116` vs `Makefile:132-133`), so nothing else would
have compiled the new fixture under ASan and TSan. `make ci` was not run.

## Phases discovered by batch 4

- [x] **Phase 45** — **`docs/spec/09-expressions.md:83` states a shift rule the
      compiler does not implement, and the direction is fail-*open* relative to
      the spec.** The sentence is "**Bitwise and shift** (`& | ^ ~ << >>`).
      Operands MUST be the same integer type." That is exactly right for `& | ^`
      — `src/tychoc.c:6211` requires `lt == rt` — and wrong for `<<` and `>>`,
      which `src/tychoc.c:6101-6107` accepts over any two integer types, giving
      the result the left operand's width. Found by batch 4 while deriving the
      `fuzz/run_typeparity.py` oracle from the spec: the derived rule and the
      compiler disagreed on 16 cases, all of them mixed-width shifts.
  - The compiler's behaviour is the defensible one (a shift *count* is not the
    shifted *value*; Go requires only that the count be an integer), so this is a
    spec repair, not a compiler repair. Do not "fix" it in `src/tychoc.c` — that
    would break `x << n` for every `n: int` against a `u32` `x`.
  - Scope: split the sentence in `docs/spec/09-expressions.md:83` into the
    bitwise rule and the shift rule, and check `docs/spec/appendix-e-conformance.md`
    for a §13.2 row that inherits the wrong claim. `fuzz/run_typeparity.py`'s
    shift clause carries a comment pointing here and should be updated to point
    at the repaired sentence.
  - Done when: the spec states the implemented shift rule, and
    `python3 fuzz/run_typeparity.py` still reports 4608/4608 with its shift
    comment no longer describing a spec defect.
  - Verify: `sh scripts/spec_check.sh`, then `python3 scripts/check_citations.py`.

- [x] **Phase 46** — **`tests/rtparity/run.py` is the other half of phase 22 and
      was not done.** Phase 22's entry proposed, beside the `expect` table,
      turning `tests/rtparity/run.py` into a single-runtime lane asserting the C
      emitted for `tests/rtparity/surface.ty` still contains each expected
      `getenv()` name, trap text and stats row against a recorded list. Batch 4
      closed the `fuzz/run_typeparity.py` half only — the two share a motive but
      not a file, a mechanism, or a verification, and folding an unexamined
      second runner into a batch about generator blind spots would have been the
      scope creep this plan keeps filing phases to avoid.
  - Read `tests/rtparity/run.py` first and re-derive what it asserts **today**;
    phase 22's description of it was written before the batch and was not checked
    by batch 4.
  - Done when: the lane has a written-down oracle or is honestly retired, decided
    the way phase 22 was — with the option not taken written down.
  - Verify: `python3 tests/rtparity/run.py`, then `make test`.

## Batch 5 evidence — phases 20, 21, 26, fixtures and the gates watching them

Ran at `0412395`. Phase 28 was already closed by batch 2 and was left alone.

### Phase 20 — the diagnosis in the phase entry was wrong, and that is the finding

The entry says the golden's cache-name hash "derives from the URL, and the URL
embeds `$PWD`, so the golden is only reproducible in the directory it was
recorded in", and concludes that fixing it "is a `core:http` change". Every part
of that is false, and believing it is why the lane stayed red: it made a
one-command repair look like a corelib redesign.

Read the program instead of the comment. The hash is the **response body's**:

- `examples/fetch/main.ty:45` — `sha := sha256.hex(body)`
- `examples/fetch/main.ty:65` — `cachepath := "/tmp/tycho_fetch_" + sha[0:16] + ext`

The only other URL-derived output field is `examples/fetch/main.ty:46`'s
`path.base(url)`, a basename. Nothing in the output can depend on `$PWD`.

**Measured, not assumed.** One binary, two unrelated absolute paths:

    $ ./tychoc examples/fetch/main.ty -o $T/fetchbin
    $ $T/fetchbin "file://$PWD/examples/fetch/fixture.json" $T/a.cache
    source : fixture.json
    bytes  : 153
    sha256 : 5124059f6a7ee320f20ca58672982b9852d1b25c98e0de8ec8324a5cc00741f3
    $ $T/fetchbin "file:///tmp/tmp.9b1WwT3uTi/some/deep/other/place/fixture.json" $T/b.cache
    source : fixture.json
    bytes  : 153
    sha256 : 5124059f6a7ee320f20ca58672982b9852d1b25c98e0de8ec8324a5cc00741f3

Byte-identical. The output is path-independent already.

**The actual fault: a stale golden, and the commit that staled it is named.**
`39d75be` (the Hier -> Tycho rename) edited the fixture body and did not
re-record the golden:

    $ git show 39d75be^:examples/fetch/fixture.json | sha256sum
    e3de3da05e1cd879055580125a2ec0898b5d3fa457886359560022741f7f4b2b  -   (152 bytes, "name": "hier")
    $ sha256sum examples/fetch/fixture.json
    5124059f6a7ee320f20ca58672982b9852d1b25c98e0de8ec8324a5cc00741f3      (153 bytes, "name": "tycho")

    $ git show 39d75be -- examples/fetch/expected.out
    -cached : hier_fetch_e3de3da05e1cd879.json
    +cached : tycho_fetch_e3de3da05e1cd879.json

That diff is the whole story: the rename rewrote the *prefix* `hier_fetch_` and
left the *hash* untouched, along with `bytes  : 152` and the `sha256` line — all
three describing a body that no longer exists. `e3de3da05e1cd879` is exactly the
pre-rename fixture's hash and `5124059f6a7ee320` is exactly the post-rename
one, which is why the "wants X, gets Y" pair in the phase entry looked like a
machine-dependence and was not. `39d75be`'s own message claims the dogfood digest
goldens were re-recorded; this one was not, and no lane runs it, so nothing said
so for as long as the commit has been in the tree.

**Decision: re-record. Not "make the hash path-independent" (it already is), not
"drop the assertion".** The assertion is the only thing in the tree that checks
`core:http` + `json` + `sha256` + `io` + `path` composing end to end, and it costs
one command to restore. The two rejected options were rejected for stated
reasons: there is no path-dependence to remove, and dropping a golden because it
was left stale rewards the neglect.

Real output, before and after:

    $ sh examples/fetch/run.sh          # before
    FAIL: output != golden
          3,4c3,4
          < bytes  : 152
          < sha256 : e3de3da05e1cd879055580125a2ec0898b5d3fa457886359560022741f7f4b2b
          ---
          > bytes  : 153
          > sha256 : 5124059f6a7ee320f20ca58672982b9852d1b25c98e0de8ec8324a5cc00741f3
          6c6
          < cached : tycho_fetch_e3de3da05e1cd879.json
          ---
          > cached : tycho_fetch_5124059f6a7ee320.json
    fetch: FAIL

    $ RECORD=1 sh examples/fetch/run.sh
    rec  fetch
    fetch: green (http+json+sha256+io+path compose; tychoc+ASan; real libcurl via file://; the tychoc0 leg was retired 2026-07-29)

    $ sh examples/fetch/run.sh          # after
    fetch: green (http+json+sha256+io+path compose; tychoc+ASan; real libcurl via file://; the tychoc0 leg was retired 2026-07-29)

No network was needed or used: the runner GETs `file://` (a libcurl protocol), so
the whole lane is offline and deterministic. The ASan/UBSan leg ran and was
silent throughout — it was never the failing part.

**The restored golden catches something.** One byte of the fixture changed:

    $ sed -i 's/"stars": 9/"stars": 10/' examples/fetch/fixture.json
    $ sh examples/fetch/run.sh
    FAIL: output != golden
          < bytes  : 153 / > bytes  : 154
          < sha256 : 5124059f6a7ee320... / > sha256 : 8299412e5c77f2e1...
          < cached : tycho_fetch_5124059f6a7ee320.json
          > cached : tycho_fetch_8299412e5c77f2e1.json
    fetch: FAIL

Fixture restored afterwards; `git status --short examples/fetch/` shows it clean.

**One repair beyond the golden, because the record path was fail-open.** `RECORD=1`
copied the run's output over the golden unconditionally — including from a run
whose compile had already failed, which would have written an empty file over the
only assertion. It is now guarded. Demonstrated on the guard's real path (a
compile failure, not a golden mismatch — a golden mismatch is what RECORD exists
for and must still record):

    $ printf '\nfn ((( broken\n' >> examples/fetch/main.ty   # 82 lines -> 84
    $ RECORD=1 sh examples/fetch/run.sh
    FAIL: tychoc compile
          error: expected a procedure name, at the appended line 84
    $ grep '^bytes' examples/fetch/expected.out
    bytes  : 152          <- unchanged; the pre-guard code would have emptied it

The wrong `$PWD` explanation was also removed from `examples/fetch/run.sh` and
replaced with the measured one, since it is the comment a future reader will
believe.

**Not fixed, and filed rather than absorbed:** the lane is still in no Makefile
target. That is phase 47.

### Phase 21 — settled by deciding that nothing moves, plus two fixtures that come home

Checked before doing work, as instructed. The fold-back had closed **part** of
it: `tests/postfreeze/` is gone, its goldens are covered by `.gitignore:94`'s
`!/tests/*.out`, and `tests/nested_pattern` is home. It had **not** closed the
phase — `docs/spec/appendix-e-conformance.md` said so itself, in the note the
fold-back left behind: "The remaining fixtures have not been relocated yet; that
is tracked as its own phase." This is that phase.

**The decision, which is that "come home" was the wrong frame.** Two clauses were
still flagged as having no `tests/` fixture *for freeze reasons* — §3.9.4's `\r`
and adjacent-literal join (with §12.2's string fold), and §6.2(7)'s `Ok`/`Err` in
a tuple literal. Their witnesses are `corelib/test/csv`, `corelib/test/httpd`,
`corelib/test/result` and `server/main.ty`. Relocating any of those would be
wrong and the phase entry's "those fixtures can come home" quietly assumes it:
each is **the lane for its own package**, and would sit exactly where it sits
with or without a freeze. Their placement was over-determined, and the freeze was
never the load-bearing reason.

What the freeze actually cost was the **`tests/` witness** — the main golden loop's
native + ASan + byte-identical + golden discipline, which none of those four
runners applies. So nothing was moved and two fixtures were written, following
the precedent `tests/nested_pattern` already set when it came back:

- `tests/crlf_adjacent.ty` — `\r` as byte 13 (`term_bytes = 13,10,13,10`), CR/LF
  counts over a joined multi-line header block, `"ab" "cd" == "ab" + "cd"`, a raw
  piece joined to a cooked one (proving no escape is interpreted inside it:
  `...,114,97,119,92,114,92,110`), and §12.2's `const GREET = "hel" + "lo"` fold.
- `tests/result_tuple.ty` — `Ok`/`Err` built directly in a tuple literal in a
  `return`, at a call site, with a payload-carrying `Err` variant, and with a heap
  `[int]` `Ok` payload.

The convention this settles on is written into
`docs/spec/appendix-e-conformance.md` §E.1 as a dated note: a clause gets a
`tests/` fixture; a package's own lane covers the package; no carve-out. The three
E.2 matrix rows now cite the new fixtures, and the two E.2.1 entries are amended
to say they are no longer flagged, keeping the old rationale as history because it
is why the gap lasted. The two source files that still asserted a live constraint —
`corelib/test/result/main.ty` and `corelib/test/httpd/main.ty` — now say the
constraint is dead and that they stay for their own reasons. Both still compile.

**The gate that watches this is `sh scripts/spec_check.sh`,** per `CLAUDE.md`'s
gate table: Appendix E's backticked `tests/…` paths must resolve. It passes, and
it is demonstrably looking at the new citations rather than ignoring them:

    $ sed -i 's|`tests/result_tuple`|`tests/result_tuplo`|' docs/spec/appendix-e-conformance.md
    $ sh scripts/spec_check.sh
    spec-check: FAIL — Appendix E cites fixtures that do not exist:
        tests/result_tuplo
    $ # restored
    $ sh scripts/spec_check.sh
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 9 runnable example(s), all pass

### Phase 26 — nothing remained; the box was open, the work was not

Both halves had landed. Established by blame rather than by reading the phase
text:

    $ git blame -L 3241,3241 src/tychoc.c
    3f68a00  feat(compiler)!: phase 7 — delete the range() counting form
    $ git blame -L 78,78 docs/spec/13-concurrency.md
    a4f2991  docs: phase 9 — spec the Odin-style loop forms and the removal of range()

`src/tychoc.c:3241` now reads ``parallel supports `for i in 0..<N` and `for x in
collection` loops only`` — the wording phase 26 specified, verbatim, including the
`0..<N` spelling phase 5 added and without the `range(...)` spelling phase 7
deleted. The second aspect the entry flags is the prose, and it is the half the
entry assigns to phase 9: `docs/spec/13-concurrency.md:78` opens §22 with "applies
to a **counting** or a **foreach** loop" and the counting spelling is given as
`parallel for i in 0..<N:`; `range(a, b, step)` survives in that section only as
"the form this replaced". Nothing was redone.

One thing the phase did **not** cover and that this batch did not widen into: no
`tests/diag/` fixture asserts that message text, so the wording is correct today
and unguarded tomorrow. Filed as phase 48.

### Gates

    $ sh examples/fetch/run.sh
    fetch: green (http+json+sha256+io+path compose; tychoc+ASan; real libcurl via file://; the tychoc0 leg was retired 2026-07-29)

    $ sh scripts/spec_check.sh
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 9 runnable example(s), all pass

    $ make test
    passed: 547   failed: 0
    all green

    $ python3 scripts/check_citations.py
    citation check: ok (152 anchored contain the token they name, 2128 bare in bounds,
    116 source->doc citations resolve, 138 source->source in bounds, 12 source->source anchored)

    $ sh scripts/check_links.sh
    link check: ok (134 markdown files, no dead relative links)

`make ci` was deliberately not run; batch 6 closes with the full sweep.

**`make test`'s 547 includes the two new fixtures, and their goldens bite.** A
green count alone would not prove either, so both goldens were corrupted by one
value and the suite re-run:

    $ sed -i 's/ok:3/ok:4/' tests/result_tuple.out
    $ sed -i 's/13,10,13,10/13,10,13,11/' tests/crlf_adjacent.out
    $ make test
    FAIL  crlf_adjacent  (output != golden (tests/crlf_adjacent.out))
    FAIL  result_tuple  (output != golden (tests/result_tuple.out))
    passed: 545   failed: 2
    failed: crlf_adjacent result_tuple

545 + 2 = 547, so the two fixtures are inside the count and are scored by name.
Goldens restored; the green run above is the post-restore state.

The two `corelib/test/*/main.ty` edits are comment-only and so cannot move a
`make corelib` golden, but a comment can still break a parse, so both were
compiled: `compiles: corelib/test/result`, `compiles: corelib/test/httpd`.

## Phases discovered by batch 5

- [x] **Phase 47** — **`examples/fetch/run.sh` is in no Makefile target, which is
      the reason phase 20 existed at all.** The golden was stale from `39d75be`
      onward and nothing said so, through an entire prior plan and five batches of
      this one. `scripts/entrypoints.sh` proves the entry point *compiles*; it
      does not run the runner. Three of the four sibling example runners are wired
      in and this one is not.
  - Scope: `Makefile` and, if the target is one `scripts/ci.sh` already calls,
    that file. Nothing in `examples/fetch/` — it is green and was repaired by
    batch 5.
  - Decide which target: the lane needs `libcurl` and self-skips without it
    (`fetch: SKIP (libcurl not installed)`), so it is safe to add unconditionally,
    but note it builds under ASan/UBSan and costs real seconds — `make ci` may be
    the honest home rather than `make test`.
  - **Do this in or after batch 6, never before it.** Batch 6 closes with the
    full `make ci` sweep and batch 3 added a `[12b/13]` step that has never been
    exercised in a full run; adding a second unexercised step to the same sweep
    would make a failure ambiguous between the two.
  - Done when: a named `make` target runs `examples/fetch/run.sh`, and a
    deliberately corrupted `examples/fetch/expected.out` is shown reddening that
    target — not just the runner, which batch 5 already demonstrated.
  - Verify: the new target, then `make ci` once.

- [x] **Phase 48** — **the `parallel for` diagnostic's wording is asserted by
      nothing.** Batch 5 confirmed `src/tychoc.c:3241` carries the text phase 26
      specified, but confirmed it by reading the source, which is exactly the
      check that does not survive the next edit. `tests/reject/*.ty` asserts only
      a nonzero exit and a non-empty diagnostic (`tests/run.sh` says so in the
      reject loop's header comment), so a message that silently degrades to
      something useless still passes. `tests/diag/` is the lane that goldens
      diagnostic *text* — `diag_range_removed` and `diag_dotlt_sequential` already
      live there — and it has no `parallel` fixture at all.
  - Scope: one `tests/diag/` fixture and its golden. No `src/tychoc.c` change —
    the message is already correct.
  - The program to reject is `parallel for i := 0; i < 3; i += 1:`, the
    three-clause form, which is the shape phase 26 named as the user-facing case.
    Check the foreach-of-an-expression refusal too if it lands in the same arm.
  - Done when: the fixture exists, `make test` scores it, and flipping one word of
    the message in `src/tychoc.c` is shown reddening it.
  - Verify: `make test`.

- [x] **Phase 49** — **25 `corelib/test/*/main.ty` headers still claim "the C
      compiler and tychoc0 must agree".** No lane has built `tychoc0` since
      2026-07-29 and `corelib/run.sh:6` already records the retirement in its own
      header, so every per-test header beneath it now advertises a differential
      guarantee the tree does not provide. This is the same class batch 3 swept
      for documents, one directory it did not reach. Counted, not estimated:
      `grep -rl 'tychoc0 must agree' corelib/test/*/main.ty | wc -l` gives 25.
  - Scope: the header comment of each matching `corelib/test/*/main.ty`. Comments
    only — no program text, so no golden can move.
  - Say what the lane actually asserts now (golden-validated by `corelib/run.sh`)
    rather than deleting the sentence, and keep the loss visible the way
    `corelib/run.sh`'s own header does.
  - Done when: no `corelib/test/*/main.ty` asserts a live `tychoc0` comparison,
    and each edited file still compiles.
  - Verify: `make corelib`, then `python3 scripts/check_citations.py`.

- [x] **Phase 50** — **`corelib/test/io/main.ty` still explains two fixtures'
      *location* by the dead freeze.** `corelib/test/io/main.ty:43` says the
      nested-pattern arm is written there because "no runner feeds corelib/test/
      to the frozen compiler/tychoc0.ty", and `corelib/test/io/main.ty:148` says
      the interior-NUL `bytes` fixture "CANNOT live in tests/ -- frozen tychoc0
      rejects `b[i]`". Batch 5 closed exactly this class for
      `corelib/test/result/main.ty` and `corelib/test/httpd/main.ty` but did not
      widen to `corelib/test/io/main.ty`, because the NUL case is a clause
      (§3.9.4's interior NUL) that neither batch 5 fixture covers and it needs its
      own decision, not a comment edit bolted onto someone else's phase.
  - Scope: `corelib/test/io/main.ty`'s two comments, and — if the decision goes
    that way — one `tests/` fixture plus its golden and the matching Appendix E
    row. `tests/string_nul` already exists; check first whether it covers the
    `b[i]` / slice-length / concat properties the comment enumerates, in which
    case the work is only to correct the comment and cite it.
  - Apply batch 5's convention from `docs/spec/appendix-e-conformance.md` §E.1:
    a clause gets a `tests/` fixture, a package's own lane covers the package,
    and nothing moves merely because it now could.
  - Done when: neither comment asserts a live constraint, and §3.9.4's interior
    NUL either has a `tests/` witness or a written reason it does not.
  - Verify: `sh scripts/spec_check.sh`, then `make test`.

## Phases discovered by batch 6

- [x] **Phase 51** — **`char` is a real element type with no way to write its
      name, no conversion into it, and no hex escape — and that is a language
      change, not cleanup.** Split out of phase 16, which batch 6 closed as
      "needs its own plan" after re-verifying all four symptoms against the
      built compiler. The compiler has `T_CHAR` (`src/tychoc.c:548`), a builtin
      that produces one (`char_at`, `src/tychoc.c:4526`), a deliberately
      narrower operator set (`+` and `-` only, `src/tychoc.c:1029`), a C
      representation (`tycho_int`, `src/tychoc.c:1361`) and a diagnostic
      spelling (`src/tychoc.c:7435` returns the string `"char"`). What it has
      no way to do is let a user *write* the type.
  - **Three independent decisions, in dependency order:**
    1. **The type name.** Two parse sites reject it differently: a parameter or
       return position gives `unknown type 'char'` (`src/tychoc.c:2141`), an
       element position gives `expected a type (int, float, bool, string,
       [int], or a struct)`. Adding the name to both immediately raises what
       `[]char` *means* next to the existing `bytes` — two spellings of a byte
       sequence, with different operator sets. That is a §12 / §16 question.
    2. **`to_char`** (`unknown procedure 'to_char'`): a `Sig` row, a codegen
       case, and a decision on out-of-range ints — abort like the bounds
       accessors, wrap to a byte, or return a `Result`.
    3. **`\xNN`** in both literal forms — the char escape table
       (`src/tychoc.c:464`) and the string one (`src/tychoc.c:384`) — plus what
       `\xNN` above 0x7F means in a string the tree treats as UTF-8.
  - **Why it matters beyond ergonomics:** `char` is the one element type whose
    operator set is narrower than `int`'s, and it is also the only one that
    cannot appear in a fixture's type annotations. The narrow rule at
    `src/tychoc.c:1029` is therefore the least directly testable rule in the
    resolver. Any plan here should add the fixtures *first*.
  - Done when: each of the three has a decision recorded (implemented or
    declined with a reason), and §12/§16 match the tree.
  - Verify: `make test`, `sh scripts/spec_check.sh`.

  > **Evidence (batch 12).** Two of the three implemented, one declined and
  > filed. `src/tychoc.c` stayed at **12774 lines** across all four edits, by the
  > batch-7/batch-11 discipline — measured first: 1373 bare and 55 anchored
  > `src/tychoc.c:N` citations sit above the lexer edit point, so a single
  > inserted line would have rotted 1373 refs that `scripts/check_citations.py`
  > only bounds-checks. The gate's clean run below is the proof it held.
  >
  > **Fixtures first, as the brief required, and they earned it.** The narrow
  > operator rule is `elem_arith_ok`'s char arm, `src/tychoc.c:1029@et == T_CHAR`.
  > It could not be annotated, but it *is* reachable by inference: an array
  > literal of `char_at` calls infers `[char]` without the type ever being
  > written. Measured at HEAD, before any compiler change:
  >
  >     [char] + [char], [char] - [char]   compile and run
  >     [char] * [char]   error: `*` is not defined element-wise on [char], because `*` is not defined on char
  >     [char] / [char]   same shape
  >     [char] % [char]   same shape -- but from src/tychoc.c:1025@TK_PERCENT, NOT the char arm
  >
  > That last line is why `%` got its own fixture: deleting the char arm at
  > `src/tychoc.c:1029` would leave `tests/reject/char_elem_mod.ty` green and the
  > other two red, so the three are not redundant. Four fixtures
  > (`tests/char_elem_ops.ty` plus the three rejects) now pin a rule that
  > previously had no element-wise witness at all — the pre-existing
  > `tests/reject/char_int_*.ty` exercise the *scalar* char arm at
  > `src/tychoc.c:6232-6234`, a different site.
  >
  > **Decision 1 — the type name: DECLINED, and the fork filed as phase 67.**
  > Not a judgement call. The absence is **normative published text in four
  > places**, all re-read: `docs/spec/03-types.md:75` ("arises by inference;
  > there is no `char` type keyword"), `docs/spec/02-grammar.md:175` ("There is
  > no `char` or `void` type spelling"), `docs/spec/01-lexical.md:116`, and
  > `docs/spec/01-lexical.md:373` (the tree-sitter grammar is called *wrong* for
  > listing `char` as a type keyword). It is also pinned by a fixture that
  > predates this phase: `tests/reject/char_as_type.ty`, whose comment states the
  > rule outright. Adding the name would falsify four sentences and turn a green
  > reject red. The brief's own framing — that `[]char` beside `bytes` is a
  > genuine design fork belonging to the user — is the second reason, and the
  > two parse sites re-derived (`src/tychoc.c:2141` for a named-type position,
  > `src/tychoc.c:2159` for the keyword switch) are recorded in phase 67 so the
  > work is one edit if the user says yes.
  >
  > **Decision 2 — `to_char`: IMPLEMENTED, aborting out of range.** The brief
  > asked for the established answer rather than a fourth. There are four
  > candidates in tree and they split cleanly two-and-two:
  >
  >     chr(n)            int -> one-byte string   ABORTS outside 0..255
  >     to_int(float)     float -> int             ABORTS (runtime/tycho_rt.c:185-187)
  >     to_u8 .. to_i64   numeric -> fixed width   TOTAL, wraps
  >     char +/- int      arithmetic on a byte     wraps to a byte
  >
  > The abort side wins on a stated rule, not a majority: `runtime/tycho_rt.c:1180-1182`
  > says of `chr` "A value outside 0..255 is a program error and aborts cleanly
  > (like a bad index), rather than silently masking to a byte." That is the same
  > domain (`0..255`) and the same direction as `to_char`. The wrapping pair are a
  > different category — `to_u8` is documented as a **total reinterpretation**
  > (`docs/spec/06-conversions.md:40`), and `char ± int` is arithmetic, whose
  > wrap is separately normative at `docs/spec/03-types.md:76-77`.
  >
  > Implementation reuses `tycho_chr` rather than adding a runtime function, on
  > purpose: `to_char` therefore emits **no new `tycho:` trap text**, so the
  > rtparity oracle (CI step `[2d/13]`) sees nothing new, and the abort is
  > byte-identical to the one `tests/abort/chr_oob.ty` already pins. Two limits
  > were accepted rather than fixed, and both are written into
  > `docs/spec/16-builtins.md` rather than left for a reader to discover: the
  > abort message names `chr` even when the call was `to_char`, and `to_char` is
  > **not** added to `is_ufcs_builtin` — that list carries a "Kept byte-identical
  > with tychoc0.ty's is_ufcs_builtin" invariant at `src/tychoc.c:4869`, and
  > `compiler/tychoc0.ty` can no longer be built since the frontparity retirement,
  > so the invariant cannot be re-established once broken. `to_char(n)` is the
  > spelling; `n.to_char()` is not.
  >
  > **Decision 3 — `\xNN`: IMPLEMENTED in char literals, DECLINED in strings.**
  > The brief's warning was correct and `docs/spec/01-lexical.md` §3.9.4 states it
  > verbatim: joining is defined on the literals' **escaped source text** and is
  > sound only because every escape is exactly two characters, so a greedy `\x`
  > absorbs a hex digit across a join — `"\x4" "1"` would mean one byte where the
  > author wrote two. The same paragraph is the recorded reason `\0` was kept out.
  > That reason is about **representation, not hex**, which is exactly why it does
  > not transfer to a char literal: `src/tychoc.c:452-477` decodes the escape to a
  > byte in `ival` at lex time, and it never reaches a C string literal. `\0` was
  > already legal in a char literal and illegal in a string for that same
  > asymmetry, so `\xNN` joins an existing precedent rather than creating one.
  > Fixed at **exactly two** hex digits — `'\x4'` is an error, not a short read —
  > because a variable-width `\x` is the C behaviour being avoided. Verified:
  >
  >     '\x41' == 'A'         true      '\x0a' == '\n'     true (and '\x0A')
  >     to_int('\x00')        0         to_int('\xFF')     255
  >     '\x4'                 error: \x takes exactly two hex digits (e.g. '\x41')
  >     '\xZ1'                same
  >     "\x41"                error: unsupported escape \x (use \n \t \r \\ \")   <- decision 3's decline, still red
  >     to_char(300)          tycho: chr(300) out of byte range 0..255
  >
  > **Spec brought level with the tree.** §12's `char` row
  > (`docs/spec/12-aggregates.md:258`) already matched the compiler but had no
  > witness; it now names all four fixtures and explains why `%`'s is separate.
  > §16 gained the `to_char` row and its out-of-range reasoning; §5.2.4 gained
  > `to_char` and a plain statement that the missing keyword is what makes the
  > narrow operator set hard to test; §3.9.3 and Appendix A gained the `\xNN`
  > grammar (both edited — `spec_check.sh` compares them); §3.9.4's "the reason
  > `\0` and `\xNN` are not in the escape set" now says *this* escape set and
  > points at the char-literal exception; Appendix E gained three rows.
  >
  > **Gates, run one per command in the foreground. `make ci` NOT run** (no CI
  > step added or altered — `to_char` deliberately introduces no new runtime trap,
  > and no `editors/` or `tools/` file enumerates builtin names, so
  > `editors-check` and `tools_check.sh` are untouched by construction):
  >
  >     make test                        -> passed: 560   failed: 0   all green
  >     sh scripts/spec_check.sh         -> Appendix A grammar matches §3/§4 (ok)
  >                                         all Appendix E fixture citations resolve (ok)
  >                                         9 runnable example(s), all pass
  >     python3 scripts/check_citations.py -> ok (165 anchored contain the token they name,
  >                                         2616 bare in bounds, 129 source->doc citations
  >                                         resolve, 171 source->source in bounds,
  >                                         12 source->source anchored)
  >     sh scripts/check_links.sh        -> ok (134 markdown files, no dead relative links)
  >
  > **560 = 552 + 8**, every unit accounted for: 3 positives
  > (`tests/char_elem_ops.ty`, `tests/char_to_char.ty`,
  > `tests/char_hex_escape.ty`) and 5 rejects (`tests/reject/char_elem_mul.ty`,
  > `char_elem_div.ty`, `char_elem_mod.ty`, `hex_escape_in_string.ty`,
  > `hex_escape_one_digit.ty`). No existing count moved.
  >
  > **What is NOT verified.** `make ci`, `sh scripts/asan_self.sh`,
  > `sh scripts/tools_check.sh` and `make editors-check` were not run — out of
  > this phase's stated gate budget. The reasoning above for why none of them can
  > redden is a source argument, not a measurement.

- [x] **Phase 52** — **the default build leaves `<base>.c` beside the source
      too.** Found by batch 6 while closing phase 25, which named only
      `--emit-c`. `./tychoc tests/for3.ty` writes `tests/for3.c`, hands it to
      `cc` (`src/tychoc.c:12757`) and never removes it, so the plain compile
      path strays the same untracked artifact `--emit-c` used to — and phase
      25's finding that no by-pattern `.gitignore` rule is safe (31 directories
      hold both `.ty` sources and tracked `.c` shims) applies here unchanged.
  - Not the same fix: stdout is not available to this path, which needs the C
    on disk for `cc`. The options are a temp file removed on success, or
    keeping it and saying so.
  - **Weigh it against `-g`:** `docs/guides/debugging.md:37` tells users to run
    `--emit-c -o program` precisely to *keep* the `.c` for a debugger, so the
    emitted file is a documented artifact for at least one workflow. Deleting
    it unconditionally would break that instruction.
  - Done when: the default path either does not leave a file in the source
    tree, or its doing so is documented as intended and the `.gitignore`
    situation is stated.
  - Verify: `make test`, then `git status --short` after a plain build.

- [x] **Phase 53** — **delete `Stmt.r_step`, with a Done-when that is actually
      satisfiable.** Replaces phase 30, which batch 6 closed as "the guards
      stay" after finding its acceptance criterion self-contradictory: it
      demanded the emitted C be unchanged, but the step machinery *is* emitted
      C (`src/tychoc.c:10885-10889`), so no honest deletion can satisfy it.
  - Corrected Done when: `grep -n r_step src/tychoc.c` is empty; every
    `S_FORRANGE` loop's emitted C loses the `_stopN`/`_stepN` pair, the
    `tycho: range step is zero` abort and the `_stepN > 0 ? ... : ...` ternary,
    keeping only `h_i < _stopN` and `h_i += 1`; **behaviour** is unchanged on a
    fixture of each shape (foreach over an array, foreach over a map,
    `parallel for`), shown by running them, not by diffing C; and `make test`
    and `make conc` hold at their counts.
  - **Order: after phase 17.** The deletion shifts essentially every one of the
    485 live `src/tychoc.c:N` citations at lines ≥ 1553. The anchored subset
    reddens `scripts/check_citations.py` and can be repaired mechanically; the
    bare majority would rot silently, and what happens to that population is
    exactly what phase 17 is still open on.
  - **Batch 8 added a THIRD blocker the two above do not cover, and it is the
    one that would have shipped a wrong spec.** `r_step`'s existence is asserted
    *normatively* by the spec, not merely by a comment:
    `docs/spec/10-statements.md:132` says "The step codegen and its zero-step
    guards still exist but are unreachable", with the provenance
    `src/tychoc.c:1553-1559` — the `Stmt` field itself. Deleting the field
    falsifies a published sentence and orphans a spec citation. The Done-when
    above says nothing about the spec and the Verify line omits
    `sh scripts/spec_check.sh`, so an agent following this entry as written
    would leave §14.4 describing code that no longer exists. **Both are now part
    of the phase**: the deletion must rewrite `docs/spec/10-statements.md:132-133`
    (the zero-step *trade* at `docs/spec/10-statements.md:115-121` is unaffected and stays) and re-point
    its provenance. This is also the reason batch 8 did not simply delete the
    field despite the brief's "either the reasoning is new or the guards go" —
    the reasoning is new, and phase 17 is still open besides.
  - Verify: `make test`, `make conc`, `sh scripts/spec_check.sh`,
    `python3 scripts/check_citations.py`.

- [x] **Phase 54** — **the three DYNAMIC array element diagnostics are
      allow-lists that name neither what works nor what does not.** Found by
      batch 7 while fixing the same defect at the fixed-size sites; the dynamic
      sites were out of that phase's scope and are filed rather than absorbed.
  - `src/tychoc.c:2036` and `src/tychoc.c:2353` both say "array elements must be
    int, float, bool, string, a struct, or an array"; `src/tychoc.c:5110` says
    the same plus "or an Option". All three guard a test of `elem == T_VOID`
    alone, so the list is decorative — it describes neither the check above it
    nor the language.
  - **Measured, not assumed.** `[bytes]`, `[(int,int)]` and `[[string: int]]`
    all compile today and none is named by the message. This is the identical
    defect `tests/reject/bounded_elem_bool.ty` was written to complain about,
    and which batch 7 fixed at `src/tychoc.c:1999-2000` and `:2018-2019`.
  - Done when: the three messages name the real rule (only `void` is refused),
    and a `tests/reject/` fixture pins one of them.
  - Verify: `make test`, plus the two doc gates.

- [x] **Phase 55** — **literal adaptation does not reach array-literal
      elements.** `a: [u32] = [1, 2]` is rejected with "declared type `[u32]`
      but value is `[int]`", while `[u32]` is a perfectly legal type — the same
      array built with `a := []u32` then `push(a, 1)` compiles. Found by batch 7
      while checking which element types the dynamic sites accept.
  - `docs/spec/06-conversions.md:11-27` describes literal adaptation for
    scalars; whether it is meant to distribute over an array literal is not
    stated either way, so this is a spec question before it is a compiler one.
  - Done when: the spec says which it is, and whichever answer is chosen is
    pinned by a fixture — an accepting one under `tests/`, or a
    `tests/reject/` case whose diagnostic explains the `push` workaround.
  - Verify: `make test`, `sh scripts/spec_check.sh`, plus the two doc gates.

- [x] **Phase 56** — **§30 still promises the zero-step abort §14.4 says was
      removed.** Found by batch 8 while establishing what `r_step` is worth
      (phase 53). `docs/spec/17-runtime.md:41-42` lists, among the defined
      aborts, "**A `range` step of zero** — a literal `0` step is a compile
      error; a step that evaluates to `0` at run time aborts (§10)".
  - `range(a, b, step)` was removed on 2026-07-29 and
    `docs/spec/10-statements.md:115-121` says so at length, in the opposite
    direction: "**The zero-step guarantee is gone, and that is a deliberate
    trade, not an oversight** … `for i := 0; i < n; i += 0:` is an infinite loop
    and the implementation **does not diagnose it**, at compile time or at run
    time." Two normative sections of the same document contradict each other,
    and the §10 cross-reference points at the section that refutes it.
  - Not a compiler change: the code already matches §14.4. Delete or rewrite the
    §30 bullet, and check the abort list in `appendix-e-conformance.md` for a
    matching row while there.
  - Done when: no section of `docs/spec/` claims a zero-step diagnostic.
  - Verify: `sh scripts/spec_check.sh` plus the two doc gates. **Not**
    `make test` — no `.ty` and no compiler source is involved.

- [x] **Phase 57** — **three cleanup entries that phase 52 made dead.** Filed,
      not absorbed, because phase 52's scope was `src/tychoc.c`'s build path.
  - `Makefile:314`'s `clean` still does `rm -f … tycho.c tychofmt.c
    tycho-lsp.c`. Those three were the sibling C left by `make tycho`,
    `make tychofmt` and `make tycho-lsp`; the plain build now removes its own
    intermediate, so the three `rm -f` arguments can never match. Harmless
    (`rm -f` on a missing file is a no-op) but misleading.
  - `.gitignore:35`, `:39`, `:43`, `:49` and `:52` (`/tycho.c`, `/tychofmt.c`,
    `/tycho-lsp.c`, `/tycho-httpd.c`, `/server/main.c`) ignore the same
    now-unreachable artifacts. `/compiler/*.c` and `/tychoc0.c` are **not** in
    this set — those come from `make bootstrap`, a different path.
  - Decide per entry rather than sweeping: an entry that still catches a real
    artifact stays. Confirm by building each target and looking, not by reading.
  - Done when: every remaining entry is one a build can still produce.
  - Verify: `make tycho tychofmt tycho-lsp server`, then `git status --short`
    is empty. **Not** `make test`.

## Batch 6 evidence

Six phases: 16, 25, 29, 30, 39, 40. Three are code, three are recorded
decisions. Every claim below was run, not reasoned about.

### The three code changes

**Phase 29 — the LSP's three missing keywords.** `tools/lsp.ty:1175`'s
`sem_is_keyword` gained `parallel`, `select` and `or_return`. The phase's
"check both directions first" caveat was checked: `handle` is a hard lexer
token (`src/tychoc.c:187`) and `const`/`sink`/`where`/`soa` are soft ones —
`TK_IDENT` compared by text in the parser (`src/tychoc.c:3116`, `:3661`,
`:3695`, `:1911`) — so the list had drifted in one direction only and nothing
was removed. `sh scripts/tools_check.sh` reports `semtok=True`.

**Phase 39 — the parfor capture hole.** `pf_scan_expr`
(`src/tychoc.c:6456-6466`). Read by hand, because the fuzz runner compares only
accept-vs-reject and cannot tell these two rejections apart:

    $ ./tychoc pc.ty --emit-c -o pc          # push(xs, i), xs captured
    pc.ty:4: error: parallel for cannot mutate captured variable 'xs' in place
         4 |         push(xs, i)
    $ ./tychoc ic.ty --emit-c -o ic          # xs[0] = i, the sibling
    ic.ty:4: error: parallel for cannot mutate captured variable 'xs' in place
         4 |         xs[0] = i

Identical wording, which is the phase's "Done when". The gate is not a blanket
ban — a chunk-local `push` still compiles:

    $ ./tychoc local.ty --emit-c -o lo       # ys := [1] declared INSIDE the loop
    wrote lo.c   (rc=0)

**Phase 25 — `--emit-c` with no `-o`.** Now stdout (`src/tychoc.c:12708`):

    $ rm -f tests/for3.c
    $ ./tychoc --emit-c tests/for3.ty > x.c
    emit-c-no-o rc=0
    stray tests/for3.c exists: NO
    captured C lines: 2595   first line: /* Tycho runtime - embedded verbatim ...
    stderr: (empty)
    $ ./tychoc --emit-c tests/for3.ty -o withO   # -o path unchanged
    with -o rc=0 wrote: YES

### The three decisions

**Phase 16 — `char`: reduced to "this needs its own plan" (phase 51).** All
four symptoms re-verified against the built compiler:

    fn f(c: char) -> char:        -> error: unknown type 'char'
    cs: []char = []               -> error: expected a type (int, float, bool, string, [int], or a struct)
    print(to_char(65))            -> error: unknown procedure 'to_char'
    c := '\x41'                   -> error: unsupported char escape (use \n \t \r \0 \\ \')

Note the first two differ: two parse sites reject the name in two different
ways, so "add `char` to the type parser" is two edits and a `[]char`-vs-`bytes`
design question. Three separate language additions, one of which has a spec
section attached. Not cleanup.

**Phase 30 — `r_step` stays, and phase 7's stated blocker was wrong.** Phase 7
deferred this because "phase 27's elision recogniser is specified against
`s->r_step == NULL`". Phase 27's recogniser is `for3_elidable_arr`
(`src/tychoc.c:8023-8048`) and contains no mention of `r_step` — it matches
S_FOR3's init/cond/post triple, which has no step. The recogniser that *does*
test it is the older S_FORRANGE one (`src/tychoc.c:10902`); `git blame` dates it
to the pre-plan commit *"hierc bounds-check elision for monotone loop indices
(array-pipeline 132->47ms ~2.8x)"*. The real blocker is that the phase's own
"Done when" cannot be met — `r_step` is emitted C, not just an AST field:

    $ ./tychoc --emit-c r2.ty | grep -n '_step\|_stop'     # for x in xs:
    2447:        tycho_int _stop1 = ((h__fc0).len), _step1 = 1L;
    2448:        if (_step1 == 0) { fprintf(stderr, "tycho: range step is zero\n"); exit(1); }
    2449:        for (tycho_int h__fi0 = 0LL; _step1 > 0 ? h__fi0 < _stop1 : h__fi0 > _stop1; h__fi0 += _step1) {

Every foreach loop in the corpus emits those three lines. "Still emit the same
C as before" is unsatisfiable by any honest deletion. Refiled as phase 53 with a
behaviour-based criterion and an ordering constraint after phase 17 — measured:
**485 live `src/tychoc.c:N` citations sit at lines ≥ 1553** (plus 515 in the
frozen archives) and would all shift.

**Phase 40 — recorded in `for3_elidable_arr`'s header
(`src/tychoc.c:8006-8022`), option (a).** Half the recording already existed:
`bench/guard.sh:49-62` carries the measurement for the reader asking why that
lane asserts C text instead of a ratio. The reader it did not serve is the one
editing ~50 lines whose failure mode is a memory-safety bug. Not the spec (a
codegen trade-off with no observable language semantics); not a new
`docs/internals/` file (nobody opens it). Option (a) — keep it, because it is
the only thing that elides at `-O0`/`-O1`, which is what `tychoc -g` builds
(`src/tychoc.c:12754`). Option (b) stays open and gated on a second toolchain,
and that instruction now lives in the header rather than only here.

### What the sweep found — three failures no targeted gate had seen

This is the part worth reading. `make ci` ran four times; the first three were
red, and none of the three was a mistake in the six phases' own logic.

**1. `make conc`, 13 of 38 fixtures — `FAIL <name> (tychoc)`.**
`tests/conc/run.sh:41` emitted with **no `-o`** and then did
`mv "${f%.ty}.c" "$c"`: it created `tests/conc/<name>.c` *inside the tree* on
every run and moved it out again — the exact stray-artifact behaviour phase 25
removed. With the C going to stdout, its `>/dev/null` swallowed it. Fixed by
giving it `-o`; the `mv` is gone with it. `make conc: passed 38 failed 0`, and
`ls tests/conc/*.c` is empty afterwards.

**2. `sh scripts/tools_check.sh` — the bytes-rehome lane.** Same cause,
`scripts/tools_check.sh:283`. It failed loudly with the message its own header
(`:275`) predicts for a rotted fixture — `grep: .../brh/main.c: No such file or
directory` → *"bytes field NOT re-homed -- copy_into missing T_BYTES (dangling
UAF!)"* — which is the lane working as designed. Fixed with `-o`; re-run
`tools-check: ok`.

Together these two correct the claim written into phase 25's first draft: "every
in-tree caller passes `-o`" was **false**, drawn from a truncated grep. An
exhaustive re-scan of every `--emit-c` *invocation* (as opposed to prose
mention) in `*.sh`, `*.py` and `*.ty` now shows all of them explicit.

**3. `scripts/ci.sh:124` — a shell syntax error in the `[12b/13]` step, which
had never been reached.** Batch 3 added the step with a triple backtick inside a
double-quoted string; to `/bin/sh` that is backquote command substitution, and
dash aborted the whole suite with `scripts/ci.sh: 133: Syntax error: EOF in
backquote substitution` — after 17 minutes of green steps. Batches 4 and 5
deliberately deferred `make ci` to this batch, so this was the first time the
line was parsed. Rewritten without backticks; `sh -n` and `dash -n` both clean.

**4. `editors/zed/README.md:14` — corpus count 829, tree 832.** The lane batch 3
added (phase 12) works; nothing had run it since three `.ty` fixtures landed —
`tests/crlf_adjacent.ty`, `tests/result_tuple.ty` (batch 5) and
`tests/conc/bare_for_arrarith.ty` (batch 4). Count corrected;
`make editors-check: ok`.

### Citation repair

Adding ~55 lines to `src/tychoc.c` shifted **130 anchored citations** across 17
files. They were repaired mechanically, not by hand-guessing: a
`difflib.SequenceMatcher` map from `git show HEAD:src/tychoc.c` to the new file
(12694 of 12703 old lines map 1:1; the 9 unmapped are the lines actually
edited), applied only to the refs `scripts/check_citations.py` itself flagged,
each verified by re-running the gate. The **bare** `src/tychoc.c:N` population
shifted too and is *not* repaired here — that is phase 17's open decision, and
the same call phase 27 made. The map is reproducible from this commit's diff.

Three of the 17 are `docs/internals/` records, two of them frozen `-DONE.md`
archives, which looks like it violates the leave-archives-alone rule. It does
not: that rule exempts archives from the *mandatory-anchor* and absolute-path
rules, not from anchor **staleness** — an anchored ref in an archive is still
checked, so an archive with anchored refs into `src/tychoc.c` must move with it
or the gate reddens. Precedent is exact: phase 27 (`fc921d7`) changed 18 lines
of `docs/internals/plan-postfreeze-rawstring-DONE.md` alongside its own
`src/tychoc.c` edit, for this reason. Only the line numbers move; no archive
prose was touched.

### Gate output

    $ make test
    passed: 547   failed: 0
    all green

    $ sh scripts/tools_check.sh
    832 files checked  (compilable=393)  idempotence-fails=0  semantic-fails=0
    semtok=True
    bytes field re-homed on struct return
    tools-check: ok

    $ python3 scripts/check_citations.py
    citation check: ok (152 anchored contain the token they name, 2176 bare in bounds,
    117 source->doc citations resolve, 154 source->source in bounds, 12 source->source anchored)

    $ make conc
    conc: passed 38   failed 0

    $ make editors-check
    ok  README says 832 committed .ty files, and so does the tree
    832 files parsed; the only failure is the enumerated known-bad set
    editors-check: ok

    $ make docs-fences
    docs-fences: 10 fence(s) compiled, 30 skipped (reasons above), 0 failure(s)

    $ make check-links
    link check: ok (134 markdown files, no dead relative links)

    $ make ci        # the closing sweep, 4th run, all 13 steps
    >>> [13/13] make check-links  (every relative Markdown link resolves ...)
    ================================================================
     CI GREEN -- tree is good
    ================================================================
    CI_EXIT=0

`CI_EXIT=0` is the observed status of the run, captured by the wrapper, not
derived from the banner.

## Status — ALL SIX BATCHES COMPLETE, PLAN NOT

Six batch commits: `fd361e9` (1), `2e6e698` + `46eddaa` (2), `a24242e` +
`74a6eeb` (3), `0412395` (4), `c61dd45` (5), and this one (6).

**Batch 6 closed all six of its phases: 16, 25, 29, 30, 39, 40.** Three by
patch (25, 29, 39), three by recorded decision (16 → its own plan; 30 → the
guards stay, with phase 7's stated blocker disproved; 40 → recorded at the
risk, option (a) taken). Phases 16 and 30 are ticked as *closed decisions*, and
their successors — 51 and 53 — carry the work forward with criteria that can
actually be met.

**15 phases remain unchecked.** None is batch work; each was deliberately left
open by the batch that found it:

- **17** — the citation-population decision (167 refs in dated design records,
  38 in frozen evidence blocks). Blocks phase 53. The oldest open question here
  and the one most worth answering: batch 6 shifted the bare population again.
- **33, 42, 43, 44, 45, 46** — filed by batches 3 and 4 (ungated fences beyond
  the whole-program set, untagged fences, doc→doc citations, a spec/compiler
  disagreement on shifts, `tests/rtparity/run.py`'s lost oracle).
- **47, 48, 49, 50** — filed by batch 5. **47 is now unblocked**: it was held
  for after batch 6's sweep specifically so a `make ci` failure could not be
  ambiguous between two unexercised steps, and the sweep is done.
- **51, 52, 53** — filed by batch 6 (the `char` language plan, the default
  build's stray `.c`, the corrected `r_step` deletion).

**The sweep earned its place.** It was billed as possibly ceremonial. It found a
CI step that could never have run, two gates silently depending on a behaviour
phase 25 removed, and a corpus count three fixtures stale — none of which any
targeted gate would have reported, because the point of failure was the sweep
itself. It also cost four 19-minute runs, three of them discovering one failure
each; the cheaper path was to run each step's own gate after each fix and keep
`make ci` for confirmation. Both halves of that lesson belong in the record.

## Batch 7 evidence — phases 42 and 45, the two spec/compiler divergences

Ran 2026-07-30 against `6a5348b`. Both phases asked which side was wrong. **In
both cases it was the spec**, and in both cases the corpus — not preference —
settled it. The compiler's accept/reject behaviour is unchanged by this batch;
the only `src/tychoc.c` edit is diagnostic wording, and it is net-zero in lines
on purpose (see below).

### Phase 42 — `[bool]` is legal, and always was

**The spec was wrong, and the phase's own framing of the compiler was too.**

The phase offered two readings and called the second a guess. The corpus answers
it outright: `tests/bool_array.ty` is a committed fixture *with a golden* whose
header records the decision in so many words — tychoc used to reject bool arrays
while tychoc0 accepted them, and "tychoc now accepts dynamic bool arrays
(matching tychoc0 + Go/Swift/Odin). Fixed-size `[N]bool` stays rejected on both."
It exercises nine forms: literal, `push`, index-write, iteration, `str`, `==`, a
`struct` field, a map value, and nesting. `tests/cond_stmt_expr.ty:23` carries a
second `[bool]` field. So `[bool]` was **deliberately allowed**, two committed
programs depend on it, and §16.7 documented an intention nobody implemented — a
spec defect by the batch's own definition.

Confirmed by running the compiler rather than reading it:

```
ACCEPT  [bool]   (inferred `:= [true, false]` and annotated `a: [bool]`)
REJECT  [3]bool  -> error: array elements must be int, float, string, a struct, or an array
```

**The phase's claim that "the two messages now disagree" does not survive
contact.** They do not share a rule, so they cannot disagree: `src/tychoc.c:2036`
(dynamic) lists `bool` because a dynamic `[bool]` *is* legal, and
`src/tychoc.c:2000` / `:2019` (fixed) omit it because `[N]bool` is not. Each was
correct for its own site. What *was* wrong at the fixed sites is the shape of the
message — an allow-list, when `bool` and `void` are the only element types those
sites refuse. Measured: `[2]bytes`, `[2](int,int)`, `[2]u32`, `[2][int]` and
`[2]string` all compile, and the old message named none of the first three. This
is the identical complaint `tests/reject/bounded_elem_bool.ty` was written to
make, and which the bounded site already fixed at `src/tychoc.c:1934`. Both fixed
sites now read:

    a fixed-size array element cannot be bool or void -- a dynamic [bool] is legal

Nothing asserted the old text: it survives only in `compiler/tychoc0.ty:1861`,
`:1876` and `:1888`, which is frozen and built by no gate since the 2026-07-29
retirement.

**The edit is deliberately net-zero in line count** (4 insertions, 4 deletions).
A first draft added a six-line explanatory comment, which pushed every line past
`src/tychoc.c:2000` down by six and would have invalidated the anchored citations
at the shift arm along with an unknown slice of the ~344 bare refs phase 17 is
still open on. The rationale moved into `tests/reject/fixarr_elem_bool.ty`'s
header and §16.7 instead, where it costs nothing. This is the same hazard phase
53 records for `r_step`.

Spec repairs, both now carrying `> Provenance:` blocks citing the implementing
lines: `docs/spec/12-aggregates.md` §16.7 gained a per-form table (`void`
rejected everywhere; `bool` accepted dynamic, rejected in `[N]T`, `[$N]T`,
`bounded[N]T`) and a note recording that the old sentence was never implemented;
`docs/spec/03-types.md` §5.3.1 was rewritten to match. The stale
`src/tychoc.c:2033-2034` / `:2016-2018` refs are gone, along with the two
orphaned bare refs in §16.7's old note (four-digit line numbers in the 1600s,
left over from a much older tree) that bound to whatever path was named last. `docs/spec/appendix-e-conformance.md` gained a §16.7 row — there was none,
so Appendix E never inherited the wrong claim.

New fixture `tests/reject/fixarr_elem_bool.ty` pins the fixed-size half; the
accept half was already pinned by `tests/bool_array.ty`. That is the +1 in the
test count.

### Phase 45 — the shift rule, exactly as the phase predicted

**The spec was wrong**, and the evidence is that no version of tycho ever
implemented the sentence. `docs/spec/09-expressions.md:83` lumped shift in with
bitwise and required "the same integer type". That is true of `& | ^` —
`src/tychoc.c:6214` tests `lt != rt` — and false of `<< >>`, whose arm at
`src/tychoc.c:6103-6109` accepts any two integers and returns the *left* type
(`src/tychoc.c:6109@lt`). Verified by running it: a `u32` shifted by an `int`
compiles today. Tightening the compiler to match the spec would break `x << n`
for every `n: int`, which is why this is a spec repair.

The sentence is now split into a **Bitwise** rule (operands must match) at
`docs/spec/09-expressions.md:83` and a **Shift** rule at
`docs/spec/09-expressions.md:85-89` stating that widths need not match and the
result takes the left operand's type, with a `> Provenance:` block. One drafting
error was caught and fixed before the gates: the following §4.5 precedence
sentence was initially absorbed into the `>` block, silently turning a normative
paragraph into part of a citation aside.

`fuzz/run_typeparity.py` was updated on both ends — the header's derivation list
now cites the two rules separately, and the shift clause no longer describes a
spec defect. Its comment previously ended "the spec sentence is the defect. Filed
as a plan.md phase"; it now records that the disagreement was resolved in the
compiler's favour and that spec and oracle agree. The encoded rule did not
change, which is why the lane still reports 4608/4608.

### Gates — all five, foreground, real output

```
$ make test
passed: 548   failed: 0
all green
```

547 at batch 5 → **548**, accounted for exactly: `reject_fixarr_elem_bool`, the
one new fixture. No other count moved.

```
$ python3 fuzz/run_typeparity.py
type-parity: 4608/4608 scalar binop cases match the `expect` oracle (640 accept / 3968 reject;
             every accept emits compilable C, no crash on any case).

$ sh scripts/spec_check.sh
spec-check: Appendix A grammar matches §3/§4 (ok)
spec-check: all Appendix E fixture citations resolve (ok)
spec-examples: 9 runnable example(s), all pass

$ python3 scripts/check_citations.py
citation check: ok (157 anchored contain the token they name, 2194 bare in bounds,
119 source->doc citations resolve, 154 source->source in bounds, 12 source->source anchored)

$ sh scripts/check_links.sh
link check: ok (134 markdown files, no dead relative links)
```

`make ci` was **not** run and is not needed here: the change set is one
diagnostic string, one new reject fixture, and four Markdown files. `make test`
covers the first two and the three doc/spec gates cover the rest. Per CLAUDE.md
it is confirmation, not discovery, and this batch had nothing left to confirm.

### What a future reader should not have to re-derive

- **`[bool]` is legal and `[N]bool` is not, and that is intentional.** The split
  is a codegen limit on the inline fixed forms, not a type-system principle.
  Anyone "fixing the inconsistency" by rejecting `[bool]` breaks
  `tests/bool_array.ty` and `tests/cond_stmt_expr.ty`.
- **`[N]bool` could probably be allowed now.** The comment blaming tychoc0's
  missing fixarr-bool codegen is moot post-retirement, as phase 42 noted. It was
  *not* done here: it is real codegen work, not a wording change, and nothing in
  the corpus asks for it. Left deliberately, not overlooked.
- **Unverified:** whether any *downstream* consumer of the two changed fixed-size
  diagnostics matches on their old text. Searched `tests/`, `docs/`, `*.py`,
  `*.sh` and `*.out` — the only hits are in the frozen `compiler/tychoc0.ty`. If
  something outside those trees greps for that string, it would break, and I did
  not search outside the repo.

## Batch 8 evidence

Five phases: 25, 52, 53, 54, 55. One was already done and only needed its box
ticked; three are code; one is a recorded refusal with new reasoning. Every
claim below was run against the built compiler, not reasoned about.

### Phase 25 — already landed by batch 6; nothing to redo

The box was unticked, the work was not undone. `src/tychoc.c:12708` computes
`c_to_stdout`, `src/tychoc.c:12735` opens `stdout` instead of the sibling file,
and `src/tychoc.c:12741` prints `wrote <path>` only on the `-o` path. Re-run:

    $ rm -f tests/for3.c && ./tychoc --emit-c tests/for3.ty > p25.c
    rc=0  captured_lines=2595  first=/* Tycho runtime - embedded verbatim int
    stray tests/for3.c: NO
    stderr: (empty)
    $ ./tychoc --emit-c tests/for3.ty -o withO
    wrote withO.c        rc=0  withO.c exists: YES

Ticked, not re-implemented.

### Phase 52 — the plain build now removes its own intermediate

`src/tychoc.c:12771` is a `remove(c_path)` placed **after** the `cc` return-code
check, so a failing compile still leaves the C beside the printed command — the
evidence that message refers to. Measured before and after on the same file:

    $ ./tychoc p52src.ty            # HEAD e9a5457
    built p52src                     sibling p52src.c: YES
    $ ./tychoc p52src.ty            # now
    built p52src                     sibling p52src.c: NO
    $ ./p52src
    asc=01234
    desc=54321

The three things that had to be checked first, and were:

- **Nothing in the tree reads the sibling `.c` of a plain build.** Every in-tree
  caller that wants the C asks for it: `tests/run.sh:70`, `tests/conc/run.sh:47`,
  `scripts/entrypoints.sh:63`, `scripts/tools_check.sh:283`, `bench/guard.sh:28`
  and `bench/guard.sh:63` all pass `--emit-c -o`, which this change does not
  touch. `make test`, `make conc` and `sh scripts/tools_check.sh` were run anyway
  because batch 6 was burned by exactly this class.
- **`--emit-c -o name` still writes `name.c` and still keeps it.** That is the
  documented way to keep the C (`docs/guides/debugging.md:37`) and it is
  untouched; `README.md:190` and the command table say so.
- **The overwrite hazard is pre-existing and was NOT widened.** 27 tracked `.c`
  files share a basename with a sibling `.ty` — all of `bench/`, the hand-written
  C ports `README.md:29` builds against. A plain `tychoc bench/json/json.ty`
  already *overwrote* `bench/json/json.c` before this change existed; the removal
  happens to a file the same invocation just wrote. Nothing in the tree performs
  that invocation. Recorded in the comment at `src/tychoc.c:12760` so the next
  reader does not re-derive it.

Filed as phase 57, not absorbed: `Makefile:314` and five `.gitignore` entries now
sweep files that can no longer exist.

### Phase 53 — `r_step` stays, and the reason is one nobody had recorded

Not a third "it stays, annotated". Two blockers were already on record (phase 17
is open, and the 485 citations at lines ≥ 1553 would shift). Batch 8 found a
third that neither covers, and it is the one that would have shipped a wrong
document: **the spec asserts the field normatively.**
`docs/spec/10-statements.md:132` reads "The step codegen and its zero-step guards
still exist but are unreachable: every remaining `S_FORRANGE` producer writes a
NULL step", with the provenance `src/tychoc.c:1553-1559` — the `Stmt` field
declaration itself. Deleting `r_step` falsifies that sentence and orphans that
citation, and phase 53's Verify line did not list `sh scripts/spec_check.sh` at
all. The phase entry now carries both.

Re-verified while there, so the next attempt does not re-derive it: `r_step` has
23 mentions in `src/tychoc.c`, is written `NULL` at all six construction sites
(`src/tychoc.c:3372`, `src/tychoc.c:3414`, `src/tychoc.c:3433`,
`src/tychoc.c:6628`, `src/tychoc.c:6655`, `src/tychoc.c:6743`), and the only two
readers that can act on a non-NULL value — `src/tychoc.c:6670`'s `parallel for`
refusal and `src/tychoc.c:7316`'s literal-zero rejection — are already commented
unreachable.

### Phase 54 — the three DYNAMIC diagnostics now name the real rule

Measured first. All six of these compile today and **none** appeared in the old
allow-list "int, float, bool, string, a struct, an array, or an Option":

    [bytes] rc=0    [(int, int)] rc=0    [[string: int]] rc=0
    [Option(int)] rc=0    [bool] rc=0    [[int]] rc=0

The real rule is that `void` is the only refused element type, so all three
messages say that: `src/tychoc.c:2036`, `src/tychoc.c:2353` and
`src/tychoc.c:5110`.

**The three sites are not equal, and the comments now say which is which.** The
two parse sites are defensive: `parse_type_inner`'s only `return T_VOID`
(`src/tychoc.c:2161`) sits after a `die_at` in the `default:` arm, and every
branch that could produce a void type dies first (`src/tychoc.c:1952`,
`src/tychoc.c:1966`, `src/tychoc.c:2044`), so no spelling of a type reaches
them — `[void]` is `unknown type 'void'`, not this message. The resolver site
`src/tychoc.c:5110` *is* reachable, and is what the new fixture pins:

    $ ./tychoc tests/reject/arr_elem_void.ty
    tests/reject/arr_elem_void.ty:17: error: an array element cannot be void -- this element produces no value (a call to a procedure that returns nothing?)
        17 |     xs := [nothing()]

New fixture `tests/reject/arr_elem_void.ty`. Nothing outside `src/tychoc.c` and
the frozen `compiler/tychoc0.ty` matched the old text.

### Phase 55 — a defect, not a rule, and the fixture that said otherwise was wrong

The phase asked whether this is a compiler fix or a rule to document. The
deciding measurement is that the **fixed and bounded destinations already
adapt** — `src/tychoc.c:6295` for `bounded[N]T`, and the fixed branch since 1.6 —
so "no element-literal adaptation inside an array" was never a rule the compiler
followed. Only the dynamic `[T]` was left out:

    a: [3]u32        = [1, 2, 3]   rc=0   (before and after)
    a: bounded[4]u32 = [1, 2]      rc=0   (before and after)
    a: [u32]         = [1, 2]      rc=1 -> rc=0   was: declared type [u32] but value is [int]
    a: [float]       = [1, 2]      rc=1 -> rc=0
    a: [f32]         = [1.0, 2.0]  rc=1 -> rc=0
    a: [string]      = [1]         rc=1 -> rc=1   now: element 1 of a [string] array is the wrong type

`src/tychoc.c:6283`'s condition widened from `IS_FIXARR(want)` to any array
destination that is not bounded and not a `[$N]T`/`[$T]` template, and the count
check became conditional. Line-neutral, deliberately: the block sits directly
above the provenance range `src/tychoc.c:6303-6314` that
`docs/spec/06-conversions.md:9` cites, and growing it would have moved that range
and the citation population phase 17 is open on. Verified with `difflib` against
HEAD — every edit but the last is an exact in-place replacement, and the last
block starts at `src/tychoc.c:12759`, past every live citation into that file.

`docs/spec/06-conversions.md:27` now states the rule, also line-neutrally,
because `fuzz/run_typeparity.py:91` cites `docs/spec/06-conversions.md:13-16` and
`fuzz/run_typeparity.py:92` cites `docs/spec/06-conversions.md:24` — both would
have rotted silently under an inserted paragraph. New accepting fixture
`tests/arr_lit_adapt.ty` plus its golden.

**`make test` caught the thing worth catching, and it is the part to read.**
`tests/reject/sum_annot_array_payload_widen.ty` failed: its payload was the
literal `Some([1])` against `Option([u32])`, and its comment asserted "there is
no element-literal adaptation inside an array (only a bare `[]` and a top-level
scalar literal adapt)". That claim was already false for two of the three array
forms when it was written. The fixture's *stated purpose* — "locks
check_sum_leaf's non-literal payload comparison" — is untouched by phase 55, so
the payload became a non-literal one (`xs := [1]`, then `Some(xs)`), which still
rejects with the original diagnostic:

    error: declared type Option([u32]) but value is Some([int])

Its header records why, so nobody "simplifies" it back to a literal. A **value**
still never adapts; only a literal does, which is §8.1's opening sentence and did
not change.

### Gates — foreground, one per command, real output

    $ make test
    passed: 550   failed: 0
    all green

548 at batch 7 → **550**, accounted for exactly: `arr_lit_adapt` (phase 55's
accepting fixture) and `reject_arr_elem_void` (phase 54's).
`reject_sum_annot_array_payload_widen` was rewritten in place, not added, so it
moves no count.

    $ sh scripts/tools_check.sh
    ...  semtok=True
    >>> bytes-rehome: a bytes field of a returned struct is deep-copied into the caller's arena
        bytes field re-homed on struct return
    tools-check: ok

    $ make conc
    conc: passed 38   failed 0

    $ sh scripts/spec_check.sh
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 9 runnable example(s), all pass

    $ python3 scripts/check_citations.py
    citation check: ok (158 anchored contain the token they name, 2246 bare in bounds,
    121 source->doc citations resolve, 158 source->source in bounds, 12 source->source anchored)

    $ sh scripts/check_links.sh
    link check: ok (134 markdown files, no dead relative links)

**The citation gate earned its keep on this batch.** Its first run was RED, and
the stale reference was a real one, not a line shift:
`docs/spec/12-aggregates.md:213` anchored line 2036 of `src/tychoc.c` on the
token `bool` and said in prose "its diagnostic lists `bool` as permitted" — an
anchor (reproduced here without its `@token`, which the gate would read as a
live citation and redden on) whose whole point
was the allow-list phase 54 deleted. The clause now says the diagnostic states
the rule instead of enumerating it, anchored `src/tychoc.c:2036@void`. Any
rewrite of a diagnostic in this tree should expect the spec to be quoting it.

`sh scripts/tools_check.sh` and `make conc` were run because the change touches
the compiler's output path and batch 6 found two lanes that depended on it
(`scripts/tools_check.sh:283`, `tests/conc/run.sh:41`); both pass `-o` now and
both stayed green. `make ci` was **not** run: per `CLAUDE.md` it is confirmation,
not discovery, and the gates above cover a change set of one C file, three
fixtures, one spec section and two README lines.

### What a future reader should not have to re-derive

- **The plain build's `.c` is gone on success and kept on failure.** If you need
  the C, ask: `--emit-c -o name`. Do not "restore" the sibling file — phases 25
  and 52 removed it from two different paths for the same reason, and no
  `.gitignore` pattern is safe (31 directories hold both `.ty` sources and
  tracked `.c` files).
- **`[N]T` and `bounded[N]T` adapt their literal elements and always did.** The
  1.6 fixed-array branch is where the behaviour started; phase 55 only extended
  it to `[T]`. Anyone "restoring symmetry" by removing it breaks
  `tests/arr_lit_adapt.ty`, which pins all three forms.
- **The two parse-site void guards are unreachable and are kept on purpose.**
  Deleting them as dead code would remove the fail-closed behaviour the moment
  `parse_type_inner` gains a branch that can yield `T_VOID`.
- **Unverified:** whether `remove(c_path)` breaks a *user* workflow outside this
  repo that depends on the sibling `.c` of a plain build. Searched `Makefile`,
  `scripts/`, `tests/`, `bench/`, `examples/` and `fuzz/` — every in-tree
  consumer uses `--emit-c -o`. A consumer outside the repo would break, and
  `README.md:190` plus the command table are the only notice they get.

## Batch 9 evidence — phases 46, 47, 48, 49, 50

Ran at `5939670`. Subject: runners and fixture headers describing a world the
retired `tychoc0` freeze took with it.

### Phase 46 — the decision: an `expect` oracle, plus one property leg, and why not the other two

Read the runner before deciding. `tests/rtparity/run.py` was not merely
oracle-less, it was **dead code**: `main()` printed a retirement notice and
`return 0`-ed at what is now `tests/rtparity/run.py:210` in the old file, with
the entire comparison unreachable below it, and `emitted_c()` calling `die()`
before its own `return`. Nothing in the tree ran it either — `grep -rn rtparity`
outside `tests/rtparity/` hits only `FRICTION.md`, `plan.md`, four archived
`docs/internals/plan-*-DONE.md`, `docs/architecture.md`, `compiler/README.md`,
`docs/bootstrap.md` and `compiler/tychoc0.ty`. **No Makefile target and no CI
step**, though its own docstring advertised `make rtparity`.

The subject differs from typeparity's, and that is what decided it. Typeparity
gates a *decision table* (accept/reject per operand pair). This lane gates
whether the **user-observable runtime surface actually reaches the emitted
program**: the `getenv()` knobs, the `tycho: ...` trap texts, the
`TYCHO_ARENA_STATS` row labels. That question is answerable with one
implementation, which is why retirement was rejected: the named victim in the
old docstring — `TYCHO_ARENA_STATS` present in `runtime/tycho_rt.c` and a no-op
in every tychoc0-built binary, found by hand, fixed in `2b24ca6` — is a
single-implementation bug class.

**A pure property check was measured and rejected as the whole answer.** The
strongest available property is "everything `runtime/tycho_rt.c` defines reaches
the emitted C", and it is nearly vacuous today because the embed is verbatim —
measured, not assumed:

    $ python3 - # extract with the lane's own regexes
    env emitted= 3 rt= 3 emitted-only= []
    msg emitted= 28 rt= 24 emitted-only= ['tycho: non-exhaustive match\n',
        'tycho: push to a full bounded[4]\n', 'tycho: range step is zero\n', 'tycho: slice [%']
    row emitted= 5 rt= 5 emitted-only= []
    full rt verbatim in emitted: True

Deleting a `getenv` from the runtime moves both sides together, so the property
cannot see it. It is **kept anyway** (`rt_subset()`), as the only thing that
would notice a future tychoc that emits the runtime piecewise — but on its own
it would catch nothing.

**The `expect` oracle was chosen**, in phase 22's shape: 36 recorded items — 3
env knobs, 28 diagnostics (24 from the runtime, 4 written inline by codegen), 5
stats rows. At this arity an enumerated list is a golden, not the photograph
phase 22 refused at 4608 rows; it is the same mechanism `tests/diag/*.err` uses.
The four codegen entries are where the lane earns its keep, each citing the
emitter: `src/tychoc.c:10021`, `:10734` (non-exhaustive match), `:11743`
(bounded push), `:10886` (range step is zero), `:9647`, `:9666` (slice bounds).
Both directions fail — a lost trap, and an emitted trap nobody wrote down.

**Negative control — the oracle bites.** `src/tychoc.c:10886`'s emitted trap
text deleted, compiler rebuilt:

```
rtparity: FAIL - diagnostic "tycho: range step is zero\n" is in the oracle but NOT emitted. A runtime capability
          disappeared, or the construct in tests/rtparity/surface.ty that pulls it in did.
rtparity: FAIL - 1 runtime-surface difference(s) against the oracle in
          tests/rtparity/run.py. ... rc=1
```

`src/tychoc.c` restored with `git checkout` and rebuilt; `git status --short
src/tychoc.c` clean.

**What it still does not buy**, said in the file's header too: the sets were
recorded off the compiler they gate, so a trap that was *always* wrong is
invisible. This lane sees a surface item vanish, not one that never worked.

`make rtparity` now exists (`Makefile`, beside `conc`) so the docstring's claim
is true; it is in no aggregate lane yet — phase 58.

### Phase 47 — the phase's premise was false, and the real hole was one level up

`make fetch` **already existed** and has since `39d75be` — `Makefile:206` in the
pre-batch tree. What was true is the consequence: no *aggregate* lane ran it, so
the stale golden `39d75be` left behind survived a prior plan and five batches.
`scripts/ci.sh:69-75` even enumerated `fetch` among the runners deliberately
left outside CI.

Wiring required three edits, not one: `make -s fetch` added to `scripts/ci.sh`'s
step `[3/13]` beside `site`/`raytrace`/`mandelbrot`; the step's own label and the
"NOTHING else in the tree" comment above `[3b]` corrected, since both enumerated
the step's contents; and the `fetch:` target comment in `Makefile`, which
asserted "not in `make ci`" as if it were a decision. `CLAUDE.md`'s step→gate
table row for `[3]` now names `make fetch` too.

Chosen home: `make ci`, not `make test`. It links `libcurl` and builds under
ASan — real seconds — and self-skips (`fetch: SKIP (libcurl not installed)`), so
it is safe there unconditionally and needs no network (`file://` through
libcurl).

**Negative control on the target, not the runner** (the Done-when asked for
exactly this):

```
$ printf 'bytes  : 999\n' >> examples/fetch/expected.out && make fetch
FAIL: output != golden
      7d6
      < bytes  : 999
fetch: FAIL
make: *** [Makefile:221: fetch] Error 1
make rc=2
```

Golden restored; `git status --short examples/fetch/` clean.

`make ci` was **not** run — the parent brief forbade it and `CLAUDE.md` says it
is confirmation, not discovery. `sh -n scripts/ci.sh` parses, and the exact
command the new line runs (`make -s fetch`) was run in the foreground and is
green. The full sweep is phase 58, which is where a new CI step belongs.

### Phase 48 — the diagnostic is pinned, and the phase's line number had drifted

The message is at `src/tychoc.c:3243`, not `:3241` as the phase entry said —
`:3241` is the `parse_stmt` call and `:3242` the `S_FORRANGE` test. Cited from
the fixture as read.

`tests/diag/parallel_three_clause.ty` + `.err` now pin it byte-for-byte under
`make test`'s diag loop. The golden:

```
tests/diag/parallel_three_clause.ty:13: error: parallel supports `for i in 0..<N` and `for x in collection` loops only
    13 |     parallel for i := 0; i < 3; i += 1:
```

**Negative control — one word flipped** (`only` -> `ONLY`) in `src/tychoc.c`,
compiler rebuilt, fixture recompiled:

```
$ cmp tests/diag/parallel_three_clause.err /tmp/dg.log
... differ: byte 115, line 1
< ... loops only
> ... loops ONLY
```

Restored and rebuilt; the golden matches again and `git status --short
src/tychoc.c` is clean.

**Checked, and it is not the same arm.** The phase asked about the
foreach-of-an-expression refusal. `parallel for x in 3:` gives a *different*
message from a *different* site — `src/tychoc.c:3411`, "parallel for over a
collection or channel must name a variable (bind it first)" — so it is not
covered by this fixture and is not in this phase's scope. Filed as phase 60.

### Phases 49 and 50 — headers now say what is true, with the history dated

The convention applied: archived `docs/internals/plan-*-DONE.md` files are
frozen records and are never rewritten; these are **live source headers
describing current behaviour**, the opposite case, so they state today's
guarantee and put the retired one in the past tense with its date.

`grep -rl 'tychoc0 must agree' corelib/test/*/main.ty | wc -l` gave the phase's
25, and all 25 were rewritten. **The grep undercounted the class by seven**,
because it is line-oriented and these headers wrap: `compress`, `bignum`,
`raster` (`must` / `agree` split across lines), `arrays` and `regex` (three-way
"tychoc0 --bundle and standalone tychoc0 must all agree"), `iter` ("All three
compile paths must agree") and `crypto` ("the tychoc-vs-tychoc0 diff proves the
two compilers agree on the FFI"). All seven asserted the same dead differential
and were corrected the same way — 32 files in total. `grep -rn "must agree"
corelib/test/*/main.ty` now returns only `corelib/test/cli/main.ty:87`, which is
about `parse_spec` agreeing with a fixture, not about a compiler.

The rewrite was scripted with a review pass, and the first attempt was **thrown
away**: an `[^.]*\.` sentence match stopped inside `corelib/test/base64.out` and
left tails like `2026-07-29.out (corelib/run.sh).` behind. Reverted with `git
checkout -- corelib/test/`, redone with the sentence end matched explicitly, and
all 25 headers re-read afterwards; one leftover lowercase sentence start in
`corelib/test/decimal/main.ty` was fixed by hand.

**Phase 50's decision: correct the comments, move nothing.** Both claims in
`corelib/test/io/main.ty` were expired, but they are separate clauses:

- The nested-pattern arm's "no runner feeds `corelib/test/` to the frozen
  compiler" — the same case batch 5 closed for `httpd` and `result`, and it now
  reads the way those do, pointing at `tests/nested_pattern.ty` as the
  construct's own fixture.
- The interior-NUL `bytes` fixture's "It CANNOT live in `tests/`". Checked
  first, as the phase asked: `tests/string_nul.ty` and `tests/strbytes.ty` cover
  the **string** side of §3.9.4's interior NUL (length past a NUL, indexing,
  substr, concat, accumulator, map keys, `find` across a NUL) — so §3.9.4 has
  its `tests/` witness already. The `b[i]` / `b[i:j]` / `b + b` properties the
  comment enumerates are **§5.2.6**, a different clause, whose Appendix E row
  (`docs/spec/appendix-e-conformance.md:98`) already names its witnesses:
  `corelib/test/io`, the §5.2.6 spec example, and `server/main.ty`'s `log_safe`.
  Applying §E.1's convention — "a package's own lane covers the package, and
  nothing moves merely because it now could" — the fixture **stays**, and the
  comment now says `tests/` is a place it *could* go rather than one it cannot.

Out of scope and filed as phase 59: the Appendix E note under that row
(`docs/spec/appendix-e-conformance.md:322-345`) is still written in the
live-freeze present tense ("would be a program `tychoc` accepts and the frozen
`tychoc0` refuses", "which `scripts/frontparity.sh` would report"). Rewriting it
is a documentation phase of its own, not a comment edit bolted onto this one.

### Gate output — the real runs

```
$ make corelib
corelib: all green (tychoc matches goldens)

$ make corelib-examples
corelib examples: all green

$ make test
passed: 551   failed: 0
all green

$ python3 tests/rtparity/run.py
rtparity: env knobs         3/ 3 as recorded (ok)
rtparity: diagnostics      28/28 as recorded (ok)
rtparity: arena-stats rows  5/ 5 as recorded (ok)
rtparity: runtime file  every defined surface reaches the emitted C (ok)
rtparity: emitted runtime surface matches the oracle (3 env knobs, 28 diagnostics, 5 stats rows)

$ sh examples/fetch/run.sh
fetch: green (http+json+sha256+io+path compose; tychoc+ASan; real libcurl via file://; the tychoc0 leg was retired 2026-07-29)

$ make fetch      # the wired target, same result
$ make rtparity   # the new target, same result

$ python3 scripts/check_citations.py
citation check: ok (159 anchored contain the token they name, 2246 bare in bounds,
121 source->doc citations resolve, 166 source->source in bounds, 11 source->source anchored)

$ sh scripts/check_links.sh
link check: ok (134 markdown files, no dead relative links)
```

**551, not 550**, and the +1 is accounted for: `diag_parallel_three_clause`, the
phase-48 fixture. Nothing else in the count moved; phases 49 and 50 touch
comments only, so no golden could move, and `make corelib` proves all 32 edited
files still compile.

**The citation gate caught the Makefile edit.** Adding the `rtparity` target
shifted `Makefile:253@SKIPPED` to `:267`, reddening four references
(`scripts/asan_self.sh:11`, `:72`, `scripts/check_citations.py:213`,
`scripts/editors_check.sh:29`). Repointed to `Makefile:267` — verified the line is the
ilp32 ASan-skip `echo` — and the gate is green. Anyone adding a Makefile target
should expect this; it is the third time an anchored `Makefile:N` has moved
under an unrelated edit.

`make ci` was **not** run. `sh scripts/spec_check.sh` was not run either: no
fixture directory moved and no Appendix E path changed — the one file added,
`tests/diag/parallel_three_clause.ty`, is a new path, not a moved one.

## Phases discovered by batch 9

- [x] **Phase 58** — **two lanes now exist that `make ci` has never swept.**
      Batch 9 added `make -s fetch` to `scripts/ci.sh`'s step `[3/13]` and
      created a `make rtparity` target that is in no aggregate lane at all. The
      fetch line was verified by running the exact command it runs, and
      `sh -n scripts/ci.sh` parses, but the full sweep has not run since — and
      per `CLAUDE.md`, a phase that adds a CI step is exactly the phase that owes
      the sweep. Batch 6's closing `make ci` is the last full run, and it
      predates both.
  - Scope: `scripts/ci.sh` (decide whether `rtparity` joins, and under which
    step number — it is a `tests/` lane, not a corelib dogfood), and one full
    sweep.
  - Note the numbering convention: `2b`, `2c`, `3b`, `9b` are sub-lanes of a
    step and the `/13` denominator counts only the numbered steps.
  - Done when: `make ci` has been run once, green, with both lanes in it, and
    the `Makefile` comment on `rtparity` that currently points here is updated.
  - Verify: `make ci` once. This is the deliberate closing sweep, not discovery.

- [x] **Phase 59** — **Appendix E's §5.2.6 note is still written as if `tychoc0`
      ran.** `docs/spec/appendix-e-conformance.md:322-345` argues that a `tests/`
      fixture for the `bytes` operators "would be a program `tychoc` accepts and
      the frozen `tychoc0` refuses", cites what `scripts/frontparity.sh` "would
      report as a divergence" and `compiler/fixpoint.sh` "as a build failure",
      and enumerates 13 corelib packages that "may **not** use a `bytes`
      operator" — a live prohibition derived entirely from lanes retired
      2026-07-29. The row itself
      (`docs/spec/appendix-e-conformance.md:98`) ends "no `tests/` fixture, see
      the note below". Batch 9 corrected the two `corelib/test/io/main.ty`
      comments that pointed at this note but deliberately did not widen into the
      spec appendix.
  - Scope: that note, the §5.2.6 row, and any sibling note in the same section
    written in the same present tense — batch 3 swept documents for this class
    and Appendix E's notes were not reached. Check the three notes above it,
    which the text calls the same mechanism biting the first three times.
  - The blocked-13 enumeration is a **historical measurement** and should be
    kept as one, in the past tense with its date — not deleted, and not left
    reading as a live rule.
  - Decide, and write down, whether §5.2.6 now gets its own `tests/` fixture:
    the block expired, but §E.1's convention says nothing moves merely because
    it now could, and `corelib/test/io` already covers it.
  - Done when: no sentence in the section asserts a live `tychoc0` constraint,
    and §5.2.6's coverage claim matches what the tree actually runs.
  - Verify: `sh scripts/spec_check.sh`, `python3 scripts/check_citations.py`,
    `sh scripts/check_links.sh`. Markdown only — do **not** run `make test`.

- [x] **Phase 60** — **the second `parallel for` refusal is asserted by
      nothing.** Phase 48 pinned the three-clause message; the neighbouring arm
      at `src/tychoc.c:3411` — "parallel for over a collection or channel must
      name a variable (bind it first)", reached by `parallel for x in <expr>` —
      has no `tests/diag/` fixture, and `grep -rn "must name a variable" tests
      docs src` finds it only in `src/tychoc.c` and a prose mention in
      `docs/guides/concurrency.md:102`. It is the same gap phase 48 closed, one
      arm over: `tests/reject/*.ty` would accept any nonzero exit with any
      message.
  - Scope: one `tests/diag/` fixture and its golden. No `src/tychoc.c` change —
    the message reads correctly, it is simply unasserted.
  - Done when: the fixture exists, `make test` scores it (552), and flipping one
    word of the message is shown reddening it.
  - Verify: `make test`.

## Batch 10 evidence — phases 44, 17, 33, 43: the documentation-citation infrastructure

Gate first, then the sweeps it enables. Phase 44 is what makes 17, 33 and 43
answerable, which is why it ran first.

### Phase 44 — the doc→doc hole, before and after

`docs/` was absent from `SRC_PREFIX`, so a citation from one document into
another was `continue`d before any check ran. Measured, not estimated:

```
$ python3 scripts/check_citations.py --stats          # at b5c8406, before
citation check: 159 anchored (content-checked, 90 of them the mandatory `> Provenance:`
             single-line refs), 2266 bare (bounds only), 121 source->doc (existence),
             167 source->source (bounds), 11 source->source anchored (content-checked)
citation check: ok

$ python3 scripts/check_citations.py                  # with "docs/" in SRC_PREFIX
citation check: FAILED (77 stale citation(s) above)
    25 NO SUCH FILE, 52 OUT OF BOUNDS
```

77, not batch 3's 103 — batches 4–9 repaired 26 incidentally. The split by citing
file: `docs/rfc/ffi-threading-design-review.md` 22, `plan.md` 18,
`docs/spec/appendix-h-differences.md` 2, and 35 inside the frozen
`docs/internals/plan-*-DONE.md` archives.

**Every one of the 52 OUT OF BOUNDS is a bare `:N` that inherited a `docs/` path
from its sentence while meaning `src/tychoc.c`.** Not one is a doc→doc citation
that merely drifted. `plan.md:1712`'s `:3191-3277` sits two words after
`docs/spec/10-statements.md:8-10`; the chapter has 172 lines. That is why the
widened bounds check needed no extra machinery — the note on this phase floated
"require an explicit path in `docs/`-targeted refs", and the data says the
ordinary check already reddens every one of them by two orders of magnitude.

### Both directions, on the real tree

**The hole, demonstrated at HEAD.** The same two bad refs, appended to
`docs/README.md`, against the gate as it stood:

```
$ git show HEAD:scripts/check_citations.py > scripts/_head_gate.py
$ python3 scripts/_head_gate.py
citation check: ok (159 anchored ..., 2279 bare in bounds, ...)      # GREEN on both
```

**The gate, reddening.** Same two refs, gate as shipped:

```
STALE  docs/README.md:38  docs/spec/01-lexical.md:99999 -> docs/spec/01-lexical.md has 386 lines: OUT OF BOUNDS
STALE  docs/README.md:38  docs/spec/gone.md:1 -> docs/spec/gone.md: NO SUCH FILE
citation check: FAILED (2 stale citation(s) above)
--- restored ---
citation check: ok (159 anchored ..., 2548 bare in bounds, ...)
```

**The frozen-archive exemption, reddening.** `if frozen and` replaced by
`if False and`, nothing else:

```
$ python3 scripts/check_citations.py | grep -c '^STALE  docs/internals/plan-'
35
citation check: FAILED (35 stale citation(s) above)
--- restored ---
citation check: ok
```

Those 35 are the same mis-inheritance class, inside records the ARCHIVED rule
forbids editing. They are skipped and **counted**: `--stats` now prints
`242 doc->doc skipped as frozen archive`, so the hole is declared, not silent.

### What the 42 live failures got, and why

| where | n | treatment |
|---|---|---|
| `docs/rfc/ffi-threading-design-review.md` | 22 | repointed `docs/{ffi,concurrency}.md` → `docs/guides/…`, line numbers mapped |
| `plan.md` evidence blocks | 15 | given the explicit `src/tychoc.c` path the sentence meant |
| `plan.md` | 3 | de-backticked: anchor drifted, or the dead path is the subject |
| `docs/spec/appendix-h-differences.md` H5 | 2 | docs/generics.md:11, :205-208 (de-backticked: dead path) → `docs/guides/generics.md:13`,`:207-210` |

The rfc repoint is not a path substitution. `68e5b39` was `R097`/`R098`, not a
pure rename, and three later commits edited each file. A difflib equal-block map
was built from `68e5b39^` to today and **the cited text was compared old-vs-new
for all 22 — identical in every one**, which is what makes the new number
evidence of where the referent went rather than a guess. One exception, recorded:
docs/ffi.md:73 (de-backticked: dead path) was the blank line under the type table when it was written and
its sentence has always been on the next line, so it became :74 — repaired, not
mapped.

The `plan.md` set has the trap worth naming. `plan.md:3341` reads
"``docs/spec/16-builtins.md:332`` cited `die`'s codegen at :8791-8792" (de-backticked: it means the compiler, not the chapter) and
the next line's "two siblings at ``docs/spec/16-builtins.md:20`` and ``docs/spec/16-builtins.md:86``" means the *chapter*, not
the compiler. Making the first ref explicit re-binds the siblings to
`src/tychoc.c`, in bounds and wrong. Both had to move, in opposite directions.

Three refs were de-backticked rather than repointed. `src/tychoc.c:3386` no
longer contains `"range"` and `:11797` no longer contains `pop from an empty
array`; the third quotes it **because** that path is dead
— docs/corelib.md:204-210, the citation phase 15 repaired. Repointing any of them would make an
evidence block claim something its phase never checked. `plan.md:1450-1452`
established this convention and says so in words.

### The gate's own docstring staled 18 citations into itself

Adding the doc→doc section shifted every line below it. All 18 refs into
`scripts/check_citations.py` stayed **in bounds** and went silently wrong — the
exact class this batch exists for. Caught by mapping `HEAD`→working with difflib
and re-verifying each pair: **18/18 text-identical at the new line**. Repaired,
and `scripts/docs_fences.sh:21` was promoted to the anchored form `@ARCHIVED` so
it cannot rot silently again.

Twice, a placeholder path written into the new docstring was parsed as a live
citation by the pass being documented. The docstring already warns about this for
the doc→source shape table; it now does the same for its own doc→doc example.

### Phase 17 — the decision, and the numbers it rests on

Re-derived at `b5c8406`, because batch 2's split is eight batches old. **1457**
refs name `src/tychoc.c`: 660 archived, 797 live. The design-record class is
**127** (90 non-archived `docs/internals/`, 37 `docs/rfc/`), not batch 2's 167.
Batch 2's "three in still-open entries (phases 26 and 30)" is **moot** — both are
`[x]` as of a later batch.

**All 139 anchored refs are correct** — the anchor test re-run over every one,
0 mismatches. So a sweep would not move the anchored half. The other 1318 are
bare, i.e. bounds-only, and bounds is exactly the property a drifted citation
keeps when the target is 12774 lines long.

| class | n | decision |
|---|---|---|
| archived `plan-*-DONE.md` | 660 | never sweep — phase 4's rule, unchanged |
| dated design records | 127 | **do not repoint** — annotate and freeze |
| `plan.md` completed-phase evidence | most of 240 | do not renumber — same rule |
| live entries of open phases 51/53/57/60 | 72 | the phase that acts on them verifies them; briefs are claims to check, not facts |

The refusal to repoint the dated records is the decision the phase asked for.
`frontend-restriction-audit-2026-07-25.md` dates itself in its own filename. Give
it current coordinates and you get a document whose prose is dated and whose
citations are not, with nothing telling the reader the halves disagree. **A stale
ref in a dated record is legible; a fresh one is a lie the reader cannot detect.**

Retired to `FRICTION.md` — the repo's container for recorded-not-actioned — with
the count, the reason, and the only mechanism that would actually fix it:
conversion to anchored form, 1318 hand-verified citations. Batch 10's phase-44
work is a 42-ref instance of that job and it took a batch. **The box is ticked on
the decision, not on a sweep**, which is what its own Done-when asked for.

### Phases 33 and 43 — the fences

90 fences tagged by reading each one: **79 `tycho`, 6 `text`, 4 `sh`, 1 `ebnf`**
across `docs/reference/` (48), `docs/guides/` (32), `docs/tutorial.md` (10).
Nothing guessed — the eleven non-Tycho are gdb/lldb/`tychoc` invocations, the
`TYCHO_ARENA_STATS` dump, a directory tree, the inference pattern table, a quoted
compiler warning, the `subscript <name>(<recv>…)` template, a program's stdout,
and the file grammar.

| | before (`b5c8406`) | after |
|---|---|---|
| fences in `docs/` | 252 | 252 |
| tagged ```` ```tycho ```` | 40 | **119** |
| CHECKed (compiled) | 10 | **39** (6 via the no-main retry) |
| FRAGMENT | 19 | 58 |
| MARKED `fence-skip` | 6 | 17 |
| FROZEN | 5 | 5 |
| bare, untagged | ~155 | **64** (`docs/internals/` 56, `docs/rfc/` 4, `docs/` 4) |

```
$ make docs-fences
docs-fences: 39 fence(s) compiled (6 of them with an appended empty main), 80 skipped (reasons above), 0 failure(s)
```

**Two real documentation bugs, which is the point of the exercise.** Tycho has no
one-line suite: `if c: stmt` and `for x in xs: stmt` are both
`error: expected newline` on the built `./tychoc`, against two-line controls that
compile. `docs/guides/arrays-structs.md` showed
`if len(xs) > 0: return Some(xs[0])`; `docs/reference/functions.md` showed
`for x in xs: acc = acc + x`. Same class as the `1_000_000` of phase 33, in two
of the most-read files in the tree, and neither had ever been parsed by anything.

**11 of the 79 needed a `fence-skip`** — phase 33's marker, not a second
convention: three one-file-of-a-multi-file-package examples, three with
statements at top level, two ellipsis-body placeholders, one importing
`core:math` without the `package main` line the compiler requires, and two
calling a helper the prose names but never defines (`bump`, `parse_digit`).

### Two changes to the fence gate, one of them a real bug

**The no-main retry.** A fence declaring whole `fn`s and no `main` was
un-checkable, because `--emit-c` needs an entry point; the only honest option was
a skip. It is now retried with an **empty** `fn main()` appended — which
typechecks exactly the declarations the document contains and invents nothing.
This is not the synthetic-main wrapper phase 33 rejected: that one would have
invented a body for loose statements. Break proof, both directions, on
`docs/reference/arrays-slices.md:67` (a no-main fence):

```
docs-fences: FAIL docs/reference/arrays-slices.md:67 -- does not compile
      <fence>.m:11: error: returning string but proc returns int
          11 |     return "not an int"
make rc=2
--- restored ---
docs-fences: 39 fence(s) compiled (6 of them with an appended empty main), 80 skipped, 0 failure(s)
make rc=0
```

An error in a **non-main** function still reddens, so the lane is coverage rather
than a rubber stamp. (A first attempt at this proof used a generic `fn id(x: $T)`
and did *not* redden — an uninstantiated generic body is never checked. Recorded
because a negative control that fails to fail is the one that would have shipped
a vacuous lane.)

**The collision bug.** Carved fences were written to `f_<closing-line>_<n>.ty`
with `n` restarting in each document, so two documents closing their first fence
on the same line produced the same filename and one silently overwrote the other
— the gate then compiled one document's fence while printing the other's path.
**It was live at `b5c8406`**: `docs/spec/12-aggregates.md:43` and
`docs/spec/15-program.md:43` collide, so one of those two was never actually
compiled by batch 3's gate. At 119 fences there were 8 collisions, which is how
it surfaced: `docs/guides/generics.md:47` reported a failure whose source text
came from a concurrency fence. The document path is now part of the name.

### Gate output — the real runs

```
$ python3 scripts/check_citations.py --stats
citation check: 159 anchored (content-checked, 90 of them the mandatory `> Provenance:`
             single-line refs), 2558 bare (bounds only), 124 source->doc (existence),
             166 source->source (bounds), 12 source->source anchored (content-checked),
             242 doc->doc skipped as frozen archive
citation check: ok (159 anchored contain the token they name, 2558 bare in bounds,
             124 source->doc citations resolve, 166 source->source in bounds,
             12 source->source anchored)

$ make docs-fences
docs-fences: 39 fence(s) compiled (6 of them with an appended empty main), 80 skipped (reasons above), 0 failure(s)

$ sh scripts/check_links.sh
link check: ok (134 markdown files, no dead relative links)

$ sh scripts/spec_check.sh
spec-examples: 9 runnable example(s), all pass
```

Bare citations went 2266 → 2558: +282 doc→doc refs that were checked by nothing
before, now bounds-checked. Source→doc went 121 → 124, source→source 167 → 166
with anchored 11 → 12 (one bare ref in `scripts/docs_fences.sh` promoted to
anchored).

`make test` was **not** run and is unchanged at 551: no `.ty` file, fixture or
golden was touched — the diff is Markdown, one Python gate and one shell gate,
and per `CLAUDE.md`'s gate ladder those cannot affect a compiled artifact.
`make ci` was **not** run; the closing sweep is phase 58's.

## Phases discovered by batch 10

- [x] **Phase 61** — **64 fences in `docs/` still carry no language tag**, so
      `make docs-fences` cannot see them: **56 in `docs/internals/`, 4 in
      `docs/rfc/`, 4 in `docs/`**. This is the residue of phase 43, which tagged
      the reader-facing tree (`docs/reference/`, `docs/guides/`,
      `docs/tutorial.md`) and left this deliberately rather than absorbing it.
  - It is a different job from phase 43 and probably a smaller one. Most of the
    56 sit in the dated design records that **phase 17 decided not to touch** —
    `generics-stage2-body-cloning.md`, `generics-gap-fixes-plan.md`, the
    `*-audit-2026-07-25.md` files — and a fence in a dated study documents syntax
    as it was, so tagging one `tycho` opts a historical snippet into a gate that
    checks today's grammar. Decide that before tagging, not after the gate
    reddens: the answer is probably `text` for the historical ones and `tycho`
    only where the snippet is still meant to be current.
  - The same rule as phase 43 applies and is not negotiable: **do not automate
    the tagging by guessing the language.** A fence mistagged `tycho` reddens the
    gate on prose and the gate gets disabled.
  - Done when: every fence in `docs/`, `docs/rfc/` and `docs/internals/` carries
    a tag, the historical-vs-current decision is written down, and
    `make docs-fences` is green over the enlarged set.
  - Verify: `make docs-fences`, `sh scripts/check_links.sh`.

- [x] **Phase 62** — **the fence gate classifies an `extern fn`-only fence as
      FRAGMENT, and three of them would pass if it did not.** `scripts/docs_fences.sh`
      decides FRAGMENT with `$0 ~ /^[ \t]*fn[ \t]/`, which an `extern fn getpid()
      -> int` line does not match, so `docs/guides/ffi.md:34`, `docs/reference/ffi.md:12`
      and `docs/reference/generics.md:42` are skipped as "no fn declaration"
      while all three compile (verified: each was run through `tychoc --emit-c`
      by hand, two of them needing only the no-main retry the gate already has).
  - Widening the test to `/^[ \t]*(extern[ \t]+)?fn[ \t]/` is one character
    class, but it is **not** a free change: it also opts in `docs/guides/ffi.md`'s
    five-line `extern` catalogue, which fails with `'sqrt' is already defined`
    because the doc re-declares a builtin. That is arguably a doc bug worth
    fixing rather than marking, and deciding which is the phase.
  - Scope: `scripts/docs_fences.sh` and whichever `extern` fences the widened
    rule reddens. No compiler change.
  - Done when: the rule counts `extern fn`, every newly-CHECKed fence either
    compiles or carries a `fence-skip` naming why, and the widening is shown
    reddening on a broken `extern` fence and going green on restore.
  - Verify: `make docs-fences`.

- [x] **Phase 63** — **a mis-inherited bare `:N` that lands *inside* the
      document it wrongly binds to is still invisible**, and phase 44 proved the
      mis-inheritance is common rather than theoretical: 52 of its 77 failures
      were that bug, caught only because a compiler line number is far outside a
      386-line chapter. A `docs/spec/16-builtins.md` paragraph citing `:20` and
      meaning `src/tychoc.c:20` would pass silently, and one such pair was
      repaired by hand in `plan.md:3343` during batch 10 for exactly this reason.
  - Two candidate fixes, and the phase is choosing between them. (a) Require an
    explicit path on any ref whose *inherited* path is under `docs/` — fails
    closed, but costs the continuation form in the appendix-h-style tables where
    a genuine doc→doc pair sits on one line. (b) Leave the grammar alone and
    convert the population to anchored form, which is the general cure recorded
    in `FRICTION.md` for the same class of blindness.
  - Do NOT ship (a) without counting what it reddens first; batch 3 measured
    phase 44 before shipping it and that is why phase 44 took one batch instead
    of two.
  - Verify: `python3 scripts/check_citations.py`, plus the count of what the
    chosen rule reddens, taken before the change lands.

## Batch 11 evidence — phases 57, 59, 56, 60, 62, 63, 61, 53, 58: the final cleanup

Nine phases, run cheapest first, each on its own targeted gate. `make ci` ran
once, at the end, as phase 58's deliberate closing sweep. Everything below was
run; nothing is reasoned about.

### Phase 57 — the entries are not dead, and running the build is what showed it

The phase's premise was that phase 52 made three `Makefile` `rm -f` arguments and
five `.gitignore` entries unreachable. Half true. Two facts, both measured:

- **`make tycho tychofmt tycho-lsp server` now leaves no `.c` at all.** All four
  built, `git status --short` came back empty, and `ls` found none of
  `tycho.c`, `tychofmt.c`, `tycho-lsp.c`, `tycho-httpd.c`, `server/main.c`. A
  plain `./tychoc server/main.ty` still leaves the `server/main` **binary**, so
  that entry was never in question.
- **`--emit-c -o <base>` still writes and KEEPS `<base>.c`.** Run verbatim:
  `./tychoc tools/tycho.ty --shim tools/tycho_shim.c --emit-c -o tycho` printed
  `wrote tycho.c`, the file appeared at the repo root, and `git status --short`
  stayed empty — i.e. the `.gitignore` entry caught it. `src/tychoc.c:12740-12742`
  is the early return that skips the `remove(c_path)` at `src/tychoc.c:12771`,
  and `c_path` is `base + ".c"` (`src/tychoc.c:12693`), so the root-level names
  are exactly what a contributor debugging the toolchain produces
  (`docs/guides/debugging.md:37` is that workflow).

So the phase's own instruction decided it: "an entry that still catches a real
artifact stays." Every entry stays; what was wrong was the **comment** on each,
which attributed the file to `make tycho` / `make server`. Those now name the
route that still produces it, with the date and the verification. `Makefile`'s
`clean` keeps its three `.c` arguments for the same reason — `clean` is where an
`--emit-c` leftover belongs — with a comment saying they are no longer produced
by the `make` target above them.

**The phase entry's own citation had drifted:** it named `Makefile:314` for
`clean`; `clean` was at `Makefile:327` on this tree before this batch's edits.

### Phase 59 — Appendix E stops asserting a retired compiler

`docs/spec/appendix-e-conformance.md`'s §5.2.6 note argued a live prohibition out
of two lanes retired on 2026-07-29. Rewritten in two parts: a present-tense
paragraph saying what covers the clause **today** (`corelib/test/io`'s four
cases, the §5.2.6 example under `scripts/spec_check.sh`, `server/main.ty`'s
`log_safe`) and that **no corelib package is blocked** — all 37 may use a `bytes`
operator; then the whole freeze argument in the past tense, dated, keeping the
13-blocked enumeration as the historical measurement it was.

**The decision the phase asked for, written down: §5.2.6 does not get a `tests/`
fixture.** The block expired, but E.1's convention is that nothing moves merely
because it now could, and the three lanes above already assert every operator in
the clause. That is why `make test` did not move for this phase.

**A sibling note was in scope and had the same defect.** The §29.12 / §29.12.1
note (the "third time this mechanism bites") still read `no corelib package can
use either form` in the present tense. Same treatment. The other two of the
"three notes above it" — §3.9.4 and §14.3.1/§6.2(7) — already carried dated
`Closed`/`Amended` paragraphs and needed nothing.

### Phase 56 — the spec no longer contradicts itself about a zero step

`docs/spec/17-runtime.md`'s §30.2 abort list carried "A `range` step of zero — a
literal `0` step is a compile error; a step that evaluates to `0` at run time
aborts (§10)", cross-referencing the very section that refutes it. The bullet is
deleted and a dated `> Removed` note under the list records what it said and why
it went, pointing at §14.4.

Checked while there, as the entry asked: **Appendix E's abort rows have no
matching entry** — `grep -n "step\b" docs/spec/appendix-e-conformance.md` returns
only the §14.4 loop-shapes row and an unrelated "CI step 17", so there was
nothing to repair there. A tree-wide grep for `zero-step` / `step of zero` /
`step is zero` over `docs/` leaves only dated internal audits (past tense by
construction) and `docs/reference/basics.md:145`, which already says `0..<N` "has
no zero-step case at all" — consistent with §14.4.

### Phase 60 — the second `parallel for` refusal is now pinned

The line was re-derived, not trusted: `grep -n "must name a variable"
src/tychoc.c` puts it at `src/tychoc.c:3411`, where the entry predicted it.
`tests/diag/parfor_expr_source.ty` reaches it with `parallel for v in build():`
and its golden locks the message byte-for-byte:

    tests/diag/parfor_expr_source.ty:22: error: parallel for over a collection or channel must name a variable (bind it first)
        22 |     parallel for v in build():

**Shown reddening, as the Done-when required.** One word flipped at
`src/tychoc.c:3411` (`variable` -> `symbol`), rebuilt, and the golden diverged on
exactly that word; source restored, rebuilt, `diff` clean again. `make test`
went 551 -> **552**, the number the entry predicted.

### Phase 62 — the fence gate counts `extern fn`, and one doc bug fell out

`scripts/docs_fences.sh` decided FRAGMENT with a bare `fn` test. Widened to
`/^[ \t]*(extern[ \t]+)?fn[ \t]/`, plus a second test for the
`extern "lib" fn` form the anchored one cannot reach — the library name sits
between the two keywords, which the phase entry did not mention and which is why
six fences moved rather than three.

Measured, before and after: 39 CHECKed -> 45, with **3 failures**, each read
rather than swept:

- `docs/guides/ffi.md:47` — `'sqrt' is already defined`. **A doc bug, and it is
  fixed, not marked.** The surface-syntax catalogue told the reader to write
  `extern fn sqrt(x: float) -> float`; `sqrt` is a builtin, so that exact line
  does not compile. It is now `hypot` (also libm, not a builtin — verified by
  compiling it), and the prose below says you cannot re-declare a builtin, with
  the date and what the line used to be.
- `docs/guides/ffi.md:109` — loose statements at top level under a whole
  `extern`. A genuine fragment; `fence-skip` naming that.
- `docs/internals/typed-handles-design.md:41` — `unknown type 'Db'`: the design
  sketch names `Db` before its `handle` block. `fence-skip` naming that.

Final: 43 CHECKed, 0 failures — 45 once phase 61 added its two `tycho` fences.
**Shown reddening and going green**, as required: mistyping `float` as `flooat`
in the widened-in catalogue gives `docs-fences: FAIL docs/guides/ffi.md:47 --
does not compile ... unknown type 'flooat'`; restoring returns it to 0 failures.

### Phase 63 — a `docs/` path is inherited only along its own line

**Option (a) was counted before anything shipped, and the count produced a third
option that beat it.** Forbidding an inherited `docs/` path outright reddens
**45** refs. Restricting inheritance to the **same line** reddens **16**. The 29
in the difference are exactly the continuation form the phase warned option (a)
would cost — a path and its sibling range inside one table cell in `docs/rfc/`
and `docs/spec/appendix-h-differences.md`. The failure mode needs *distance*:
nobody loses track of the subject halfway along a line. So the rule shipped is
(a) narrowed to preserve precisely what (a) would have destroyed, and
`scripts/check_citations.py`'s header carries both numbers so the choice stays
re-checkable.

**Negative control, run after the repair:** appending two lines to
`docs/rfc/value-lifetime-regions.md` that name `docs/spec/16-builtins.md:10` on
one line and write the bare continuation form (colon, then 20) on the next — the exact batch-10 scenario, and a
number well inside the chapter — fails with `a bare ref inheriting the docs/ path
docs/spec/16-builtins.md from line 469`. Removed; gate green.

**The 16 repairs found two live defects the bounds check could never see.**

- `docs/internals/spec-plan.md` at 585-589 claimed the W3C EBNF dialect is
  declared at `docs/spec/00-conventions.md:131` with the form table at
  `docs/spec/00-conventions.md:133-142`. Line 131 is in §1.5 Versioning.
  `Both use a W3C-style EBNF:` is at `docs/spec/00-conventions.md:154`, the table
  runs `docs/spec/00-conventions.md:156-168`, and the `ebnf` fence language is
  fixed at `docs/spec/00-conventions.md:193`, not 170. All three repointed:
  spelling the path on a citation already known to be wrong would have cemented
  it.
- Making that ref explicit **exposed three more**. `docs/internals/spec-plan.md`
  at 600 and 602 cite §9.5 as `docs/spec/07-memory-model.md:64-81`, and had been
  `docs/spec/07-memory-model.md:69-70` and `docs/spec/07-memory-model.md:70-71`,
  and had been inheriting `00-conventions.md` — the sentence names
  `docs/spec/07-memory-model.md` **without a line number**, so it never set the
  binding. They now name `docs/spec/07-memory-model.md` outright. This is the
  bug the phase exists for, caught by the phase's own rule within a minute of it
  landing.

The remaining thirteen were spelled with the path the gate already resolved them
against, which changes no meaning. Three anchored refs to the `ARCHIVED` tuple in
`scripts/check_citations.py` had to move from 282 to 316 because this phase's
docstring insertion shifted the file — the hazard the brief names, caught by the
gate itself.

### Phase 61 — every fence in `docs/` now carries a tag

64 bare fences, each opened and read: 56 in `docs/internals/`, 4 in `docs/rfc/`,
4 in `docs/` — the counts the gate's docstring predicted. **Nothing was
automated and no language was guessed.** The rule, now also in
`scripts/docs_fences.sh`'s header:

- **`text`** (59 of the 64) for anything in a dated design record or an archived
  plan, for command output and transcripts, commit lists, symbol/line tables,
  and deliberately-broken illustrations. Tagging a historical snippet `tycho`
  would opt syntax-as-it-was into a gate that checks today's grammar — the
  failure phase 43 named, and the one this phase had to avoid at 29 fences in
  `docs/internals/plan-option-result-DONE.md` alone.
- **`sh`** for the two that are shell command lines:
  `docs/internals/integer-overflow.md` at 39 and `docs/thesis.md` at 236.
- **`tycho`** for only three: `docs/from-c-to-arenas.md` at 62 and 75, and
  `docs/internals/value-semantics-limits.md` at 56. Each was **confirmed by the
  gate compiling it**, not by inspection — two became CHECKed (both via the
  no-main retry), which is why CHECK went 43 -> 45 and the retry count 10 -> 12.

Verified zero remain: a fence-toggle scan over `git ls-files 'docs/*.md'` reports
252 fences and the tag histogram `tycho 122, text 73, ebnf 23, c 13, sh 10,
output 9, python 2` — **no bare fence**. Of the 122 `tycho`: 45 CHECK, 53
FRAGMENT, 19 MARKED, 5 FROZEN. `make docs-fences`: 0 failures.

### Phase 53 — `r_step` is deleted, and the spec sentence that blocked it is amended

Three blockers were on record. Phase 17 closed in batch 10. The citation-shift
blocker was **neutralised rather than accepted**: every deletion was padded with
a comment of exactly the length it removed, so `src/tychoc.c` is **12774 lines
before and 12774 after**, and not one bare `src/tychoc.c:N` citation moved. That
is why this phase reddened no citation of its own.

The third blocker was the spec, and this batch had authority to settle it.
`docs/spec/10-statements.md`'s §14.4 Provenance block asserted the field
normatively — "The step codegen and its zero-step guards still exist but are
unreachable" — citing the `Stmt` declaration. A normative sentence describing
dead code is a spec defect, not a reason to keep the code. The block now says
what is true (`Stmt` carries `r_start` and `r_stop` only, `src/tychoc.c:1555-1561`;
every `S_FORRANGE` emits `h_i < _stopN; h_i += 1`, `src/tychoc.c:10885-10889`)
and an `Amended 2026-07-30` paragraph records the old sentence, its window of
truth (2026-07-29 to 2026-07-30) and exactly what went. The zero-step **trade**
paragraph at `docs/spec/10-statements.md:115-121` is untouched, as the entry
required. `sh scripts/spec_check.sh` — which the entry's own Verify list had
omitted until batch 8 added it — is green.

**What the emitted C lost, measured on one fixture holding all three shapes**
(foreach over an array, foreach over a map's `keys`, `0..<N`, and `parallel for`):

    before:  _step 12 occurrences · "range step is zero" 4 · "_stepN > 0 ? ..." 4
    after:   _step  0 occurrences · "range step is zero" 0 · "_stepN > 0 ? ..." 0
    loop header now: for (tycho_int h__fi0 = 0LL; h__fi0 < _stop1; h__fi0 += 1) {

**Behaviour unchanged, shown by running, not by diffing C:** the same program
prints `arr=7 / map=3 / dotlt=10 / par=40` before and after — `diff` of the two
outputs is empty. `make test` **552** (unchanged from phase 60), `make conc`
**38 passed, 0 failed**.

**One deviation from the Done-when, stated rather than hidden.** It asked for
`grep -n r_step src/tychoc.c` to be empty. Six mentions remain and every one is a
comment recording the removal — the field declaration's history note, the deleted
`parallel for` refusal, the deleted literal-zero-step refusal quoted verbatim,
and three one-line notes where a scan or clone call used to be. Erasing the name
would satisfy the letter and destroy the one string a future reader would grep
for. The field and every use of it are gone; that is the criterion that mattered.

### Phase 58 — the closing sweep, and the lane that paid for itself on the way in

**`rtparity` joins `make ci` as step `[2d/13]`.** It is a `tests/` lane, not a
corelib dogfood, so it is a sub-lane of step 2 rather than a new number — the
convention the entry names. It costs ~1s (one `--emit-c`, no `cc`), so there was
no argument for keeping it out. The `Makefile` comment that pointed at this phase
now records the decision.

**It reddened immediately, and it was right.** Run before wiring it in:

    rtparity: FAIL - diagnostic "tycho: range step is zero\n" is in the oracle but NOT emitted.

Phase 53 had just deleted that abort. `make test` (552), `make conc` (38) and the
ASan lanes were all green over the same tree — **this lane was the only gate that
saw it**. The oracle was out of date, not the codegen, and
`tests/rtparity/run.py` now records the removal with its date and reason rather
than dropping the line silently. After: `diagnostics 27/27 as recorded (ok)`.

Two other things the sweep had never covered are in this run: `make fetch` in
step `[3/13]` (batch 9) and the `docs-fences` step `[12b/13]` whose
triple-backtick defect batch 6 fixed. Batch 6's closing run predates both.

`sh -n scripts/ci.sh` parses. The observed sweep result is in the Status section
below.

## Phases discovered by batch 11

- [x] **Phase 64** — **§E.3's Tier 2 paragraph still describes a two-compiler
      oracle that no longer runs.** Found by phase 59 while sweeping Appendix E's
      E.2.1 notes for retired-`tychoc0` language; filed rather than absorbed
      because phase 59's scope was that notes section, and this is a different
      one.
  - `docs/spec/appendix-e-conformance.md` §E.3 Tier 2 says `scripts/spec_examples.sh`
    "builds it with **both** the reference `tychoc` and the self-hosted `tychoc0`,
    runs each, and asserts both produce stdout equal to the `output` block", and
    calls it "the two-compiler oracle of E.1 applied to the spec's own examples:
    a divergence between the compilers ... is a defect that blocks the build".
  - The script itself says otherwise, in its own header: `scripts/spec_examples.sh:14-15`
    records that through 2026-07-26 each example was also run through the
    self-hosted `tychoc0`, that the compiler is frozen and **no gate builds it**,
    so that leg is gone. `sh scripts/spec_check.sh` prints `(tychoc)` on every
    example, one compiler.
  - Same class as phase 59, same cure: say what runs now, keep the two-compiler
    history in the past tense with its date. Check the parenthetical about
    "building `tychoc0` from source each..." that trails the paragraph while
    there — it is part of the same claim.
  - Done when: no sentence in §E.3 asserts a live `tychoc0` leg.
  - Verify: `sh scripts/spec_check.sh` plus the two doc gates. **Not**
    `make test` — Markdown only.

- [x] **Phase 65** — **two `docs/internals/spec-plan.md` citations into Appendix E
      are drifted, and the sentence around them is self-describing about it.**
      Found by phase 63 while repairing the sixteen cross-line inheritances; the
      repair made the binding explicit, which is all phase 63 owed, but did not
      re-derive the numbers.
  - `docs/internals/spec-plan.md` at 607-608 now reads
    `docs/spec/appendix-e-conformance.md:188` twice and says of it "`:188` is a
    fixture row (§17.3 today, the §24.2 row when the drift was first noticed)".
    Line 188 of that file is today the **§15.2 parameter-passing-modes** row, so
    the sentence has drifted a third time since it was written to record a drift.
  - This is the residual the bounds check cannot see and phase 63's rule does not
    address either: an explicit, in-bounds, wrong single-line ref. The general
    cure on record for it is the anchored form (`path:N@token`), which is what
    `FRICTION.md` recommends and what `scripts/check_citations.py` already
    enforces inside `> Provenance:` blocks.
  - Decide whether the sentence still earns its place at all: it is a note about
    a citation that used to be wrong, inside a resolved bullet, and rewriting it
    to name the clause rather than a line number would end the cycle.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.
    **Not** `make test`.

### Phase 58 — what the sweep found, and the observed exit codes

**Two full sweeps, and the first one is the finding.** It is recorded here rather
than quietly re-run, because a sweep that reddens is the sweep doing its job.

    run 1:  17m49s (1069s)  CI_EXIT=2   failed at [9b/13] make editors-check
    run 2:  18m06s (1086s)  CI_EXIT=0   CI GREEN -- tree is good

**Run 1, `[9b/13]`:** `STALE: editors/zed/README.md claims 832 committed .ty
files, tree has 837. Fix the README to say 837.` The count is a `find` over the
working tree (`scripts/editors_check.sh:57-59`), and at HEAD 06118f7 the tree
held **836** tracked `.ty` files against the README's 832 — so it was **already
stale by four before this batch**, and phase 60's new fixture made it five. The
last full sweep was batch 6's; nothing between then and now ran this lane. That
is precisely the gap phase 58 was filed to close.

Fixed by editing the number, which is what the gate's own message and the
README's "Edit the number only" instruction ask for. `make editors-check` alone
then returns `ok README says 837 committed .ty files, and so does the tree` and
`editors-check: ok`.

**Per `CLAUDE.md`, `make ci` was not used as the debugging loop.** The failing
step's own gate was re-run, and the five lanes run 1 never reached were then run
individually before spending a second sweep: `sh bench/guard.sh` (ok, tycho at
23% of C on `maptree`, elision live), `make recursion` (all green),
`sh scripts/spec_check.sh` (9 runnable examples, all pass), `make docs-fences`
(45 compiled, 0 failures), `make check-links` (links ok, citations ok). Only then
was run 2 spent, to confirm what was already believed.

**Run 2 reached every lane**, in order: `[1] [2] [2b] [2c] [2d] [3] [3b] [4] [5]
[6] [7] [8] [9] [9b] [10] [11] [12] [12b] [13]` — nineteen, including the new
`[2d/13] make rtparity`, the `make fetch` line inside `[3/13]` and the
`[12b/13] make docs-fences` step, the three lanes `make ci` had never swept.

## Status — CLEANUP COMPLETE

Batch 11 closed nine phases: **53, 56, 57, 58, 59, 60, 61, 62, 63**. With them,
every phase in this plan is closed except one decision that belongs to the user.

**Commits in this batch**

    chore: batch 11 — the final cleanup and the closing sweep

One commit, deliberately: the nine phases are cleanup with a single closing
sweep over all of them, and splitting them would have meant nine sweeps or eight
unverified commits.

**Counts at close**

    make test        552   (551 at batch 9; +1 = tests/diag/parfor_expr_source)
    make conc         38
    make ci          exit 0, 18m06s, all 19 lanes
    src/tychoc.c   12774 lines, unchanged by phase 53's deletion (padded)
    docs/ fences     252, zero untagged (122 tycho: 45 CHECK, 53 FRAGMENT, 19 MARKED, 5 FROZEN)
    citations        ok (159 anchored, 2605 bare, 127 source->doc, 168+12 source->source)
    unchecked phases   3

**What remains open**

- **Phase 51 — `char` element types.** Untouched by this batch on instruction: it
  is a language-design decision for the user, not a cleanup.
- **Phase 64 — §E.3's Tier 2 two-compiler claim** (filed by this batch, from
  phase 59). Markdown only.
- **Phase 65 — two drifted Appendix E citations in `docs/internals/spec-plan.md`**
  (filed by this batch, from phase 63). Markdown only.

Both new phases are documentation and neither blocks anything. Nothing in the
tree is left in a half-finished state: every gate this batch could redden was run
and is green, and the one deviation from a Done-when (phase 53's `grep` criterion,
six surviving history comments) is stated in its evidence rather than papered
over.

## Batch 12 — phases 64 and 65 (2026-07-30)

Markdown only. Per `CLAUDE.md`'s gate budget, no compiled gate was run: nothing
in this batch can reach a compiled artifact, and batch 11's sweep
(`CI_EXIT=0`, `make test` 552) stands.

### Phase 64 — §E.3 Tier 2 no longer claims a live `tychoc0` leg

**What was true.** `scripts/spec_examples.sh` records the retirement in its own
header at `scripts/spec_examples.sh:13-16` — "Until 2026-07-26 each example was
also run through the self-hosted tychoc0; that compiler is frozen (see
compiler/tychoc0.ty) and no gate builds it, so that leg is gone". Batch 11's
brief cited `scripts/spec_examples.sh:14-15`; the sentence now begins mid-line 13
and ends mid-line 16, so the range was re-derived rather than copied. `git log`
on that script confirms the cut: commit `4bb97e7` ("freeze tychoc0, cut it from
every gate"). Two `tychoc0` mentions survive in the script and both are inside
that historical sentence.

**What §E.3 claimed.** The Tier 2 bullet said the gate builds each example with
"**both** the reference `tychoc` and the self-hosted `tychoc0`, runs each, and
asserts both produce stdout equal to the `output` block", called it "the
two-compiler oracle of E.1 applied to the spec's own examples", and closed with
a parenthetical blaming `make spec-check`'s wall time on "Building `tychoc0`
from source each run" — the same claim in three places, all present tense.

**The fix** (`docs/spec/appendix-e-conformance.md:412-431`): the bullet is now
titled "example execution on the reference compiler", states the single-compiler
rule against `docs/spec/00-conventions.md` §1.3, and notes that every example
the gate runs is reported `(tychoc)`. The two-compiler oracle and the wall-time
parenthetical are kept, moved into one **Historical, through 2026-07-26**
parenthetical in the past tense, pointing at E.1's note and at
`scripts/spec_examples.sh:13-16`. Same shape as phase 59's cure. No sentence in
§E.3 now asserts a live `tychoc0` leg — Done-when met.

### Phase 65 — the drifted `:188` pair, and the frozen-vs-live call

**The determination: `docs/internals/spec-plan.md` is LIVE, so it was
repointed.** Reasoned rather than reflexed, because batch 10's principle cuts
the other way for dated records — a *fresh* citation inside a record frozen at a
date is a lie the reader cannot detect, where a stale one announces itself. Four
facts decided it:

1. It is not in the frozen family. The frozen records are
   `docs/internals/plan-*-DONE.md`; this file is not named that way and is not
   one of them.
2. Its own header (`docs/internals/spec-plan.md:1-11`) calls it "the working
   plan for writing the spec", and its status line has been updated *after* its
   2026-07-12 date ("last item cleared 2026-07-20").
3. Its history is a history of maintenance, not of freezing: `eaf0064` corrected
   a stale punch-list tally, `2e6e698` (batch 2) swept its drifted citations,
   `a24242e` (batch 3) repaired references to things that no longer exist, and
   `6ea4c35`'s phase 63 made its bare-`:N` bindings explicit.
4. The bullet in question already carries corrections dated after its own
   `RESOLVED (2026-07-23)` — the `make fixpoint` retirement note. A frozen
   record would not have accreted those.

So a repoint here is not an undetectable fresh fact in a sealed record; it is
what this file has done every time it drifted.

**What was wrong.** `docs/internals/spec-plan.md` at 607-608 cited
`docs/spec/appendix-e-conformance.md:188` twice while *describing* that
citation's own drift: "(§17.3 today, the §24.2 row when the drift was first
noticed)". Verified against the file: line 188 of
`docs/spec/appendix-e-conformance.md` is today
`| §15.2 | parameter passing modes | ...` — a third drift, exactly as filed.

**The fix.** Rather than repoint to a fourth line number and re-arm the cycle,
the sentence now names the clause: it says the old citation pointed into E.2's
per-clause fixture-coverage matrix, never E.2.1's flagged-clause list, and
records the three drifts (§24.2 → §17.3 → §15.2) in prose with no line number to
go stale. This is phase 65's own preferred option and matches `FRICTION.md`'s
anchored-citation reasoning: a citation that must survive edits should name
something stable. The sentence keeps its place because it explains why the
neighbouring `make fixpoint` citation is gone, which is still load-bearing.

### Verification — three gates, foreground, real output

    $ python3 scripts/check_citations.py
    citation check: ok (159 anchored contain the token they name, 2605 bare in bounds,
    128 source->doc citations resolve, 169 source->source in bounds, 12 source->source anchored)

    $ sh scripts/check_links.sh
    link check: ok (134 markdown files, no dead relative links)

    $ sh scripts/spec_check.sh
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: ok docs/spec/03-types.md:142 (tychoc)
    spec-examples: ok docs/spec/03-types.md:233 (tychoc)
    spec-examples: ok docs/spec/12-aggregates.md:47 (tychoc)
    spec-examples: ok docs/spec/12-aggregates.md:235 (tychoc)
    spec-examples: ok docs/spec/12-aggregates.md:350 (tychoc)
    spec-examples: ok docs/spec/12-aggregates.md:540 (tychoc)
    spec-examples: ok docs/spec/12-aggregates.md:685 (tychoc)
    spec-examples: ok docs/spec/12-aggregates.md:763 (tychoc)
    spec-examples: ok docs/spec/15-program.md:40 (tychoc)
    spec-examples: 9 runnable example(s), all pass

Nine examples, every one tagged `(tychoc)` and nothing else — the observation
that backs the rewritten §E.3 sentence. Citation counts moved by one in two
buckets (127→128 source->doc, 168→169 source->source) because the new
`scripts/spec_examples.sh:13-16` reference in Appendix E is a doc-to-source
citation the gate now resolves.

## Phase discovered by batch 12

- [x] **Phase 66** — **a third drifted Appendix E citation, in the same
      `docs/internals/spec-plan.md` bullet phase 65 repaired.** Filed rather
      than absorbed: phase 65's scope was the `:188` pair named in its brief,
      and this is a different citation on a different line.
  - `docs/internals/spec-plan.md:605` cites
    `docs/spec/appendix-e-conformance.md:235-241` as the place where §9.5 is
    evidenced by the whole differential suite. That range lands on E.2's
    fixture rows for §29.5 through §30.1 (`| §29.5 | string builtins | …`
    onward) — ordinary coverage-matrix rows, not the flagged-clause list.
  - The flagged-clause bullet it means is
    `docs/spec/appendix-e-conformance.md:250-256`: "**§5.1 identity, §9.4
    uniqueness, §9.5 transparent optimizations, §10.4 soundness** — properties
    of the model exercised by the whole corpus rather than by one fixture",
    including the ASan/UBSan-vs-native agreement and `make fuzz`, and closing
    with the note that `eqparity`/`typeparity`/`make fixpoint` went with the
    `tychoc0` freeze.
  - Same residual class as phase 65: explicit, in-bounds, wrong — invisible to
    the bounds check. Prefer the same cure phase 65 used (name the clause,
    E.2.1's flagged-clause list) over repointing to a range that will drift
    again; the range form cannot be anchored, which is exactly why it rots.
  - Done when: no citation in that bullet resolves to a row it does not mean.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.
    **Not** `make test`, **not** `make ci` — Markdown only.

Phase 51 was not started: it is the user's language-design decision and its own
conclusion is that it needs a separate plan.

  ### Evidence — phase 66, 2026-07-30

  Fixed directly rather than by an agent: one citation, target already identified
  by batch 12. `docs/spec/appendix-e-conformance.md:250-256` was confirmed the real
  target by reading it — the bullet for "§5.1 identity, §9.4 uniqueness, §9.5
  transparent optimizations, §10.4 soundness", whose text ("every golden fixture is
  built native *and* under ASan/UBSan", "`make fuzz` applies the same differential")
  is what the citing sentence in `docs/internals/spec-plan.md` describes.

  **Repaired by NAME, not by number.** This one reference has now drifted four
  times — §24.2 → §17.3 → §15.2 → E.2 fixture rows — and each repair set it up to
  drift again. It now names the E.2.1 bullet by its clause list, which does not
  move when lines are inserted above it. That is the only form of this citation
  that stops costing a phase every time Appendix E is edited.

  Gates: `citation check: ok (159 anchored, 2614 bare in bounds, 128 source->doc,
  169 source->source in bounds, 12 source->source anchored)` — the bare count fell
  by exactly one, the drifting number being removed rather than repointed;
  `link check: ok (134 markdown files)`; `spec-check` all three legs green, 9
  runnable examples pass. No compiled gate run: Markdown only.

## Phases discovered by batch 12

- [x] **Phase 67** — **the question phase 51 could not answer for the user: does
      `[]char` exist, and if so what is it for beside `bytes`?** Phase 51 declined
      to make `char` spellable and this is why. The decline was on evidence, not
      taste: the absence of a `char` type keyword is asserted normatively in
      `docs/spec/03-types.md:75`, `docs/spec/02-grammar.md:175`,
      `docs/spec/01-lexical.md:116` and again at `docs/spec/01-lexical.md:373`
      (which calls the tree-sitter grammar **wrong** for listing `char` as a type
      keyword), and it is pinned by `tests/reject/char_as_type.ty`. Four published
      sentences and a green fixture are not something an agent should overturn on
      its own initiative.
  - **The actual question, stated so it can be answered yes or no:** if `char`
    becomes writable, `[]char` becomes writable with it, and the tree then has
    **two spellings of a byte sequence with different operator sets** —
    `bytes`, which already has `len`, indexing, slicing and concatenation
    (`docs/spec/03-types.md:128`, and batch 7's phase 7 added the rest), and
    `[]char`, which would get the element-wise arithmetic table instead
    (`docs/spec/12-aggregates.md:258`: `+` and `-` only). Neither can be
    converted to the other today. Three answers are coherent:
    1. **No** — keep `char` inference-only. Costs nothing, changes nothing, and
       is what the spec currently says. This is the status quo and needs no work.
    2. **Yes, scalar only** — `char` becomes a type keyword usable in a parameter,
       a return and a field, but `[]char` and `[N]char` stay refused explicitly,
       with a diagnostic naming `bytes` as the byte-sequence type. Keeps one
       spelling for a byte sequence.
    3. **Yes, fully** — `[]char` is a first-class array and the overlap with
       `bytes` is accepted, with §12/§16 documenting when to reach for which.
  - **The work if the answer is 2 or 3 is small and already located.** Two parse
    sites reject `char`, re-derived at batch 12 and both still current: a named
    type position falls through to `src/tychoc.c:2141@unknown type` (`char` is
    lexed as an identifier, so there is no `TK_KW_CHAR` — that is why the two
    sites differ), and the keyword switch's `src/tychoc.c:2159@expected a type`
    is where a `TK_KW_CHAR` case would land. `T_CHAR` already has a C
    representation (`src/tychoc.c:1361`), a diagnostic spelling
    (`src/tychoc.c:7435`) and a full operator story; only the *name* is missing.
  - **Whichever answer, it is a spec change first.** The four sentences above and
    `tests/reject/char_as_type.ty` must be rewritten or deleted in the same
    commit as the parser change, or the tree ships a spec that contradicts its
    compiler. Answer 1 means editing nothing.
  - Done when: the user has answered, and either the tree is unchanged with the
    answer recorded here, or the parse sites, the four spec sentences and the
    fixture have all moved together.
  - Verify (answers 2 and 3 only): `make test`, `sh scripts/spec_check.sh`,
    `python3 scripts/check_citations.py`, and — because a new type keyword
    changes what every `.ty` file in the tree may contain — `make editors-check`
    and `sh scripts/tools_check.sh`, sequenced per `CLAUDE.md`'s "tooling before
    corpus".

  ### Decision — phase 67, DECLINED by the user, 2026-07-30

  **`char` does not become a writable type name.** It stays produced (`char_at`,
  `to_char`) and inferred, never written. Asked and answered directly rather than
  delegated, because phase 51 established this was a language decision and not a
  gap to be filled.

  What the decision preserves, all of which adding the name would have cost:

  - Four normative sentences stay true — `docs/spec/03-types.md:75`,
    `docs/spec/02-grammar.md:175`, `docs/spec/01-lexical.md:116` and `:373` each
    state the absence as a rule, not as an omission.
  - `tests/reject/char_as_type.ty` stays green. It is a pre-existing fixture
    asserting the refusal; naming the type would have inverted a passing test,
    which is the clearest possible signal that the absence was designed.
  - `bytes` remains the single spelling for a byte sequence. The `[]char` fork —
    two spellings with different operator sets, and a §12/§16 answer for how they
    convert — never has to be answered because it never arises.

  What it costs, stated plainly: `char` is still the one element type whose
  operator set is narrower than `int`'s *and* which cannot appear in a type
  annotation. That rule is now pinned by the eight fixtures phase 51 added
  through inference, so it is tested — but it is tested indirectly, and a reader
  cannot write the type to explore it. That is the accepted trade.

  Phase 51's other two decisions stand and are unaffected: `to_char` exists and
  aborts out of range, and `\xNN` works in char literals.

## Status — ALL PHASES CLOSED, 2026-07-30

**67 phases filed, 67 closed. `make ci` green, `CI_EXIT=0` observed.**

The closing sweep reddened once before it passed, and the failure is worth
recording because it was caused by the brief, not by the work. Phase 51 added
`\xNN` to char literals; its gates were scoped to `make test` and
`sh scripts/spec_check.sh` and it was told not to sweep, so nothing it could run
would have shown that the editor grammars did not know the new escape. Three
fixtures newly failed the zed corpus parse.

`CLAUDE.md`'s "sequence tooling before corpus" rule was written earlier the same
day, out of the phase 6/8 reordering, and then not applied when scoping phase 51.
The rule was right; applying it is the part that has to happen every time.

Fixed at `31b2018`: both grammars learned the escape, `tests/reject/hex_escape_one_digit.ty`
joined the known-bad set as the lexical reject it is, and the zed README's corpus
count went 837 → 845 — caught by the gate batch 3 built for exactly that, which
has now fired on three separate occasions.

**What the day shipped**, beyond the cleanup: backtick raw strings; element-wise
array arithmetic with scalar broadcast; three-clause `for`, bare `for:` and
`parallel for i in 0..<N:`; `range()` deleted across 566 sites; the `tychoc0`
freeze lanes retired; bounds-check elision restored for the three-clause form;
`to_char` and char-literal `\xNN`.

**Three deliberate losses, all recorded rather than discovered later:** continuous
proof that `tychoc0` accepts what `tychoc` accepts, the zero-step guarantee that
died with `range()`, and — until phase 27 restored it — bounds-check elision.

**Two things closed by decision rather than by work**, which is why the count
reached zero: phase 17's remaining ~1300 bare citations were retired into
`FRICTION.md` with the reason and the only real cure, and phase 67 declined the
`char` type name on four normative spec sentences and a green reject fixture.
