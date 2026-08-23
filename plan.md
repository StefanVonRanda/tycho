# Rewrite the bootstrap compiler in Tycho (`compiler/`)

## Goal

A second Tycho compiler, written in Tycho, targeting the whole locked language —
not a subset. Seven packages under `compiler/`: `lex parse ast types lower emit
driver`, entry `compiler/main.ty`, `make tychoc1` -> `./tychoc1`.
`runtime/tycho_rt.c` is unchanged and stays C; `tychoc1` emits against the same
runtime ABI. Done when `TYCHOC=./tychoc1 make test` is green at the same count
as `./tychoc`, and the fixpoint holds — `tychoc1` built by tychoc, then
self-built twice, the two emitted `.c` identical.

## Pre-flight

- **Worst case:** `tychoc1` compiles the corpus but emits subtly different C —
  a wrong arena parent, a missed deep-copy elision — so `make test` is green
  while generated programs leak or alias. The fixpoint leg does not catch this;
  only the golden-per-fixture comparison does.
- **Reversibility:** total. `compiler/` is a new directory, `src/tychoc.c` is
  untouched, and `make tychoc1` is a new target. Deleting `compiler/` restores
  the tree exactly.
- **Verified:** `compiler/` is empty and untracked (`git ls-files compiler/`
  returns nothing; `603d8fbd` removed the old one wholesale).
  `src/tychoc.c` is 14,509 lines / 471 static functions.
  The emit target is `runtime/tycho_rt.c` embedded verbatim, then
  `void h_<fn>(Arena *_parent)` with `arena_child` per scope (`Makefile:25`,
  `Makefile:17-21`). **60** `run.sh` files set `TYCHOC=` — the "55" in the old
  plan was stale — and 3 sites call `./tychoc` inline, which an override would
  silently miss: `bench/transpile/run.sh:6`, `tools/tycho-ar/run.sh:266`,
  `tools/tycho-ar/run.sh:267`.
- **Assuming — bootstrap story:** `src/tychoc.c` stays the bootstrap forever;
  `tychoc1` is built by `./tychoc` and no generated `.c` is committed. This is
  the reversible default and does not foreclose committing one later. If the
  release story needs a self-contained clone, this decision flips and Phase 10
  changes.
- **Assuming — runtime source:** `tychoc1` reads `runtime/tycho_rt.c` from disk
  rather than replicating the `$(EMBED)` awk step into a generated header. Risk
  if wrong: `tychoc1` is not relocatable outside the repo. Phase 1 settles it.
- **Scale, stated plainly:** phases 5–8 are each large. This is not a
  single-session task and the plan does not pretend otherwise.

## Phases

- [x] **Phase 1 — skeleton and `print(str(1))` end to end**
  - Scope: `compiler/{lex,parse,ast,emit,driver}/`, `compiler/main.ty`, a
    `tychoc1` target in `Makefile`. No `types/` yet. `print(1)` does not
    compile under `./tychoc` — `print` takes a string — so the milestone
    program is `fn main(): print(str(1))`.
  - Done when: `make tychoc1` builds, and `./tychoc1` on that program emits C
    that `cc` compiles to a binary whose stdout is byte-identical to the binary
    `./tychoc` produces from the same source.
  - Verify: build both, run both, `cmp` the two stdouts; show the diff of the
    two emitted `.c` and state what differs and why.
  - **Done 2026-08-22.** `compiler/{ast,lex,parse,emit,driver}/` + `compiler/main.ty`,
    `tychoc1` target at `Makefile:28-34`, `/tychoc1` at `.gitignore:23`.

    ```
    $ make tychoc1
    ./tychoc compiler/main.ty -o tychoc1
    built tychoc1
    $ ./tychoc  $S/ms/m.ty --emit-c -o $S/a     # m.ty is `fn main():\n    print(str(1))`
    $ ./tychoc1 $S/ms/m.ty --emit-c -o $S/b
    $ diff $S/a.c $S/b.c
    2845,2849d2844
    < (five blank lines)
    $ cc -O2 -fwrapv -std=c11 -w $S/a.c -o $S/a.bin -lpthread -lm
    $ cc -O2 -fwrapv -std=c11 -w $S/b.c -o $S/b.bin -lpthread -lm
    $ $S/a.bin > $S/a.out; $S/b.bin > $S/b.out; cmp $S/a.out $S/b.out
    STDOUT-IDENTICAL            # both files are the single byte `1` (print adds no newline)
    ```

    **What differs in the `.c`, and why:** exactly five blank lines, nothing
    else. Both files carry `runtime/tycho_rt.c` verbatim as lines 1-2841
    (`diff <(head -2841 a.c) runtime/tycho_rt.c` is empty), then
    `/* ---- generated from Tycho source ---- */`. `./tychoc` emits a blank
    separator for each of its empty declaration sections (typedefs, structs,
    enums, generics, extern protos) before the function prototypes; `tychoc1`
    has no such sections yet and emits one blank line. Every generated line —
    the `h_main` prototype, the `arena_child` scope, the temp-arena statement
    `{ Arena _t = arena_new(0); tycho_print_s(tycho_int_to_str(&_t, 1LL)); arena_free(&_t); }`,
    and the whole `int main` stanza — is byte-identical.

    **Negative control** (the `cmp` leg is not vacuous): `tychoc1` on
    `print(str(2))` built and run the same way gives `c.out`, and
    `cmp a.out c.out` exits 1 with `differ: byte 1, line 1`.

    **The runtime-source assumption in Pre-flight is settled: read-at-emit-time
    works.** `driver.read_runtime` reads `runtime/tycho_rt.c` through
    `core:io.read_text`, defaulting to that repo-relative path, with
    `--runtime <path>` as the override. The cost is real and unchanged: run
    from anywhere but the repo root and `--runtime` is mandatory.

- [x] **Phase 2 — lexer complete**
  - Scope: `compiler/lex/`. Every token of the locked language: the 30 reserved
    words, `$T` type params, `bounded[N]`, string/char/byte/number literals with
    escapes, comments, and the significant-indentation INDENT/DEDENT rules.
  - Done when: a `--dump-tokens` mode lexes all 274 `tests/*.ty` plus every
    `.ty` under `corelib/` with zero unknown-token errors.
  - Verify: the dump over that corpus, plus a round-trip control — tokens
    re-joined must reproduce the source modulo whitespace on at least 50 files,
    and a deliberately corrupted token table must redden it.
  - **Done 2026-08-22.** `compiler/lex/lex.ty` is the whole token set read out of
    `src/tychoc.c@keyword` and its operator chain — not inferred from fixtures.
    `soa` is deliberately NOT a keyword, because it is not one there either: it
    lexes as an identifier and the parser will treat it contextually.
    `--dump-tokens` and `--round-trip` are at `compiler/main.ty@mode`.

    **Leg 1 — the dump over the corpus** (274 `tests/*.ty` + 91 `.ty` under
    `corelib/`):

    ```
    leg1: files=365 failures=0
    ```

    **Leg 2 — round trip**, run over the whole corpus rather than the 50 asked
    for. Normalisation, stated plainly: every token carries its exact source
    slice, comment trivia included; the slices are concatenated and compared
    against the source with **whitespace removed from both sides** — inside
    string literals too, since the same removal is applied to each. The property
    that survives is real (every non-whitespace byte is covered by exactly one
    token, in order) but it is a COVERAGE property and **cannot see a token
    split**: drop `..<` and `..` + `<` re-joins to the same bytes.

    ```
    leg2: files=365 round-trip-ok=365 failed=0
    ```

    So a third assertion carries that class — a **token-kind census** over the
    same dump, which is what the two controls below move:

    ```
    char=73 dedent=4439 eof=365 float=394 ident=43309 indent=4439
    int=8736 kw=11537 newline=16045 op=74757 str=5791
    ```

    **Leg 3 — three negative controls, each reddening a different leg**, each
    observed, each reverted (the census returns to the block above):

    | break | effect |
    |---|---|
    | drop `"for"` from `is_keyword` | leg 1 stays **green** (an identifier is not an error); census moves `kw=11537 -> 10950`, `ident=43309 -> 43896` |
    | drop `\r` from the string escape set | leg 1 **fails 6 files** with `unsupported escape`; census loses 6 files' worth of every kind |
    | drop the 3-char `..<` case from `op_len` | leg 1 and leg 2 both stay **green**; census moves `op=74757 -> 74758`, and the corpus contains exactly one `..<` |

    The first and third are the ones that matter: they are the corruptions the
    two legs the brief named cannot see, and without the census this phase would
    have shipped two legs that pass against a broken keyword table.

    **Phase 1's milestone is unmoved**: `./tychoc1` on `fn main(): print(str(1))`
    still emits C differing from `./tychoc`'s by the same five blank lines.
    `compiler/parse/parse.ty` was retargeted from the Phase 1 kind names
    (`ident`/`punct`) to `kw`/`op`; nothing else outside `compiler/lex/` moved.

