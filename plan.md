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

- [ ] **Phase 1 — retire the `tychoc0` freeze lanes**
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

- [ ] **Phase 2 — fold `tests/postfreeze/` back into `tests/`**
  - Scope: `tests/postfreeze/*.ty` + goldens → `tests/`; `tests/postfreeze/abort/`
    → `tests/abort/`; the two loops in `tests/run.sh` that serve them;
    `scripts/asan_self.sh`'s glob; `.gitignore`'s `!/tests/postfreeze/*.out`
    exception; `docs/spec/appendix-e-conformance.md`'s references to the lane.
  - Done when: `tests/postfreeze/` does not exist, every fixture it held runs from
    its new home with its golden, and `make test` counts the same number of
    passes as before the move.
  - Verify: `make test` (record the count before and after — they must match).

- [ ] **Phase 3 — lex `;` and `..<`**
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

- [ ] **Phase 4 — three-clause `for init; cond; post:` and bare `for:`**
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
