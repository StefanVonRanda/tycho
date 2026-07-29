# Post-freeze fixtures, then raw strings

The previous plan is complete and archived at
[docs/internals/plan-friction-DONE.md](docs/internals/plan-friction-DONE.md)
(ten phases, closed 2026-07-26). Its `plan.md` phase-N references throughout
`FRICTION.md` and `docs/` are narrative and still resolve; the citation gate is
green across the move (`citation check: ok (22 anchored, 1671 bare in bounds, 77
source->doc)`, `link check: ok (129 markdown files)`), which is why no repair
phase appears below.

## Goal

Two things, in order, and the first exists to make the second possible at all.

**One.** Give the tree a place to put a fixture that exercises syntax the frozen
`compiler/tychoc0.ty` does not accept. Today there is no such place: every
`tests/*.ty` is fed to a `tychoc0`-derived binary by two gates, so a fixture for
any language feature added after the 2026-07-26 freeze reddens `make ci` by
construction. This is recorded as an open item in `FRICTION.md` ("**new language
syntax can no longer be given a `tests/` fixture**", found by the previous plan's
phase 2) and it blocks every future language change, not only this one.

**Two.** Add backtick-delimited raw string literals — `` `...` `` — where no
escape is interpreted and a newline is a literal newline. This is the one item
from `new_ideas.md` that is genuinely absent from the language.

Done looks like: `make ci` green, with a `tests/postfreeze/` fixture that uses a
raw string, a golden that records its output, and the two `tychoc0` lanes still
asserting exactly what they asserted before over exactly the corpus they covered
before.

## Pre-flight

- **Worst case:** a lane is widened or narrowed by accident. Narrowing it
  silently stops gating real programs — the failure mode `scripts/frontparity.sh`
  itself was written to fix (`:1-11`) and re-discovered in its own header at
  `:86-98`. Phase 1's verify therefore pins the *counts* both lanes report, not
  just their exit status.
- **Reversibility:** fully. Every phase is a commit on `main`, no data is
  touched, and no destructive path exists in this plan.
- **Verified — the blocker is real, both halves of it:**
  - `compiler/fixpoint.sh:24` — `for f in tests/*.ty examples/*.ty` builds each
    with `./tychoc` (`:26`, skipping what tychoc refuses via `|| continue`) and
    then requires `B`, a `tychoc0`-derived binary, to emit C that compiles and
    matches (`:28-29`). New syntax → tychoc accepts → `B` refuses → `FAIL <nm>`.
    The second loop at `:68-72` walks the same glob for the split-compiler
    dogfood.
  - `scripts/frontparity.sh:152-156` — the same glob plus `tests/conc/*.ty`,
    `tests/warn/*.ty`, `tests/abort/*.ty`, `tests/diag/*.ty`, `tools/*.ty` and
    `compiler/tychoc0.ty`, each through `check_one` (`:135-150`), which skips
    only when **tychoc** refuses (`:137-141`) and fails when tychoc accepts and
    `tychoc0` refuses (`:145-147`).
- **Verified — the escape hatch that already exists is location-based, not
  per-fixture.** `scripts/frontparity.sh:74-83` names the two directories held
  outside the lane on purpose and says why: `examples/corelib/` holds "two
  DELIBERATE divergences the freeze created and Appendix E records
  (`result/main.ty` uses a nested pattern, `httpd/main.ty` a `\r` escape)", and
  `server/` is "the program deliberately written OUTSIDE the freeze". Phase 1
  extends that existing convention to `tests/`; it does not invent a mechanism.
- **Verified — the positive lane that must pick the new directory up.**
  `tests/run.sh:113-118` walks `examples/*.ty tests/*.ty`, derives `name` from
  the basename, and calls `run_one` with `tests/$name.out` as golden and
  `tests/$name.in` as stdin if present. `tests/pkg/*/` is handled by a second
  loop at `:124-134` with goldens at `tests/pkg/<name>.out` — the precedent for
  a subdirectory with its own loop.