- [x] **Phase 2b — three lexer parity gaps deferred out of Phase 2**
  - Scope: `compiler/lex/lex.ty`. Each is a place where `src/tychoc.c` does more
    than lexing and Phase 2 stopped at the token boundary.
    1. **No integer-literal overflow check.** `src/tychoc.c` dies with
       `integer literal out of range`; `compiler/lex/` accumulates the value and
       wraps under `-fwrapv`, so an over-wide literal lexes to a wrong number in
       silence. **The same hole exists for a FLOAT literal** (found in Phase 3):
       `strings.parse_float` fails to `Err` and `compiler/lex/` substitutes
       `0.0`, where `src/tychoc.c` dies `float literal out of range`. Three
       reject fixtures ride on this — `tests/reject/int_hex_overflow.ty`,
       `tests/reject/int_literal_overflow.ty`,
       `tests/reject/float_lit_overflow_neg.ty` — and they are the only three
       syntax rejections Phase 3 does not make.
    2. **`# deprecated:` doc notes are dropped.** `src/tychoc.c@dnote_scan` runs
       during lexing and attaches the note to the fn below it. Comments are
       trivia here, so the deprecation warning `make grid-check` gates has no
       source yet.
    3. **The unclosed-bracket recovery dies instead of batching.** `src/tychoc.c`
       pushes the diagnostic and resumes real layout so the rest of the file
       still lexes; `compiler/lex/` dies at the first one, which loses error
       batching. Belongs with Phase 9's diagnostics work if not done sooner.
  - Done when: (1) an over-wide literal is refused, (2) a `# deprecated:` note is
    reachable from the token stream, (3) the recovery resumes rather than dying.
  - Verify: a probe per item, plus the Phase 2 dump/round-trip/census legs
    unmoved over the 365-file corpus.
  - Do NOT run: any test lane. Nothing outside `compiler/` is touched.

  - **Done 2026-08-23.** All three in `compiler/lex/lex.ty`, plus a
    `--dump-dnotes` mode (`compiler/main.ty`, `compiler/driver/driver.ty`) so
    item 2 is observable. `tokenize_ex` is now a wrapper over `tokenize_all`,
    which returns `([Tok], [DNote])`; the notes are a SIDE TABLE, as in
    `src/tychoc.c`, because a note is not a syntactic element and putting one in
    the stream would make every consumer skip it.

    **Item 1 — over-wide literals.** Ported `src/tychoc.c:657`'s
    `v > (INT64_MAX - d) / litbase` accumulator guard and, for floats, the
    `strings.Overflow` arm of `strings.parse_float`. Only `Overflow` dies:
    `src/tychoc.c`'s `c_strtod` returns 0 for a total underflow and does not
    check, so dying on `Underflow` would be a divergence, not a fix.

    ```
    $ ./tychoc1 tests/reject/int_hex_overflow.ty --parse
    tychoc1: line 2: integer literal out of range                    (was: PARSE-OK 1)
    $ ./tychoc1 tests/reject/int_literal_overflow.ty --parse
    tychoc1: line 3: integer literal out of range                    (was: PARSE-OK 1)
    $ ./tychoc1 tests/reject/float_lit_overflow_neg.ty --parse
    tychoc1: line 2: float literal out of range: `1e400` exceeds the largest
    float (IEEE-754 binary64); write 1.0/0.0 for an infinity          (was: PARSE-OK 1)
    ```

    The BEFORE column is measured, not remembered: a compiler built from
    `git archive HEAD compiler` accepts all three with exit 0.

    **The boundary control** — a refuse-everything check scores full marks
    without it. `9223372036854775807`, `0x7FFFFFFFFFFFFFFF`, the 63-bit binary
    form, `1.7976931348623157e308` and `1e-400` all still parse (`PARSE-OK 1`),
    and one past each boundary is refused with the message `./tychoc` prints
    verbatim:

    ```
    9223372036854775808   tychoc1(1): integer literal out of range | tychoc(1): integer literal out of range
    0x10000000000000000   tychoc1(1): integer literal out of range | tychoc(1): integer literal out of range
    1e309                 tychoc1(1): float literal out of range: `1e309` ... | tychoc(1): float literal out of range: `1e309` ...
    ```

    **Item 2 — `# deprecated:` notes.** `dnote_of` is `src/tychoc.c@dnote_scan`
    verbatim, called only from the comment-ONLY line branch, so a trailing
    comment can never carry a note. `dnote_above` is the line-above lookup.

    ```
    $ ./tychoc1 corelib/sort/sort.ty --dump-dnotes
    109 deprecated: use sort_by(xs, cmp) -- by_key(xs, k) is sort_by(...). Removed in 1.0.
    attached fn by_key: use sort_by(xs, cmp) -- ...
    $ ./tychoc1 corelib/decimal/decimal.ty --dump-dnotes
    32 deprecated: use from_str_checked(s) -- ...
    attached fn from_str: use from_str_checked(s) -- ...
    ```

    A five-case probe pins the two false-positive/false-negative shapes
    `make grid-check` gates (FRICTION #46, #47): the note is found and trimmed
    at both ends, prose that merely MENTIONS `deprecated:` mid-sentence is not a
    note, a TRAILING comment is not a note, and an empty tail is no note at all.

    **Item 3 — the recovery batches.** The decl-keyword site records the
    diagnostic and puts `bracket_depth` back to 0 so real layout resumes; every
    recorded error is printed together at the end.

    ```
    $ ./tychoc1 <probe with two unclosed brackets> --parse
    tychoc1: line 2: unclosed '(' or '[' opened on line 2 -- `fn` here can only start a declaration, ...
    tychoc1: line 6: unclosed '(' or '[' opened on line 6 -- `fn` here can only start a declaration, ...
    ```

    Before: **one** error, line 2, then exit. `./tychoc` on the same file prints
    those two first, then two parser errors that are Phase 9's work.

    **Negative controls, each observed and reverted.** The first one did not
    apply — deleting the guard left `imax` unused, the build failed, the lane
    ran the STALE `tychoc1` and reported `all green`. That green was the
    control lying, not the fix working (tycho-verify §3, §4), and it is why the
    build line is checked before every verdict below.

    | control | observed |
    |---|---|
    | delete the int accumulator guard | `parse-check` exit 1, `rejected=45 missed=2`, both int fixtures named |
    | float `Overflow` arm -> `fv = 0.0` | `parse-check` exit 1, `rejected=46 missed=1`, the float fixture named |
    | delete the `dnote_of` call | `--dump-dnotes` empty on the probe AND on `corelib/sort/sort.ty` |
    | recovery no longer clears `bracket_depth` | line 2 reported three times, line 6 never — the bracket stays open and the file never re-lexes |

    The two halves of item 1 redden the lane independently.

    **`compiler/run.sh` leg 2's expectation was updated, deliberately and
    stated here.** It named the three misses by NAME, so fixing them reddened
    it. The exemption was not widened: the `KNOWN` list is DELETED and replaced
    with `[ "$sa" = 0 ]`, so any accepted SYNTAX fixture now fails the lane —
    a strictly tighter check than the set comparison it replaces. Both int
    controls above are the proof that the new leg can still fail.

    ```
    leg1  tests/*.ty: files=274 parse-ok=274 fail=0
    leg1b corelib/**.ty: files=91 parse-ok=91 fail=0
    leg2  tests/reject/*.ty: SYNTAX=47 rejected=47 missed=0 | SEMANTIC=290 accepted=290 wrongly-rejected=0
    leg3  census: 120 kinds, 83576 nodes over 365 files
    parse-check: all green
    ```

    **The Phase 2 legs are unmoved.** Over the same 365-file corpus, run under
    `/bin/sh` because zsh does not word-split an unquoted `$FILES` and a first
    attempt silently scored one "file":

    ```
    corpus=365 round-trip: ok=365 fail=0 | dump identical to HEAD tychoc1 on 365 of 365
    ```

    The dump leg is stronger than a count: every token line is compared
    byte-for-byte against a compiler built from `git archive HEAD compiler`.
    `compiler/census.expected.out` is **unchanged** — `cmp` matched and the file
    is absent from `git diff --stat`.

- [ ] **Phase 2b-1 — `src/tychoc.c` has the unclosed-bracket recovery block twice**
  - Found while porting item 3. The `decl_kw` recovery appears at
    `src/tychoc.c:527` and again at `src/tychoc.c:552`, character-identical
    apart from a comment. The second is DEAD: the first sets `bracket_depth = 0`,
    so the second's `bracket_depth > 0` guard can never hold. `grep -c 'decl_kw\[\] = '`
    is 2.
  - Scope: `src/tychoc.c` only. Delete one block.
  - Done when: one block remains and the two-unclosed-bracket probe prints the
    same two diagnostics it prints today.
  - Verify: `make test`, and re-anchor citations (`scripts/reanchor_citations.py`)
    — every `src/tychoc.c` edit moves 60-140 refs.

- [x] **Phase 3 — parser: expressions and statements**
  - Scope: `compiler/parse/`, `compiler/ast/`. Full expression grammar with
    precedence, `if`/`elif`/`else`, `for`/`in`, `for <cond>:` (Tycho has no
    `while` — the conditional loop is `for` and the compiler says so by name,
    found in Phase 1), `match`, `select`,
    `spawn`, `return`/`break`/`continue`, destructuring, expression-valued
    `if`/`match`.
  - Done when: every `tests/*.ty` that `./tychoc` accepts parses without error,
    and every `tests/reject/` fixture that is a *syntax* rejection is rejected.
  - Verify: the parse sweep over both corpora, with the accept and reject counts
    printed separately; a reject leg that cannot fail is decoration, so show one
    deliberate break reddening it.
  - **Done 2026-08-22.** `compiler/ast/ast.ty` (10 `Ty`, 35 `Expr`, 19 `Stmt`
    forms), `compiler/ast/census.ty`, `compiler/parse/parse.ty`. The grammar is
    read out of `src/tychoc.c`'s parser, not inferred: precedence is
    postfix > unary > `* / % << >> &` > `+ - | ^` > `is` > comparisons+`in` >
    `not` > `and` > `or`, matching `src/tychoc.c@parse_mul` through
    `src/tychoc.c@parse_expr`. Nothing is RESOLVED — `src/tychoc.c`'s
    `parse_type` consults the struct/enum/const tables as it parses and this one
    cannot, which is what the reject split below is about. `--parse` and
    `--parse-census` are at `compiler/main.ty@mode`.

    **Leg 1 — the accept corpus.** `./tychoc` accepts all 274, measured first,
    so the leg is not comparing against an empty set:

    ```
    tychoc accepts 274 of 274
    leg1 tests/*.ty: files=274 parse-ok=274 fail=0
    leg1b corelib/**.ty: files=91 parse-ok=91 fail=0
    ```

    **Leg 2 — the reject corpus, split.** *"Everything in `tests/reject/` was
    rejected"* is the failure mode this leg exists to catch: most of those
    fixtures are TYPE or SEMANTIC rejections that a parser must ACCEPT. The
    split is grounded in `src/tychoc.c` rather than in tychoc1's own behaviour:
    every `die_at`/`die` format string in that file is extracted with its line
    number, each fixture's real `./tychoc` message is matched back to the most
    specific format that produced it, and the SITE decides the class —
    **SYNTAX** if it sits in the lexer or inside `parse_program`'s reach and
    needs no symbol table, **SEMANTIC** otherwise, including the parse-region
    sites that DO need one (`unknown type`, `is already defined`, a `where`
    predicate checked against the typaram list, an affine container rule). Six
    messages are assembled by a helper or carry a backslash the extractor cannot
    reproduce and are classified by hand with the reason recorded.

    ```
    leg2 tests/reject/*.ty: SYNTAX=47 rejected=44 missed=3 | SEMANTIC=290 accepted=290 wrongly-rejected=0
    ```

    The three misses are **one gap, already an open phase**: `int_hex_overflow`,
    `int_literal_overflow` and `float_lit_overflow_neg`, all the literal-range
    check Phase 2b item 1 records as deferred. Nothing else in the 47 is missed
    and nothing in the 290 is wrongly rejected.

    **Leg 3 — an AST node-kind census**, because an accept/reject count is blind
    to a parse that SUCCEEDS with the wrong tree. 120 kinds, 83,576 nodes over
    the same 365-file corpus; the full table is reproducible with
    `./tychoc1 <file> --parse-census`. Spot values:

    ```
    e.Call=12749 e.CallQual=1928 e.Name=13673 e.IntLit=8564 e.StrLit=5464
    e.Binop:+=5324 e.Binop:===817 e.Named=9  s.Decl=3535 s.ExprStmt=3771
    s.ReturnS=2096 s.IfS=1218 s.For3=310 s.WhileS=204 s.MatchS=255 s.SelectS=5
    t.TPrim=3943 t.TName=935 t.TParam=672 t.TArr=393 t.TFix=354 t.TSoa=11
    d.fn=1506 d.struct=198 d.enum=57 d.subscript=4 d.variadic=4
    ```

    **Leg 4 — three negative controls, each observed, each reverted** (the
    census returns byte-identical to the baseline afterwards):

    | break | effect |
    |---|---|
    | drop the `elif` chain from `ifstmt` | leg1 **272/274**, leg1b **73/91**, and one SEMANTIC fixture wrongly rejected |
    | drop the four removed-`map_*` refusals from `primary` | legs 1 and 3 stay **green**; leg2 SYNTAX rejected **44 -> 40**, naming all four `map_*_removed` fixtures |
    | drop the `ast.Named` wrapper from a `name: value` call argument | legs 1 and 2 stay **fully green**; the census loses `e.Named=9` |

    The third is the one that matters: the same tokens are consumed, the
    accept/reject verdict is identical on all 611 files, and the argument NAME is
    silently gone — exactly what the two legs the brief named cannot see.

    **A control that did NOT redden, recorded rather than hidden:** disabling the
    reserved-keyword check in `stmt` (`'for' cannot be used as a variable name`)
    moved no leg. Those fixtures die a second way — the following `_ident` call
    refuses the keyword — so that guard is redundant on this corpus. It is kept
    because `src/tychoc.c` has it and Phase 9 pins message text.

    **Phase 1 and Phase 2 are unmoved**: `./tychoc1` on `fn main(): print(str(1))`
    still differs from `./tychoc`'s C by the same five blank lines, and
    `--dump-tokens` / `--round-trip` are still 365/365 with 0 failures.
    `compiler/emit/emit.ty` was adapted to the widened AST (it now carries a `_`
    arm that dies "codegen lands in Phase 7"); `compiler/driver/` and
    `compiler/main.ty` gained the two modes and nothing else.

- [x] **Phase 3b — the parse sweep is not a runnable lane**
  - Scope: a `run.sh` (or a `scripts/` entry) for the Phase 3 legs. The sweep and
    the SYNTAX-vs-SEMANTIC classifier that produced the evidence above live in a
    scratch directory, so nothing in the tree can redden when the parser
    regresses — and the classification is the expensive half to rebuild.
  - Done when: the three legs run from the repo, with the classifier's table
    committed so the split is auditable rather than recomputed.
  - Verify: the lane green at 274/91/47/290, plus one of Phase 3's three
    controls reddening it.
  - Do NOT run: any test lane. Nothing outside the new script is touched.

  - **Done 2026-08-23.** `compiler/run.sh` (`make parse-check`), the committed
    split in `compiler/reject_class.tsv` (337 rows), the census golden
    `compiler/census.expected.out`, and the generator
    `scripts/classify_rejects.py` recovered from scratch. The lane rebuilds
    `tychoc1` through its Makefile dependency, so a compiler that no longer
    BUILDS cannot present as green — hit once during this phase, when a first
    attempt at the control failed to compile and `sh compiler/run.sh` scored
    the stale binary all green.

    ```
    $ make parse-check
    leg1  tests/*.ty: files=274 parse-ok=274 fail=0
    leg1b corelib/**.ty: files=91 parse-ok=91 fail=0
      SYNTAX-NOT-REJECTED tests/reject/float_lit_overflow_neg.ty :: float literal out of range: ...
      SYNTAX-NOT-REJECTED tests/reject/int_hex_overflow.ty :: integer literal out of range
      SYNTAX-NOT-REJECTED tests/reject/int_literal_overflow.ty :: integer literal out of range
    leg2  tests/reject/*.ty: SYNTAX=47 rejected=44 missed=3 (KNOWN 3) | SEMANTIC=290 accepted=290 wrongly-rejected=0
    leg3  census: 120 kinds, 83576 nodes over 365 files
    parse-check: all green
    ```

    The three misses are the literal-range check Phase 2b owns. They are
    KNOWN by NAME, not by count: the lane compares the missed set against
    three literals, so a new miss reddens and so does a fixed one.

    **Cost: ~2.5 s** (2.84 / 2.47 / 2.46 s, three runs, first cold).

    **The control — the `ast.Named` one, because it is the only one of Phase
    3's three that the accept/reject legs cannot see.** Dropping the wrapper
    from a `name: value` call argument in `_args`:

    ```
    leg1  tests/*.ty: files=274 parse-ok=274 fail=0
    leg1b corelib/**.ty: files=91 parse-ok=91 fail=0
    leg2  tests/reject/*.ty: SYNTAX=47 rejected=44 missed=3 (KNOWN 3) | SEMANTIC=290 accepted=290 wrongly-rejected=0
    leg3  census: 119 kinds, 83567 nodes over 365 files
    parse-check: the AST census moved -- the tree changed shape, not just the verdict
    72d71
    < e.Named=9
    parse-check: FAILED
    make: *** [Makefile:167: parse-check] Error 1
    ```

    Legs 1, 1b and 2 stayed fully green on all 611 files. Reverted; the census
    is back at 120 kinds / 83576 nodes and the lane is green.

    Two gates my diff can move, both run: `make goldens-check` ok (519 golden
    files, all tracked, none ignored — it required the `!/compiler/*.out`
    un-ignore, without which the census golden is green on disk and absent
    from a fresh clone) and `make script-check` ok (25 .py, 96 .sh).
    `python3 scripts/check_citations.py` ok.

- [x] **Phase 4 — parser: declarations**
  - Scope: `compiler/parse/`. `fn`, `struct`, `enum`, `newtype`, `handle`,
    `soa`, `bounded[N]T`, generics with `$T` and `where` (comma-separated, five
    closed predicates), `import`, `package`, `const`, `extern`, `subscript`,
    `sink`/`inout`, `# deprecated:`.
  - Done when: the whole tree parses — `tests/`, `corelib/`, `tools/`,
    `examples/`, `server/`, `bench/` — matching `./tychoc`'s accept/reject
    verdict on every file.
  - Verify: a per-file verdict differential against `./tychoc --emit-c`
    (parse-only comparison: both accept, or both reject), zero disagreements.

  - **Done 2026-08-23.** Corpus stated first: the new ground was **179** `.ty`
    files (`tools` 57, `examples` 70, `server` 1, `bench` 51) on top of Phase 3's
    274 + 91. The verdict differential is committed as
    `compiler/verdict_diff.py`, run as **leg5** of `make parse-check`, and
    covers **1,078** files — every `.ty` under the six roots, including the
    `tests/conc`, `tests/diag` and `tests/reject/pkg` subtrees no other leg
    reaches. It is the leg that found three of the four verdict defects.

    ```
    $ make parse-check
    leg1  tests/*.ty: files=274 parse-ok=274 fail=0
    leg1b corelib/**.ty: files=91 parse-ok=91 fail=0
    leg1c tools+examples+server+bench: files=179 parse-ok=179 fail=0
    leg2  tests/reject/*.ty: SYNTAX=57 rejected=57 missed=0 | SEMANTIC=280 accepted=280 wrongly-rejected=0
    leg3  census: 122 kinds, 182646 nodes over 544 files
    leg4  declaration rules: refused=5/5 accepted=5/5
    leg5  whole-tree verdicts: files=1078 tychoc(accept=552 semantic=452 syntax=74) disagreements=0
    parse-check: all green
    ```

    **Cost: ~11.0 s** (11.17 / 10.85 / 10.95 s), up from ~2.5 s; leg1c, leg4 and
    leg5 are the added ~8.5 s.

    Four verdict defects, each found by the differential and each fixed:

    - a subscript written as a PLACE (`b.grid.at(r, col) = v`,
      `tools/tycho-grid/main.ty:88`) was refused. `src/tychoc.c:4136` allows
      `E_CALL` as an assignment target, and its one `E_CALL` covers both the
      named and the indirect call, which split in this AST into `Call` and
      `CallVal`. Only `Call` was listed. The single failure in the 179.
    - eleven declaration rules `src/tychoc.c` enforces at parse time were not
      enforced at all: a variadic that is also `inout`/`sink`; `sink` or `...`
      on an `extern` parameter; `inout`/`sink` on a `subscript` parameter;
      `where` on a non-generic fn; a `where` predicate outside the closed five;
      a `where` naming a type parameter the signature does not have, in both the
      predicate and the type-set form; a `const` that does not fold to one
      scalar literal, at top level (backward refs resolved) and locally (not);
      and the two structural subscript rules — the yielded place must be rooted
      in a parameter, and no parameter may appear in it twice.
    - `parallel(65)` and `parallel(0)` were accepted. `src/tychoc.c:3883`
      range-checks a LITERAL width at parse time.
    - `parallel for x in EXPR` accepted a non-name source. `src/tychoc.c:4024`
      requires a variable, because a channel cannot be aliased into a temp.

    **The reject split moved 47/290 to 57/280, and that is a correction, not a
    regression.** Nine fixtures classified SEMANTIC by Phase 3b were refused the
    moment the parser learned these rules. Re-reading each site: the `where`
    predicate set is five fixed names, the type-parameter list is what the
    signature just read, the subscript place rules are structural, and a `const`
    is folded by `src/tychoc.c@const_fold` at parse time. **None reaches a symbol
    table.** Eight entries were removed from `scripts/classify_rejects.py`'s
    `NEEDS_SYMBOLS` and the table regenerated; a tenth fixture
    (`tests/reject/const_expr_localref.ty`) moved to SYNTAX with it and forced
    the local-const fold, which `src/tychoc.c:3752` runs with backward refs OFF
    — so `const D = K * 2` is legal at top level and refused in a body.

    Two more classifier corrections came from leg5: package privacy and the
    foreign-variant spelling ARE semantic (`is_imported_pkg`, the variant table)
    and were being scored SYNTAX; the unclosed-bracket and no-package messages
    are built by `snprintf`/`fprintf` and matched no format string, which the
    differential reports as a failure rather than skipping.

    **Eight negative controls, each observed and reverted.** C1 the variadic
    marker rule, C2 `extern` taking `sink`, C3 `subscript` taking `inout` — each
    reddened its OWN leg4 probe and nothing else (`refused=3/5`, `4/5`, `4/5`).
    C4 the open `where` predicate set, C5 the unfolded top-level `const`, C6 the
    unchecked subscript root — each reddened leg2 by name and nothing else. C8
    the unchecked `parallel for` source, run through leg5, took it to
    `disagreements=1` naming `tests/diag/parfor_expr_source.ty`.

    **C7 is the one the verdict differential cannot see**, which is why it is
    here: dropping the `# deprecated:` attachment left legs 1, 1b, 1c, 2 and 4
    fully green over all 1,090 files, and only the census moved.

    ```
    leg1  tests/*.ty: files=274 parse-ok=274 fail=0
    leg1b corelib/**.ty: files=91 parse-ok=91 fail=0
    leg1c tools+examples+server+bench: files=179 parse-ok=179 fail=0
    leg2  tests/reject/*.ty: SYNTAX=57 rejected=57 missed=0 | SEMANTIC=280 accepted=280 wrongly-rejected=0
    leg3  census: 121 kinds, 182643 nodes over 544 files
    parse-check: the AST census moved -- the tree changed shape, not just the verdict
    < d.deprecated=3
    leg4  declaration rules: refused=5/5 accepted=5/5
    parse-check: FAILED
    ```

    The census golden was re-recorded for three reasons, all intended: the
    corpus grew from 365 to 544 files, `d.deprecated` is a new kind, and a
    top-level `const` now holds its FOLDED literal, which is what drops
    `e.Binop:*` and its siblings.

    leg4 exists because eleven of these rules had no fixture anywhere. Its five
    refusals are each paired with an accepting twin one token away, and all ten
    were checked against `./tychoc` first — five REJECT, five accept, matching.
    The `ok_subscript` twin is also the only program in either corpus that
    writes through a subscript, which is the `CallVal` place form above.

    Three gates my diff can move, all run: `make goldens-check` ok (519 golden
    files, all tracked), `make script-check` ok (25 .py, 96 .sh — it covers the
    new `compiler/verdict_diff.py`), `python3 scripts/check_citations.py` ok.

- [x] **Phase 5 — `types/`: names, packages, scoping**
  - Scope: `compiler/types/`. Package and import resolution, the function table,
    variable scoping, const folding, corelib discovery.
  - Done when: every name in the tree resolves to the same declaration
    `./tychoc` resolves it to, and undefined-name rejections agree.
  - Verify: a symbol-table dump differential over `corelib/` and `tools/`,
    plus the reject corpus for undefined names.

  `compiler/types/load.ty` walks the package graph (corelib root, per-file
  imports, post-order, cycle detection) and `compiler/types/resolve.ty` builds
  the global table and resolves every use against the scope stack.
  `--resolve` / `--resolve-census` / `--dump-symbols` expose it.

  **What it is differentiated AGAINST, stated plainly.** `./tychoc` has no
  symbol-table dump, so option (b) was taken: its own DIAGNOSTICS.
  `scripts/classify_rejects.py` already extracts every `die_at` format string
  from `src/tychoc.c` with its line and matches a fixture's real message back to
  the site that emitted it; Phase 5 adds a third class, NAME, for the sites the
  symbol table and the scope stack decide alone. **The class is decided by the
  SITE, never by what tychoc1 does** — otherwise the leg passes by construction.
  NAME splits SEMANTIC only, so Phase 4's SYNTAX boundary is untouched:
  57 SYNTAX / 27 NAME / 253 SEMANTIC (was 57 / 280).

  What that cannot see: it compares a VERDICT and the identifier a message
  names, never which declaration was chosen. Leg 7 exists for exactly that
  (below), and it is a self-recorded golden, like leg 3.

  Also deferred by design, and the reason this pass can only ever accept MORE
  than `./tychoc`: a method call `x.f()` on a value (UFCS needs the receiver's
  type) and every type NAME (`unknown type` stays SEMANTIC, Phase 6).

  ```
  $ make parse-check            # 15.5 s, was 11.0 s at Phase 4
  leg1  tests/*.ty: files=274 parse-ok=274 fail=0
  leg1b corelib/**.ty: files=91 parse-ok=91 fail=0
  leg1c tools+examples+server+bench: files=179 parse-ok=179 fail=0
  leg2  tests/reject/*.ty --parse: SYNTAX=57 rejected=57 missed=0 | NAME+SEMANTIC=280 accepted=280 wrongly-rejected=0
  leg2b tests/reject/*.ty --resolve: SYNTAX+NAME=84 rejected=84 missed=0 | SEMANTIC=253 accepted=253 wrongly-rejected=0
  leg3  census: 122 kinds, 182646 nodes over 544 files
  leg7  resolution census: 4011 distinct targets, 165333 resolved uses
  leg4  declaration rules: refused=5/5 accepted=5/5
  leg5  whole-tree verdicts: files=1078 tychoc(accept=552 semantic=403 name=49 syntax=74) disagreements=0
  leg6  whole-tree resolution: disagreements=0 unused-local/import on an accepted file=0
  parse-check: all green
  ```

  leg6 is the whole-tree half: the same 1,078 files scored a second time under
  `--resolve`, plus the assertion that a file `./tychoc` ACCEPTS resolves with
  no unused local and no unused import. leg7 is the resolution census — every
  use printed as the package-mangled declaration it resolved to.

  **The `late` split is not cosmetic.** `declared and not used` and
  `imported and not used` are drained by `src/tychoc.c` only after resolve
  succeeds, so a resolver with no type checker reaches them on programs
  `./tychoc` never does. Making them part of the verdict rejected 21 SEMANTIC
  fixtures whose real refusal is a type error — a rejection for the wrong
  reason. They are kept, printed as `late:`, and gated on the accept corpus,
  where `./tychoc` does reach the end and the count must be 0.

  **Seven negative controls, each applied to `compiler/types/resolve.ty`,
  observed, and reverted.** The first two are the class the brief asked for: a
  name resolved to the WRONG declaration with every verdict leg green.

  ```
  N1 const looked up BEFORE the local scope
     leg2 / leg2b / leg5 / leg6 ALL GREEN, only leg7 moved
     parse-check: the RESOLUTION census moved -- a name resolves to a different declaration
  N2 an unqualified call tries the BARE name before the package-local one
     leg2 / leg2b / leg5 / leg6 ALL GREEN, only leg7 moved
  N3 `_` in a match pattern pushed as a binding instead of a discard
     leg2b SYNTAX+NAME rejected=83 missed=1 | leg6 disagreements=1 | leg7 moved
  N4 the builtin-Sig collision check deleted
     leg2b GREEN, leg7 GREEN, leg6 disagreements=3   <- leg6 alone catches it
  N5 f-string interpolations no longer marked as uses
     leg6 unused-local/import on an accepted file=9 | leg7 moved
  N6 `late` made fatal again
     leg2b SEMANTIC accepted=232 wrongly-rejected=21
  N7 the import ALIAS dropped from pkg_prefix_for
     leg6 disagreements=1
  ```

  N4 is the one worth reading twice: deleting a rule left both reject corpora
  and both censuses green, and only the whole-tree leg reddened — because the
  three files it catches (`corelib/bignum/bignum.ty` with its `fn pow`,
  `corelib/regex/regex.ty` with `fn find`, `tests/diag/shadow_builtin_main.ty`
  with `fn die`) are only refused when compiled as an ENTRY file, where their
  names are unprefixed. No corpus in legs 1-4 compiles a corelib package as an
  entry point.

  Two behaviours were MEASURED against `./tychoc` rather than inferred, each
  with its own probe, because the source did not say and a guess would have
  refused legal code: an unused local inside a `$T` function is ACCEPTED (a
  generic template body is never resolved as written) while the same local in a
  plain fn is refused; and `bench/trie/trie.ty` + `trie_pool.ty` are both
  REFUSED by `./tychoc` (`'Trie' is already defined` — two `package main`
  programs sharing a directory), so tychoc1 refusing them is agreement, not a
  regression.

  Three gates my diff can move, all run: `make goldens-check` ok (520 golden
  files, all tracked — `compiler/rcensus.expected.out` is the new one and
  `.gitignore:120` already un-ignores it), `make script-check` ok (26 .py,
  96 .sh), `python3 scripts/check_citations.py` ok.

> **Phase 6 was split on 2026-08-23, before dispatch.** As written it covered
> `src/tychoc.c:5716-9350` — resolve, inference, generics, newtypes, affine,
> `where`, `bounded` — in one brief. That is not independently completable or
> verifiable in one pass, and a phase that stalls halfway commits nothing. The
> split is by what the verdict differential can score separately.

- [x] **Phase 6a — `types/`: the monomorphic type checker**
  - Scope: `compiler/types/`. Type resolve and inference over the non-generic
    language: scalars, `string`/`bytes`, arrays, maps, structs, enums, tuples,
    `Option`/`Result`, function signatures, operators, indexing, field access,
    `match` exhaustiveness, assignment and return compatibility.
  - Done when: every file in the tree that uses no generic, newtype, handle or
    `bounded` gets the same accept-vs-reject verdict as `./tychoc`, and the
    SEMANTIC fixtures whose refusal is a monomorphic type error are refused.
  - Verify: the lane's verdict differential with a TYPE class added to
    `compiler/reject_class.tsv` the way Phase 5 added NAME — decided by the
    `die_at` site in `src/tychoc.c`, never by tychoc1's behaviour. `leg2b
    wrongly-rejected=0` is the leg that matters more than the new refusals.
    State the new SYNTAX/NAME/TYPE/SEMANTIC split.

  **Done 2026-08-23.** `compiler/types/tcheck.ty` (~1,100 lines), `--typecheck`
  and `--type-census` at `compiler/main.ty@mode`. A type is a STRING and `?` is
  UNKNOWN: every rule fires only where `compat()` genuinely disagrees, so a
  generic instantiation, a UFCS method or a `$T` value is `?` and refuses
  nothing. That is what makes `wrongly-rejected=0` structural rather than lucky.

  **The split, decided by the `die_at` SITE and never by tychoc1's behaviour**
  (`scripts/classify_rejects.py@TYPE_SITES`, with `TYPE_EXCLUDE` for the one
  format whose site is inside `instantiate_generic`):
  `python3 scripts/classify_rejects.py compiler/reject_class.tsv` ->
  `classified 337 {'TYPE': 147, 'SEMANTIC': 103, 'SYNTAX': 57, 'NAME': 30}`,
  from 57 / 30 / 250. Left SEMANTIC with the reason recorded at the list: the
  generic, newtype, affine, `bounded`, `where`, subscript, extern-C-ABI, sink
  consume and "cannot infer ..." families, each a rule family of its own.

  ```
  $ make parse-check                      # ~20.3 s (20.48 / 20.13 / 20.15), was 15.5 s
  leg2c tests/reject/*.ty --typecheck: SYNTAX+NAME+TYPE=234 rejected=230 missed=4 (KNOWN 4) | SEMANTIC=103 accepted=103 wrongly-rejected=0
  leg9  type census: 1666 distinct types, 117612 inference sites, 2590 deferred to Phase 6b
  leg5  whole-tree verdicts: files=1078 tychoc(accept=552 semantic=135 name=151 type=166 syntax=74) disagreements=0
  leg10 whole-tree typecheck: disagreements=0 (9 TYPE files known-missed)
  leg11 TYPE diagnostic file:line vs ./tychoc: scored=157 agree=157 disagree=0 unlocated=0
  leg12 files whose program uses a generic/newtype/handle/bounded: 202 of 552 accepted
  parse-check: all green
  ```

  legs 1/1b/1c/2/2b/3/4/4b/7/8 are unmoved and both older goldens are
  UNCHANGED (`git status` shows neither `census.expected.out` nor
  `rcensus.expected.out` modified); `compiler/tcensus.expected.out` is the one
  new golden.

  **Nothing is excluded by FILE.** The brief's exclusion count is leg12 instead:
  202 of the 552 accepted programs contain a generic, newtype, handle or
  `bounded`, and 2,590 of the 117,612 inference sites are `?`. Both are printed
  every run and are Phase 6b's numbers to drive to zero.

  **The four TYPE fixtures not refused are KNOWN BY NAME**, compared as a SET so
  a new miss reddens and so does a fixed one: three need generic instantiation
  (`generic_bounded_field_degraded`, `generic_inst_inout_fnvalue`,
  `generic_params_17`), and `len_scalar.ty` writes `len(x)` INSIDE an f-string,
  whose interpolations the parser still keeps as raw text. `verdict_diff.py`
  carries five more of the first kind under `tests/diag` and
  `tests/reject/pkg`, which leg2c's corpus does not reach.

  **Five negative controls, each observed and reverted.** C3c is the one the
  phase was written to find:

  | control | what reddened |
  |---|---|
  | C1 the if-condition rule disabled | leg2c missed 4 -> 8, the miss-SET check, leg10 disagreements=4 |
  | C2 enum exhaustiveness disabled | leg2c missed 4 -> 5 (`match_non_exhaustive`), leg10=2 |
  | C3 an unknown operand no longer poisons the result (`f * 2` at an unknown `f` read as int -- the real defect, found by corelib/test/toml) | leg9 moved 277 sites; leg2/2b/2c all green, leg10 found 1 file |
  | **C3c `str(x)` answers `?` instead of `string`** | **leg9 ALONE**: 3,098 call results and 6,644 downstream sites re-typed while legs 1, 1b, 1c, 2, 2b, 2c, 5, 6, 8, 10 and 11 stayed fully green over all 1,078 files |
  | C4 `main`'s two signature rules lose their line | leg11 disagree=6, every verdict leg green |
  | C5 the literal-adaptation rule deleted (`x: u32 = 1`) | leg10 disagreements=6 -- the accept side can redden |

  Two real defects the legs found while building it, both of the "green verdict,
  wrong answer" class: the operand poison above, and `keys(m)`/`push` needing the
  expected type threaded into the argument. One ordering fact was measured rather
  than assumed: the shape-directed builtins resolve BEFORE the Sig table
  (`src/tychoc.c:6740-6990` precedes its `sig_find`), without which a user
  `fn len` in the entry package captured the bare `len(b)` inside `core:strings`
  and refused a program `./tychoc` accepts.

  Three gates my diff can move, all run: `make goldens-check` ok (521 golden
  files, all tracked), `make script-check` ok (26 .py, 96 .sh),
  `python3 scripts/check_citations.py` ok.


- [ ] **Phase 6b — `types/`: generics, newtypes, affine, `where`, `bounded`**
  - Scope: `compiler/types/`. Generic inference and monomorphisation, explicit
    type arguments, the five closed `where` predicates, newtype distinctness
    across a package boundary, the affine rules for handle/task/channel, and
    `bounded[N]` capacity.
  - Done when: `tychoc1` agrees with `./tychoc` on accept-vs-reject for every
    file in `tests/`, `tests/reject/` and `corelib/` — the whole Phase 6
    condition, now reachable.
  - Verify: the verdict differential, counts printed; **and the affine refusals
    checked individually**, since a checker that refuses everything scores the
    same as a correct one on a reject corpus. `tools/tycho-ledger/run.sh` and
    `tools/tycho-fh/run.sh` are the two lanes in this tree whose subject is
    exactly this — read what they assert before writing the legs.

- [ ] **Phase 7 — `lower/` + `emit/`: core codegen**
  - Scope: `compiler/lower/`, `compiler/emit/`. Arena scoping, structs, enums,
    arrays, strings, control flow, functions, the runtime ABI.
  - Done when: `TYCHOC=./tychoc1 make test` reaches a stated pass count, climbing
    from Phase 1's one program. State the number; a count that moves down is a
    regression.
  - Verify: `TYCHOC=./tychoc1 make test`, count recorded in the commit message.

- [ ] **Phase 8 — codegen: maps, soa, generics, affine, concurrency**
  - Scope: `compiler/emit/`. Compact-dict maps, `soa`, monomorphised generics,
    handle destructors, `spawn`/`channel`/`select`, `parallel for`.
  - Done when: `TYCHOC=./tychoc1 make test` is green at the same count as
    `./tychoc` — 719 fixtures at the last measurement, to be re-measured.
  - Verify: both runs, counts compared; plus `make corelib` under the override.

- [ ] **Phase 9 — diagnostics wording**
  - Scope: `compiler/` diagnostics. Goldens in `tests/` pin message text,
    batching order and the second "declared here" location.
  - Done when: every golden that contains compiler output matches byte for byte
    under `TYCHOC=./tychoc1`.
  - Verify: the golden comparison, with the count of message-bearing goldens
    stated so a silent shrink is visible.

- [ ] **Phase 10 — `TYCHOC` plumbing, fixpoint, release story**
  - Scope: the 3 inline `./tychoc` sites named in Pre-flight; the `TYCHOC=`
    line in each of the 60 `run.sh` files -> `TYCHOC="${TYCHOC:-./tychoc}"`;
    `make tychoc1` self-build; docs for the bootstrap decision.
  - Done when: the fixpoint holds — `tychoc1` built by tychoc, self-built twice,
    the two emitted `.c` byte-identical — and no `run.sh` reaches `./tychoc`
    inline.
  - Verify: `cmp` on the two self-built `.c`; and
    `grep -rn '\./tychoc\b' --include=run.sh .` returning only `$TYCHOC`
    assignments.

- [ ] **Phase 11 — `make clean` does not remove `tychoc1`** (found in Phase 1,
  out of scope there)
  - Scope: `Makefile:387`, the first `rm -f` line of `clean`. It names `tychoc`
    and every other built binary; `tychoc1` was added at `Makefile:28-34` and
    is not in that list, so `make clean && make` leaves a stale `tychoc1`
    behind — which is exactly the artifact a fixpoint check must not inherit.
  - Done when: `make clean` removes `tychoc1`.
  - Verify: `make tychoc1 && make clean && test ! -e tychoc1`.
  - Do NOT run: any test lane. This is one word in a `rm -f` line.

- [ ] **Phase 4b — `CONTRIBUTING.md`'s gate table has no `parse-check` cost**
  (found in Phase 4). Phase 3d already notes the row is missing entirely. The
  figure has since moved: the lane is ~15.5 s, not the ~2.5 s Phase 3b measured,
  because legs 1c, 4, 5, 2b, 6 and 7 were added. Whoever writes that row writes
  15.5 s and the eight legs, not the two-leg description.
  - Do NOT run: any test lane. It is a Markdown edit; the two doc gates only.

- [ ] **Phase 3c — `scripts/check_goldens.py` carried a stale NO_GOLDEN entry**
  - Found while wiring Phase 3b: `NO_GOLDEN` still excused `compiler/run.sh`
    ("a differential, not a golden") for a file `603d8fbd` deleted wholesale.
    The entry was removed in Phase 3b's commit, because the new lane could not
    go green under it — the gate itself printed "is in NO_GOLDEN but the scan
    found a golden -- remove the entry", which is the gate working.
  - Scope: audit the other 19 `NO_GOLDEN` entries for runners that no longer
    exist. Nothing else in the tree checks that list against `git ls-files`.
  - Verify: `make goldens-check` (~0.08 s) and nothing else.

- [ ] **Phase 3d — `CONTRIBUTING.md`'s gate table has no `parse-check` row**
  - Phase 3b added the row to the gitignored `CLAUDE.md` only, which its scope
    lock named. The tracked contributor copy ("Which gate for which change") is
    what a fresh clone reads, and it is now one gate behind.
  - Scope: one trimmed row in `CONTRIBUTING.md`. No internal history.
  - Verify: `python3 scripts/check_citations.py` and `sh scripts/check_links.sh`.
    Nothing else — it is a Markdown edit.

- [x] **Phase 5b — the AST carries no line numbers** (found in Phase 5, out of
  scope there)
  - `ast.Fn`, `ast.StructD` and almost every `ast.Expr` variant have no line
    field, so every diagnostic `compiler/types/` emits names the FILE and no
    line — `src/tychoc.c` names `file:line` for all of them. Phase 9 pins
    message text byte for byte and cannot be met without this.
  - Scope: a `line` field on the declaration structs and on the expression and
    statement nodes that can carry a diagnostic, set by `compiler/parse/`.
  - Verify: `make parse-check`. Expect leg3's census to be UNCHANGED — a field
    is not a node kind — and say so if it moves.
  - Do NOT run: `make test`. Nothing here reaches `src/tychoc.c`.

  - **Done 2026-08-23.** Every `ast.Expr` and `ast.Stmt` variant carries its
    source line as its FIRST payload element, every declaration struct carries
    one as its LAST field, and `ast.line_of` / `ast.sline_of` read it back.
    `compiler/parse/` stamps it from the token the node starts at (`_ln`), and
    `types.R` carries the walk's current line so `_err` / `_late` print
    `file:line: error: msg` as `src/tychoc.c` does.

    **Leg 1 — `make parse-check`, all eight legs plus the new one:**

    ```
    leg1  tests/*.ty: files=274 parse-ok=274 fail=0
    leg1b corelib/**.ty: files=91 parse-ok=91 fail=0
    leg1c tools+examples+server+bench: files=179 parse-ok=179 fail=0
    leg2  tests/reject/*.ty --parse: SYNTAX=57 rejected=57 missed=0 | NAME+SEMANTIC=280 accepted=280 wrongly-rejected=0
    leg2b tests/reject/*.ty --resolve: SYNTAX+NAME=84 rejected=84 missed=0 | SEMANTIC=253 accepted=253 wrongly-rejected=0
    leg3  census: 122 kinds, 182646 nodes over 544 files
    leg7  resolution census: 4011 distinct targets, 165333 resolved uses
    leg4  declaration rules: refused=5/5 accepted=5/5
    leg5  whole-tree verdicts: files=1078 tychoc(accept=552 semantic=403 name=49 syntax=74) disagreements=0
    leg6  whole-tree resolution: disagreements=0 unused-local/import on an accepted file=0
    leg8  NAME diagnostic file:line vs ./tychoc: scored=49 agree=49 disagree=0 unlocated=0
    parse-check: all green
    ```

    **Both censuses are UNCHANGED**, as predicted — a field is not a node kind,
    and neither golden was re-recorded (`git status` shows `census.expected.out`
    and `rcensus.expected.out` unmodified). Phase 1's milestone is unmoved too:
    `./tychoc1` on `fn main(): print(str(1))` still differs from `./tychoc`'s C
    by the same five blank lines.

    **Leg 2 — the lines checked against `./tychoc`, not merely present.** This
    is where the phase earned its keep. The first run of the comparison scored
    **7 disagreements out of 27** while every verdict leg stayed green:

    ```
    LINE-DISAGREE tests/reject/const_dup.ty              tychoc=:3  tychoc1=:1
    LINE-DISAGREE tests/reject/dup_fn_variant.ty         tychoc=:10 tychoc1=:7
    LINE-DISAGREE tests/reject/dup_fn_variant_fn_first.ty tychoc=:5 tychoc1=:9
    LINE-DISAGREE tests/reject/dup_struct_enum.ty        tychoc=:3  tychoc1=:1
    LINE-DISAGREE tests/reject/handle_then_enum.ty       tychoc=:4  tychoc1=:1
    LINE-DISAGREE tests/reject/handle_then_newtype.ty    tychoc=:4  tychoc1=:1
    LINE-DISAGREE tests/reject/handle_then_struct.ty     tychoc=:4  tychoc1=:1
    ```

    **The cause was REGISTRATION ORDER, and it is a real defect the line
    exposed.** `build_table` walked the seven declaration categories in array
    order — every `fn`, then every struct, then every enum — so of two colliding
    declarations it named whichever category came first. `src/tychoc.c` registers
    a struct/enum/newtype/handle/const/subscript *as it parses*, so the one
    written LATER loses; a `fn` is checked afterwards in `resolve_program`
    (`src/tychoc.c@die_dup_proc`), which names the PROC whichever side was
    written first. `dup_fn_variant_fn_first.ty`'s own header states that rule.
    `compiler/types/resolve.ty` now merges the six parse-time categories into
    one source-ordered walk (`DeclRef`) and registers functions last. After the
    fix: **49 of 49 NAME files agree with `./tychoc` on both file and line**, up
    from the 27 reject fixtures the brief named — leg8 scores the whole tree's
    NAME class, which is 49 files.

    One diagnostic had no location at all on tychoc1's side and was found by the
    same leg: the package-less-`import` refusal in `compiler/types/load.ty` was a
    bare `die()`. It names `entry:line` now, from the offending import's own line.

    **Leg 3 — two negative controls, each observed, each reverted.** Neither is
    visible to any other leg, which is the whole argument for leg8:

    | control | leg8 | legs 5/6 |
    |---|---|---|
    | `_ln` records `line + 1` (the declaration/parameter sites) | **disagree=27**, exit 1 | green |
    | `ast.line_of` returns `l + 1` for `Name` (the expression sites) | **disagree=12**, exit 1 | green |

    Two controls rather than one because the lines reach `_err` by two routes,
    and a single control would have left the other route unproven. Both were
    reverted and the lane is green at 49/49.

    **Leg 4 — the lane carries it from the repo.** leg8 lives in
    `compiler/verdict_diff.py`, which already runs both compilers over all 1,078
    files, so the comparison costs no extra process; `make parse-check` is
    ~15.5 s, unchanged. It fails on a disagreement, on an unlocated NAME
    diagnostic, and if the number of files it scored is not the number of NAME
    files leg5 counted — a leg that silently scores 0 of 49 is the failure mode
    it is written against.

- [ ] **Phase 5d — `'X' is already defined` says more than `src/tychoc.c` does**
  (found in Phase 5b, out of scope there: 5b's contract was the LINE, not the
  message). Where a top-level name collides, `compiler/types/resolve.ty` appends
  `(<kind> in <file>)` and `src/tychoc.c` does not:

  ```
  tychoc :  tests/reject/dup_struct_enum.ty:3: error: 'X' is already defined
  tychoc1:  tests/reject/dup_struct_enum.ty:3: error: 'X' is already defined (struct in tests/reject/dup_struct_enum.ty)
  ```

  `src/tychoc.c@die_dup_proc` does have a two-file form, but it fires only when
  the other declaration is in a DIFFERENT file of the same package, and it is
  worded differently. Phase 9 pins message text byte for byte and owns this;
  recorded here so it is not rediscovered.
  - Done when: the seven `'%s' is already defined` sites agree with
    `src/tychoc.c` on text as well as line, including the cross-file form.
  - Verify: extend leg8 in `compiler/verdict_diff.py` to compare the MESSAGE for
    the NAME class, not only `file:line`. State the new agree count.
  - Do NOT run: `make test`.

- [x] **Phase 5c — three name rules `compiler/types/` does not implement**
  (found in Phase 5). Each is a rule the symbol table alone decides, each still
  classified SEMANTIC because tychoc1 accepts it, and each is one fixture:
  - `'a' is already declared in this scope` — `src/tychoc.c@g_dup_base`, a
    duplicate `:=` against a parameter or an enclosing block.
  - `no 'main' procedure` — a whole-program check after the walk.
  - `package '%s' has no variant, const or function` currently answers with the
    `has no symbol` wording; the two are separate formats in `src/tychoc.c`.
  - Done when: each is in `NAME_SITES` and `leg2b` still shows
    `wrongly-rejected=0`.
  - Verify: `python3 scripts/classify_rejects.py compiler/reject_class.tsv`
    then `make parse-check`. State the new NAME/SEMANTIC split.

  **Verified 2026-08-23.** Split rebuilt: `python3 scripts/classify_rejects.py
  compiler/reject_class.tsv` -> `classified 337 {'SEMANTIC': 250, 'SYNTAX': 57,
  'NAME': 30}`, from 57/27/253. `make parse-check` (15.6 s) all green:

  ```
  leg2b tests/reject/*.ty --resolve: SYNTAX+NAME=87 rejected=87 missed=0 | SEMANTIC=250 accepted=250 wrongly-rejected=0
  leg4b package-member wording: passed=3/3 (field format, call format, accepting twin)
  leg5  whole-tree verdicts: files=1078 tychoc(accept=552 semantic=301 name=151 syntax=74) disagreements=0
  leg6  whole-tree resolution: disagreements=0 unused-local/import on an accepted file=0
  leg8  NAME diagnostic file:line vs ./tychoc: scored=151 agree=151 disagree=0 unlocated=0
  ```

  NAME went 49 -> 151 files, all 100 of the tree's `no 'main'` refusals included.
  Two defects the named legs found, both of the Phase 5b class (a diagnostic
  that is present but WRONG, with every verdict leg green):

  - `no 'main'` carries **g_srcname as PARSING left it** — the last file of the
    merge order, not the entry. Measured over all 100: 99 name their package's
    alphabetically-last sibling, the 100th is a package-less single-file build.
  - the import leak on a DECLARED TYPE is emitted by `parse_type`
    (`src/tychoc.c:2557`), so it OUTRANKS the whole-program check; the same
    message from an expression qualifier (`src/tychoc.c:6532`) does not. tychoc1
    banks the first in `perrs` and splices it in front.

  Six negative controls, each observed reddening the lane and naming its own leg:

  | control | leg that reddened |
  |---|---|
  | the duplicate scan is skipped | leg2b missed=2 (`dup_local`, `param_shadow`) |
  | the fn top body stops reaching over the params | leg2b missed=1 (`param_shadow` only) |
  | the `no 'main'` check is removed | leg2b missed=1, leg8 unlocated=100 |
  | `no 'main'` names the ENTRY file | leg8 disagree=3 (`tools/tycho-sheet/cell/`) |
  | the field format collapses onto the call one | leg4b `R3-FIELD-WRONG` |
  | the type leak stops being a parse-phase error | leg8 disagree=2 (`import_leak_type`) |

  leg4b is new and is the only thing that can see rule 3: BOTH spellings are a
  refusal, so leg2b/5/6/8 are green either way. Rule 1's scope was measured
  against ./tychoc rather than guessed — a nested block shadowing an outer local
  is LEGAL, a `for x in` body colliding with its own loop variable is not
  (`src/tychoc.c:4037` makes `x := COLL[i]` the body's first statement), and
  For3, `parallel for` and match-arm bindings do not participate.
  `python3 scripts/check_citations.py` ok.


- [ ] **`no 'main' procedure` names the wrong file, in `src/tychoc.c`**
  (found in Phase 5c, out of its scope). The check is
  `src/tychoc.c:9267`, an `fprintf` of `g_srcname` — which parsing left pointing
  at the LAST file of the merge order, not the entry the user named. Measured
  over all 100 files in the tree tychoc refuses this way, 2026-08-23: 99 name
  their package's alphabetically-last sibling. `tools/tycho-sheet/cell/cell.ty`
  is reported as `tools/tycho-sheet/cell/fold.ty`, a file the reader never
  mentioned and which is not missing anything.
  - Done when: the message names the entry file, and `compiler/types/resolve.ty`
    is changed back to `p.entry_path` in the same commit — tychoc1 reproduces the
    wart today, deliberately, because leg8 compares the two.
  - Verify: `make parse-check` (leg8 must stay at 151/151), plus `make test`,
    which owns `tests/reject/no_main.ty`'s golden.
  - Do NOT run: `make ci`.

- [ ] **f-string interpolations are still raw text, so nothing inside one is
  checked** (found in Phase 6a, out of scope there). `ast.FStrLit` keeps the body
  as written and `compiler/types/resolve.ty@_fstring_uses` only scans it for
  identifiers to mark used. `src/tychoc.c:2712` parses each `{...}` into a real
  expression. The visible cost today is one fixture:
  `tests/reject/len_scalar.ty` is `print(f"{len(x)}")` at an int `x`, and it is
  one of the four KNOWN misses in `compiler/run.sh`'s leg2c.
  - Scope: parse each interpolation into `ast.Expr` and hang them off the node.
  - Done when: `len_scalar.ty` leaves `KNOWN_TYPE_MISS` in both
    `compiler/run.sh` and `compiler/verdict_diff.py`.
  - Verify: `make parse-check`. Expect leg3's census to MOVE (the interpolations
    become real nodes) and say by how much; both other censuses should not.
  - Do NOT run: `make test`. Nothing here reaches `src/tychoc.c`.
