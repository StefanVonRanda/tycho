# Post-freeze fixtures, then raw strings

The previous plan is complete and archived at
[plan-friction-DONE.md](plan-friction-DONE.md)
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
  - `compiler/fixpoint.sh` — `for f in tests/*.ty examples/*.ty` builds each
    with `./tychoc` (`:26`, skipping what tychoc refuses via `|| continue`) and
    then requires `B`, a `tychoc0`-derived binary, to emit C that compiles and
    matches (`:28-29`). New syntax → tychoc accepts → `B` refuses → `FAIL <nm>`.
    The second loop at `compiler/fixpoint.sh` walks the same glob for the split-compiler
    dogfood.
  - `scripts/frontparity.sh` — the same glob plus `tests/conc/*.ty`,
    `tests/warn/*.ty`, `tests/abort/*.ty`, `tests/diag/*.ty`, `tools/*.ty` and
    `compiler/tychoc0.ty`, each through `check_one` (`scripts/frontparity.sh`), which skips
    only when **tychoc** refuses (`scripts/frontparity.sh`) and fails when tychoc accepts and
    `tychoc0` refuses (`scripts/frontparity.sh`).
- **Verified — the escape hatch that already exists is location-based, not
  per-fixture.** `scripts/frontparity.sh` names the two directories held
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
    **Not** touched: the globs in `compiler/fixpoint.sh`, `compiler/fixpoint.sh` and
    `scripts/frontparity.sh` — `tests/postfreeze/*.ty` is excluded by
    those globs already, and that is the point; the phase proves it rather than
    editing them.
  - The canary must use syntax `tychoc` accepts and frozen `tychoc0` refuses, so
    that the lane is proven rather than asserted. **Nested patterns are the
    documented candidate** — `scripts/frontparity.sh` records
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
    `scripts/frontparity.sh` does:
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
    `:37`, invalidating four `compiler/fixpoint.sh` citations in
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

