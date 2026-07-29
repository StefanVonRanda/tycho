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

- [ ] **Phase 6 (found by phase 2, not absorbed) — bare `src/tychoc.c:N`
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

- [ ] **Phase 7 (found by phase 3, not absorbed) — nothing in any gate ever
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
