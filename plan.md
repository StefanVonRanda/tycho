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

- [x] **Phase 1 — `tests/postfreeze/`: a fixture lane the frozen compiler never sees**
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
  - **DONE 2026-07-29.** Shipped: `tests/postfreeze/nested_pattern.ty` + its
    golden `.out`; a new loop at `tests/run.sh:135-153`; the deliberate-exclusion
    notes in `scripts/frontparity.sh`'s COVERAGE block and `compiler/fixpoint.sh`'s
    header; the §14.3.1 row and E.2.1 note in
    `docs/spec/appendix-e-conformance.md`. **No glob was edited** — verified below
    by the unchanged `agreed` count.

    **The canary shape: nested patterns, as the plan predicted.** The fixture
    matches a `Result(int, Why)` with `Err(NotFound)` and `Err(TooBig(n))` arms
    (`docs/spec/10-statements.md:47-55`). Worth recording precisely, because half
    of it does NOT diverge: a `tychoc0` built at HEAD *accepts* the payload-free
    `Err(NotFound)`, reading the name as a payload BINDING — the exact misreading
    `10-statements.md:61-64` forbids. Only the payload-carrying nested pattern
    `Err(TooBig(n))` is unspellable in its grammar, so the canary carries one.

    (b) **`tychoc0` at HEAD refuses the canary** — built the way
    `scripts/frontparity.sh:122` does:
    ```
    $ ./tychoc compiler/tychoc0.ty -o /tmp/h0
    built /tmp/h0
    $ /tmp/h0 tests/postfreeze/nested_pattern.ty --emit-c >/dev/null; echo "H0_RC=$?"
    parse: line 34: unexpected token
    H0_RC=1
    ```
    Line 34 is the `Err(TooBig(n))` arm.

    (a) **`tychoc` accepts it and its output matches the golden**, through the
    new `tests/run.sh` loop, under full native-vs-ASan + golden discipline:
    ```
    $ make test
    ok    postfreeze_nested_pattern
    passed: 527   failed: 0
    all green
    ```

    (c) **Both frozen-compiler lanes green, counts identical to baseline.** The
    baseline was recorded on the unmodified tree BEFORE any edit, and re-run after:
    ```
    BEFORE: frontparity: agreed: 292   diverged: 0   (skipped, tychoc refused: 15)
    AFTER:  frontparity: agreed: 292   diverged: 0   (skipped, tychoc refused: 15)
            frontparity: all green (tychoc0's frontend accepts every program tychoc accepts)
    ```
    `agreed` is unchanged, which is the phase's real assertion: the new directory
    did not leak into the lane and no existing program left it. And:
    ```
    $ sh compiler/fixpoint.sh
    ok   B == C : tychoc0 reproduces itself byte-identically (35691 lines C)
    ok   split tychoc0 (2 packages) self-hosts E==F and matches the single-file compiler
    fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    ```

    (d) **`make ci` green** (exit code 0):
    ```
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 8 runnable example(s), all pass
    link check: ok (130 markdown files, no dead relative links)
    citation check: ok (22 anchored contain the token they name, 1699 bare in bounds, 79 source->doc citations resolve)
    ================================================================
     CI GREEN -- tree is good
    ================================================================
    ```

    **One file outside the phase's named scope, and why it had to be.**
    `.gitignore:89` is a broad `*.out`, un-ignored per directory (`!/tests/*.out`,
    `!/tests/pkg/*.out`, …). `git check-ignore -v` on the new golden returned
    `.gitignore:89:*.out`, i.e. the golden would NOT have been committed and a
    fresh clone would report `no golden — run 'make test-update'`. Added
    `!/tests/postfreeze/*.out` beside the existing exceptions — four lines, no
    behaviour change, and without it the phase's own done-when (a) is false for
    everyone but this working tree.

    **Environment note, so the next reader does not lose an hour to it.** On this
    machine every ASan binary aborted with `ASan runtime does not come first in
    initial library list`, reddening 251 of 527 fixtures — *at HEAD, before any
    change*, confirmed by `git stash`. Cause is not the tree: the session exports
    `LD_PRELOAD=/home/igzo/phonic/tools/block-nnp.so`, which loads ahead of
    libasan. Every gate above was run as `env -u LD_PRELOAD make …`. Nothing in
    the repo was changed for this; it is a shell-environment artifact.

    **Citation drift this phase caused, and repaired.** Adding a 13-line header
    comment to `compiler/fixpoint.sh` moved its `tests/*.ty` loop from `:24` to
    `:37`, invalidating four `compiler/fixpoint.sh:24` citations in
    `docs/spec/appendix-e-conformance.md` — on its lines 234, 252, 274 and 286.
    All four repaired to `:37`; `make check-links` re-run green afterwards. The
    citation gate would NOT have caught this — it only checks bare `:N` refs are
    in bounds, and `:24` still is. See the new out-of-scope phase below for the
    same drift in `FRICTION.md`, which this phase did not touch.

    **Ordering caveat, stated rather than glossed:** the `CI GREEN` run above
    completed *before* that four-line citation repair and before this evidence
    block was written. The two gates that actually read the changed files were
    re-run afterwards and are green — `spec-check: all Appendix E fixture
    citations resolve (ok)` and `citation check: ok (22 anchored …, 1711 bare in
    bounds, 79 source->doc …)`. No compiled artifact changed between the two
    points (the diff is prose line numbers in one Markdown file plus `plan.md`),
    so the rest of `make ci` cannot be affected. Not re-run end to end: the full
    ~15-minute gate.

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
  - Verify: `make test`, then `sh scripts/frontparity.sh` (the lexer is shared
    with nothing else, but a scanner change that reddens `tools/*.ty` under
    `tychoc0` must surface here). **Not `make ci`** — see "Gate ladder" below.
    Foreground, one command each.

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
    `tychoc0`), then — as the **last** phase in the chain — the full
    `make ci` with fuzz on. This is the one place the whole suite runs.