- **Verified — what the language already has, so this plan does not rebuild it.**
  Interpolation ships as f-strings, `f"a{e}b"` desugaring to `"a" + str(e) + "b"`
  at parse time (`docs/spec/01-lexical.md:302-317`; lexing `src/tychoc.c:289`,
  `:297-367`, desugar `:1826-1866`). `\r` is in the escape set
  (`docs/spec/01-lexical.md:264`; `src/tychoc.c:382`). A multi-line string is
  written as adjacent pieces joined inside parens
  (`docs/spec/01-lexical.md:272-282`). **`new_ideas.md` item 1 (`${name}`) is
  therefore dropped on the user's call** — it is a second spelling of a shipped
  feature, not a missing one.
- **Verified — raw strings are absent.** `grep -rn 'backtick\|raw string'
  docs/spec/*.md` returns exactly one hit, `01-lexical.md:265`, and it is about a
  *raw control byte*, not a raw literal. `docs/spec/01-lexical.md:284` states the
  rule positively: "There is no other multi-line string form and no backslash
  line-continuation."
- **Assuming — the join rule is the constraint raw strings must respect, and I
  have not yet read the code that enforces it.** `docs/spec/01-lexical.md:287-291`
  says adjacent-piece joining is defined on the literals' *escaped source text*,
  "sound only because every escape is exactly two characters", which is the
  stated reason `\0` and `\xNN` were kept out. A raw literal has no escapes at
  all, so it is not obviously safe to let `` `a` `` join with `"b"`. **Risk if
  wrong:** a raw piece joins across a boundary and silently changes the interned
  literal's length. **Phase 2 resolves this by reading `src/tychoc.c:2150-2166`
  before writing the lexer change**, and the conservative default it should carry
  unless that code says otherwise is the f-string rule at `01-lexical.md:285-287`
  — a raw literal never joins, and `` `a` "b" `` is a syntax error.
- **Assuming — `tests/postfreeze/` is the right shape rather than a per-file
  opt-out marker.** A marker (say a `# postfreeze` first line the two lanes
  grep for) would be finer-grained, but it puts the skip decision in a comment
  that nothing validates. The directory is checkable by `ls`. **Risk if wrong:**
  low; a marker can be layered on later without moving anything.

## Phases

- [ ] **Phase 1 — `tests/postfreeze/`: a fixture lane the frozen compiler never sees**
  - Scope: new directory `tests/postfreeze/` with one canary fixture and its
    golden; `tests/run.sh` (new loop, modelled on the `tests/pkg/*/` loop at
    `:124-134`); the header comments of `scripts/frontparity.sh` (the COVERAGE
    block, `:66-84`) and `compiler/fixpoint.sh` (`:1-9`) to name the new
    directory as deliberately out of scope; `docs/spec/appendix-e-conformance.md`
    to record it alongside the two `examples/corelib/` divergences already there.
    **Not** touched: the globs in `compiler/fixpoint.sh:24`, `:68` and
    `scripts/frontparity.sh:152-172` — `tests/postfreeze/*.ty` is excluded by
    those globs already, and that is the point; the phase proves it rather than
    editing them.
  - The canary must use syntax `tychoc` accepts and frozen `tychoc0` refuses, so
    that the lane is proven rather than asserted. **Nested patterns are the
    documented candidate** — `scripts/frontparity.sh:78` records
    `examples/corelib/result/main.ty` diverging on exactly that. Confirm against
    a built `tychoc0` before committing to it; if it does not diverge, pick
    another shape from Appendix E's list and say which.
  - Done when: (a) the canary compiles under `tychoc` and its output matches its
    golden through `tests/run.sh`; (b) a `tychoc0` built at HEAD **refuses** the
    canary, captured as the diagnostic it prints; (c) `sh scripts/frontparity.sh`
    and `sh compiler/fixpoint.sh` are both green and report the same counts as
    before the phase; (d) `make ci` green.
  - Verify: `sh scripts/frontparity.sh` (expect `agreed: 292  diverged: 0
    (skipped, tychoc refused: 15)` — the numbers recorded at the end of the
    archived plan; **a change in `agreed` means the new directory leaked into the
    lane and the phase has failed**), then `sh compiler/fixpoint.sh`, then
    `make test`, then `make ci`. One command each, foreground.