- [x] **Phase 2 — backtick raw string literals in `tychoc`**
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
  - **DONE 2026-07-29.** Shipped: the raw-literal scanner at
    `src/tychoc.c:402-448`; §3.9.4 of `docs/spec/01-lexical.md` (grammar, prose,
    Provenance); the `RawPiece` / `RawElem` productions plus a note and
    Provenance line in `docs/spec/appendix-a-grammar.md`;
    `tests/postfreeze/rawstring.ty` + its golden; and
    `tests/reject/rawstring_unterminated.ty`.

    **Answers to the three questions the phase was told to settle.**

    1. **Does a backtick mean anything to the lexer today?** No. Grepping
       `src/tychoc.c` for a backtick at HEAD returned only comment prose — no
       token kind, no case, no table entry. It fell through the operator chain to the
       catch-all `die_at(line, "unexpected character '%c'", c)` (pre-edit `:463`,
       now `:511`). Confirmed from the other side: a `tychoc0` built at HEAD
       still does exactly that —
       ```
       $ /tmp/h0 tests/postfreeze/rawstring.ty --emit-c >/dev/null; echo "H0_RC=$?"
       lex: unexpected character
         18 |     a := `hello raw`
                       ^
       H0_RC=1
       ```
       which is why the fixture belongs in `tests/postfreeze/` and not `tests/`.

    2. **The join decision: a raw literal MAY join.** The lines that decided it
       are `src/tychoc.c:2234-2246` (the comment's soundness argument is stated
       as a property of the *stored escaped text* — "every Tycho escape is
       exactly two characters" — not of the syntax that produced the token) and
       `:2246` itself, `while (at(ps, TK_STR) && !cur(ps)->ival) { sv =
       sfmt("%s%s", sv, cur(ps)->text); ... }`, which is a plain text
       concatenation gated only on `ival`, the f-string flag. So the join is
       safe for any token whose text meets that two-character-escape invariant.
       The scanner therefore **re-escapes** a raw piece's bytes as it reads them
       (`\n` `\t` `\\` `\"`, at `:430-433`) and emits an ordinary `TK_STR` with
       `ival = 0`. Nothing downstream can tell which spelling produced it, so
       the parser needed **no change at all** and both `` `raw ` "and normal" ``
       and `` "normal and " `raw` `` are one literal. The conservative
       alternative the Pre-flight offered (never join, `` `a` "b" `` a syntax
       error) was rejected because it is the *larger* change — it needs a new
       per-token rawness flag and an error path — and because the code's own
       stated condition is met.

       Re-escaping is also load-bearing for a second reason, independent of the
       join: codegen `:9003` pastes `sval` verbatim into a C string literal
       (`tycho_str_intern("%s")`), so a raw newline or `"` must never reach it.

    3. **Does the per-piece length bound apply to raw pieces?** Yes, and
       deliberately the same bound. The quoted scanner's `char buf[4096]` /
       `bn + 2 >= (int)sizeof buf` pair (`:326`, `:332`) is mirrored by
       `char rbuf[4096]` at `:425` and the two `string too long` checks at
       `:437` and `:440` — two-byte check on the re-escape path, one-byte on
       the verbatim path. A raw piece that overflows reports `string too long`
       at its **opening** line, as does the unterminated case (`:444`).

    **Control bytes stay rejected, in the raw scanner itself.** `:434-435`
    repeats the quoted branch's rule with newline now excepted alongside tab —
    a raw CR, NUL or other byte below `0x20` is still
    `raw control byte in string literal`. The rule did not live elsewhere.

    **Verify — gate 1, `env -u LD_PRELOAD make test`:**
    ```
    ok    postfreeze_rawstring
    ok    reject_rawstring_unterminated
    -----------------------------------------
    passed: 529   failed: 0
    all green
    ```
    (527 before this phase, +2 for the two new fixtures.) The reject fixture's
    asserted diagnostic, captured directly:
    ```
    $ ./tychoc tests/reject/rawstring_unterminated.ty --emit-c -o /tmp/rj; echo "RC=$?"
    tests/reject/rawstring_unterminated.ty:13: error: unterminated raw string literal
        13 |     s := `this literal never closes
    RC=1
    ```
    Line 13 is the **opening** line, which is the point of `startline`.

    **Verify — gate 2, `env -u LD_PRELOAD sh scripts/frontparity.sh`:**
    ```
    frontparity: agreed: 292   diverged: 0   (skipped, tychoc refused: 15)
    frontparity: all green (tychoc0's frontend accepts every program tychoc accepts)
    ```
    `agreed` is 292, unchanged from phase 1's baseline: the new scanner path did
    not redden a single `tools/*.ty` under the frozen `tychoc0`.

    **A gate the phase was not told to run, run anyway, because the edit broke
    it.** Inserting 48 lines at `src/tychoc.c:402` shifted every line below it,
    and `scripts/check_citations.py` validates 22 **anchored** `path:N@token`
    citations for actually containing the token they name. Fifteen of them went
    stale immediately:
    ```
    citation check: FAILED (15 stale citation(s) above)
    ```
    All fifteen were a pure `+48` shift and were repaired to the lines the
    checker itself reported, across four files —
    `docs/spec/15-program.md`, `docs/spec/03-types.md`,
    `docs/internals/frontend-restriction-audit-2026-07-25.md` and
    `docs/internals/plan-front-door-DONE.md`. Those last two are **outside this
    phase's named scope**; they were touched because leaving them stale reddens
    `make ci`, which phase 3 must run, and the repair is a line number in a
    citation and nothing else. After:
    ```
    link check: ok (130 markdown files, no dead relative links)
    citation check: ok (22 anchored contain the token they name, 1720 bare in bounds, 82 source->doc citations resolve)
    ```
    Also re-run, because Appendix A is a **mechanically generated projection**
    of the §3/§4 grammar blocks (`gen_grammar.sh`, marker at
    `appendix-a-grammar.md:32`): the first draft put prose inside the generated
    region and `make spec-check` correctly refused it. The prose and Provenance
    now sit after `<!-- END GENERATED -->` and the productions match §3.9.4 byte
    for byte:
    ```
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 8 runnable example(s), all pass
    ```

    **Golden is tracked, not ignored** (phase 1's `.gitignore` finding):
    ```
    $ git check-ignore -v tests/postfreeze/rawstring.out
    .gitignore:100:!/tests/postfreeze/*.out	tests/postfreeze/rawstring.out
    $ git add -n tests/postfreeze/rawstring.out
    add 'tests/postfreeze/rawstring.out'
    ```

    **Not run:** `make ci` — per the Gate ladder, it runs once at the end of
    phase 3. The three gates above are the ones this edit can redden, plus the
    two documentation gates it did redden and now does not.

- [x] **Phase 3 — raw strings in `tychofmt`, the LSP, and the editor grammars**
  - Scope: `tools/tychofmt.ty`, `tools/lsp.ty`, `editors/vscode/`,
    `editors/zed/`. Each has its own lexer or grammar and each will mis-tokenise
    a backtick literal until taught it.
  - Note the constraint that makes this phase real: `tools/*.ty` is inside
    `scripts/frontparity.sh`'s glob, so `tychofmt` and `lsp` are compiled
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
  - **DONE 2026-07-29.** Shipped: the raw-literal branch in `tychofmt`'s lexer
    (`tools/tychofmt.ty:162-184`) plus the `ends_nl` guard in `fmt`
    (`:245-252`, `:417-420`); the `inraw` cross-line state in **both** of the
    LSP's own scanners — `find_occurrences` (`tools/lsp.ty:783-805`) and
    `handle_semantic_tokens` (`:1181-1208`); the `string.quoted.other.raw.tycho`
    begin/end rule in `editors/vscode/syntaxes/tycho.tmLanguage.json:27-32`; and
    a second alternative inside the tree-sitter `string` rule in
    `editors/zed/grammars/tycho/grammar.js`, with `src/parser.c`,
    `src/grammar.json` and `src/node-types.json` regenerated.

    **No `tools/*.ty` source uses a raw string** — the constraint that made this
    phase real. Both lexers detect the token by byte value (`c == 96`), and every
    backtick that appears in `tools/` is inside a `#` comment, which both the
    frozen `tychoc0` and `tychofmt` consume before the raw branch is ever
    reached. Proven by gate 2 below: `agreed` is still 292.

    **Design decision: a raw literal is a `KStr` / `ty = 4`, not a new kind.**
    `tychofmt` reuses `KStr` so `prev_is_value` (`:71-83`), `is_operand`
    (`:295-300`) and `sep_needs_space` (`:306-325`) treat it as the string
    operand it is, with no edits to any of them. The zed grammar likewise puts
    both spellings in the one `string` node, so `languages/tycho/highlights.scm`'s
    `(string) @string` covers it with **no query change**.

    **The one thing a line-based scanner gets wrong, and how each fixed it.** A
    raw literal may span source lines, so every scanner that iterates
    `split(text, "\n")` needs state carried across the loop. `tychofmt` does not
    (its token stream is one flat pass over the source, and stage 2 splits only
    on `KNL` tokens, so the literal's interior newlines never reach the indent
    stack). The LSP's two scanners do, hence `inraw`.

    **(a) `tychofmt` round-trips the fixture — proven by `cmp`, not by eye.**
    ```
    $ ./tychofmt tests/postfreeze/rawstring.ty > /tmp/rs.fmt.ty
    $ cmp tests/postfreeze/rawstring.ty /tmp/rs.fmt.ty && echo IDENTICAL
    IDENTICAL
    ```
    (`cmp` is silent on success; `diff -u` printed nothing.) The change is
    load-bearing — the **pre-phase-3** `tychofmt`, rebuilt from `git show HEAD`,
    corrupted the literal's bytes on 32 diff lines by re-spacing its interior:
    ```
    -    a := `hello raw`          +    a := ` hello raw `
    -    b := `a\nb`               +    b := ` a \ nb `
    -    c := `one                 +    c := ` one
     two                            two
    -three`                        +three `
    ```
    `` `a\nb` `` becoming `` ` a \ nb ` `` is a semantic change, not cosmetics.

    **The one bug this phase found in phase 2's own fixtures.** With the raw
    branch added, `tests/reject/rawstring_unterminated.ty` failed
    `scripts/tools_check.sh:29`'s idempotence bar:
    ```
      NOT IDEMPOTENT: ./tests/reject/rawstring_unterminated.ty
        813 files checked  (compilable=381)  idempotence-fails=1  semantic-fails=0
    ```
    Cause, read rather than guessed: an **unterminated** raw literal runs to EOF,
    so its token text swallows the file's final newline; `fmt`'s code branch then
    appended `"\n"` unconditionally, and the file grew by one newline per pass.
    Fixed by `ends_nl` — append the newline only when the line's text does not
    already end in one. This is a no-op for every other line in the tree (no
    other token can contain a newline), which is why `idempotence-fails` and
    `semantic-fails` are both 0 across all 813 files afterwards. The reject
    fixture now formats to itself byte for byte:
    ```
    $ ./tychofmt tests/reject/rawstring_unterminated.ty > /tmp/u1.ty
    $ ./tychofmt /tmp/u1.ty > /tmp/u2.ty && cmp /tmp/u1.ty /tmp/u2.ty && echo IDEMPOTENT ok
    IDEMPOTENT ok
    $ cmp tests/reject/rawstring_unterminated.ty /tmp/u1.ty && echo "AND byte-identical to source"
    AND byte-identical to source
    ```

    **(b) How the LSP was checked: a scripted JSON-RPC session, not inspection.**
    A three-line raw literal whose interior repeats the identifier being renamed,
    driven through `semanticTokens/full` + `textDocument/rename`
    (buffer: `fn main():` / `    q := \`one q` / `two q` / `three\`` /
    `    println(q)`). Decoded from the LSP delta encoding to absolute
    `(line, char, len, type)`; legend index 4 is `string`, 2 `function`,
    3 `variable`:
    ```
      semantic tokens (line, char, len, type):
        (0, 0, 2, 0)     fn        -> keyword
        (0, 3, 4, 2)     main      -> function
        (1, 4, 1, 3)     q         -> variable
        (1, 9, 6, 4)     `one q    -> STRING (opens at col 9, runs to EOL)
        (2, 0, 5, 4)     two q     -> STRING (whole line)
        (3, 0, 6, 4)     three`    -> STRING (through the closing backtick)
        (4, 4, 7, 2)     println   -> function      <- resynchronised
        (4, 12, 1, 3)    q         -> variable      <- resynchronised
      raw-open=True raw-mid=True raw-close=True  resync(line4 code)=True
      rename edits=[(1, 4), (4, 12)]
      LSP_RAW_RC=0
    ```
    Line 4 classifying as code is the "does not desynchronise the rest of the
    file" assertion. The rename returning **exactly two** edits — the declaration
    and the use, and neither of the two `q`s **inside** the literal — is the
    `find_occurrences` half. Without `inraw` the pre-change code took the
    catch-all `else: c = c + 1` / `i = i + 1` at the backtick, so lines 2-3 were
    scanned as ordinary code: the literal's text would have been classified as
    variables and a rename would have **edited bytes inside a string literal**.

    **(c) Both editor grammars.** vscode: a `begin`/`end` pair on `` ` `` with no
    nested patterns (no escapes to model) and no end-of-line anchor, so it spans
    lines. zed: verified by running the regenerated parser over the corpus —
    ```
    $ npx tree-sitter-cli@0.25 generate --abi 15        # per editors/zed/README.md
    $ npx tree-sitter-cli@0.25 parse tests/postfreeze/rawstring.ty | grep -c 'ERROR\|MISSING'
    0
    $ ... | grep 'string \['
      (string [30, 9] - [32, 6])      <- the three-line raw literal, ONE node
      (string [39, 9] - [39, 15])     <- `raw `
      (string [39, 16] - [39, 28])    <-        "and normal"   (the join pair)
    $ npx tree-sitter-cli@0.25 parse -q <all 813 .ty files> | grep -c 'ERROR\|MISSING'
    1
    ```
    That single remaining error is `tests/reject/rawstring_unterminated.ty`,
    which is a reject fixture and must not parse. `editors/zed/README.md`'s
    stale "parses all 462 committed `.ty` files" claim was refreshed to this
    measurement (it predates several hundred files and was already wrong).

    **Verify — gate 1, `env -u LD_PRELOAD sh scripts/tools_check.sh`:**
    ```
    >>> formatter: idempotence + semantic preservation
        813 files checked  (compilable=381)  idempotence-fails=0  semantic-fails=0
    >>> lsp: scripted JSON-RPC smoke
        init=True  diag(valid->[]=True invalid->diag=True loop-warn=True)  hover(local=True fn=True)  def=True
        docsym=True  completion=True  references=True  rename=True  inlay=True  fstr-rename=True  sighelp=True  wsym=True  semtok=True
    tools-check: ok
    ```
    `semantic-fails=0` over 381 compilable files is the strong half: `tychoc
    --emit-c` is byte-identical before vs after formatting.

    **Verify — gate 2, `env -u LD_PRELOAD sh scripts/frontparity.sh`:**
    ```
    frontparity: agreed: 292   diverged: 0   (skipped, tychoc refused: 15)
    frontparity: all green (tychoc0's frontend accepts every program tychoc accepts)
    ```
    292, the phase 1 and phase 2 baseline: `tools/tychofmt.ty` and `tools/lsp.ty`
    still compile under the **frozen** `tychoc0` after learning the token.

    **Verify — gate 3, `env -u LD_PRELOAD make ci`, all thirteen steps, exit 0.**
    Ran ~19 minutes, past the 10-minute per-command cap, so it was run detached
    to a log and waited on in-turn:
    ```
    [2/13]  make test          passed: 529   failed: 0   ->  all green
    [2b/13] make ilp32         -m32 toolchain OK (32-bit long, 64-bit int64_t verified)
    [2c/13] make asan-self     compiled: 542   failed: 0   (ASan+UBSan over the whole corpus)
    [3/13]  make corelib       corelib: all green (tychoc matches goldens) / corelib examples: all green
    [3b/13] make entrypoints   ok (11 entry points compile with tychoc)
    [4/13]  make conc          passed 37   failed 0
    [5/13]  make ffi           green
    [6/13]  make fuzz N=200        DONE: ok=177 skip=23 timeout=0 FAIL=0
    [7/13]  make fuzz-reject N=200 DONE: accepted=31 rejected=169 FAIL=0
    [8/13]  make fuzz-leak N=150   DONE: ok=131 skip=19 FAIL=0
    [9/13]  make tools-check   tools-check: ok
    [10/13] bench-guard        ok
    [11/13] make recursion     recursion-cap: all green
    [12/13] make spec-check    Appendix A grammar matches §3/§4 (ok); Appendix E citations resolve (ok);
                               spec-examples: 8 runnable example(s), all pass
    [13/13] make check-links   link check: ok (130 markdown files)
                               citation check: ok (22 anchored, 1732 bare in bounds, 82 source->doc)
    ================================================================
     CI GREEN -- tree is good
    ================================================================
    CI_RC=0
    ```
    Phase 2's new scanner path is covered here by `[2c] asan-self` (the ASan+UBSan
    compiler over 542 programs) and by the three fuzz sweeps, none of which
    regressed.

    **Ordering caveat, same discipline as phase 1.** One file changed *after* the
    `CI GREEN` run: the `editors/zed/README.md` count above. It is prose in a
    Markdown file that no compiled artifact reads, and the one gate that reads it
    was re-run afterwards and is green — `link check: ok (130 markdown files)` /
    `citation check: ok (22 anchored …, 1732 bare in bounds, 82 source->doc …)`.
    Not re-run end to end: the full ~19-minute gate.

    **Deliberately NOT changed, so the next reader does not think it was missed.**
    `editors/vscode/language-configuration.json` lists `"` in `autoClosingPairs`
    and `surroundingPairs` but not `` ` ``. Typing a backtick therefore does not
    auto-close. That is an editing affordance, not highlighting, and this phase's
    Done-when is highlighting; it is recorded as phase 7 below rather than
    absorbed.

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

- [x] **Phase 4 (found by phase 1, not absorbed) — stale `:N` citations to the
      two frozen-compiler scripts**
  - Phase 1 repaired the four `compiler/fixpoint.sh` refs inside its own scope
    (`docs/spec/appendix-e-conformance.md`) and stopped there. Two classes remain,
    both **pre-existing or newly drifted, neither breaking a gate**:
    - `FRICTION.md:215` and `:334` still say `compiler/fixpoint.sh`; the loop is
      at `:37` since phase 1.
    - `scripts/frontparity.sh` is cited in `FRICTION.md:215`, `:219`, `:244`,
      `docs/spec/appendix-e-conformance.md:275` and `:285`, and has been stale
      **since before this plan** — the glob was already at `:152` at HEAD and phase
      1's header note moved it to `:164`. Also `scripts/frontparity.sh`'s own
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
  - **DONE 2026-07-29.** Seventeen citations repaired across five files. The
    phase's own line map was itself stale in places, so every number below was
    re-derived by reading the cited file at HEAD, not by applying the plan's
    offsets. Two of the listed refs turned out to be **right already** and were
    left alone: `compiler/fixpoint.sh` (cited by `FRICTION.md:329` and
    `docs/bootstrap.md:5`) really is the `docs/bootstrap.md` header line, and
    `docs/spec/appendix-e-conformance.md:253-254`'s `:37`/`:81`/`:164` were
    already repaired by phase 1.

    **The repairs, each with the text of the line landed on.**

    | citation site | old | new | text at the new line |
    |---|---|---|---|
    | `FRICTION.md:215` | `fixpoint.sh:24` | `:37` | `for f in tests/*.ty examples/*.ty; do` |
    | `FRICTION.md:215` | `frontparity.sh:127` | `:164` | `for hi in examples/*.ty tests/*.ty tests/conc/*.ty tests/warn/*.ty \` |
    | `FRICTION.md:219` | `frontparity.sh:127` | `:157` | `echo "FAIL  $name  (tychoc ACCEPTED it, tychoc0 REFUSED it)"` |
    | `FRICTION.md:222` | `frontparity.sh:126-127` | annotated | see "the one that must not be repointed" below |
    | `FRICTION.md:244` | `frontparity.sh:127` | `:157` | (same `echo "FAIL …"` line) |
    | `FRICTION.md:334` | `fixpoint.sh:24` | `:37` | `for f in tests/*.ty examples/*.ty; do` |
    | `FRICTION.md:334` | `frontparity.sh:127` | `:164` | (same glob line) |
    | `appendix-e:235` | `frontparity.sh:127` | `:164` | (same glob line) |
    | `appendix-e:274` | `frontparity.sh:127` | `:157` | (same `echo "FAIL …"` line) |
    | `appendix-e:285` | `frontparity.sh:127` | `:157` | (same `echo "FAIL …"` line) |
    | `docs/bootstrap.md:50` | `fixpoint.sh:21-22` | `:34-35` | `if ! cmp -s "$T/cA.c" "$T/cB.c"; then …` / `echo "ok   B == C : …"` |
    | `docs/bootstrap.md:53` | `fixpoint.sh:24-30` | `:37-43` | the `tests/*.ty examples/*.ty` differential loop |
    | `docs/bootstrap.md:55` | `fixpoint.sh:31-53` | `:44-66` | `# Package programs (Stage D): …` through its `done` |
    | `frontparity.sh:15` | `fixpoint.sh:24-30` | `:37-43` | (same loop) |
    | `frontparity.sh:17` | `fixpoint.sh:26` | `:39` | `./tychoc "$f" -o "$T/ref" >/dev/null 2>&1 \|\| continue` |
    | `frontparity.sh:19` | `fixpoint.sh:28-29` | `:41-42` | `"$T/B" < "$f" > "$T/g.c" 2>/dev/null && $CC …` |
    | `frontparity.sh:23` | `fixpoint.sh:41-52` | `:44-66` | `# Package programs (Stage D): …` |
    | `frontparity.sh:24` | `tests/conc/run.sh:63-67` | `:37-61` | `for f in tests/conc/*.ty; do` … `done` |
    | `frontparity.sh:24` | `tests/run.sh:199` | `:203-220` | `for hi in tests/abort/*.ty; do` … `done` |
    | `frontparity.sh:38` | `fixpoint.sh:28` | `:41` | the `2>/dev/null` line that discards tychoc0's stderr |
    | `frontparity.sh:43` | `tests/run.sh` lines 291-314 † | `:260-283` | `for hi in tests/warn/*.ty; do` (6 fixtures, `ls` confirms) |
    | `frontparity.sh:54` | `tests/run.sh:70` | `:72` | `elif ! $CC -O2 -fwrapv -std=c11 -o "$nat" "$c" -lm …` — the separate cc step |
    | `frontparity.sh:71` | `fixpoint.sh:48` | `:61` | `if "$T/B" "$entry" > "$T/sd.c" …` — the standalone `tychoc0 <entry>` driver |
    | `tests/run.sh:139` | `fixpoint.sh:24`/`:68`, `frontparity.sh:152-153` | `:37`/`:81`, `:164-165` | the two globs and the frontparity glob |

    † Written with the path and the numbers separated on purpose. Quoting the old
    value in citation form made the gate red on **this table** — the only one of
    the 23 old values that was out of bounds rather than merely wrong:
    ```
    STALE  plan.md:649  `tests/run.sh:<291-314>` -> tests/run.sh has 295 lines: OUT OF BOUNDS
    citation check: FAILED (1 stale citation(s) above)
    ```
    (angle brackets added here too — the checker reads fenced blocks, so quoting
    its own verbatim message reproduced the failure a second time at `plan.md:658`.)
    An unplanned demonstration of the recommendation at the end of this phase: the
    bounds check caught the one citation that pointed past EOF and was silent about
    the other 22, every one of which named a real line describing something else.

    **The header block was not merely renumbered — one of its claims was false.**
    `scripts/frontparity.sh` said tychoc0 "is built at `tests/run.sh:148` and
    used only on the *reject* lane (`:159`, `:178`), the *abort* lane (`:199`) and
    the *diag* goldens (`:262`)". `grep -n tychoc0 tests/run.sh` returns **eight
    hits, every one of them inside a comment**: the freeze removed the reject leg
    (`tests/run.sh:163-166`, "that half is gone") and the abort leg
    (`:199-202`) on 2026-07-26, and the diag lane never had one (`:226`, "tychoc
    only"). `:148` is now the `tests/postfreeze/` loop phase 1 added — so the
    sentence named a line that exists and describes an unrelated lane, the worst
    kind of stale citation. The paragraph now states the removal instead of the
    use. The lane's *own* reason to exist is unchanged and still true.

    **Line count of `scripts/frontparity.sh` was held constant on purpose** (189
    before, 189 after). **Seven sites depend on that glob staying at `:164`** —
    its own header at `:76` ("The glob at :164 says `tests/*.ty`"),
    `tests/run.sh:139`, `FRICTION.md:215`, `:222`, `:334`, and
    `appendix-e:235` and `:253` — so an edit that grew the header by even one line
    would have invalidated the citations this phase exists to fix.
    Each replaced paragraph was rewritten to the same number of lines; verified by
    `wc -l` and by re-reading `:164-165`.

    **The one that must not be repointed, and why.** `FRICTION.md:222` cites
    `scripts/frontparity.sh` inside a `~~struck-through~~` entry describing
    the pre-2026-07-26 blind spot ("feeds `examples/*.ty` but never
    `examples/<dir>/main.ty`"). No current line says that — phase 8 of the archived
    plan fixed it. Repointing to `scripts/frontparity.sh` would make the sentence assert
    something false about today's script. Rewritten to name both: "`scripts/frontparity.sh` when
    this was written; `scripts/frontparity.sh` today, with the blind spot closed."

    **Archived plans deliberately left alone.** `docs/internals/plan-*-DONE.md`
    carries 14 more `fixpoint.sh:N` / `frontparity.sh:N` refs (`plan-friction-DONE.md`
    ×8, `plan-front-door-DONE.md` ×5, `plan-int64-DONE.md` ×3). Those are frozen
    verification *evidence* — line numbers recorded as they stood when the work was
    done. Renumbering them to today's tree would falsify the record rather than
    repair it, so they stay. This is a decision, not deferred work.

    **One file outside the phase's letter, declared.** `tests/run.sh:139` is a
    `fixpoint.sh:N` ref in neither `FRICTION.md` nor `docs/`, so the scope lock did
    not name it — but phase 1 of *this* plan wrote it, and it was stale on arrival
    for exactly the reason phase 4 exists. One line, three numbers, no behaviour.
    Repaired and recorded here rather than left as a known-false citation.

    **Verify — gate 1, `env -u LD_PRELOAD python3 scripts/check_citations.py`:**
    ```
    citation check: ok (22 anchored contain the token they name, 1773 bare in bounds, 82 source->doc citations resolve)
    CIT_RC=0
    ```
    **Verify — gate 2, `env -u LD_PRELOAD sh scripts/check_links.sh`:**
    ```
    link check: ok (130 markdown files, no dead relative links)
    LNK_RC=0
    ```
    Not run, deliberately: `make ci`. Per the Gate ladder, nothing here can reach a
    compiled artifact — the diff is comment prose in two shell scripts and line
    numbers in three Markdown files.

    **The open question the phase was asked to answer: should
    `scripts/check_citations.py` anchor script citations the way it anchors the 22
    named ones? Recommendation: yes, but not by anchoring all of them — make the
    anchored form available and require it only where a citation names a
    *mechanism*.** The reason is the failure mode this phase actually found. Bare
    `:N` is checked for bounds only, so the two ways a citation rots are invisible
    to it: the silent `+N` shift (which is noise), and the far worse case where the
    number still resolves but now names an unrelated line — `frontparity.sh:8`'s
    `tests/run.sh:148`, which drifted from a tychoc0 build onto phase 1's
    `tests/postfreeze/` loop, and (phase 6) §3.8's `src/tychoc.c:402`, which now
    names the raw-string scanner while claiming to name `::`. Both would have been
    caught by `:N@token`; neither was caught by bounds. The 22 anchored citations
    survived phase 2's `+48` shift *because* the gate checks them. Against blanket
    anchoring: 1740 bare refs is far too many to convert by hand, and most are
    range refs (`:37-43`) or narrative pointers where no single token is the
    subject. So the cheap, high-yield rule is: teach the checker `:N@token` for
    **any** path (it already parses the form), then require it for citations that
    name a specific construct — a loop, a glob, a table entry, a `Provenance:`
    line — and leave range and narrative refs bare. Phase 6 should settle the same
    question for `src/tychoc.c` and the two should land as one gate change, which
    is why this is a recommendation here and not an edit.

- [x] **Phase 5 (found by phase 1, not absorbed) — should `scripts/asan_self.sh`
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
  - **DONE 2026-07-29. Decision: the omission was an OVERSIGHT, and
    `tests/postfreeze/*.ty` is now in the glob.** Shipped: the directory added to
    the glob (`scripts/asan_self.sh:150-152`, was `:135-137`) and to the COVERAGE
    IN list (`:63`), plus a new titled block at `:75-90` stating why this lane
    includes what the two frozen-compiler lanes deliberately exclude.

    **What settled it, in the order it was read.**

    1. **The freeze boundary cannot apply here, because no frozen binary runs.**
       `compiler/fixpoint.sh` and `scripts/frontparity.sh` exclude
       `tests/postfreeze/` because they drive a **`tychoc0`-derived** binary that
       by construction cannot parse post-freeze syntax. This lane's subject is
       `$SAN`, built from `src/tychoc.c` with `-fsanitize` at
       `scripts/asan_self.sh:99-100` and invoked at `:113` — the **live**
       compiler. `compiler/tychoc0.ty` does appear in the glob, but as a
       *subject file being compiled*, never as the compiler. So the two lanes'
       shared reason for the exclusion has no counterpart here.
    2. **The COVERAGE note names every exclusion it means, and does not name this
       one.** The `NOT:` list (`:69-73`) gives four exclusions each with its
       reason — `corelib/` and `examples/corelib/` (per-module dependency skips),
       the fuzz corpora (generated, not committed), `-m32` (no 32-bit ASan
       runtime), and emitted-program runtime behaviour (`tests/run.sh` owns it).
       `tests/postfreeze/` is in neither list. Read together with the phase's own
       observation that the glob names each subdirectory explicitly, silence is
       not a boundary here: this file's convention is that a boundary is *stated*.
    3. **The dates make it conclusive rather than inferential.** The lane was
       written before the directory existed, so the COVERAGE note could not have
       excluded it:
       ```
       $ git log --diff-filter=A -1 --format='%ad %h' --date=short -- scripts/asan_self.sh
       2026-07-25 1d620c5
       $ git log --diff-filter=A -1 --format='%ad %h' --date=short -- tests/postfreeze/
       2026-07-29 b895e66
       ```
       Four days apart, and `b895e66` is phase 1 of *this* plan.
    4. **The IN list's own stated invariant was broken by the omission.** It
       describes its corpus as "the same corpus `make test` and `make conc`
       score". `make test` has scored `tests/postfreeze/` since phase 1 added the
       loop at `tests/run.sh:148-152`. Adding the directory restores the note's
       own claim; leaving it out would have required weakening that sentence.

    **The plan's claim about raw-string coverage — checked, and true, with one
    qualification worth recording.** Searching every file inside the *old* glob
    for a backtick in code position (stripping `#` comments) returns hits in
    `compiler/tychoc0.ty` (≈30), `tools/lsp.ty:740` and many `tests/`/`examples/`
    files — but **every one of them is a backtick inside a double-quoted string
    literal** (diagnostic text like `` "a select arm is `recv(ch, x):`" ``), which
    the quoted-string scanner consumes without the raw branch at
    `src/tychoc.c:402-448` ever being entered. The single genuine raw literal in
    the old glob is `tests/reject/rawstring_unterminated.ty:13`, and it exercises
    only the **failure** path — it runs to EOF and dies at `src/tychoc.c:444`. So
    before this phase, the raw scanner's success paths — the terminating
    backtick, the re-escape loop at `src/tychoc.c:430-433`, the `rbuf[4096]`
    bound at `src/tychoc.c:425` and `src/tychoc.c:437-440`, the control-byte
    rejection at `src/tychoc.c:434-435`, and the multi-line and join cases —
    had **no ASan coverage at all**. `tests/postfreeze/rawstring.ty` is the only
    fixture in the tree that covers them, which is the strongest single argument
    for the decision.

    **Verify — gate 1, `env -u LD_PRELOAD sh scripts/asan_self.sh`:**
    ```
    asan-self: building build/tychoc-asan  (ASan+UBSan, -fno-sanitize-recover=all)
    -----------------------------------------
    asan-self: compiled: 544   failed: 0
    asan-self: all green (tychoc's own execution is ASan+UBSan clean over the corpus)
    ASAN_RC=0
    ```
    **544 is the assertion, not just the `failed: 0`.** Phase 3 recorded
    `[2c/13] make asan-self  compiled: 542` — so the delta is exactly +2, the two
    files in `tests/postfreeze/`. A different delta would have meant the glob
    edit caught something other than what it named. **No sanitizer report on
    phase 2's new buffer-handling code**: the raw scanner is clean under
    ASan+UBSan on its success paths, measured rather than assumed.

    **Verify — gate 2, `env -u LD_PRELOAD python3 scripts/check_citations.py`:**
    ```
    citation check: ok (22 anchored contain the token they name, 1793 bare in bounds, 82 source->doc citations resolve)
    CIT_RC=0
    ```
    Also `sh -n scripts/asan_self.sh` → `SYNTAX OK`.

    That gate was run **twice**, and the first run was red on *this evidence
    block* — worth recording, because it is phase 6's thesis reproduced by
    accident. The paragraph above originally wrote the raw scanner's lines as
    bare `` `:444` ``, `` `:430-433` `` and so on. The checker binds a bare `:N`
    to the **last path named in the prose**, which was
    `tests/reject/rawstring_unterminated.ty` (15 lines), so all six resolved
    against the wrong file:
    ```
    STALE  plan.md:812  `<:444>` -> tests/reject/rawstring_unterminated.ty has 15 lines: OUT OF BOUNDS
    citation check: FAILED (6 stale citation(s) above)
    ```
    (angle brackets added so quoting the message does not reproduce the failure,
    the same dodge phase 4 needed.) It was caught only because the wrong file was
    *short*; had the mis-bound target been long enough, all six would have passed
    the bounds check while naming unrelated lines. Repaired by spelling
    `src/tychoc.c:` on each.

    **The header's line count was NOT held constant, but `:63-73` was — and that
    was the constraint.** Phase 4's lesson applies: two live citations point at
    `scripts/asan_self.sh:69-70` (`scripts/frontparity.sh`, and
    `docs/internals/plan-front-door-DONE.md:5907`, archived). The `NOT:` list had
    to stay on exactly those lines. So the IN list was **re-wrapped to the same
    six lines** (`:63-68`) rather than grown, and the twelve-line rationale block
    was inserted **after** `:73` where it shifts nothing any citation names. Both
    numbers re-checked after the edit: `:69` is still `# NOT: corelib/ …` and
    `:70` still `#      skips this lane deliberately does not replicate);`.

    **One citation now stale by design, declared rather than repaired.** This
    phase's own problem statement above says `scripts/asan_self.sh:135` globs the
    corpus; the glob is at `:150` after the edit. That sentence is the **as-found**
    record of why the phase existed, in the style of `FRICTION.md:222`'s
    struck-through entry — repointing it to `:150` would make it describe a glob
    that no longer has the property it complains about. `docs/internals/
    plan-webserver-DONE.md:169`'s `asan_self.sh:137` is frozen archived evidence
    and stays, per phase 4's rule. Neither breaks the gate: both are bare `:N`
    refs and both are in bounds, which is precisely the rot phase 6 is about.

    **Not run, deliberately:** `make ci`. Per the Gate ladder the full sweep runs
    at the end of a chain; the diff here is one shell script's comment block and
    one glob line, and the single gate that glob can redden is gate 1 above, which
    ran the whole ASan corpus.

- [x] **Phase 6 (found by phase 2, not absorbed) — bare `src/tychoc.c:N`
      citations all shifted by +48, and §3.8's Provenance now points at the
      wrong feature outright**
  - Phase 2 inserted 48 lines at `src/tychoc.c:402`. Every **anchored**
    (`path:N@token`) citation below that line was repaired inside phase 2,
    because `scripts/check_citations.py` fails on those and `make ci` would have
    gone red. Every **bare** `:N` citation below line 402 is now off by 48 and
    the gate is silent about it, because it only checks bare refs for being *in
    bounds* — 1720 of them are. Same rot mechanism phase 4 records for the two
    shell scripts, now with a much larger blast radius.
  - **One of them is worse than merely stale and is the reason this is a phase
    rather than a footnote.** `docs/spec/01-lexical.md`'s §3.8 Provenance reads
    `src/tychoc.c:398-433` with "`::` is lexed at `:402`". That range was
    already wrong before phase 2 (the operator table was at `:429-465` at HEAD);
    after phase 2 `:402` is the **opening line of the raw-string scanner**, so
    the citation now confidently names an unrelated feature. Post-phase-2 the
    operator table is at `:477-513` and `::` at `:482`. §3.8 was outside phase
    2's named scope (§3.9.4 and the appendix grammar), so it was left alone
    rather than silently absorbed.
  - Worth deciding here, not just sweeping: whether `check_citations.py` should
    grow an anchored form for these, or whether a spec Provenance line should be
    required to be anchored (`:N@token`) so that a source edit can never
    silently rot it. The 22 anchored citations survived phase 2 *because* the
    gate checks them; the 1720 bare ones did not.
  - Done when: bare `src/tychoc.c:N` refs across `docs/` and `FRICTION.md`
    resolve to the line the surrounding sentence describes (spot-checked by
    reading the cited line, starting with §3.8), and `make check-links` green.
  - Sequencing: **after** phase 3, which will edit `tools/*.ty` and the editor
    grammars but not `src/tychoc.c`, so the +48 offset is stable from here.
  - **DONE 2026-07-29. 45 citations repaired across four files. The phase's
    headline premise is FALSE and the measurement is the phase's main product:
    the bare `src/tychoc.c:N` citations did NOT "all shift by +48". Exactly 23
    of 447 in-scope suspects did. The other ~400 are stale by 68 to 3520 lines
    and were already wrong before this plan started.** That population is filed
    as phase 9 below rather than swept here, for the reason given there.

    **The insert, confirmed before anything was touched.** `git diff
    --unified=0 b895e66~1 b895e66 -- src/tychoc.c` is a single hunk,
    `@@ -401,0 +402,48 @@`, and the file went 12154 → 12202 lines. So old
    `src/tychoc.c:1-401` is untouched and old `src/tychoc.c:402-12154` is today's
    `src/tychoc.c:450-12202`. `b895e66` is also the LAST commit to touch
    `src/tychoc.c`, so the offset is stable, exactly as the phase predicted.

    **Population, counted with `scripts/check_citations.py`'s own `CITE` regex
    and its own paragraph-scoped path inheritance** (so a continuation `` `:N` ``
    is attributed to the same file the gate would attribute it to).

    | population | count |
    |---|---|
    | citations resolving to `src/tychoc.c` in tracked Markdown | 921 |
    | — of those, anchored (`path:N@token`, gate-checked, repaired in phase 2) | 15 |
    | — of those, bare | 906 |
    | bare, ABOVE the insert (`N` < 402) — unaffected by construction | 64 |
    | bare, BELOW the insert (`N` >= 402) — the suspect set | 842 |
    | suspects in archived `docs/internals/plan-*-DONE.md` — deliberately left | 374 |
    | suspects in `plan.md` itself — outside the phase's named scope | 21 |
    | **suspects in scope (`FRICTION.md` + `docs/` minus the archived plans)** | **447** |

    The 374 archived refs break down as `docs/internals/plan-front-door-DONE.md`
    242, `docs/internals/plan-friction-DONE.md` 89,
    `docs/internals/plan-int64-DONE.md` 39, `docs/internals/plan-1.0-freeze-DONE.md`
    2, `docs/internals/plan-option-result-DONE.md` 1 and
    `docs/internals/plan-webserver-DONE.md` 1. **Left alone on phase 4's rule** —
    frozen verification evidence; renumbering it would falsify the record.

    Non-Markdown, the "any script or source comment" half: **65** citations,
    after excluding `compiler/tychoc0.ty` (79 refs, frozen, excluded by name the
    way phase 8 below requires), `scripts/check_citations.py`'s docstring (which
    shows citation *syntax*, not citations) and `src/tychoc.c`'s own quotation of
    a historical UBSan message. **Not one of the 65 is a +48 shift either** —
    deltas run 2 to 787 — so 64 of them go to phase 9 with the rest. The one
    repaired here is `corelib/net/net_shim.c:202`, because it names the same
    site as `FRICTION.md:220` and leaving the two disagreeing would be worse
    than either being stale.

    **How each new number was derived, and why it is not arithmetic.** For every
    citation: `git blame` the citing line → fetch `src/tychoc.c` at that commit →
    take the exact text of the cited range → search today's file for that text.
    That produces a *candidate*, which was then **confirmed by opening the
    current source and reading it against the sentence**. The confirmation step
    is not ceremony: the candidate generator was WRONG at least three times,
    because a citation that was already stale when its doc line was last edited
    yields "where the stale target went". `FRICTION.md:253`'s `` `:4663-4668` ``
    ("`Ok`/`Err` resolving to a *partial* is deliberate") generated
    `src/tychoc.c:4743`, which is `return e->type = task_of(s->ret);` — spawn,
    not `Result`. The real site is `src/tychoc.c:4804-4809`, found by grepping
    `T_OK_PARTIAL`. Same for `` `:5904-5911` `` and `FRICTION.md:220`'s
    `` `src/tychoc.c:8387` ``.

    **§3.8's Provenance — the one the phase called worse than stale.**
    `docs/spec/01-lexical.md:174` read `` `src/tychoc.c:398-433` `` with "`::` is
    lexed at `:402`". Today `src/tychoc.c:402` is `if (c == '`') {` — the opening
    line of phase 2's raw-string scanner, an unrelated feature. Re-derived by
    reading, and plan.md's predicted values are **confirmed**: the operator block
    runs `src/tychoc.c:477-513` (`477` is the comment `/* operators (two-char
    first) */`; `511` the `unexpected character '%c'` catch-all; `512-513` the
    bracket-depth bookkeeping that closes the block) and `::` is
    `src/tychoc.c:482`, `else if (c == ':' && c2 == ':') { k = TK_COLONCOLON;
    len = 2; }`. Both written.

    **The repairs, each with the text of the line landed on.** 45 edits; every
    path is spelled in full because a bare `` `:N` `` in a Markdown table binds
    to the previously-named path and would redden the gate on this very table —
    the trap that caught phase 4 and phase 5.

    | citation site | old | new | what is at the new line |
    |---|---|---|---|
    | `docs/spec/01-lexical.md:47` | `src/tychoc.c:437` | `src/tychoc.c:520` | `if (*p == '#') while (*p && *p != '\n') p++;` |
    | `docs/spec/01-lexical.md:83` | `src/tychoc.c:442-443` | `src/tychoc.c:525-526` | the `TK_DEDENT` flush loop + `TK_EOF` push |
    | `docs/spec/01-lexical.md:140` | `src/tychoc.c:3689-3698` | `src/tychoc.c:4208-4217` | the top-level `package`/`import`/`extern`/`const`/`subscript` contextual chain |
    | `docs/spec/01-lexical.md:141` | `src/tychoc.c:2611`/`src/tychoc.c:2627` | `src/tychoc.c:3060`/`src/tychoc.c:3076` | `!strcmp(t->text, "const")` / `!strcmp(t->text, "delete")` |
    | `docs/spec/01-lexical.md:141` | `src/tychoc.c:1582`/`src/tychoc.c:2026` | `src/tychoc.c:1855`/`src/tychoc.c:2343` | `!strcmp(t->text, "soa")` (type) / `!strcmp(t->text, "soa")` (literal) |
    | `docs/spec/01-lexical.md:141` | `src/tychoc.c:3028` | `src/tychoc.c:3523` | `!strcmp(cur(ps)->text, "where")` |
    | `docs/spec/01-lexical.md:142` | `src/tychoc.c:2994` | `src/tychoc.c:3489` | `!strcmp(cur(ps)->text, "sink")` |
    | `docs/spec/01-lexical.md:142` | `src/tychoc.c:2748` | `src/tychoc.c:3198` | `!strcmp(cur(ps)->text, "range")` |
    | `docs/spec/01-lexical.md:174` (§3.8) | `src/tychoc.c:398-433` | `src/tychoc.c:477-513` | the operator table (see above) |
    | `docs/spec/01-lexical.md:174` (§3.8) | `src/tychoc.c:402` | `src/tychoc.c:482` | `k = TK_COLONCOLON; len = 2;` |
    | `docs/spec/01-lexical.md:184` (§3.9) | `src/tychoc.c:2227-2232` | `src/tychoc.c:2557-2563` | `parse_unary` + `expression nesting too deep` |
    | `docs/spec/01-lexical.md:251` (§3.9.3) | `src/tychoc.c:402-425` | `src/tychoc.c:450-473` | the char-literal scanner — **a true +48** |
    | `docs/spec/01-lexical.md:355` (§3.9.5) | `src/tychoc.c:1826-1866` | `src/tychoc.c:2125-2179` | `interp_join` through the end of `desugar_interp` |
    | `docs/spec/14-ffi.md:119` | `src/tychoc.c:10385-10397` | `src/tychoc.c:10433-10445` | `gen_extern_proto`'s written-param loop — **a true +48** |
    | `corelib/net/net_shim.c:202` | `src/tychoc.c:8387` | `src/tychoc.c:8512-8515` | the `gen_extern_raw` header comment naming the `tycho_str_copy` wrap |
    | `FRICTION.md:214` | `src/tychoc.c:4006-4012` | `src/tychoc.c:4147-4151` | `if (e->op == TK_PLUS && a->kind == E_STR && b->kind == E_STR)` — five lines, as the sentence says |
    | `FRICTION.md:214` (×2) | `src/tychoc.c:8671` | `src/tychoc.c:9003` | `tycho_str_intern(\"%s\")` — the verbatim-paste emit site |
    | `FRICTION.md:214` | `src/tychoc.c:11722-11734` | `src/tychoc.c:12085-12096` | `c_escape_path` — the existing ~10-line escaper |
    | `FRICTION.md:220` | `src/tychoc.c:8387` | `src/tychoc.c:8512-8515` | (same `gen_extern_raw` comment) |
    | `FRICTION.md:224` | `src/tychoc.c:8745` | `src/tychoc.c:8828-8829` | the `to_str`/`to_bool`/`to_under`/`to_bytes` zero-cost arm |
    | `FRICTION.md:224` | `src/tychoc.c:8743` | `src/tychoc.c:8826-8827` | `to_bytes` over `T_ARRAY_INT` → `tycho_bytes_from_intarr` (the one real allocation) |
    | `FRICTION.md:224` | `src/tychoc.c:8155` | `src/tychoc.c:8238` | `static int is_place(Expr *e) {` |
    | `FRICTION.md:225` | `src/tychoc.c:612-616` | `src/tychoc.c:660-664` | the `Channel(T)` type comment — **+48** |
    | `FRICTION.md:225` | `src/tychoc.c:1095-1097`/`src/tychoc.c:7450` | `src/tychoc.c:1143-1145`/`src/tychoc.c:7498` | the store guard and the `IS_CHAN(pr->ret)` return guard — **+48** |
    | `FRICTION.md:225` | `src/tychoc.c:5392`/`5401`/`5412` | `src/tychoc.c:5440`/`5449`/`5460` | the `send`/`recv`/`close` `IS_CHAN` checks — **+48** |
    | `FRICTION.md:233` | `src/tychoc.c:6795-6836` | `src/tychoc.c:6903-6942` | the enum arm loop, `covered[]` alloc through `free(covered)` |
    | `FRICTION.md:233` | `src/tychoc.c:9849-9899` | `src/tychoc.c:10193-10226` | the enum tag-test chain, `_m%d->tag == %d` |
    | `FRICTION.md:234` | `src/tychoc.c:2847`/`src/tychoc.c:2876` | `src/tychoc.c:2895`/`src/tychoc.c:2924` | `ctrl_rewrite_tails` / `ctrl_collect_tails` — **+48** |
    | `FRICTION.md:234` | `src/tychoc.c:6613-6635` | `src/tychoc.c:6681-6704` | `case S_DECL:` through the tail-unification loop |
    | `FRICTION.md:235` | `src/tychoc.c:12069-12071`/`src/tychoc.c:11759` | `src/tychoc.c:12117-12119`/`src/tychoc.c:11807` | `detect_package` argv arm / `scan_pkg_files` — **+48** |
    | `FRICTION.md:236` | `src/tychoc.c:10385-10397` | `src/tychoc.c:10433-10445` | `gen_extern_proto` — **+48** |
    | `FRICTION.md:242` | `src/tychoc.c:6895` | `src/tychoc.c:7188` | `Type at_ = resolve_expr(e->args[j]);` inside `instantiate_generic` |
    | `FRICTION.md:242` | `src/tychoc.c:5565` | `src/tychoc.c:5741` | `Type at_ = resolve_exp(e->args[i], s->params[i]);` |
    | `FRICTION.md:253` | `src/tychoc.c:4663-4668` | `src/tychoc.c:4804-4809` | `case E_OK: case E_ERR:` → `T_OK_PARTIAL`/`T_ERR_PARTIAL` |
    | `FRICTION.md:253` | `src/tychoc.c:5904-5911` | `src/tychoc.c:6097-6101` | the `IS_RES(want)` grounding of a partial |
    | `FRICTION.md:318` | `src/tychoc.c:498`/`src/tychoc.c:4937` | `src/tychoc.c:546`/`src/tychoc.c:4985` | the `T_BYTES` repr comment / `T_STRING \|\| T_BYTES` index → `T_INT` — **+48** |
    | `FRICTION.md:321` | `src/tychoc.c:1289` | `src/tychoc.c:1337` | `case T_BYTES: return "char *";` — **+48** |
    | `FRICTION.md:321` | `src/tychoc.c:8702` | `src/tychoc.c:8828-8829` | the zero-cost reinterpret arm |
    | `FRICTION.md:329` | `src/tychoc.c:4523` | `src/tychoc.c:4571` | the `docs/guides/map-mutation.md` comment the gate found — **+48** |
    | `FRICTION.md:333` | `src/tychoc.c:7858` | `src/tychoc.c:8192` | `case T_BYTES:` in `copy_into` |
    | `FRICTION.md:335` | `src/tychoc.c:11976` | `src/tychoc.c:12102` | `const char *cc = "cc";` |
    | `FRICTION.md:340` | `src/tychoc.c:8955`/`8144`/`7126` (the "is now" half) | `src/tychoc.c:9003`/`8192`/`7174` | intern site / `copy_into` `T_BYTES` / `instantiate_generic` — **+48** |

    **Four citations deliberately NOT repointed, each for a stated reason.**
    1. `FRICTION.md:233`'s `` `src/tychoc.c:2732` at `667f0d9` `` and
       `FRICTION.md:253`'s `` `src/tychoc.c:4670-4681` at `667f0d9` `` are
       **pinned to a named commit**. They cite a historical tree on purpose and
       cannot rot; repointing them to HEAD would destroy the pin.
    2. `FRICTION.md:340`'s three "cited by X **as** `src/tychoc.c:8671` /
       `:7858` / `:6895`" values are the *as-found record* of what other entries
       used to say — phase 4's `FRICTION.md:222` rule. Only the "**is now**" half
       was repointed. Because this phase then repaired those other entries, one
       clause was added to that sentence saying so, rather than leaving the
       reader to discover that the `as` numbers no longer match the entries they
       quote.
    3. `FRICTION.md:233`'s `` `src/tychoc.c:9799-9803` `` names code that **no
       longer exists** — the hard binary `if` that `gen_match_side` replaced. No
       line can be correct, so the sentence now says "as it stood then; the
       replacement `gen_match_side` is at `src/tychoc.c:9481`", which is the same
       treatment `FRICTION.md:222` got.

    **Verify — gate 1, `env -u LD_PRELOAD python3 scripts/check_citations.py`:**
    ```
    citation check: ok (22 anchored contain the token they name, 1794 bare in bounds, 82 source->doc citations resolve)
    CIT_RC=0
    ```
    **Verify — gate 2, `env -u LD_PRELOAD sh scripts/check_links.sh`:**
    ```
    link check: ok (130 markdown files, no dead relative links)
    LNK_RC=0
    ```
    **Verify — gate 3, `env -u LD_PRELOAD sh scripts/spec_check.sh`** (run
    because `docs/spec/01-lexical.md` and `docs/spec/14-ffi.md` were touched):
    ```
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 8 runnable example(s), all pass
    SPEC_RC=0
    ```
    Not run, per the Gate ladder: `make ci`. The whole diff is line numbers
    inside citations in three Markdown files plus one C **comment**; nothing
    reaches a compiled artifact.

    **Scope, restated honestly.** The phase's Done-when says *every* bare
    `src/tychoc.c:N` ref in `FRICTION.md`, `docs/` and any script or source
    comment. `FRICTION.md` and `docs/spec/01-lexical.md` (the chapter §3.8 lives
    in) are complete — every suspect in both was read and either repaired or
    recorded above. The other ~400 are **not** done, and were re-scoped into
    phase 9 rather than swept, because the phase's own instruction forbids
    landing numbers with "a false air of correctness" and the candidate
    generator demonstrably produces those when a citation was already stale.
    Roughly 400 unread repairs would be exactly that failure at scale.

    **The open question the phase was asked to settle: should
    `scripts/check_citations.py` grow an anchored form for these, or should a
    spec `> Provenance:` line be REQUIRED to be anchored? Answer: require it for
    Provenance lines, and this phase is the evidence.** §3.8 is the proof by
    construction — `src/tychoc.c:402` stayed in bounds through phase 2 and
    quietly became the raw-string scanner while claiming to be `::`. Written
    `src/tychoc.c:606@TK_COLONCOLON` it could not have survived, exactly as the
    22 anchored citations survived. The numbers now support making it a rule
    rather than a suggestion: a `> Provenance:` line names a specific mechanism
    by definition, there are far fewer of them than the 1794 bare refs, and each
    already has an obvious distinctive token. This agrees with phase 4's
    recommendation and narrows it: **the two should land as one gate change,
    which is phase 9's second half.**

- [x] **Phase 7 (found by phase 3, not absorbed) — nothing in any gate ever
      parses the two editor grammars, so both can rot silently**
  - Phase 3 changed `editors/vscode/syntaxes/tycho.tmLanguage.json` and
    `editors/zed/grammars/tycho/grammar.js` and had to verify them **by hand**,
    because no gate touches `editors/`: `scripts/tools_check.sh:25` excludes it
    from the formatter sweep by name (`-not -path './editors/*'`) and none of
    `scripts/ci.sh`'s thirteen steps mentions the directory (`grep -rn editors
    Makefile scripts/*.sh` returns exactly that one line). Consequences already
    visible in the tree:
    - `editors/zed/README.md` claimed the grammar "parses all 462 committed
      `.ty` files" — the tree has had 813 for a long time. Phase 3 re-measured
      and rewrote the claim, but nothing stops it rotting again.
    - The checked-in `src/parser.c` is a **generated** artifact. Nothing verifies
      it is in sync with `grammar.js`; an edit to the `.js` alone would ship a
      parser that silently does not implement it.
    - The tmLanguage JSON is not even syntax-checked, let alone exercised.
  - Cheapest real gate, and it needs no new dependency at CI time beyond the one
    the README already documents: `npx tree-sitter-cli@0.25 generate --abi 15`
    into a temp dir and `cmp` against the committed `src/`, then
    `tree-sitter parse -q` over the corpus asserting the ERROR count is exactly
    the number of `tests/reject/` fixtures. Both were run by hand in phase 3 and
    both are one command. Guard it behind a "tree-sitter CLI present" check so an
    offline `make ci` skips rather than fails, the way `[2b] ilp32` already skips
    its ASan lane.
  - Also in this phase, since it is the same file set and phase 3 deliberately
    left it: `editors/vscode/language-configuration.json` has `"` in
    `autoClosingPairs` and `surroundingPairs` but not `` ` ``, so typing a
    backtick does not auto-close. Two entries.
  - Done when: a gate exists that fails if `grammar.js` and `src/parser.c`
    disagree, and `make ci` is green with it.

  **DONE.** New gate `/home/igzo/github/tycho/scripts/editors_check.sh`, wired as
  `make editors-check` (`/home/igzo/github/tycho/Makefile:59-64`) and as step
  `[9b/13]` of `/home/igzo/github/tycho/scripts/ci.sh:103-113`. Plus the two
  backtick entries in
  `/home/igzo/github/tycho/editors/vscode/language-configuration.json:9` and `:11`.

  **The three facts phase 3 filed, re-verified before building on them:**
  `/home/igzo/github/tycho/scripts/tools_check.sh:25` still excludes the
  directory by name, and `grep -rn editors Makefile scripts/*.sh` still returns
  exactly that one line. `npx --yes tree-sitter-cli@0.25 generate --abi 15` into
  a temp dir still reproduces
  `/home/igzo/github/tycho/editors/zed/grammars/tycho/src/` byte for byte (all
  six files: `parser.c`, `grammar.json`, `node-types.json`, and the three
  `tree_sitter/*.h`), so the committed artifact was in sync at the time of
  writing — the gate is a guard, not a repair. Neither vscode JSON was checked
  by anything.

  **The ERROR count is NOT the reject-fixture count, and the phase text's guess
  that it would be is wrong.** `ls tests/reject/ | wc -l` = 239, but
  `tree-sitter parse -q` over all 813 `.ty` files reports exactly ONE failure:

  ```
  $ cd <tmp generated grammar> && npx --yes tree-sitter-cli@0.25 parse -q $(find /home/igzo/github/tycho -name '*.ty' -not -path '*/.git/*')
  /home/igzo/github/tycho/tests/reject/rawstring_unterminated.ty   Parse: 0.02 ms   37208 bytes/ms   (ERROR [12, 9] - [14, 0])
  ```

  The other 238 reject fixtures are **semantic** rejects — type mismatches,
  affine violations, arity errors — whose syntax is well-formed and which a
  highlighting grammar MUST parse. Only one is lexical. So the assertion encoded
  in `/home/igzo/github/tycho/scripts/editors_check.sh:86-88` is the enumerated
  known-bad set (one path), diffed **both ways** at
  `/home/igzo/github/tycho/scripts/editors_check.sh:97`: a new ERROR is a
  regression, and a known-bad file that starts parsing means the grammar grew a
  hole. The reasoning is in that file's comment at
  `/home/igzo/github/tycho/scripts/editors_check.sh:76-85` so nobody re-derives
  "1" as a magic constant.

  **Step numbering: `[9b/13]`, not `[14/14]`.** `/home/igzo/github/tycho/scripts/ci.sh`
  already uses `2b`, `2c` and `3b` for a sub-lane of a numbered step, and the
  `/13` denominator counts the numbered steps, of which there are still exactly
  13. Checked that no other file prints these labels:
  `grep -rn '/13\]' --include='*.md' --include='*.sh' --include=Makefile .`
  returns only `/home/igzo/github/tycho/plan.md` evidence blocks.

  **Verify 1 — the gate directly, `sh scripts/editors_check.sh`, exit 0:**
  ```
  >>> editors: JSON syntax (vscode)
      ok  editors/vscode/syntaxes/tycho.tmLanguage.json
      ok  editors/vscode/language-configuration.json
  >>> editors: zed grammar regenerated with npx --yes tree-sitter-cli@0.25 (tree-sitter 0.25.10 (da6fe9beb4f7f67beb75914ca8e0d48ae48d6406))
      src/ matches grammar.js byte for byte (parser.c, grammar.json, node-types.json, tree_sitter/)
  >>> editors: zed grammar over the corpus (813 .ty files)
      813 files parsed; the only failure is the enumerated known-bad set (tests/reject/rawstring_unterminated.ty )
  editors-check: ok
  ```

  **Verify 2 — the divergence proof, BOTH directions.** Perturbed
  `/home/igzo/github/tycho/editors/zed/grammars/tycho/grammar.js:43` only —
  `choice("true", "false", "null")` -> `choice("true", "false", "null", "nil")` —
  leaving the committed `src/` untouched, exactly the "edited the .js alone"
  failure phase 3 described. RED, exit 1:
  ```
  >>> editors: zed grammar regenerated with npx --yes tree-sitter-cli@0.25 (tree-sitter 0.25.10 (...))
      GENERATED src/ IS STALE: editors/zed/grammars/tycho/src does not match grammar.js.
      Regenerate: (cd editors/zed/grammars/tycho && npx --yes tree-sitter-cli@0.25 generate --abi 15)
        diff -r /tmp/tmp.EKwOwmVrIC/src/grammar.json editors/zed/grammars/tycho/src/grammar.json
        205,208d204
        <         },
        <         {
        <           "type": "STRING",
        <           "value": "nil"
        diff -r /tmp/tmp.EKwOwmVrIC/src/node-types.json editors/zed/grammars/tycho/src/node-types.json
        242c242
        <     "named": false
        ...
  editors-check: FAIL
  ```
  Note the corpus lane stayed green through this — the corpus alone would NOT
  have caught it, because a new keyword alternative breaks no existing file.
  Only the `cmp` catches it. Then restored from a pre-edit copy
  (`git diff --stat editors/zed/grammars/tycho/grammar.js` empty), GREEN again,
  exit 0:
  ```
  >>> editors: zed grammar regenerated with npx --yes tree-sitter-cli@0.25 (tree-sitter 0.25.10 (da6fe9beb4f7f67beb75914ca8e0d48ae48d6406))
      src/ matches grammar.js byte for byte (parser.c, grammar.json, node-types.json, tree_sitter/)
  editors-check: ok
  ```

  **Verify 3 — the CLI-absent skip.** A PATH with no `npx` and no `node`
  (symlink dir holding only `python3 mktemp cp diff sed find sort wc tr awk head
  rm cat dirname basename ls sh`) — note `/usr/bin/npx` exists on this host, so
  merely dropping the nvm bin was not enough and the stub dir is the honest
  test. `env PATH=<stub> /bin/sh scripts/editors_check.sh`, exit **0**:
  ```
  --- npx on that PATH?
  npx: NOT FOUND
  >>> editors: JSON syntax (vscode)
      ok  editors/vscode/syntaxes/tycho.tmLanguage.json
      ok  editors/vscode/language-configuration.json
  >>> editors: zed grammar SKIPPED (tree-sitter CLI unavailable: 'npx --yes tree-sitter-cli@0.25 --version' failed -- offline, or no npx. The JSON lane above still ran.)
  editors-check: ok (grammar lanes skipped)
  ```
  The JSON lane still ran and still asserted — it needs only `python3`, which
  `/home/igzo/github/tycho/scripts/ci.sh:87` already depends on. Skip wording
  follows `/home/igzo/github/tycho/Makefile:238`'s "ASan lane SKIPPED for ilp32".

  **Verify 4 — `sh scripts/tools_check.sh`:**
  ```
  >>> bytes-rehome: a bytes field of a returned struct is deep-copied into the caller's arena
      bytes field re-homed on struct return
  tools-check: ok
  ```

  **Verify 5 — `make ci`, exit 0, all 13 numbered steps + 4 lettered sub-lanes:**
  ```
  >>> [9/13] make tools-check  (formatter idempotence + semantic preservation + LSP smoke)
  >>> [9b/13] make editors-check  (zed grammar: src/ still generated from grammar.js, corpus still parses; vscode JSON is JSON)
      ok  editors/vscode/syntaxes/tycho.tmLanguage.json
      ok  editors/vscode/language-configuration.json
      src/ matches grammar.js byte for byte (parser.c, grammar.json, node-types.json, tree_sitter/)
      813 files parsed; the only failure is the enumerated known-bad set (tests/reject/rawstring_unterminated.ty )
  editors-check: ok
  >>> [10/13] bench-guard  (tree-alloc wall: tycho must beat C -- perf regression gate)
  ...
  >>> [13/13] make check-links  (every relative Markdown link resolves to a real file; every provenance citation still resolves)
  citation check: ok (104 anchored contain the token they name, 1911 bare in bounds, 82 source->doc citations resolve)
  ================================================================
   CI GREEN -- tree is good
  ================================================================
  ```
  Cost: the `[9b]` lane is ~15s wall (one npx resolve + one generate + 813
  parses at ~0.02ms each), on a `make ci` that already runs ~11 minutes.

  **What this gate does NOT cover, stated so nobody assumes otherwise.** It
  never *builds* the vscode extension or validates the tmLanguage against the
  TextMate schema — it only asserts the file is parseable JSON, so a
  syntactically valid but semantically wrong `match` regex still ships silently.
  It does not check the zed `languages/` queries (`highlights.scm` etc.) at all.
  And the `813` in `/home/igzo/github/tycho/editors/zed/README.md:14` is still a
  hand-written number no gate compares against the tree — filed as phase 12.

- [x] **Phase 8 (found by phase 4, not absorbed) — the same rot outside
      `FRICTION.md` and `docs/`: non-Markdown runners citing each other**
  - Phase 4 swept `FRICTION.md` and `docs/` and, by declared exception, the one
    line phase 1 had written into `tests/run.sh`. It did **not** sweep the rest of
    the tree's script-to-script citations, which rot by the identical mechanism and
    are equally invisible to `scripts/check_citations.py`'s bounds-only check.
  - The one confirmed instance, read at HEAD: `tests/rtparity/run.py:15` cites
    `compiler/fixpoint.sh` for the claim that `make fixpoint` "compares
    tychoc0 against ITSELF byte-for-byte and against tychoc only BEHAVIOURALLY".
    Both halves moved — the self-emission chain is `:29-35` and the behavioural
    differential is `:37-43`, so `:29-43` is the range the sentence describes.
    `:16-30` today spans `cd`/`CC=`/`mktemp` setup plus part of the chain.
  - Not swept at all, and the reason this is a phase rather than a one-line fix:
    nobody has counted how many such citations exist. The search is every tracked
    non-Markdown file (`*.sh`, `*.py`, `*.ty`, `Makefile`) citing another
    non-Markdown file with a `:N`. `compiler/tychoc0.ty` is a known population —
    `docs/bootstrap.md:106` records its own self-citations being off by −50 —
    and it is **frozen**, so it cannot be repaired and must be excluded by name.
  - Done when: the population is enumerated, each live one points at the line its
    sentence describes, `compiler/tychoc0.ty` is excluded with the reason stated,
    and the citation gate is green.
  - Sequencing: after phase 6, which will settle the `:N@token` question for bare
    citations; if the gate grows an anchored form, this sweep should adopt it
    rather than land more bare `:N` refs that rot the same way.
  - **DONE 2026-07-29.** Population counted for the first time, 17 references
    across 12 files repaired by reading, and the gate extended to a third
    direction. **Every path below is written in full**, because a bare `` `:N` ``
    in this block inherits the previously-named path and reddens the gate on this
    very evidence — the trap that caught phases 4, 5, 6 and 10.

    **THE POPULATION, counted at HEAD (`cc3b1a3` + this phase's edits).** Every
    tracked non-Markdown file was scanned for a `path:N` / `path:N-M` naming
    another tracked non-Markdown file. **121 explicit references**, plus **10
    bare `:N` continuations** riding on a path named earlier on the same physical
    line (`compiler/fixpoint.sh`'s `` `:37` ``/`` `compiler/fixpoint.sh` ``, `Makefile` refs in
    `/home/igzo/github/tycho/scripts/asan_self.sh`, and so on) — **131 total**.
    The 121 is the number the new gate reports, because it resolves only the
    explicit form.

    | citing file | refs |
    |---|---|
    | `/home/igzo/github/tycho/scripts/frontparity.sh` | 16 |
    | `/home/igzo/github/tycho/tests/arity_limits_max.ty` | 10 |
    | `/home/igzo/github/tycho/scripts/check_citations.py` | 6 (syntax examples in its own header) |
    | `/home/igzo/github/tycho/scripts/asan_self.sh` | 6 |
    | `/home/igzo/github/tycho/tests/rtparity/run.py` | 5 |
    | `/home/igzo/github/tycho/tests/postfreeze/rawstring.ty` | 4 |
    | `/home/igzo/github/tycho/server/main.ty` | 3 |
    | `/home/igzo/github/tycho/tools/lsp.ty` | 3 |
    | `/home/igzo/github/tycho/tests/reject/rawstring_unterminated.ty` | 3 |
    | `/home/igzo/github/tycho/corelib/cli/cli.ty`, `/home/igzo/github/tycho/corelib/httpd/httpd.ty`, `/home/igzo/github/tycho/corelib/test/result/main.ty`, `/home/igzo/github/tycho/src/tychoc.c`, `/home/igzo/github/tycho/tests/run.sh`, `/home/igzo/github/tycho/tools/tychofmt.ty` | 2 each |
    | 40 further files (the `tests/reject/*.ty` fixture headers, one each) | 1 each |

    By **target**, which is the split that decided the scope: `src/tychoc.c` 76,
    `tests/run.sh` 14, `Makefile` 10, `compiler/fixpoint.sh` 5,
    `scripts/frontparity.sh` 5, `compiler/tychoc0.ty` 4,
    `scripts/tools_check.sh` 3, and 14 singletons.

    **Two classes excluded, by name, with the reason.**
    1. `/home/igzo/github/tycho/compiler/tychoc0.ty` as a **citing** file —
       frozen, and `/home/igzo/github/tycho/docs/bootstrap.md:106` already records
       its self-citations as off by −50. Citations *into* it from live files were
       checked and two were repaired (below).
    2. `tests/diag/*.err` and `tests/warn/*.err` — **20 hits that are not
       citations at all.** A golden line like
       `tests/diag/dym_var.ty:3: error: unknown variable 'coutn'` is compiler
       output, generated, owned by `make test`. Counting them would have inflated
       the population by 15% and pointed a doc gate at a generated file.

    **The 17 repairs, each with the text of the line landed on.** All 17 were
    **in bounds** and named a real line describing something else — §3.8's failure
    mode, not the harmless `+N` shift.

    | citation site | old target | new target | text at the new line |
    |---|---|---|---|
    | `/home/igzo/github/tycho/tests/rtparity/run.py:15` | `compiler/fixpoint.sh` | `compiler/fixpoint.sh` | `./tychoc "$H" -o "$T/A"` … through the `tests/*.ty examples/*.ty` differential `done` |
    | `/home/igzo/github/tycho/tests/rtparity/run.py:5` | `src/tychoc.c:26` | `src/tychoc.c:28` | `#include "tycho_rt_embed.h"   /* defines: static const char *TYCHO_RUNTIME */` |
    | `/home/igzo/github/tycho/tests/rtparity/run.py:7` | `compiler/tychoc0.ty:9595` | `compiler/tychoc0.ty:10555` | `fn preamble() -> string:` |
    | `/home/igzo/github/tycho/tests/rtparity/run.py:40` | `src/tychoc.c:9636` | `src/tychoc.c:10343` | the `if (_step%d == 0) { … "tycho: range step is zero\n" … }` emit |
    | `/home/igzo/github/tycho/tests/rtparity/run.py:40` | `compiler/tychoc0.ty:8593` | `compiler/tychoc0.ty:9513` | tychoc0's `"tycho: range step is zero\n"` header emit |
    | `/home/igzo/github/tycho/tests/cond_stmt_expr.ty:81` | `compiler/tychoc0.ty:8802` | `compiler/tychoc0.ty:9454` | `# (:1185) and a value-`if` re-enters here via SValDecl (:8519), so` |
    | `/home/igzo/github/tycho/corelib/test/result/main.ty:17` | `compiler/fixpoint.sh` | `compiler/fixpoint.sh` + `:47` | `for f in tests/*.ty examples/*.ty; do` and `for d in tests/pkg/*/; do` |
    | `/home/igzo/github/tycho/corelib/test/result/main.ty:18` | `scripts/frontparity.sh` | `scripts/frontparity.sh` | the two-line `for hi in examples/*.ty tests/*.ty …` glob |
    | `/home/igzo/github/tycho/tools/lsp.ty:258` | `scripts/frontparity.sh` | `scripts/frontparity.sh` | same glob — it is the line that carries `tools/*.ty` |
    | `/home/igzo/github/tycho/scripts/asan_self.sh:10` | `Makefile:85-86` | `Makefile:103-106` | `# Differential test suite: every examples/*.ty and tests/*.ty built both` … `# tests/run.sh and docs/thesis.md §3.` |
    | `/home/igzo/github/tycho/scripts/asan_self.sh:11` | `Makefile:202` + `:214` | `Makefile:245` + `:246` | the `ilp32: ASan lane SKIPPED …` echo and `@CC="gcc -m32" TYCHO_NO_ASAN=1 sh tests/run.sh` |
    | `/home/igzo/github/tycho/scripts/asan_self.sh:72` | `Makefile:214` | `Makefile:245` | the same `ASan lane SKIPPED for ilp32` echo |
    | `/home/igzo/github/tycho/scripts/editors_check.sh:24` | `Makefile:238` | `Makefile:245` | the same echo — the string the sentence quotes verbatim |
    | `/home/igzo/github/tycho/scripts/frontparity.sh:102` | `examples/fetch/run.sh:35` | `examples/fetch/run.sh:33` | `if ! { "$TYCHOC" examples/fetch/main.ty --bundle 2>/dev/null \| "$T/h0" > "$T/h0.c" …` |
    | `/home/igzo/github/tycho/scripts/frontparity.sh:102` | `examples/sqlite/run.sh:31` | `examples/sqlite/run.sh:29` | `if ! { "$TYCHOC" demo.ty --bundle 2>/dev/null \| "$T/h0" > "$T/h0.c" …` |
    | `/home/igzo/github/tycho/tests/reject/rawstring_unterminated.ty:10` | `tests/run.sh:161-166` | `tests/run.sh:167` | `for hi in tests/reject/*.ty; do` |
    | `/home/igzo/github/tycho/tests/postfreeze/nested_pattern.ty:12` | `scripts/frontparity.sh` | `scripts/frontparity.sh` | `#      holds two DELIBERATE divergences … (`result/main.ty` uses a nested pattern, `httpd/main.ty` a `\r` escape)` |

    **Three findings worth more than the table.**

    1. **`/home/igzo/github/tycho/scripts/editors_check.sh:24` was stale the day
       it was written.** Phase 7 landed that file on 2026-07-29 and cited
       `Makefile:238` for a string that has never been on line 238 — `:238` is
       `@gcc -m32 build/.m32probe.c -o build/.m32probe …`, and the quoted
       `"ASan lane SKIPPED for ilp32"` is at `Makefile:245`. This is the
       "garbage in" case the checker header names, produced by *this plan*, one
       phase before the phase that hunts it.
    2. **The two worst were in `/home/igzo/github/tycho/tests/rtparity/run.py`,
       and both halves of its `make fixpoint` claim were wrong in different
       directions.** Phase 4's note that `compiler/fixpoint.sh` should be
       `:29-43` is confirmed at HEAD and unchanged by phase 7: the self-emission
       chain is `compiler/fixpoint.sh` (build A, B, C; `cmp -s "$T/cA.c"
       "$T/cB.c"`) and the behavioural differential is
       `compiler/fixpoint.sh`. `:16-30` today lands on the OUT-OF-SCOPE
       comment about `tests/postfreeze/` plus the `cd`/`CC=`/`mktemp` setup —
       in bounds, plausible, about nothing the sentence describes.
    3. **`compiler/tychoc0.ty` being frozen does NOT make citations into it
       safe.** Two of them (`/home/igzo/github/tycho/tests/rtparity/run.py:7`
       and `:40`, and `/home/igzo/github/tycho/tests/cond_stmt_expr.ty:81`) were
       off by 861, 920 and 652 lines respectively. A frozen target cannot drift,
       so these were wrong when written — and nothing has ever read them since.

    **The `src/tychoc.c` half was deliberately NOT swept, and it is 76 of the
    121.** `/home/igzo/github/tycho/tests/reject/*.ty` (≈30 headers),
    `/home/igzo/github/tycho/tests/arity_limits_max.ty` (10),
    `/home/igzo/github/tycho/server/main.ty` (2),
    `/home/igzo/github/tycho/tests/bounded_const_cap.ty` (1) and the rest are
    **verbatim the non-Markdown population phase 9 enumerated and phase 9 was
    dropped over**. Repairing them is the hand sweep that was explicitly
    de-authorised, so it was not attempted in whole or in part. Two `src/tychoc.c`
    refs WERE repaired — both in
    `/home/igzo/github/tycho/tests/rtparity/run.py`, both sitting inside a
    sentence whose other half this phase was already rewriting. Leaving half a
    sentence knowingly false to honour a scope line would have been worse than
    the two extra greps. Declared, not smuggled.

    **THE GATE QUESTION, ANSWERED: yes for bounds, no for content — and the
    second half is filed as phase 13 rather than half-built.** A third direction
    was added to `/home/igzo/github/tycho/scripts/check_citations.py`
    (260 → 321 lines: **18 lines of executable code**, the rest header). It scans
    the same tracked non-Markdown set the SOURCE → DOC pass already walks and
    bounds-checks every `path:N` naming another tracked non-Markdown file.
    - **Cheap:** it reuses `lines_of()`, the `git ls-files` call and the `fails`
      list that were already there. One new regex, one new counter.
    - **Honest about its reach:** all 17 repairs above were IN BOUNDS. This check
      would have caught **none** of them. Its value is the other half — a ref
      past EOF or naming a deleted file now reddens instead of rotting, which is
      exactly the one failure phase 4's bounds check did catch out of its 23.
    - **What is NOT cheap, and why it is phase 13:** catching the wrong-line class
      needs the anchored `path:N@token` form, and `CITE` requires a backtick span
      while source citations are written in bare comment prose
      (`(src/tychoc.c:8512-8515)`). That is a grammar change plus 121 call sites.
      Building it halfway — a new regex with no adoption — would have produced a
      counter that proves nothing.

    **Verify — gate 1, `python3 scripts/check_citations.py`:**
    ```
    citation check: ok (104 anchored contain the token they name, 1911 bare in bounds, 83 source->doc citations resolve, 121 source->source in bounds)
    RC=0
    ```
    `--stats` splits out the new class:
    ```
    citation check: 104 anchored (content-checked, 51 of them the mandatory `> Provenance:` single-line refs), 1911 bare (bounds only), 83 source->doc (existence), 121 source->source (bounds)
    ```
    (`source->doc` moved 82 → 83 because the new header block itself names
    `docs/bootstrap.md:106` as the reason `compiler/tychoc0.ty` is excluded.)

    **Verify — gate 2, the deliberate break, both directions, against the FINAL
    script.** `/home/igzo/github/tycho/tests/rtparity/run.py:15`'s repaired
    `compiler/fixpoint.sh` was temporarily widened to `29-430`:
    ```
    STALE  tests/rtparity/run.py:15  `compiler/fixpoint.sh:<29-430>` -> compiler/fixpoint.sh has 95 lines: OUT OF BOUNDS
    citation check: FAILED (1 stale citation(s) above)
    BROKEN_RC=1
    ```
    (Angle brackets added in that transcript only. The checker reads fenced
    blocks, so quoting its own message verbatim reddens the gate on THIS evidence
    block — the identical footgun phases 4 and 10 both recorded, now hit by the
    check this phase added. Recorded rather than silently worked around.)
    Restored from the pre-break copy, byte-for-byte:
    ```
    citation check: ok (104 anchored contain the token they name, 1911 bare in bounds, 83 source->doc citations resolve, 121 source->source in bounds)
    RESTORED_RC=0
    ```
    **Verify — gate 3, `sh scripts/check_links.sh`:**
    ```
    link check: ok (131 markdown files, no dead relative links)
    LNK_RC=0
    ```
    **Verify — gate 4, the script whose executable code changed, run once:** that
    is `scripts/check_citations.py` itself, run above (gates 1 and 2).

    **Zero line-shift blast radius, checked mechanically.** Every one of the 12
    edited files is 1:1 — several are themselves citation targets
    (`/home/igzo/github/tycho/scripts/asan_self.sh:69-70` is cited by
    `/home/igzo/github/tycho/scripts/frontparity.sh:88`;
    `/home/igzo/github/tycho/scripts/frontparity.sh:164-165` by four sites), so a
    line added anywhere would have created the rot this phase exists to remove:
    ```
    $ git diff --numstat        # before plan.md was touched
    3 3 corelib/test/result/main.ty      1 1 tests/postfreeze/nested_pattern.ty
    3 3 scripts/asan_self.sh             1 1 tests/reject/rawstring_unterminated.ty
    1 1 scripts/editors_check.sh         4 4 tests/rtparity/run.py
    1 1 scripts/frontparity.sh           1 1 tools/lsp.ty
    1 1 tests/cond_stmt_expr.ty
    ```
    **Not run, deliberately: `make test`, `make ci`, `compiler/fixpoint.sh`,
    `scripts/asan_self.sh`.** Per the Gate ladder in
    `/home/igzo/github/tycho/CLAUDE.md`, nothing here can reach a compiled
    artifact. The three `.ty` files and the one `corelib/` file were edited
    **inside `#` comments only**, with no line-count change, so the emitted C of
    every fixture is byte-identical and the `tests/diag/*.err` goldens that name
    fixture line numbers are untouched. The one script whose executable code
    changed is the citation gate, and it was run.

- ~~**Phase 9 (found by phase 6, not absorbed) — the `src/tychoc.c` citation
      base is stale tree-wide by 68 to 3520 lines, not by 48, and the gate is
      structurally blind to all of it**~~ **DROPPED 2026-07-29, by decision, not
      by verification.** The finding below stands — the ~400 stale references are
      real and were measured, not estimated. What was rejected is the *remedy*:
      a hand sweep costing hours to reach a state with nothing holding it. The
      next commit that inserts lines into `src/tychoc.c` re-rots every reference
      the sweep would have fixed, because these are bare refs in prose and no
      gate can see them. Phase 10 already anchored the citations that carry
      weight — the 48 single-line refs on `> Provenance:` lines, now gate-enforced
      — and that is the durable half of this phase's own recommendation. Anyone
      resuming this should extend the gate's reach first and let the sweep follow
      from what the gate reddens, rather than sweeping by hand again. Left
      unstruck below as the measurement it is.
  - Phase 6 went looking for phase 2's `+48` drift and found it in **23** of
    **447** in-scope Markdown citations. It repaired those, plus every other
    suspect in `FRICTION.md` and `docs/spec/01-lexical.md`, and stopped. What is
    left is a **different defect that this plan did not cause**: citations
    written against a `src/tychoc.c` many commits older than HEAD, whose targets
    have since moved by 68 to 3520 lines. Phase 6's own measurement of the
    delta spread is the evidence — the modal shift is not 48 but 215, 508, 495,
    2284 and so on, one cluster per compiler phase that ever inserted lines.
  - The remaining population, all of it still passing the gate because a bare
    `:N` is bounds-checked only:
    - **369 in `docs/`** — the largest holdings are
      `docs/spec/16-builtins.md` (51), `docs/internals/generics-stage2-body-cloning.md`
      (52), `docs/internals/generics-gap-fixes-plan.md` (44),
      `docs/spec/15-program.md` (39), `docs/spec/12-aggregates.md` (38),
      `docs/spec/03-types.md` (26), `docs/rfc/ffi-threading-design-review.md` (26),
      `docs/spec/02-grammar.md` (19),
      `docs/internals/frontend-restriction-audit-2026-07-25.md` (14),
      `docs/rfc/value-lifetime-regions.md` (9), and a long tail of 1–6 each.
    - **64 in non-Markdown files** — `tests/reject/*.ty` fixture headers (~30,
      each naming the exact rejection site), `tests/arity_limits_max.ty` (10),
      `server/main.ty` (2), `tests/rtparity/run.py` (2),
      `scripts/asan_self.sh` (1) and `tests/bounded_const_cap.ty` (1).
    - **Excluded by name, not forgotten:** `compiler/tychoc0.ty` (79 refs) is
      frozen and cannot be repaired — the same exclusion phase 8 above requires,
      and `docs/bootstrap.md:106` already records its self-citations being off
      by −50. The archived `docs/internals/plan-*-DONE.md` set (374 refs) stays
      frozen on phase 4's rule.
  - **The method phase 6 used, and its one failure mode, both of which this
    phase inherits.** Blame the citing line → read `src/tychoc.c` at that commit
    → find that exact text in today's file. It generates a candidate cheaply for
    the whole population at once. It is **wrong whenever the citation was
    already stale when its line was last edited**, because it then tracks the
    stale target rather than the intended one — phase 6 hit that three times in
    47 and caught it only by reading. So the candidate is a search aid, never
    the answer: every repair still costs one read of the current source.
  - **Do the gate change first, not last.** Phases 4 and 6 both concluded the
    same thing from different evidence: bare `:N` is checked for bounds and
    nothing else, which is precisely why 400-odd citations could rot invisibly
    for months. `scripts/check_citations.py` already parses `:N@token` for any
    path — it needs no new syntax, only a rule about where the anchored form is
    required. The cheap, high-yield rule both phases converged on: **require it
    on every `> Provenance:` line and on any citation naming a single named
    construct; leave range and narrative refs bare.** Landing that BEFORE the
    sweep means the sweep's 400 new numbers are gate-checked from birth instead
    of becoming the next generation of silent rot.
  - Done when: `check_citations.py` enforces the anchored form on Provenance
    lines and `make ci` is green with it; the 369 + 64 refs above are repaired
    against the current source, each read; `compiler/tychoc0.ty` and the
    archived plans are excluded with the reason stated in the checker itself.
  - Sequencing: after phase 8, which is the same defect at a smaller scale and
    should adopt whatever anchored form this phase settles.

- [x] **Phase 10 — anchor `> Provenance:` lines, and make the gate require it**
  - **Resumed 2026-07-29 by explicit request: the gate change only.** This is
    phase 9's first bullet ("do the gate change first, not last") carved out and
    run on its own. The ~400-citation hand sweep phase 9 describes is **still not
    authorised** and must not be attempted here.
  - The defect, established twice over by phases 4 and 6: `scripts/check_citations.py`
    validates a bare `path:N` only for being **in bounds**, so a citation whose
    target has moved keeps passing. Phase 6 measured the result — 921 citations
    to `src/tychoc.c`, 906 of them bare, ~400 stale by 68 to 3520 lines. The 23
    anchored `path:N@token` citations are the only ones that have ever survived a
    line shift by being *caught*, and they are why phase 2's `+48` was repaired
    the same hour it was introduced.
  - Scope: `scripts/check_citations.py` (198 lines), and the `> Provenance:`
    lines it will newly police. There are **57 outside the archived
    `docs/internals/plan-*-DONE.md` set**, concentrated in `docs/spec/`
    (`01-lexical.md` 12, `16-builtins.md` 11, `02-grammar.md` 10, `03-types.md` 5,
    and 1–2 each across the rest).
  - **The rule to implement, and its one real subtlety.** On a `> Provenance:`
    line, a **single-line** `path:N` reference MUST be written `path:N@token` and
    the gate fails if it is not. A **range** (`path:N-M`) stays bare — phase 4's
    recommendation says so explicitly, and it is right: a range has no single
    subject token to anchor to, and forcing one would produce a false anchor,
    which is worse than a bare ref. Every Provenance line therefore ends up a
    mix, and the gate must accept that mix rather than demanding uniformity.
  - Anchoring is not a mechanical rewrite: the token must be one that actually
    appears on the cited line **and** identifies what the sentence is about. Pick
    it by reading the line. If a cited line has no distinctive token, that is a
    signal the citation wants to be a range, not that the rule is wrong.
  - Done when: `scripts/check_citations.py` rejects an un-anchored single-line
    ref on a `> Provenance:` line (proven by deleting one anchor and watching it
    redden, then restoring it); all 57 Provenance lines satisfy the new rule; the
    gate is green; and the script's own header documents the rule and states why
    ranges are exempt.
  - Verify: `python3 scripts/check_citations.py`, then the deliberate-break
    check above, then `sh scripts/check_links.sh` and `sh scripts/spec_check.sh`.
    Not `make ci` — see "Gate ladder".
  - **DONE 2026-07-29.** Shipped: the mandatory-anchor rule in
    `scripts/check_citations.py` (198 → 260 lines: 19 lines of code, the rest the
    header the phase required), and 48 newly anchored single-line refs across
    eight `docs/spec/` files. **Every path below is spelled in full**, because a
    bare `` `:N` `` in this block would inherit the previously-named path and
    redden the gate on this very evidence — the trap that caught phases 4, 5 and 6.

    **Population re-derived, and the plan's count was off by one.** The phase says
    57 `> Provenance:` lines; there are **56**, and there were 56 at `782af20` too
    (`git grep -c '^> Provenance:' 782af20 -- '*.md'` summed = 56; same at HEAD).
    Per-file, all in `docs/spec/`: `01-lexical.md` 12, `16-builtins.md` 11,
    `02-grammar.md` 10, `03-types.md` 5, `04-inference.md`/`14-ffi.md`/
    `18-library.md` 2 each, and 1 each in `05-generics.md`, `06-conversions.md`,
    `07-memory-model.md`, `08-declarations.md`, `09-expressions.md`,
    `10-statements.md`, `11-functions.md`, `12-aggregates.md`,
    `13-concurrency.md`, `15-program.md`, `17-runtime.md`,
    `appendix-a-grammar.md`. Otherwise the plan's distribution is exact.

    **The archived set is excluded two ways, and the second is the one that
    matters.** (1) Empirically it has nothing to exclude: `git grep -c
    '^> Provenance:'` over tracked Markdown returns **only** `docs/spec/` files —
    zero hits in `docs/internals/plan-*-DONE.md`. (2) The checker excludes it **by
    name anyway** (`ARCHIVED` + the `frozen` guard), because phase 4 settled that
    those files are frozen verification evidence and a new gate rule must never be
    able to demand an edit there. Exclusion (1) is true today; (2) keeps it true if
    someone pastes a Provenance line into an archived plan tomorrow.

    **A scope decision the phase's wording left open: the rule polices the whole
    BLOCK, not the opening line.** A `> Provenance:` block is hard-wrapped, and
    **34 of the 48 offending refs sit on a `>` continuation line, not on the
    `> Provenance:` line itself** (14 head / 34 continuation, measured before any
    edit). A line-only rule would have policed 29% of the population and would be
    evadable by pressing Enter. The checker therefore opens the block at
    `> Provenance:` and closes it at the first non-blockquote line. Cost: two
    lines of code.

    **What the 48 turned out to be. Only 14 were merely un-anchored; 34 were
    pointing at the wrong line and were repaired by reading.**

    | outcome | count |
    |---|---|
    | anchored in place (cited line was correct) | 14 |
    | **stale — repaired against the current source, then anchored** | **34** |
    | converted from a single-line ref to a range | 0 |
    | ranges left bare, per the rule | 113 + 50 |

    Nothing was converted to a range: every one of the 48 had a real single-line
    subject once the right line was found. The nearest case was
    `docs/spec/12-aggregates.md`'s `keys()` insertion order, whose two cited lines
    named a map representation that no longer exists (a slot-linked list); the
    current design has two genuine single-line sites — the walk and the
    append — so it stayed two anchored singles rather than becoming one range.

    **The 34 stale ones, each with the text of the line landed on.**

    | citation site | old | new (anchored) | text at the new line |
    |---|---|---|---|
    | `docs/spec/01-lexical.md:47` | `src/tychoc.c:222` | `src/tychoc.c:240` | the comment-only-line skip |
    | `docs/spec/01-lexical.md:47` | `src/tychoc.c:244` | `src/tychoc.c:266` | the token loop, which stops at `#` |
    | `docs/spec/01-lexical.md:82` | `src/tychoc.c:230` | `src/tychoc.c:251` | the indent-depth bound |
    | `docs/spec/01-lexical.md:354` | `src/tychoc.c:289` | `src/tychoc.c:313` | the identifier scanner declining the `f` of `f"…"` |
    | `docs/spec/02-grammar.md:46` | `src/tychoc.c:3459` | `src/tychoc.c:4139` | `static void parse_package_decl(Parser *ps) {` |
    | `docs/spec/02-grammar.md:46` | `src/tychoc.c:3466` | `src/tychoc.c:4146` | `static void parse_import_decl(Parser *ps) {` |
    | `docs/spec/03-types.md:382` | `src/tychoc.c:567` | `src/tychoc.c:638` | `static void task_container_err(void) {` |
    | `docs/spec/03-types.md:382` | `src/tychoc.c:607` | `src/tychoc.c:678` | `static void chan_container_err(void) {` |
    | `docs/spec/03-types.md:385` | `src/tychoc.c:5418` | `src/tychoc.c:5841` | that `die_at` |
    | `docs/spec/03-types.md:462` | `src/tychoc.c:7115` | `src/tychoc.c:8808` | the `IS_FUNC(t)` arm of the equality emitter |
    | `docs/spec/10-statements.md:8` | `src/tychoc.c:2338` | `src/tychoc.c:2732` | `static Stmt *parse_if(Parser *ps, int line) {` |
    | `docs/spec/10-statements.md:9` | `src/tychoc.c:2409` | `src/tychoc.c:2838` | `static Stmt *parse_match(Parser *ps, int line, int value) {` |
    | `docs/spec/12-aggregates.md:15` | `src/tychoc.c:9641` | `src/tychoc.c:11840` | the emitted array-`pop` abort |
    | `docs/spec/12-aggregates.md:15` | `src/tychoc.c:9960` | `src/tychoc.c:12006` | the emitted SOA-`pop` abort |
    | `docs/spec/12-aggregates.md:16` | `src/tychoc.c:1613` | `src/tychoc.c:1960` | the arity cap |
    | `docs/spec/12-aggregates.md:16` | `src/tychoc.c:1617` | `src/tychoc.c:1964` | the arity floor |
    | `docs/spec/12-aggregates.md:18` | `src/tychoc.c:9918` | `src/tychoc.c:11975` | the emitted `keys()` walk |
    | `docs/spec/12-aggregates.md:18` | `src/tychoc.c:9931` | `src/tychoc.c:11930` | the append that *defines* the order |
    | `docs/spec/13-concurrency.md:10` | `runtime/tycho_rt.c:509` | `runtime/tycho_rt.c:751` | the publish in `tycho_chan_send_commit` |
    | `docs/spec/13-concurrency.md:10` | `runtime/tycho_rt.c:521` | `runtime/tycho_rt.c:763` | the acquire load in the recv claim |
    | `docs/spec/14-ffi.md:9` | `runtime/tycho_rt.c:1026` | `runtime/tycho_rt.c:1284` | that boundary copy routine |
    | `docs/spec/16-builtins.md:85` | `src/tychoc.c:8283` | `src/tychoc.c:9114` | the `eprint` emit |
    | `docs/spec/16-builtins.md:116` | `src/tychoc.c:4152` | `src/tychoc.c:4520` | the `chr` `Sig` |
    | `docs/spec/16-builtins.md:143` | `src/tychoc.c:4158` | `src/tychoc.c:4527` | the `split` `Sig` |
    | `docs/spec/16-builtins.md:145` | `src/tychoc.c:4157` | `src/tychoc.c:4526` | the `char_at` `Sig` |
    | `docs/spec/16-builtins.md:146` | `src/tychoc.c:8686` | `src/tychoc.c:9617` | the `E_INDEX` `s[i]` emit |
    | `docs/spec/16-builtins.md:148` | `compiler/tychoc0.ty:6698` | `compiler/tychoc0.ty:6770@hi_sidx` | tychoc0's `s[i]` emit |
    | `docs/spec/16-builtins.md:218` | `src/tychoc.c:6819` | `src/tychoc.c:7484` | the `defaultable` predicate |
    | `docs/spec/16-builtins.md:243` | `src/tychoc.c:4151` | `src/tychoc.c:4519` | the `ncpu` `Sig` |
    | `docs/spec/16-builtins.md:332` | `src/tychoc.c:4153` | `src/tychoc.c:4521` | the `die` `Sig` |
    | `docs/spec/16-builtins.md:337` | `src/tychoc.c:4833` | `src/tychoc.c:5217` | the `resolve_expr` call arm |
    | `docs/spec/16-builtins.md:338` | `src/tychoc.c:5468` | `src/tychoc.c:5829` | `reserve` inside that arm |

    (32 rows; the remaining two stale ones are `docs/spec/01-lexical.md:141`'s
    `` `soa` `` pair, which were correct lines but whose anchors had to be the
    *comments* `soa [Struct]` / `soa []Struct` to tell the type-position and
    literal-position sites apart — counted as anchored-in-place, not stale.)

    **Two findings worth more than the numbers.**

    1. **`docs/spec/01-lexical.md:47`, the comment section, cited neither of the
       two lines that implement comments.** Written at `a0236cd` against a
       `src/tychoc.c` whose `:222` was the comment-only-line skip and whose `:244`
       was the token loop; both drifted, and today `:222` is `line++;` and `:244`
       an INDENT/DEDENT comment. In bounds, plausible, and about nothing the
       section describes. Exactly §3.8's failure mode, in the first Provenance
       line in the spec.
    2. **The blame-and-relocate method failed where phase 9 predicted it would.**
       `docs/spec/03-types.md:382`'s `` `:567` `` and `` `:607` `` were *already*
       stale at `b895e668`, the commit that last touched that doc line, so blaming
       it just reproduced the stale target. Found by reading instead: the affine-
       element rejections are `task_container_err` / `chan_container_err`, which
       `arrc_sized_b` calls, and the two `tests/reject/bounded_*_elem.ty` fixtures
       the same Provenance line already names confirm which two of the three.

    **Zero line-shift blast radius, on purpose.** Anchors are long, so the first
    draft grew `docs/spec/01-lexical.md` by 9 lines — which would have invalidated
    every `docs/spec/01-lexical.md:N` reference in `FRICTION.md`, `docs/` and this
    plan's own phase 6 table, i.e. this phase would have created the exact rot it
    exists to stop. Every block was re-wrapped to its **original line count**
    (phase 4's technique). Verified mechanically — every hunk is `@@ -N,k +N,k @@`:
    ```
    $ git diff --numstat -- docs/spec/
    12  12  docs/spec/01-lexical.md      3   3  docs/spec/12-aggregates.md
    1   1   docs/spec/02-grammar.md      1   1  docs/spec/13-concurrency.md
    3   3   docs/spec/03-types.md        1   1  docs/spec/14-ffi.md
    2   2   docs/spec/10-statements.md   11  11 docs/spec/16-builtins.md
    $ git diff -U0 -- docs/spec/ | grep -c '^@@'   # 26 hunks
    unbalanced hunks: 0     (start line and length equal on both sides, all 26)
    ```
    No gate enforces line width in `docs/spec/` (`grep -n 'wc -L\|length'
    scripts/spec_check.sh scripts/check_links.sh` finds nothing), so a few
    Provenance lines are now long. That is the deliberate trade.

    **Verify — gate 1, `env -u LD_PRELOAD python3 scripts/check_citations.py`:**
    ```
    citation check: ok (71 anchored contain the token they name, 1864 bare in bounds, 82 source->doc citations resolve)
    RC=0
    ```
    23 anchored before this phase, 71 after. `--stats` splits out the new class:
    ```
    citation check: 71 anchored (content-checked, 51 of them the mandatory `> Provenance:` single-line refs), 1864 bare (bounds only), 82 source->doc (existence)
    ```
    (51, not 48: three single-line refs inside Provenance blocks were already
    anchored before this phase and are now covered by the rule.)

    **Why re-running that gate now prints bigger numbers, stated rather than
    left to confuse.** Every count quoted in this phase — 71 anchored, 1864
    bare — was measured on the tree **before this evidence block was written**.
    This block is itself Markdown full of citations, so writing it moved the
    totals to `104 anchored / 1911 bare`, and the 33 new anchored ones are the
    repair table above, each now content-checked by the gate it documents. The
    gate is green at both points; only the arithmetic moved.

    **Verify — gate 2, the deliberate break, both directions, run against the
    FINAL script.** Deleted the anchor on `docs/spec/01-lexical.md:174`
    (`` `:482@TK_COLONCOLON` `` → `` `:482` ``):
    ```
    STALE  docs/spec/01-lexical.md:174  `:482` -> un-anchored single-line ref in a `> Provenance:` block; write src/tychoc.c:482@<token> with a token that appears on that line. It currently reads: else if (c == ':' && c2 == ':')      { k = TK_COLONCOLON; len = 2; }
    citation check: FAILED (1 stale citation(s) above)
    BROKEN_RC=1
    ```
    (The checker reads fenced blocks, so the backticks the real output puts
    around its suggested anchored form are stripped in the transcript above —
    quoting it verbatim made the gate red a second time, on this evidence block,
    and then a third time on the sentence explaining the second. Same footgun
    phase 4 hit; recorded rather than silently worked around.)
    The diagnostic names the **file**, the **line**, the **offending reference**,
    the form to write instead, and the text of the target line — the four things
    phases 4, 5 and 6 each lost time to not having. Restored:
    ```
    citation check: ok (71 anchored contain the token they name, 1864 bare in bounds, 82 source->doc citations resolve)
    RESTORED_RC=0
    ```
    Note what this break does **not** trip: `` `:482` `` is in bounds, so the
    pre-existing bounds check was silent about it. The new rule is the only thing
    that sees it.

    **Verify — gate 3, `env -u LD_PRELOAD sh scripts/check_links.sh`:**
    ```
    link check: ok (130 markdown files, no dead relative links)
    LNK_RC=0
    ```
    **Verify — gate 4, `env -u LD_PRELOAD sh scripts/spec_check.sh`:**
    ```
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 8 runnable example(s), all pass
    SPEC_RC=0
    ```
    Not run, deliberately: `make ci`. Per the Gate ladder nothing here can reach a
    compiled artifact — the diff is one Python gate plus prose line numbers in
    eight Markdown files.

    **Explicitly NOT done, so the next reader does not think it was missed.**
    Phase 9's ~400-citation hand sweep was not authorised and was not attempted,
    not even partially. Bare **ranges** on Provenance lines were left bare, which
    is the rule, not an oversight — but several of them are demonstrably stale,
    and that is filed as phase 11 below rather than absorbed here.

- [x] **Phase 11 (found by phase 10, not absorbed) — the bare RANGES on
      `> Provenance:` lines are stale too, and the rule cannot see them**
  - Phase 10 anchored all 48 single-line refs inside `> Provenance:` blocks and
    found 34 of them pointing at the wrong line. The same blocks carry **163 bare
    ranges** (50 on the opening line, 113 on continuations) which the new rule
    deliberately exempts — a range has no single subject token, so forcing an
    anchor produces a false one. Nothing checks them beyond bounds, and several
    were confirmed wrong **while phase 10 was reading their single-line
    neighbours**:
    - `docs/spec/16-builtins.md:85` cites `src/tychoc.c:4144-4163` for the nine
      I/O `Sig` registrations; `register_builtins` is `src/tychoc.c:4336-4368` and
      `src/tychoc.c:4144-4163` is inside the *const-fold* arm. The same file's
      `src/tychoc.c:4140-4171` (line 336, named as `register_builtins` outright),
      `src/tychoc.c:4155-4156` (`substr`/`find` `Sig`, really
      `src/tychoc.c:4352-4353`) and `src/tychoc.c:4164-4165` (`is_null`/`to_ptr`
      `Sig`, really `src/tychoc.c:4361-4362`) are the same drift.
    - `docs/spec/16-builtins.md:145` cites `src/tychoc.c:8212-8220` for `char_at`
      codegen; it is `src/tychoc.c:8641-8648`.
    - `docs/spec/13-concurrency.md:9` cites `runtime/tycho_rt.c:286-610` for the
      runtime and `runtime/tycho_rt.c:381-545` for the channel ring; the channel
      section opens at `runtime/tycho_rt.c:608` and the ring runs past
      `runtime/tycho_rt.c:795`.
  - **And one anchored range shows the caveat the checker's own header warns
    about.** `docs/spec/03-types.md:376` cites
    `src/tychoc.c:850-862@arrc_sized_b` and **passes**, because line 736 — the
    function's opening line — is inside 727-739. But the function is
    `src/tychoc.c:736-748`: the range is misaligned by nine lines and its first
    nine lines are an unrelated comment about `int64_t` sizes. A live instance of
    "a drift that keeps the anchor token inside the new range".
  - Why it was not swept in phase 10: phase 10's scope lock was the single-line
    refs and the gate. Re-deriving 163 ranges by reading is the same shape of work
    as phase 9's sweep, at a tenth the size — and it should reuse phase 9's
    method and its stated failure mode rather than reinvent them.
  - Done when: every bare range inside a `> Provenance:` block resolves to the
    region its clause describes, spot-checked by reading; the anchored ranges are
    re-aligned to the construct they name; `make check-links` green.
  - Sequencing: with or after phase 9. It is the same defect and the same method.
  - **DONE 2026-07-29.** 155 ranges repointed across 17 `docs/spec/` files, plus
    the sharpened caveat in `/home/igzo/github/tycho/scripts/check_citations.py`.
    **Every path below is spelled in full**, the trap that reddened five earlier
    phases on their own write-ups.

    **Population re-derived at HEAD, and the plan's 163 was counting something
    slightly different.** Walking the checker's own block rule (`> Provenance:`
    plus every `>` continuation, closed by the first non-blockquote line) over
    `git ls-files 'docs/spec/*.md'`:

    | class | count |
    |---|---|
    | ranges inside `> Provenance:` blocks, total | **178** |
    | of those, ANCHORED (`path:N-M@token`) | 9 |
    | of those, BARE | 169 |
    | bare ranges the gate actually checks (bounds) | 154 |
    | bare ranges the gate SKIPS — no path named in the paragraph | 12 |
    | bare ranges into another document (not under `SRC_PREFIX`) | 3 |

    The plan's **163** is exactly the 154 checked bare + the 9 anchored, i.e. the
    set the checker walks. It missed the **15** it fails open on — 12 code ranges
    whose paragraph names no path (`docs/spec/17-runtime.md`, and two blocks of
    `docs/spec/02-grammar.md`) and 3 doc→doc ranges. Those 15 were worked too:
    they are the same defect, and being invisible to the gate makes them worse,
    not out of scope. **This phase closed 10 of the 12 path-less ones by writing
    the full path**, which moves them from unchecked to bounds-checked; the
    remaining 2 are held bare deliberately (see "the two left bare" below).

    **Outcome. The bare-range population was almost entirely rotten.**

    | outcome | count |
    |---|---|
    | **repointed (cited span did not describe its clause)** | **152** |
    | anchored ranges re-aligned | 3 |
    | split into two/three ranges because no single span was honest | +3 refs |
    | verified correct, left untouched | 26 |
    | total ranges after the phase | 181 |

    26 out of 178 — **85% were wrong**. That is a higher rot rate than phase 10
    found among the single-line refs (34 of 48, 71%), and the reason is
    mechanical: a range has two endpoints to drift and no anchor to catch either.

    **Method, and why it is not phase 9's.** Ranges name *constructs*, so they
    were resolved by building a function index of
    `/home/igzo/github/tycho/src/tychoc.c` (281 definitions),
    `/home/igzo/github/tycho/runtime/tycho_rt.c` (175) and
    `/home/igzo/github/tycho/compiler/tychoc0.ty` (520), matching each clause's
    named subject to its definition, then **opening both endpoints of every one
    of the 181 final ranges and reading the line**. No blame-and-shift: phase 6
    recorded that method failing 3 times in 47 and phase 8 found 17 refs that
    were wrong *when written*, so no arithmetic recovers them.

    **The seven the phase named, re-verified rather than trusted — all seven
    were wrong, and two were worse than stated.**

    | citation site | old | new | what the new endpoints actually are |
    |---|---|---|---|
    | `docs/spec/16-builtins.md:85` | `src/tychoc.c:4144-4163` | `src/tychoc.c:4340-4344`,`:4349-4350`,`:4359-4360` | the nine I/O `Sig`s are **not contiguous** — see below |
    | `docs/spec/16-builtins.md:17` | `src/tychoc.c:4140-4171` | `src/tychoc.c:4336-4368` | `static void register_builtins(void) {` … `}` |
    | `docs/spec/16-builtins.md:336` | `src/tychoc.c:4140-4171` | `src/tychoc.c:4336-4368` | same (second site, same claim) |
    | `docs/spec/16-builtins.md:143` | `src/tychoc.c:4155-4156` | `src/tychoc.c:4352-4353` | the `substr` `Sig` … the `find` `Sig` |
    | `docs/spec/16-builtins.md:117` | `src/tychoc.c:4164-4165` | `src/tychoc.c:4361-4362` | the `is_null` `Sig` … the `to_ptr` `Sig` |
    | `docs/spec/16-builtins.md:145` | `src/tychoc.c:8212-8220` | `src/tychoc.c:8641-8648` | `if (!strcmp(e->sval, "char_at")) {` … `return sfmt("tycho_str_get(%s, %s)", s, ix);` |
    | `docs/spec/13-concurrency.md:9` | `runtime/tycho_rt.c:286-610` | `runtime/tycho_rt.c:528-841` | `/* ---- tasks (CC-1: …` … the `}` closing `tycho_chan_free` |
    | `docs/spec/13-concurrency.md:10` | `runtime/tycho_rt.c:381-545` | `runtime/tycho_rt.c:623-833` | `/* CC-5: the ring is a Vyukov bounded MPMC queue…` … the `}` closing `tycho_chan_close` |
    | `docs/spec/03-types.md:376` (ANCHORED) | `src/tychoc.c:729-741` | `src/tychoc.c:736-748` | `static Type arrc_sized_b(Type elem, int64_t size, char bnd) {` … `}` |

    **`docs/spec/16-builtins.md:85` could not be repaired as one range, and that
    is a finding, not a workaround.** The prose says "All nine are `Sig` builtins";
    the nine sit in **three separate runs** of `register_builtins` —
    `src/tychoc.c:4340-4344` (`print`…`read_all`), `src/tychoc.c:4349-4350`
    (`die`/`exit`) and `src/tychoc.c:4359-4360` (`args`/`getenv`) — separated by
    `clock`/`now`/`ncpu`/`chr`/`str` and by the string family. The single span
    containing all nine would be `src/tychoc.c:4340-4360`, which also contains
    twelve builtins the sentence is not about. Three ranges, per the phase's
    "do not invent a span".

    **A second misaligned ANCHORED range the phase did not name, found by the
    same test.** `docs/spec/15-program.md:21` cited
    `src/tychoc.c:12543-12647` for the driver. `int main(` is at
    `src/tychoc.c:12098` and the file ends at `:12202`, so the range was **82
    lines early at both ends** and its first 82 lines are `find_file`/argument
    plumbing. Repaired to `src/tychoc.c:12540-12644`. Also
    `docs/spec/15-program.md:22`'s `src/tychoc.c:3811-3881`
    included the function's opening line as its **last** line; the function is
    `src/tychoc.c:3707-3777`. And `docs/spec/03-types.md:376`'s
    `src/tychoc.c:2025-2041@"bounded"` ended one line into the `bounded` branch
    that starts at `src/tychoc.c:1863` and runs to `:1880`. **Three of the nine
    anchored ranges were misaligned** — a 33% failure rate in the class the gate
    reports as green, which is the caveat's whole point.

    **Two ranges had drifted onto the WRONG FILE, not merely the wrong line.**
    In `docs/spec/03-types.md`, `compiler/tychoc0.ty` was named as bare prose
    (no `:N`), so the following `` `:9748-9783` `` and `` `:2880-2893` ``
    inherited **`src/tychoc.c`** as their path and were bounds-checked against
    it. Both now carry the path explicitly:
    `compiler/tychoc0.ty:10241-10268` (`fn comp_dep_types(…)` … `return deps`)
    and `compiler/tychoc0.ty:3126-3139` (`fn newtype_under_ok(…)` … its return).
    A citation that resolves against the wrong file is the worst shape in the
    taxonomy: in bounds, plausible, and about a different program.

    **A dead path, found because one of its two mentions was a range.**
    `docs/spec/18-library.md:34` cited `docs/corelib.md` — **that file does not
    exist**; commit `68e5b39` moved it to `/home/igzo/github/tycho/docs/guides/corelib.md`.
    No gate saw it: the md→src pass ignores paths outside `SRC_PREFIX`, the
    source→doc pass reads only non-Markdown files, and `check_links.sh` follows
    link syntax, not backticked mentions. Repointed to
    `docs/guides/corelib.md:3-8`, whose lines 3–8 are the thesis-context block on
    arena allocation that the sentence describes. The sibling mention at
    `docs/spec/18-library.md:16` carries no `:N`, so it is not a range and not in
    this phase's scope — filed as phase 15 below.

    **The full repair table** is 155 rows and is not reproduced here; the diff is
    the record, and every row was checked the same way. The per-file shape:

    | file | ranges repointed | notable |
    |---|---|---|
    | `docs/spec/16-builtins.md` | 44 | every `Sig` and every `resolve_expr` magic arm had moved |
    | `docs/spec/03-types.md` | 20 | incl. the 2 wrong-file ones and 1 anchored |
    | `docs/spec/01-lexical.md` | 8 of 23 | 15 were already correct — the best file in the tree |
    | `docs/spec/02-grammar.md` | 19 | incl. 2 held bare on purpose |
    | `docs/spec/12-aggregates.md` | 14 | |
    | `docs/spec/14-ffi.md` | 8 | 3 of them into `corelib/` shims |
    | `docs/spec/17-runtime.md` | 8 | all 8 were path-less; all 8 now carry one |
    | `docs/spec/05-generics.md` | 6 | |
    | `docs/spec/04-inference.md` | 5 | 1 split into 2 (`resolve_expr` vs `resolve_exp`) |
    | `docs/spec/08-declarations.md` | 4 | |
    | `docs/spec/06-conversions.md`, `docs/spec/09-expressions.md`, `docs/spec/10-statements.md` | 3 each | |
    | `docs/spec/13-concurrency.md`, `docs/spec/15-program.md` | 2 each | both of 15-program's were anchored |
    | `docs/spec/11-functions.md`, `docs/spec/18-library.md` | 1 each | |

    **`docs/spec/01-lexical.md` is the counter-example worth naming.** 15 of its
    23 ranges were already right, because phase 2 and phase 10 both worked in
    that file within the last day. Citation rot is a function of time since the
    last read, not of the citation's age — which is the argument for the gate,
    not against it.

    **The two left bare, deliberately, and the interaction that forced it.**
    `docs/spec/02-grammar.md:273`'s `` `:3181-3277` `` (`for`/`parallel`) and
    `` `:3135-3172` `` (`select`) were repointed but NOT given a path. Writing
    one would set the checker's `cur` for the rest of that paragraph, which would
    newly police the **eight bare single-line refs on `docs/spec/02-grammar.md:272`
    and `:274`** (`:2338`, `:2409`, `:2723`, `:2655`, `:2858`, `:2872`, `:2881`,
    `:2903`) under phase 10's mandatory-anchor rule — and they are stale
    (`parse_if` is `src/tychoc.c:2676`, `parse_match` is `src/tychoc.c:2782`), so
    the gate would go red on work this phase is not scoped to do. Filed as phase
    14. This is a real hole in phase 10's rule, not a quirk of one block: a
    Provenance block that never names a path is **exempt from the mandatory
    anchor by accident**.

    **The caveat, sharpened — the phase asked, and two real instances now exist.**
    `/home/igzo/github/tycho/scripts/check_citations.py`'s "WHAT THIS DOES NOT
    CATCH" said the wide-range hole was about picking *a common word*. Both real
    instances disprove that: `arrc_sized_b` occurs 3 times in a 12202-line file
    and `int main(` exactly once — as distinctive as tokens get — and both ranges
    still passed while misaligned by 9 and 82 lines. The failure mode is **range
    width**, not token commonness. The bullet now leads with width, cites both
    instances, and keeps the common-word warning as the second half. Three lines
    replaced by three lines; nothing in the tree cites this file by line
    (`grep -rn 'check_citations.py:[0-9]'` → no matches), so the blast radius is
    zero either way.

    **Line-count discipline: every touched file is 1:1, verified two ways.**
    ```
    $ git diff --numstat
    7    7   docs/spec/01-lexical.md        7    7   docs/spec/12-aggregates.md
    14   14  docs/spec/02-grammar.md        2    2   docs/spec/13-concurrency.md
    17   17  docs/spec/03-types.md          5    5   docs/spec/14-ffi.md
    4    4   docs/spec/04-inference.md      2    2   docs/spec/15-program.md
    3    3   docs/spec/05-generics.md       27   27  docs/spec/16-builtins.md
    2    2   docs/spec/06-conversions.md    3    3   docs/spec/17-runtime.md
    2    2   docs/spec/08-declarations.md   1    1   docs/spec/18-library.md
    2    2   docs/spec/09-expressions.md    3    3   scripts/check_citations.py
    2    2   docs/spec/10-statements.md
    1    1   docs/spec/11-functions.md
    $ git diff -U0 | grep -c '^@@'   # 53 hunks
    unbalanced hunks: 0     (start line and length equal on both sides, all 53)
    ```
    Every edit is an in-place number substitution, so no re-wrap was needed and
    no `docs/spec/N` citation anywhere in the tree moved. Phase 10 grew
    `docs/spec/01-lexical.md` by 9 lines in its first draft and would have
    invalidated its own evidence tables; that failure mode is why this is checked
    rather than assumed.

    **Verify — gate 1, `python3 scripts/check_citations.py`:**
    ```
    citation check: ok (104 anchored contain the token they name, 1960 bare in bounds, 83 source->doc citations resolve, 121 source->source in bounds)
    CIT_RC=0
    ```
    **Verify — gate 2, `sh scripts/check_links.sh`:**
    ```
    link check: ok (131 markdown files, no dead relative links)
    LNK_RC=0
    ```
    **Verify — gate 3, `sh scripts/spec_check.sh`** (run because this phase
    touches `docs/spec/`):
    ```
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 8 runnable example(s), all pass
    SPEC_RC=0
    ```
    **Verify — gate 4, the endpoint read.** All 181 final ranges were dumped with
    the source text at both endpoints and read; the report is the phase's real
    evidence and the three gates above only confirm bounds. Nine ranges were
    tightened after that read because their end landed on a trailing comment or
    one line short of the block's `}` — e.g. `src/tychoc.c:3181-3281` → `:3181-3277`
    (`:3278-3281` is the *next* arm's comment) and `src/tychoc.c:5636-5655` →
    `:5636-5656` (the arm's closing brace).

    **Not run, deliberately:** `make test`, `make ci`, `compiler/fixpoint.sh`,
    `scripts/asan_self.sh`. Per CLAUDE.md's gate table this phase is Markdown
    plus one comment block in a Python gate; nothing here can reach a compiled
    artifact. LD_PRELOAD needed no `env -u` wrapper — the shim is gone from the
    environment, as CLAUDE.md's Environment note predicted.

    **Explicitly NOT done, so the next reader does not think it was missed.**
    Bare refs **outside** `> Provenance:` blocks are phase 9's dropped population
    and were left alone even where this phase proved them wrong — the clearest
    case is `docs/spec/16-builtins.md:33`, which cites `register_builtins` as
    `src/tychoc.c:4140-4171` in §29.1 prose while the Provenance line eight lines
    below it now correctly says `src/tychoc.c:4336-4368`. The two are visibly
    inconsistent inside one file. That is what a scope lock costs, and it is
    recorded rather than quietly absorbed.

- [ ] **Phase 14 (found by phase 11, not absorbed) — a `> Provenance:` block
      that names no path is exempt from phase 10's mandatory anchor by accident**
  - `scripts/check_citations.py` resolves a bare `:N` against the last path named
    **in the same paragraph** and `continue`s when there is none (the fail-open
    rule in its header). Phase 10's mandatory-anchor check sits **after** that
    `continue`, so a Provenance block whose paragraph never spells a path has its
    single-line refs skipped entirely — not anchored, not bounds-checked, not
    seen.
  - The live instance: `docs/spec/02-grammar.md:272` and `:274` carry eight such
    refs — `:2338`, `:2409`, `:2723`, `:2655`, `:2858`, `:2872`, `:2881`,
    `:2903` — and they are stale. `parse_if` is `src/tychoc.c:2676`, `parse_match`
    is `src/tychoc.c:2782`; phase 10 anchored those same two functions correctly
    in `docs/spec/10-statements.md` and could not see this block.
  - Phase 11 repointed that block's two ranges but held them **path-less on
    purpose**: naming the path there would set `cur` and redden the gate on these
    eight refs, which is this phase's work, not phase 11's.
  - Done when: the eight refs are anchored against the lines they actually
    describe; the block's two ranges get their explicit path back; and the
    checker either warns on a Provenance block that names no path at all, or its
    header states plainly that such a block is unpoliced.
  - Verify: `python3 scripts/check_citations.py`, then delete one of the new
    anchors and watch it redden, then restore. Not `make ci`.

- [ ] **Phase 15 (found by phase 11, not absorbed) — `docs/corelib.md` does not
      exist and two gates are structurally unable to say so**
  - `docs/spec/18-library.md:16` names `docs/corelib.md` as a bare backticked
    path inside a `> Provenance:` block. The file was moved to
    `/home/igzo/github/tycho/docs/guides/corelib.md` by commit `68e5b39`. Phase 11
    repaired the *other* mention (`docs/spec/18-library.md:34`, which carried a
    line range and was therefore in scope) but not this one.
  - Why nothing catches it, stated precisely, because the gap is the point: the
    md→src pass of `scripts/check_citations.py` ignores any path outside
    `SRC_PREFIX`; its source→doc pass scans only **non**-Markdown files; and
    `scripts/check_links.sh` resolves Markdown *link* syntax, not a backticked
    mention. A Markdown file naming a dead document in prose is invisible to all
    three.
  - Same shape, worth sweeping together: `docs/spec/18-library.md:18` names
    `src/tychoc.c` and `corelib/run.sh` bare, and `docs/spec/07-memory-model.md`
    and `docs/spec/14-ffi.md` open their Provenance blocks with bare
    `docs/…​.md` mentions. Those resolve today; nothing proves they will.
  - Done when: every backticked `docs/….md` mention inside a `> Provenance:`
    block names a file that exists, and the checker gains an existence check for
    backticked doc paths in Markdown Provenance blocks (the narrowest rule that
    would have caught this one).
  - Verify: `python3 scripts/check_citations.py` plus a deliberate break, then
    `sh scripts/check_links.sh`. Not `make ci`.

- [ ] **Phase 12 (found by phase 7, not absorbed) — the corpus SIZE in
      `editors/zed/README.md` is still a hand-typed number**
  - Phase 7's gate now proves the zed grammar parses the corpus, but the corpus
    *count* in the prose is still unguarded.
    `/home/igzo/github/tycho/editors/zed/README.md:14` reads "813 committed
    `.ty` files (excluding `editors/`, `node_modules/` and …". This is the exact
    claim that rotted before: it said **462** until phase 3 rewrote it, having
    been wrong by several hundred files for an unknown number of commits.
    Phase 7 left it because a README's prose number is outside "a gate that
    parses the grammars", and inventing a prose-number checker is a different
    piece of machinery from `tree-sitter generate | cmp`.
  - The cheap version is three lines in
    `/home/igzo/github/tycho/scripts/editors_check.sh`: the script already
    computes `nfiles` (`/home/igzo/github/tycho/scripts/editors_check.sh:92`)
    with the same exclusion list the README recites, so assert that `$nfiles`
    appears in `editors/zed/README.md`. Cost: one `grep`.
  - Consider at the same time whether any *other* hand-typed corpus count in the
    tree deserves the same treatment — phase 3's evidence quotes `813 files
    checked` from `scripts/tools_check.sh` and phase 5's quotes `compiled: 542`
    from the ASan lane, and those are computed, not typed. A quick
    `grep -rn '\b813\b'` over `*.md` would say whether the README is the only
    typed one. Do NOT widen this into a general numeric-claim checker.
  - Done when: `make editors-check` fails if
    `/home/igzo/github/tycho/editors/zed/README.md`'s corpus count disagrees
    with the tree, proven by perturbing one or the other; `make ci` green.
  - Sequencing: independent of phases 8, 9 and 11. Any time after phase 7.

- [ ] **Phase 13 (found by phase 8, not absorbed) — the source→source gate is
      bounds-only, and the wrong-line class is 100% of what phase 8 found**
  - Phase 8 added a third direction to
    `/home/igzo/github/tycho/scripts/check_citations.py` that bounds-checks the
    121 `path:N` refs one tracked non-Markdown file makes to another. It repaired
    17 of them. **All 17 were in bounds**, so the new check would have caught
    zero. It is a real guard against a ref past EOF or a renamed target, and
    nothing more.
  - The fix is the anchored form the docs already use — `path:N@token`, which
    `CITE` and the anchor comparison already implement. The obstacle is
    syntactic, not conceptual: `CITE` requires the ref to sit in a backtick span,
    and source citations are written in bare comment prose —
    `(src/tychoc.c:8512-8515)` at
    `/home/igzo/github/tycho/corelib/net/net_shim.c:202`,
    `# src/tychoc.c:3661` at
    `/home/igzo/github/tycho/tests/reject/enum_typaram_max.ty:2`. So this needs a
    second matcher with its own delimiter rules, plus a decision on which of the
    121 must carry an anchor.
  - Phase 4's rule is the one to reuse and it splits this population cleanly:
    require the anchor where a citation names a **mechanism** (a glob, a loop, a
    `Sig`, a `die_at`), leave narrative and range refs bare. On phase 8's own
    numbers that is roughly the 45 non-`src/tychoc.c` refs — the `Makefile`,
    `tests/run.sh`, `compiler/fixpoint.sh` and `scripts/frontparity.sh` targets
    that runners cite at each other and that move on every edit.
  - **Do not let this drag in the 76 `src/tychoc.c`-targeted refs.** Those are
    phase 9's dropped population and dropping them was a decision. If the
    anchored form makes the sweep gate-driven rather than hand-driven, that is
    the argument for reopening phase 9 — make it explicitly, do not arrive there
    by scope creep.
  - Done when: the checker requires `path:N@token` for source→source citations
    naming a mechanism, proven by a deliberate break in both directions; the
    in-scope refs carry anchors; the two doc gates are green.
  - Sequencing: after phase 8. Independent of phases 11 and 12.

## Status — PLAN COMPLETE, 2026-07-29

Every phase is closed: **1–8, 10 and 11 done, 9 dropped by decision.** The
section below is the earlier stop at phase 6, kept as written because the
reasoning still holds for phase 9 and the reader should see that the chain was
stopped, reconsidered, and restarted rather than run straight through.

```text
30058a2  phase 1   tests/postfreeze/, a fixture lane outside the freeze
b895e66  phase 2   backtick raw string literals
d572181  phase 3   tychofmt, LSP and editor grammars learn raw strings
1d8ed4b  phase 4   repoint stale fixpoint.sh / frontparity.sh citations
309c59a  phase 5   asan_self.sh and the postfreeze corpus
782af20  phase 6   repoint src/tychoc.c citations after the raw-string insert
5c6aeae  (stop note — why 7-9 stayed unchecked at that point)
58c3593  phase 10  carved out of 9: the gate fix, authorised on its own
a2509a3  phase 10  the citation gate requires anchored Provenance refs
1afbc84  CLAUDE.md — the gate budget, so agents stop reaching for make ci
7e2e0d5  phase 7   a gate for the editor grammars
cc3b1a3  (phase 9 dropped, measurement kept)
387d7c5  phase 8   repoint script-to-script citations
1b772c6  phase 11  repoint the stale ranges on Provenance lines
```

**What the citation phases actually established**, which is worth more than the
repairs themselves: the tree's `path:line` citations were wrong at a rate nobody
had measured. Phase 10 found **34 of 48** single-line Provenance refs pointing at
the wrong line (71%). Phase 11 found **152 of 178** ranges wrong (85%) — higher,
because a range has two endpoints to drift and no anchor to catch either. Phase 8
found **17 of 131** source→source refs wrong, every one *in bounds and pointing at
an unrelated line*, several wrong on the day they were written — including one in
a script phase 7 had just created, and three into the frozen `compiler/tychoc0.ty`,
whose target cannot drift at all. Arithmetic recovers none of these; only reading
does. The gate now covers three directions and enforces anchoring where it can,
and `CLAUDE.md` records the two rules that caught five phases in a row writing
their own evidence.

**Left unchecked, all filed by phases as they ran, none blocking:** phase 12 (the
zed README's hand-typed corpus count), phase 13 (an anchored form for
source→source citations — the class phase 8's new check provably cannot catch),
phase 14 (a Provenance block naming no path escapes phase 10's mandatory anchor
by accident; 8 stale refs in `docs/spec/02-grammar.md:272-274`), phase 15 (the
dead `docs/corelib.md` path, moved to `docs/guides/corelib.md` by `68e5b39`).

## Status — the earlier stop at phase 6, 2026-07-29

**Phases 1–6 are done and the tree is green.** The plan as approved was phases
1–3; phases 4–9 were all filed by phase agents as out-of-scope discoveries,
under the scope-lock rule that says work found outside a phase is appended, never
silently absorbed. Six ran. **Phases 7, 8 and 9 are left unchecked by the user's
explicit decision**, not by oversight and not because they failed.

```text
30058a2  phase 1  tests/postfreeze/, a fixture lane outside the freeze
b895e66  phase 2  backtick raw string literals
d572181  phase 3  tychofmt, LSP and editor grammars learn raw strings
1d8ed4b  phase 4  repoint stale fixpoint.sh / frontparity.sh citations
309c59a  phase 5  asan_self.sh and the postfreeze corpus
782af20  phase 6  repoint src/tychoc.c citations after the raw-string insert
```

The Goal's two objects are both met: `tests/postfreeze/` exists and is proven
(a `tychoc0` built at HEAD refuses its canary, while both frozen-compiler lanes
still report `agreed: 292  diverged: 0`), and backtick raw strings ship in the
compiler, the formatter, the LSP and both editor grammars. The last full sweep
was phase 3's `make ci` → `CI GREEN`, exit 0; phases 4–6 touched only Markdown
and comments, and phase 5's `asan_self.sh` ran green at `compiled: 544 failed: 0`.

**Why the remaining three were stopped rather than run.** They are all one
problem, and it is not this plan's problem. Phase 6 counted **921 citations to
`src/tychoc.c` alone, 906 of them bare**, and found that only 23 of the 447
in-scope suspects were phase 2's `+48` — the rest were stale by 68 to 3520 lines
*before this plan started*. `scripts/check_citations.py` validates a bare `:N`
only for being in bounds, which is precisely why they rotted silently, and it is
why phases 4, 5 and 6 each reddened the gate **on their own evidence blocks** and
nowhere else. Sweeping ~400 more references by hand is symptom-chasing at a rate
the next line-shifting commit undoes. Phase 9 already carries the settled answer —
require `:N@token` anchoring on `> Provenance:` lines, landing *before* any sweep —
and that is the thing worth doing first, in its own plan, on its own evidence.

**If work resumes here, read this first:** phases 7, 8 and 9 below are written
against the tree as of `782af20`. Phase 9's own line numbers will have drifted if
anything touched `src/tychoc.c` in between — which is the joke, and also the
point.

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