## Gate ladder

Phases 2 and 3 do **not** run `make ci`. `scripts/ci.sh` is thirteen steps —
`make test` with ASan/UBSan/Leak, an ILP32 rebuild of the fixture suite
(`:44-45`), an ASan+UBSan build of the compiler over the whole corpus
(`:59-60`), the corelib and example dogfoods (`:62-67`), `entrypoints`, `conc`
with TSan, `ffi`, and two fuzz sweeps at 200 seeds each (`:86-88`) — and a phase
that edits a lexer or a formatter cannot break most of it. Each phase runs only
the gate that phase can redden; the full sweep runs once, at the end of the
chain, where it is the thing that catches what the targeted gates could not.

`scripts/ci.sh:15` documents `FUZZ_N=0` to skip the fuzz sweeps and
`scripts/ci.sh:16` documents `make ci N=500` to deepen them; if an intermediate
full run is ever wanted, `make ci N=0` is the cheap form.

- [ ] **Phase 4 (found by phase 1, not absorbed) — stale `:N` citations to the
      two frozen-compiler scripts**
  - Phase 1 repaired the four `compiler/fixpoint.sh:24` refs inside its own scope
    (`docs/spec/appendix-e-conformance.md`) and stopped there. Two classes remain,
    both **pre-existing or newly drifted, neither breaking a gate**:
    - `FRICTION.md:215` and `:334` still say `compiler/fixpoint.sh:24`; the loop is
      at `:37` since phase 1.
    - `scripts/frontparity.sh:127` is cited in `FRICTION.md:215`, `:219`, `:244`,
      `docs/spec/appendix-e-conformance.md:274` and `:285`, and has been stale
      **since before this plan** — the glob was already at `:152` at HEAD and phase
      1's header note moved it to `:164`. Also `scripts/frontparity.sh:6-11`'s own
      header cites `tests/run.sh` at its lines 148, 159, 178, 199, 262 and
      291-314, none of which resolve to what the sentences describe (the last is
      past the end of the file outright).
  - Why it is not urgent: `scripts/check_citations.py` validates bare `:N` refs
    only for being **in bounds**, so all of these pass. That is exactly why they
    rotted. Worth one sweep; worth considering whether the gate should anchor
    script citations the way it anchors the 22 named ones.
  - Done when: every `fixpoint.sh:N` / `frontparity.sh:N` ref in `FRICTION.md` and
    `docs/` points at the line the surrounding sentence describes, spot-checked by
    reading the cited line; `make check-links` green.

- [ ] **Phase 5 (found by phase 1, not absorbed) — should `scripts/asan_self.sh`
      see `tests/postfreeze/`?**
  - `scripts/asan_self.sh:135` globs `examples/*.ty tests/*.ty tests/conc/*.ty
    tests/reject/*.ty …`, which does not descend, so `tests/postfreeze/` is
    outside it. Unlike `fixpoint.sh` and `frontparity.sh`, **this exclusion is
    accidental, not principled**: that lane runs `tychoc` (built under ASan)
    over the corpus, and `tychoc` is precisely the compiler that *accepts*
    post-freeze syntax. So the new scanner paths phase 2 will add are the ones
    most wanting an ASan-self run and are the ones not getting it.
  - Deliberately NOT done in phase 1: the phase's scope named three files and
    this is a fourth, and the question ("is this lane's glob a freeze boundary or
    an oversight?") deserves reading `scripts/asan_self.sh:63-70`'s own COVERAGE
    note rather than a silent glob edit.
  - Done when: either `tests/postfreeze/*.ty` is in that glob and
    `sh scripts/asan_self.sh` is green, or the header states why it is out.
  - Sequencing: worth doing **after** phase 2, so it has real new-lexer code to
    cover rather than one nested-pattern canary.

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