- [ ] **Phase 2 — backtick raw string literals in `tychoc`**
  - Scope: the string lexer in `src/tychoc.c` (the literal scanner at `:319-400`,
    the escape table at `:373-382`, the control-byte rejection at `:389-391`, and
    the adjacent-join at `:2150-2166`); `docs/spec/01-lexical.md` §3.9.4 and the
    grammar in `docs/spec/appendix-a-grammar.md`; a raw-string fixture and golden
    in `tests/postfreeze/`; a reject fixture in `tests/reject/` for the
    unterminated case. **Not** touched: `tools/tychofmt.ty`, `tools/lsp.ty`,
    `editors/` — those are phase 3.
  - Read `src/tychoc.c:2150-2166` and settle the join question named in
    Pre-flight **before** editing the lexer. State in the commit which rule was
    chosen and which lines decided it.
  - Semantics to implement: `` `...` `` is a `string` value; no escape is
    interpreted (a backslash is a backslash); an embedded newline is a literal
    `\n` byte, so the literal is genuinely multi-line; there is no escape for a
    backtick, so a raw literal cannot contain one; unterminated at EOF is a
    diagnostic. Control bytes below `0x20` other than tab and newline stay
    rejected unless reading the scanner shows that rule lives elsewhere.
  - Done when: a `tests/postfreeze/rawstring.ty` fixture covering — a
    single-line raw literal, one containing `\n` as two literal characters, one
    spanning three source lines, and one adjacent to a normal literal (either
    joining or erroring, per the rule chosen) — matches its golden; the
    unterminated case is in `tests/reject/` with the diagnostic asserted; the
    spec sections state the rule with a `> Provenance:` line citing the real
    lines, in the style of `01-lexical.md:297-300`.
  - Verify: `make test`, then `make ci` (which covers the fuzzers, the ILP32
    rebuild and the ASan-self lane over the new scanner path). Foreground, one
    command each.

- [ ] **Phase 3 — raw strings in `tychofmt`, the LSP, and the editor grammars**
  - Scope: `tools/tychofmt.ty`, `tools/lsp.ty`, `editors/vscode/`,
    `editors/zed/`. Each has its own lexer or grammar and each will mis-tokenise
    a backtick literal until taught it.
  - Note the constraint that makes this phase real: `tools/*.ty` is inside
    `scripts/frontparity.sh:152-153`'s glob, so `tychofmt` and `lsp` are compiled
    by frozen `tychoc0` and **their own source may not use a raw string** even
    after phase 2 ships one. Handling the token is fine; writing one is not.
  - Done when: `tychofmt` round-trips `tests/postfreeze/rawstring.ty` unchanged
    (formatting a raw literal must not touch its bytes); the LSP tokenises a
    backtick literal as a string rather than desynchronising the rest of the
    file; both editor grammars highlight it.
  - Verify: `sh scripts/tools_check.sh`, then `sh scripts/frontparity.sh` (the
    `agreed` count must not fall — a fall means `tools/` stopped compiling under
    `tychoc0`), then `make ci`.

## Out of scope

- **`new_ideas.md` item 1 — `${name}` interpolation.** Dropped by the user's
  decision, 2026-07-29, on the finding that f-strings already provide it
  (`docs/spec/01-lexical.md:302-317`). Worth deleting from `new_ideas.md` when
  someone next touches that file, with a pointer to the f-string section.
- **`new_ideas.md` item 3 (element-wise array arithmetic) and item 4 (Odin-style
  `for i := 0; i < n; i += 1:`).** Both are real, neither is planned here. Item 4
  is a *replacement* of the shipped `for C:` / `for i in range(a,b,step):` /
  `for x in xs:` forms (`docs/spec/10-statements.md:87`, `:90`, `:98`) and so is a
  breaking change across the whole tree — it wants its own plan, and it wants
  phase 1 of this one to have landed first.
- **The two concurrency items in `FRICTION.md`** — no storable task handles, and
  no way to hand a connection to whichever worker is free. The archived plan's
  phase 10 concluded these "want a type-system answer". Nothing here touches
  them.
- **Unfreezing `compiler/tychoc0.ty`.** Not proposed. `ROADMAP.md:26-32` records
  the freeze as finished work, not deprecated work; this plan routes around it
  rather than reopening it.
