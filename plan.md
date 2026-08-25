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


- [x] **Phase 6b — `types/`: generics, newtypes, affine, `where`, `bounded`**
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

  **Done 2026-08-23.** `compiler/types/zsema.ty` (~860 lines) plus hooks in
  `tcheck.ty`. `--typecheck` is the WHOLE semantic check now: SEMANTIC has
  stopped being an accept bucket, so leg2c and leg10 score all four classes as
  refusals. Two new legs, leg13 and leg14, and each found a defect on its first
  run.

  **The filename sorts last on purpose.** tychoc reads a package's files in
  sorted order and a struct declared in a LATER file of the same package is
  invisible to an earlier one -- `sema.ty` sorted before `tcheck.ty` and every
  `inout C` failed with `unknown type 'C'`. Measured, not guessed.

  ```
  $ make parse-check                    # 20.6 s (20.87 / 20.54 / 20.49), was 20.3 s
  leg2c tests/reject/*.ty --typecheck: all=337 rejected=331 missed=6 (KNOWN 6)
  leg2b tests/reject/*.ty --resolve: SYNTAX+NAME=87 rejected=87 missed=0 | TYPE+SEMANTIC=250 accepted=250 wrongly-rejected=0
  leg9  type census: 1768 distinct types, 117802 inference sites, 1235 deferred
  leg13 affine shapes, one probe each: refused=11/11 accepted=8/8
  leg14 newtype distinctness: refused=4/4 accepted=2/2 (no message names a mangled type)
  leg5  whole-tree verdicts: files=1078 ... disagreements=0
  leg6  whole-tree resolution: disagreements=0 unused-local/import on an accepted file=0
  leg8  NAME diagnostic file:line vs ./tychoc: scored=151 agree=151 disagree=0 unlocated=0
  leg10 whole-tree typecheck: disagreements=0 (25 TYPE files known-missed)
  leg11 TYPE diagnostic file:line vs ./tychoc: scored=160 agree=160 disagree=0 unlocated=0
  leg12 accepted programs with an UNGROUNDED 6b construct: 22 of 552
  parse-check: all green
  ```

  **leg2c: 230 of 337 refused -> 331 of 337.** The 103 SEMANTIC fixtures fell to
  6. What landed: the affine discipline for channel/task/handle at all seven
  type-intern choke points and at the expression level (array/map/tuple/Option
  literal, `push`), the copy and reassign rules, closure capture,
  `channel(...)`-only-in-a-declaration, `close(h)` on a variable; the newtype
  underlying-type list; `bounded[N]` capacity and element; the FFI parameter and
  return types; `inout`/`sink` on an affine type; generic INFERENCE --
  parameter-pattern unification, explicit type arguments and their count, the
  five closed `where` predicates, size parameters, unbound-in-the-return, the
  empty variadic; generic AGGREGATE instantiation (from a written type and from a
  constructor call) with the partial-type-argument rule and the 1024 cap; the
  `sink` mention count with its heap gate; overlapping access; and eight smaller
  rules.

  **leg2c's KNOWN list was RE-CUT, not widened.** Phase 6a's four names are gone:
  `generic_bounded_field_degraded`, `generic_inst_inout_fnvalue` and
  `generic_params_17` needed generic instantiation and now have it. The six that
  remain are named with their reason in `compiler/run.sh`, and two are OUTSIDE
  this phase's scope lock -- `typeset_notin` and `len_scalar` need
  `compiler/parse/`, where a `where` type SET is parsed and discarded and an
  f-string's interpolations are kept as raw text. The other four are
  `generic_recur_grow` (needs the template BODY instantiated) and the three
  pending-type (B-3) grounding fixtures. `verdict_diff.py` carries 19, of which
  14 are new and honest: the eleven `tests/conc/reject/` fixtures are the
  CONCURRENCY rules -- what `spawn` may be applied to, `parallel for`'s reduction
  and control-flow rules, capturing a task, `wait`'s argument -- and three are
  match-ARM name rules. Both families are written up as Phase 6c.

  **leg12 was redefined, and saying so is the point.** Phase 6a's flag was set by
  a program DECLARING a generic/newtype/handle/bounded, which no amount of work
  could drive down: 202 of 552 was a census of the corpus, not of what the pass
  defers. It now marks a construct actually left `?` -- an instantiation that
  could not be ground, a generic return, a bounded capacity. **22 of 552**, and
  leg9's `?` count fell 2,590 -> 1,235 on the same change. The 22 that remain are
  legitimate: a generic aggregate whose type arguments are themselves generic, a
  UFCS method call (the receiver's method set is Phase 6c), and `bounded[$N]T`.

  **`leg2b wrongly-rejected=0` held at every step** and was re-measured after
  each of the eleven waves -- it is the property this phase could most easily
  have spent. Four false refusals were caught and fixed while it was being built,
  each a rule that was too wide: a bare `T` in a signature bound as a type
  parameter even where a struct of that name is declared
  (`corelib/testing/testing.ty` has `fn eq(t: inout T, got: $T, ...)`); the "no
  argument fixes $T" rule fired on a `$T` buried in a generic-struct parameter; a
  generic signature's NAMES resolved in the caller's package rather than the
  callee's; and the `sink` mention rule ran on a non-heap type, which is legal
  and enforces nothing (`tests/sink_scalar_noop.ty`).

  **leg13 and leg14 exist because a checker that refuses everything scores
  identically to a correct one on a reject corpus**, and each found a real defect
  on its first run. leg13 is eleven affine shapes, each its OWN probe, each
  paired with an accepting twin one token away -- including the borrow twin
  `tools/tycho-fh/run.sh` argues for (passing a handle must stay a borrow, not
  become a consume) and the same generic at a non-affine type, which is what
  proves the refusal is about the INSTANCE. Its find was in the probe itself,
  which is the failure `tycho-verify` §3 names: the capture probe's lambda never
  mentioned the channel, so the leg passed against a rule it never reached.
  leg14 is `tools/tycho-ledger/run.sh`'s argument -- a newtype is ERASED in
  lowering (spec §5.4), so no golden and no type census can see whether two
  domain types are distinct -- four probes that must fail, each refusal grepped
  for `__`. It found the newtype-under diagnostic printing `@coin__Cents`.

  **Seven negative controls, each observed and reverted.** C6b is the one the
  brief asked for:

  | control | what reddened |
  |---|---|
  | C1 the container/aggregate affine rule disabled | leg2c 331 -> 320, leg10 0 -> 11, leg13 11/11 -> 10/11 |
  | C2 the newtype underlying-type list dropped | leg2c -> 319, leg10 -> 12, **leg14 4/4 -> 1/4** |
  | C3 the five `where` predicates always satisfied | leg2c -> 326, leg10 -> 5 |
  | C4 the `sink` mention rule loses its heap gate | leg2c UNMOVED at 331, **leg10 -> 1** -- a too-wide rule reddens the ACCEPT side and nothing else |
  | C5 a bare `T` binds even where a TYPE of that name is declared | leg2c UNMOVED, leg10 -> 2, leg9 1235 -> 1252 |
  | C6 a generic call answers `?` instead of its substituted return | leg9 1235 -> 2342 deferred sites, leg12 22 -> 86, leg2c -> 330 |
  | **C6b a diagnostic prints the MANGLED type name again** | **leg14 ALONE: 4/4 -> 3/4, while leg2c (331), leg5, leg6, leg8, leg10 and leg11 stayed fully green over all 1,078 files** |
  | C7 the overlapping-access (inout alias) rules disabled | leg2c -> 328, leg10 -> 3 |

  Both older goldens are UNCHANGED (`git status` shows neither
  `census.expected.out` nor `rcensus.expected.out` modified);
  `compiler/tcensus.expected.out` is the only one re-recorded.

  Three gates my diff can move, all run: `make goldens-check` ok (521 golden
  files, all tracked), `make script-check` ok (26 .py, 96 .sh),
  `python3 scripts/check_citations.py` ok.

- [ ] **Phase 6c — the four rule families `--typecheck` still misses**
  - Scope: `compiler/types/`, and for item 1 ALSO `compiler/parse/` +
    `compiler/ast/`, which Phase 6b's scope lock excluded.
    1. **Two rules the AST cannot express.** `compiler/parse/parse.ty@_wheres`
       parses a `where T: int | string` type SET and pushes only `T`, discarding
       the members, so `typeset_notin` cannot be scored; and an f-string's
       interpolations are kept as raw text, so `len_scalar` is invisible. Both
       need an AST change first.
    2. **The concurrency rules.** Eleven `tests/conc/reject/` fixtures: what
       `spawn` may be applied to (a named user fn, not a builtin, not a closure,
       no `inout` parameters), `parallel for`'s reduction shape and its
       break/return bans, capturing a task, and `wait`'s argument. Each is a
       statement SHAPE, not a type rule.
    3. **The match-ARM name rules.** `foreign_variant_bare`, `foreign_variant_is`
       and `result_arm_mangled` — a variant of another package's enum must be
       written qualified, and a Result's arms are `Ok`/`Err`/`_`.
    4. **Pending-type (B-3) grounding**, a subsystem of its own:
       `infer_bare_empty`, `infer_use_before_ground`,
       `void_grounds_pending_push`. Plus `generic_recur_grow`, which needs the
       generic template BODY instantiated rather than only its signature, and the
       UFCS method call that leaves 22 programs ungrounded at leg12.
  - Done when: `compiler/run.sh`'s KNOWN list and `compiler/verdict_diff.py`'s
    are both EMPTY, and leg2c reads `rejected=337 missed=0`.
  - Verify: leg2c, leg10 and leg11 unmoved at 0; `leg2b wrongly-rejected=0`;
    each family gets a probe with an accepting twin, as leg13 does.
  - Do NOT run: `make test`, `make ci`. The lane is `make parse-check`.

- [ ] **Phase 7 — `lower/` + `emit/`: core codegen**
  - Scope: `compiler/lower/`, `compiler/emit/`. Arena scoping, structs, enums,
    arrays, strings, control flow, functions, the runtime ABI.
  - Done when: `TYCHOC=./tychoc1 make test` reaches a stated pass count, climbing
    from Phase 1's one program. State the number; a count that moves down is a
    regression.
  - Verify: `TYCHOC=./tychoc1 make test`, count recorded in the commit message.

  **PARTIAL, committed unticked 2026-08-23.** ~1,220 lines of `compiler/emit/`
  land here with a measured but incomplete result. The phase stays open.

  **Never run `make test` under `tychoc1` unbounded.** Three sessions were
  killed doing it; the third returned **exit 137 (SIGKILL)** — the OOM killer —
  at `TYCHO_THREADS=4`. The same command on `./tychoc` at the same throttle is
  fine, so the harness and the parallelism are not the cause. Every `tychoc1`
  invocation gets `ulimit -v 2000000` and a `timeout`, run sequentially. The
  bounded sweep is `scripts/tychoc1_sweep.sh` and takes ~2 min.

  **Measured, bounded, 2026-08-23** (`sh scripts/tychoc1_sweep.sh`):

  ```
  compile=77 clean-error=197 TIMEOUT=0 KILLED=0
  link=77 RUN=77 MATCH=76
  ```

  Baseline the same day, `TYCHO_THREADS=4 make test` on `./tychoc`:
  `passed: 752  failed: 0`. **The gate table's "719 fixtures" is stale — it is
  752.** So Phase 7 stands at **76 of 274 `tests/*.ty` matching their golden**,
  up from Phase 1's single program, and the 197 clean errors are constructs
  `compiler/emit/` does not yet emit.

  A first sweep reported one hang, `io_builtins`, and it was a HARNESS artifact
  rather than a `tychoc1` defect: it reads stdin and that sweep did not redirect
  it, where `tests/run.sh` feeds it from a `.in` file. The committed script
  feeds `tests/<name>.in` when present and `/dev/null` otherwise, which is what
  moved RUN 76 -> 77 and MATCH 75 -> 76.

  **Not established:** which fixture actually triggers the OOM. It is not in
  `tests/*.ty` — all 274 are clean under a 2 GB cap. `make test` covers 752
  fixtures including `tests/recursion/` (deliberate deep recursion) and
  `tests/conc/`, and `tychoc1` has none of the eight optimisation passes, so
  its frames are fatter than baseline. That is a hypothesis, not a measurement,
  and proving it costs a session each time it is wrong.

- [ ] **Phase 7b — the remaining `tests/*.ty` that do not compile under tychoc1**
  - Scope: `compiler/emit/`. Each is a construct Phase 7 does not emit yet.
  - Done when: the bounded sweep's `clean-error` count falls and `MATCH` rises,
    both stated. Phase 8's subjects (maps, soa, generics, affine, concurrency)
    are excluded and stay Phase 8's.
  - Verify: `sh scripts/tychoc1_sweep.sh` — never an unbounded `make test`.

  **STILL PARTIAL, committed unticked 2026-08-23 (second pass).**
  `sh scripts/tychoc1_sweep.sh`, before and after this pass:

  ```
  before  tests: compile=201 clean-error=73 link=201 RUN=201 MATCH=199
          examples 22 | tests/pkg 16 | reject 336/337 | reject/pkg 15/17
  after   tests: compile=233 clean-error=41 link=232 RUN=232 MATCH=230
          examples 22 | tests/pkg 19 | reject 336/337 | reject/pkg 15/17
  ```

  Nothing regressed; `warn` improved too (refused 5 -> 4). `make parse-check`
  still all green, `disagreements=0` on 1,078 files.

  **Landed this pass**, all six of the phase's named families bar `extern`:
  f-strings (split and hole re-lex in emit, so no parser change was needed --
  the AST census is untouched); sized ints and `bytes` (u8..i64, f32, the
  to_XXX conversions, width-preserving arithmetic and the shift guards);
  fixed arrays `[N]T` and `bounded[N]T` (both lowered to the dynamic array);
  array literals adapting to the annotated element type; function values and
  closures (the 3-word fat pointer, per-function `__clo` trampolines, per-lambda
  Env struct + copier, free-variable capture including a call's CALLEE name);
  plus two one-offs the families exposed -- char arithmetic masking to a byte
  and `str`/`==` of a fn value.

  **41 clean errors remain**, 18 of them Phase 8's:

  | n | cause | owner |
  |---|---|---|
  | 6 | `soa` (`SoaLit` 4, `TSoa` 2) | **8** |
  | 6 | channels (`ChanE` 4, `Channel(T)` 2) | **8** |
  | 3 | `handle` / `subscript` declarations | **8** |
  | 2 | `&place` outside an `inout` argument | **8** |
  | 1 | `wait` on a spawned task | **8** |
  | 3 | `extern` — see below, NOT reachable by this sweep | 7b |
  | 3 | a generic struct with a fn-typed or bounded field: `cannot infer $T` | 7b |
  | 3 | elementwise array arithmetic / broadcast (`array_arith`, `array_bcast`, `char_elem_ops`) | 7b |
  | 2 | `$N` const generics used as a VALUE (`const_generic_size`, `generic_many_typaram_names`) | 7b |
  | 3 | `None`/`Ok` with no type to belong to (`bounded_elems`, `inference`, `option_arrays`, `result_tuple`) | 7b |
  | 2 | two match-payload binds not in scope (`nested_pattern`, `result_void`) | 7b |
  | 4 | builtins: `fabs`, `list_dir`, `hash`, `len` of a non-collection | 7b |
  | 1 | `stmt:ParallelS` (`int_hex`) | 8 |

  **`extern` cannot raise MATCH and is not worth emitting until that changes.**
  The three fixtures name `tycho_test_make_locale_hostile` and
  `tycho_test_float_roundtrip`, which live in a test shim `tests/run.sh` links;
  the sweep compiles the emitted C with `cc x.c -lpthread -lm` and nothing else,
  so even a perfect `extern` lowering would fail at LINK rather than match.

  Also still open, and NOT clean errors — they emit C and then fail:
  1 fixture whose C does not compile (`newtype_over_aggregate`, below) and
  2 whose output differs (`match_payload_mut`, `int_overflow` — the latter has
  differed since Phase 7). `projections` and `compound_index_eval` are fixed.

- [x] **Phase 7c — find the fixture that OOMs the box under tychoc1**
  - Scope: diagnosis only. Bisect the 752-fixture corpus OUTSIDE `tests/*.ty`
    under `ulimit -v`, sequentially, so a runaway dies alone.
  - Done when: the fixture is named and the cause stated — fat frames from the
    missing optimisation passes, or a genuine codegen defect.
  - Verify: the named fixture reproducing under a cap, and passing under
    `./tychoc`. Do NOT run an unbounded `make test` to find it.

  **Done 2026-08-23. The fixture is `tests/reject/generic_recur_grow.ty`, and
  the cause is a genuine codegen defect, not fat frames.** It was the only
  non-terminating file in 429 swept under `ulimit -v 2000000` + `timeout 5`:
  `examples/` 23, `tests/pkg/` 23, `tests/abort/` 19, `tests/diag/` 40,
  `tests/warn/` 10 all clean; `tests/reject/` 337 gave exactly one TIMEOUT.

  It is three lines, and the whole runaway is in the type it binds:

  ```tycho
  fn grow(x: $T) -> int:
      return grow([x])
  ```

  Each instantiation binds `$T` one array deeper — `int`, `[int]`, `[[int]]`
  — so the monomorphiser never reaches a fixpoint. `./tychoc` caps at 1024 and
  refuses (`tests/reject/generic_recur_grow.ty:2: error: too many generic
  instantiations (> 1024) -- a recursive generic at a growing type?`); Phase 8's
  monomorphiser had no cap and allocated until the kernel killed it.

  **Why only the full suite died:** `tests/reject/` is outside
  `scripts/tychoc1_sweep.sh`'s corpus, so every bounded sweep run to date was
  green while the one fixture that kills the box sat just outside it.

  Fixed in `compiler/emit/emit.ty@_inst_cap`, called from all three
  instantiation paths (`_inst_struct`, `_inst_enum`, `_inst_fn`). Measured
  after the fix, **with no memory cap at all** — the OOM scenario:

  ```
  $ timeout 20 ./tychoc1 tests/reject/generic_recur_grow.ty --emit-c -o /tmp/gr
  exit=1  elapsed_ms=109
  ```

  Sweep unmoved at `compile=201 clean-error=73 link=201 RUN=201`, MATCH 199,
  `make parse-check` all green.

  **Cost of finding it late:** three killed sessions. The sweep should have
  covered every corpus `tests/run.sh` walks from the start, not just
  `tests/*.ty`. Phase 7d widens it.

- [x] **Phase 7d — `scripts/tychoc1_sweep.sh` covers only `tests/*.ty`**
  - Scope: the sweep script. `tests/run.sh` also walks `examples/*.ty`,
    `tests/pkg/`, `tests/reject/`, `tests/reject/pkg/`, `tests/abort/`,
    `tests/diag/`, `tests/warn/` and `tests/warn/pkg/`. Phase 7c found the
    session-killing fixture in `tests/reject/`, i.e. outside the sweep.
  - Done when: the sweep walks every corpus `tests/run.sh` does, each capped,
    with a per-corpus line so a regression names its own corpus.

  **Done 2026-08-23**, and widening it found a defect bigger than the one it
  was written for.

  **`--emit-c` never ran the type checker.** `compile()` was lex -> parse ->
  emit and called neither `resolve` nor `check`, so the whole front end — the
  331/337 refusals Phases 5-6b were built and gated for — was not in the
  compile path at all:

  ```
  tests/reject/type_mismatch.ty  --typecheck -> exit 1 (refused)
  tests/reject/type_mismatch.ty  --emit-c    -> exit 0 (emitted C)
  ```

  `driver.check_or_die` now runs ahead of emit. The reject corpus went
  **217 -> 336 of 337 refused**, `reject/pkg` 17/17, while `tests/` MATCH held
  at 199 — the invalid started being refused without the valid breaking. The
  narrow sweep could never have seen this: `tests/*.ty` are all valid programs.

  **The sweep's own bug, found the same way:** goldens are NOT beside the
  source. `tests/run.sh:158` keys `examples/*.ty` off `tests/<basename>.out`,
  and the first widened run scored `examples MATCH=0` against a path that does
  not exist. Corrected, it is 22 of 23.

  ```
  tests      : compile=201 clean-error=73 link=201 RUN=201 MATCH=199
  examples   : compile=23  clean-error=0  link=23  RUN=23  MATCH=22
  tests/pkg  : compile=0   clean-error=23
  abort(cmp) : n=19  refused=2   accepted=17
  diag (cmp) : n=40  refused=40  accepted=0
  warn (cmp) : n=12  refused=6   accepted=6
  reject     : n=337 refused=336 accepted=1
  reject/pkg : n=17  refused=17  accepted=0
  TOTAL TIMEOUT=0 KILLED=0
  ```

  `abort`/`diag`/`warn` compare STDERR and exit status, which this sweep does
  not model; they are swept for runaways only and their verdict column is
  deliberately meaningless. A runaway is the only outcome that fails the sweep,
  because it is the only one that can take the machine down.

- [ ] **Phase 7e — multi-package emit: 16 of 23 `tests/pkg/` fixtures**

  **PARTIAL, committed unticked.** `sh scripts/tychoc1_sweep.sh`:

  | line | before | after |
  |---|---|---|
  | `tests` | compile=201 MATCH=199 | compile=201 MATCH=199 |
  | `examples` | compile=23 MATCH=22 | compile=23 MATCH=22 |
  | `tests/pkg` | compile=0 clean-error=23 MATCH=0 | compile=16 clean-error=7 **MATCH=16** |
  | `reject` | refused=336/337 | refused=336/337 |
  | `reject/pkg` | refused=17/17 | refused=15/17 — see Phase 7g |

  `driver.compile` hands `emit.program` every file `types.load` walked, with
  the loader's own package prefix, and emit mangles `<pkg>__name` the way
  `src/tychoc.c@pkg_prefix_for` does. Collection is two passes so a name
  declared in a LATER file of the same package still resolves. A variant name
  is package-SCOPED (`tests/pkg/variant_shadow`), a qualified member
  (`levels.DEBUG`, `geom.Red`) resolves as that package's const or variant, and
  UFCS follows its receiver's package. Diagnostics are untouched and still
  print the unmangled form — `make parse-check` leg4b/leg14 green.

  The 7 that remain are NOT package work: fn values / `TFn` (`fnval`,
  `fnvalcross`, `fnvalparen`, `generic_collections`), the `u8` primitive
  (`sized_pkg`), `extern` functions (`corelib_variant_shadow`, `vendor_deps`).

  - Done when: all 23 compile and match.
  - Verify: `sh scripts/tychoc1_sweep.sh`, the `tests/pkg` line.
  - Negative control, observed: `_pfx` forced to `""` drops `tests/pkg` to
    compile=1 MATCH=1 and `tests` MATCH to 198; reverting restores 16/16/199.

- [ ] **Phase 7g — the checker accepts a generic body it should refuse**
  - `tests/reject/pkg/generic_inst_callsite` and `generic_inst_srcfile` both
    report `TYPECHECK-OK files=2`; `./tychoc` refuses each at the `x + x` in
    the sibling file, with a note naming the instantiating call. They scored as
    "refused" until Phase 7e only because emit parsed the entry file alone and
    died on an unknown call — a refusal for the wrong reason.
  - Scope: `compiler/types/`. Re-check a generic body against each binding.
  - Done when: `reject/pkg` is back to refused=17/17.
  - Verify: `sh scripts/tychoc1_sweep.sh`, the `reject/pkg` line.

- [ ] **Phase 7f — one reject fixture still compiles, and 17 abort fixtures do**
  - `reject accepted=1` of 337, and `abort refused=2 accepted=17`. The abort
    figure is expected — an abort fixture is a valid program that dies at RUN
    time — but it is unverified here, and the sweep cannot tell a correct abort
    from a compile that merely succeeded.
  - Done when: the 1 accepted reject fixture is named and refused, and the
    abort corpus is scored on its exit status rather than on compiling.
  - Verify: `sh scripts/tychoc1_sweep.sh` with an abort leg that reads exit
    status, plus the named reject fixture refused.

- [ ] **Phase 8 — codegen: maps, soa, generics, affine, concurrency**
  - Scope: `compiler/emit/`. Compact-dict maps, `soa`, monomorphised generics,
    handle destructors, `spawn`/`channel`/`select`, `parallel for`.
  - Done when: `TYCHOC=./tychoc1 make test` is green at the same count as
    `./tychoc` — **752**, measured 2026-08-23; the "719" this said was stale.
  - Verify: `sh scripts/tychoc1_sweep.sh` — never an unbounded `make test`.

  ### The soa target, read out of `./tychoc`'s own output

  `494d3475` landed the type registry and heap classification; the expression,
  lvalue, builtin and statement paths are not written, so all six soa fixtures
  still die `expr:SoaLit -- Phase 8`. The shape below is **measured**, not
  designed — `./tychoc tests/soa_basic.ty --emit-c` — so the remaining work is
  mechanical rather than exploratory. Field `N` is the element struct's Nth
  field in declaration order.

  ```c
  typedef struct { tycho_int *f0; tycho_int *f1; tycho_int *f2;
                   tycho_int len; tycho_int cap; } Soa0;
  static tycho_int Soa0_bound(Soa0 *s, tycho_int i);   /* dies out of range  */
  static void Soa0_push(Arena*, Soa0*, S_P);           /* doubles cap from 4 */
  static void Soa0_set(Arena*, Soa0*, tycho_int, S_P);
  static S_P  Soa0_pop(Soa0*);                         /* dies when empty    */
  static Soa0 Soa0_copy(Arena*, Soa0);                 /* cap = len          */
  static int  Soa0_eq(Soa0, Soa0);
  ```

  Every call site, as `./tychoc` spells it:

  | Tycho | emitted C |
  |---|---|
  | `ps := soa []P` | `Soa0 h_ps = (Soa0){0};` |
  | `push(ps, v)` | `Soa0_push(&_scope, &(h_ps), v)` |
  | `len(ps)` | `((h_ps).len)` |
  | `ps[i].x` (read) | `((h_ps).f0[Soa0_bound(&(h_ps), i)])` |
  | `ps[i].x = v` | the same expression, as an lvalue |
  | `g := ps[i]` (gather) | `(S_P){ f0[b], f1[b], f2[b] }` with `b` bound once |
  | `pop(ps)` | `Soa0_pop(&(h_ps))` |

  The scatter form is the one worth care: `ps[2].z = ps[2].z + 1` emits
  `Soa0_bound` **twice**, once per occurrence, rather than binding the index —
  match that, because a fixture's output cannot tell the two apart and a
  "tidier" single-bind diverges from the reference for no gain.

  Fixtures: `soa_basic`, `soa`, `soa_pop`, `soa_scatter`, `soa_scatter_heap`,
  `generic_soa_param`. `tests/kw_contextual_names.ty` mentions `soa` as an
  ordinary identifier and already compiles — it is not a soa test.

  **Read `tools/tycho-sim/run.sh` before believing a green soa run.** Its
  subject is swap-remove: forgetting to re-point a moved entity's slot leaves
  the pool LENGTH right, so every count and every field-wise sum still reads
  correctly while exactly one id addresses somebody else, and a golden
  re-recorded from that build agrees with it.

  **PARTIAL, committed unticked.** `sh scripts/tychoc1_sweep.sh` moved
  `compile=126 clean-error=148 / link=125 RUN=125 MATCH=122` to
  `compile=201 clean-error=73 / link=201 RUN=201 MATCH=199`, with
  `TIMEOUT=0 KILLED=0` throughout. **Maps and generics are done; soa, channels
  and handles are not.** Landed:

  - **Compact-dict maps**, one stamped kind per `(K, V)`, mirroring
    `src/tychoc.c:13481` line for line — int32 index table over dense
    insertion-ordered entries, tombstone + backward-shift index delete,
    allocation-free compaction. String / int / fieldless-enum (stored as the
    tag, rebuilt through `_sing_tab_<E>`) / struct / tuple / array keys, with a
    generated FNV deep hash per hashable struct, tuple and composite array.
    `m[k]`, `m[k] = v`, `m.get(k[, d])`, `k in m`, `delete m[k]`, `keys(m)`,
    `len(m)`, `reserve(m, n)`, `==` and `str(m)`.
  - **Monomorphised generics**: generic structs, enums and functions, one deep
    instance per distinct binding, discovered to a fixpoint (an instance body
    that instantiates another gets its own body emitted). Type arguments come
    from an explicit `f$(T)`, from the wanted type, or from unifying the
    declared parameter/field/payload types against the arguments. A bare
    variant of a generic enum resolves through the wanted type or by inferring
    its arguments — never by picking whichever instance was stamped first.
  - **UFCS** (`x.f(a)` is `f(x, a)`), in both spellings the parser produces.
  - **The owner arena for a heap `inout` parameter.** A map or array reached
    through `inout` grew its buffer in the CALLEE's `_scope`, which is freed on
    return, so `tests/maps.ty`'s `drop(&m, "x")` left a dangling entry table.
    Each heap `inout` parameter now carries its owning arena, as
    `src/tychoc.c@owner_arena_of` does with `_ina_<name>`.
  - **A bare `[]` declaration typed by its first `push`** (and by a map
    annotation), which was blocking four generic fixtures.
  - **A side-effecting index key is evaluated once.** `m[f()] += 1` desugars to
    `m[f()] = m[f()] + 1` with the same key node twice; it is hoisted to a temp.
  - **`push`/`pop`/`reserve` reach their first argument as a PLACE**, which is
    what `push(m["g"], 3)` needs — and is also what fixed `projections`, the
    fixture whose emitted C did not compile at Phase 7b.

  `compound_index_eval` and `projections` are both fixed as a side effect. The
  two remaining non-clean fixtures are `int_overflow` and `match_payload_mut`,
  both of which already differed at Phase 7b.

  **73 clean errors remain, grouped by the FIRST error each fixture reports.**
  Roughly 18 are Phase 8's own and 55 are Phase 7b's:

  | n | cause | owner |
  |---|---|---|
  | 4 | `soa` literals (`expr:SoaLit`) | **8** |
  | 2 | `soa` types (`type:TSoa`) | **8** |
  | 4 | `channel(T, n)` (`expr:ChanE`) | **8** |
  | 2 | the `Channel(T)` type | **8** |
  | 1 | `wait` on a spawned task | **8** |
  | 3 | `handle` / `subscript` declarations | **8** |
  | 2 | `&place` outside an `inout` argument (`expr:Addr`) | **8** |
  | 11 | function types (`type:TFn`) | 7b |
  | 7 | fixed-size arrays `[N]T` | 7b |
  | 5 | lambdas | 7b |
  | 5 | sized ints and `f32` as a primitive type | 7b |
  | 8 | sized/byte conversions and other builtins (`to_u8`, `to_u32`, `to_bytes`, `fabs`, `list_dir`, `hash`) | 7b |
  | 4 | `bounded[N]T` | 7b |
  | 4 | f-strings | 7b |
  | 3 | `extern` functions | 7b |
  | 3 | unknown names (a fn value, two match-payload binds) | 7b |
  | 2 | `len()` of a non-collection | 7b |
  | 2 | `None` / `Ok` with no type to belong to | 7b |
  | 1 | indexing a `char` element | 7b |

  Not done and deliberately untouched: `soa`, `spawn`/`channel`/`select`,
  `parallel for` and handle destructors. Concurrency was left last on purpose —
  it emits threads, and Phase 7c has still not named the fixture that OOMs.

  **Measurement notes.** Four sweeps in a row read `MATCH=199`; one read 198 and
  was not reproduced, so one fixture is intermittently flaky and has not been
  identified. `make parse-check` is all green (`disagreements=0` on 1,078
  files). Negative control: reversing the entry walk in the generated
  `keys()` — insertion order being the whole point of the compact dict — drops
  MATCH 199 -> 193, and reverting restores it. A FIRST control (dropping the
  key deep-copy on insert) moved nothing and is recorded here as **decoration**:
  every map key in this corpus is a literal or outlives its map, so no fixture
  can see it.

- [ ] **Phase 8b — one fixture's sweep verdict is not reproducible** (found in
  Phase 8, out of scope there)
  - Five consecutive `sh scripts/tychoc1_sweep.sh` runs on the same binary read
    `MATCH=199` four times and `198` once. The 198 was not reproduced and the
    fixture was not identified; `bad.sh`-style per-fixture reruns were stable at
    the same two mismatches three times over.
  - Scope: diagnosis. Run the sweep in a loop recording the per-fixture verdict,
    and name the fixture that moves.
  - Done when: the fixture is named and the cause stated — a timeout near the
    5 s bound, a per-process hash seed leaking into output, or a genuine
    nondeterminism in the emitted C.
  - Verify: the named fixture disagreeing with itself across repeated runs of
    the SAME binary. Do NOT run an unbounded `make test`.

- [ ] **Phase 8c — `_hashable` is depth-capped at 8 and fails closed** (found in
  Phase 8, out of scope there)
  - `compiler/emit/emit.ty@_hashable_d` stops at depth 8 and returns false,
    because a struct whose field is an array OF ITSELF is a legal recursive
    shape and recursing on it never terminates (it OOM'd two fixtures before the
    cap). The cap is a guess, not a proof: a legitimately hashable struct nested
    deeper than 8 loses its generated hash and the map that keys on it stops
    compiling, with no diagnostic naming the cap.
  - Scope: replace the depth cap with a visited set over struct/tuple ids.
  - Done when: the recursive shapes still terminate and no legal depth is cut.
  - Verify: `sh scripts/tychoc1_sweep.sh` unmoved, plus a probe nesting a
    hashable struct 12 deep and keying a map on it.

- [ ] **Phase 10a — `io.read_text` is miscompiled: the fixpoint's real blocker**
  - **A six-line reproduction, far smaller than the self-compile that found
    it.** Compiled by `./tychoc1`, linked against `corelib/io/io_shim.c` and
    `corelib/strings/strings_shim.c`:

    ```tycho
    package main

    import "core:io"

    fn main():
        match io.read_text("runtime/tycho_rt.c"):
            Ok(s): println("OK len=" + str(len(s)))
            Err(e): println("ERR " + str(e))
    ```

    prints `ERR NotFound` for a file that is present and 134,884 bytes.
    `./tychoc` compiles the same probe correctly. **No fixture catches this:
    262 of them compile, link and run, and 258 match.**
  - **Ruled OUT, each measured, do not redo:**
    - the extern CALL is correct —
      `iox_read_file(tycho_str_copy(&_scope, h_p), &(h_st))` reaches the real
      symbol, not a stub;
    - the duplicate `h_iox_read_file` stub is gone (an extern no longer gets a
      Tycho body) and removing it did not fix this;
    - the `RF_` constants fold correctly — `RF_MISS = 0`, `RF_OK = 1`, and the
      emitted `st == 1` -> Ok / `st == 0` -> Err matches the source;
    - passing extern arguments WITHOUT the arena copy (as `./tychoc` does)
      makes it worse: the probe then SEGFAULTS, exit 139. So the copy is not
      the fault and that change must not be reapplied on its own.
  - **RESOLVED 2026-08-24 (`2786196f`)**: the cause was the extern ABI for a
    `bytes` return, found exactly by the diff this line proposed. The probe
    prints `OK len=134884` now.

  ### The second defect, behind the first: emit runs away in self-compiled code

  `tychoc2` now builds, links, runs, and its whole FRONT END is correct --
  `--dump-tokens`, `--parse`, `--resolve` and `--typecheck` all give right
  answers on a two-line program. Only `--emit-c` fails, with a clean
  `tycho: out of memory`.

  **Narrowed, each measured:**
  - It is a SINGLE ENORMOUS ALLOCATION, not a loop: **7 ms** to failure.
  - It is not the runtime read -- it fails identically with `--runtime`
    pointing at an 11-byte file, so the 134 KB read is not the allocation.
  - It is therefore inside `emit.program` itself, on a two-line input.
  - It is not the duplicate `h_<name>` extern stub: that is now removed
    (an extern gets no Tycho body) and the OOM is unchanged.
  - The emitted `iox_read_file` call in `self.c` is byte-correct and matches
    `./tychoc`'s form, so `io.read_bytes` is not the site.
  - `ulimit -v` turns this into a clean diagnostic instead of a fourth killed
    session -- keep every `tychoc2` run capped.
  **The allocation is NAMED, by gdb on a `-g` build (`break tycho_oom`):**

  ```
  #1 block_get (cap=8385549048750239608)
  #2 arena_alloc (a=..., n=8385549048750239608)
  #3 tycho_str_alloc (n=8385549048750239592)
  #4 tycho_str_copy (s=0x... "o_str(&_t, 1LL)); arena_free(&_t); }\n")
  #5 h_emit__program (self.c:21635)
  ```

  The requested size decodes to ASCII, so the "length header" being read is
  TEXT: the pointer is into the middle of a buffer with no valid header. The
  statement is the extern-prototype loop this session added:

  ```c
  char *h_xnm = tycho_str_copy(&_scope,
      ((tycho_arr_K20_get(((h_cs).f_fds), h_xi)).f_name));
  ```

  `cs.fds[xi].name` -- reading a struct out of a stamped array and taking its
  string field -- yields a garbage pointer. K20 is the stamped kind for `[FD]`.

  **Ruled out by probe, each compiled with tychoc1 and RUN:**
  - string slicing (`s[a:b]`, sliced-and-concatenated in a loop): correct,
    and identical to `./tychoc`'s answer;
  - dedup through an `inout [string]` (lookup-then-push, called repeatedly):
    correct, len=2 as it should be;
  - the exact FD shape in isolation -- a 6-field struct with a string, three
    arrays and a bool, pushed into an array, then `.name` read both directly
    and through an `inout` parameter: correct;
  - the aliasing theory (passing `cs.fds[xi].name` into a call that also
    takes `&cs`): binding the name to a local first changes nothing, the
    crash is on the BINDING line itself.

  So the shape is fine in isolation and wrong at this site. Next: dump
  `cs.fds.len` and the raw element bytes at that point, or compare the K20
  element layout against FD's -- a stamped array whose element type does not
  match what was pushed would produce exactly this.
  - Done when: the probe prints `OK len=134884`, and `tychoc2` compiles
    `hello.ty`.
  - Verify: the probe, then the full self-host chain — `tychoc1` emits its own
    source, `cc` builds it with the two shims, and the resulting `tychoc2`
    compiles a program whose output matches `./tychoc`'s.

- [ ] **Phase 10b — the fixpoint needs deep-copy elision, and that is MEASURED**
  Self-hosting works for a small program (`0f53b055`): tychoc1 compiles its
  own source, the C builds into tychoc2 with the two corelib shims, and
  tychoc2 compiles `fn main(): print(str(1))` into a binary printing `1`,
  matching `./tychoc`'s.

  **Gen-2 does not exist, and the reason is not a bug.** Measured 2026-08-24
  on the same input (`compiler/main.ty`), same machine:

  | compiler | built by | peak memory for the same compile |
  |---|---|---|
  | `tychoc1` | `./tychoc` | **under 300 MB** (succeeds at `ulimit -v 300000`) |
  | `tychoc2` | `tychoc1`  | **over 7 GB** (OOMs at `ulimit -v 7000000`, ~100 s in) |

  A **>23x blowup**, and it is the eight deep-copy-elision passes at
  `src/tychoc.c:9350-13916` -- move-on-last-use, sink-argument adopt,
  match-arm payload borrow, return-slot, return-only escape, accumulator
  in-place append, construction-arg move, bounds-check elision. Phase 7's
  brief deliberately said "correct unoptimised output only" and none were
  ported. The compiler is string-concat-heavy, so without in-place append and
  move-on-last-use its own memory goes quadratic.

  So the fixpoint is blocked by a KNOWN, DELIBERATE omission rather than a
  defect, and closing it means porting the elision passes -- starting with
  accumulator in-place append and move-on-last-use, which are the two that
  bear on `out = out + x`.

  Not the cause, each measured: dropping the `_place` gate on all seven copy
  sites changes nothing (OOM at 84 s instead of 100 s, sweep identical), and
  under gdb -- which does not inherit the ulimit -- it runs past 200 s without
  failing, so it is steady growth and not a runaway allocation.

  - Done when: `tychoc2 compiler/main.ty --emit-c` completes, and its output
    is byte-identical to `tychoc1`'s (`cmp self.c self2.c`).
  - Verify: that `cmp`, plus a third generation to confirm it is stable.

  **An accumulator pass was ATTEMPTED and REVERTED 2026-08-24. Read this
  before trying again.** The emitted shape is right and was copied from
  `./tychoc tests/.../acc --emit-c`:

  ```c
  char *h_out = TYCHO_LIT("");
  tycho_int _lenh_out = ((const tycho_int *)h_out)[-1]; tycho_int _caph_out = 0;
  ...
  tycho_str_append(&_scope, &h_out, &_lenh_out, &_caph_out, TYCHO_LIT("x"));
  ```

  `tycho_str_append` is already in `runtime/tycho_rt.c` -- no runtime change.

  What the attempt got wrong, measured at each step against the 262/262/258
  baseline:
  - marking a name an accumulator from ASSIGNMENTS alone breaks 7 files at
    LINK (link 262 -> 255, MATCH 258 -> 250): a parameter, or a name bound by
    a form other than a plain `Decl`, never gets its `_len`/`_cap` shadows
    declared, so the append names undefined identifiers;
  - requiring the name to be declared by a plain `Decl` in the same body
    recovers most of it but not all -- link 257, MATCH 253, still 5 short.
    The residue is a SCOPE/SHADOWING problem: `_push` renames a shadowing
    binding (`h_out_s3`), so the shadow declared at one `Decl` and the name
    resolved at the assignment can disagree.
  - The next attempt should carry the shadow names in the variable table
    beside `vcn`, so the append always names the same instance the assignment
    resolved, instead of rebuilding them from the C name by string
    concatenation.

- [ ] **Phase 8d — concurrency codegen (the 8 remaining fixtures)**
  - `chan_param_recv`, `generic_channel`, `generic_enum_channel`,
    `generic_struct_instance_types`, `select_enum_match`,
    `select_default_closed` (channels/select), `int_hex` (`parallel for`).
    Errors are `expr:ChanE`, `generic type 'Channel'`, `stmt:ParallelS`.
  - **This is the one lane that can HANG THE MACHINE rather than print a wrong
    answer.** Three sessions were already killed by an unbounded `tychoc1` run
    (Phase 7c). Every probe goes through `scripts/tychoc1_sweep.sh` or the same
    `ulimit -v 2000000` + `timeout` wrapper, sequentially; a miscompiled spawn
    shows up as the sweep's TIMEOUT/KILLED columns, which fail it by design.
  - **`parallel for` is not a small arm** — read from
    `./tychoc tests/int_hex.ty --emit-c`, one `parallel for i in 0..<3` expands
    to: `_plo`/`_phi` bounds with a `_phi < _plo` clamp; `_pk = tycho_ncpu()`
    clamped to the range and to `[1, 64]`; an `HTask *_pts[64]`; a start loop
    allocating an `HSpawnA_<id>` in each task's own root arena, filling the
    chunk bounds `_plo + (_phi-_plo)*_pc/_pk`, and calling
    `tycho_task_start(_tk, tycho_spawn_<id>, _sa)`; then a join loop doing
    `tycho_task_join` / read `*(T *)_pts[_pc]->ret` / `tycho_task_free` and
    merging into the reduction variable. So it needs the SAME per-instance
    arg-struct and thunk machinery `spawn` does — build that first, then
    `parallel for` is a caller of it.
  - The authority is `src/tychoc.c:7603` (`parallel for` chunked fan-out with
    reduction merge). `tycho_task_new/start/join/free` and `tycho_ncpu` are
    already in `runtime/tycho_rt.c` — do NOT re-emit them, which is the
    mistake that cost four non-linking files in the element-wise work.
  - Done when: the sweep's `tests` MATCH rises by up to 8 with
    `TIMEOUT=0 KILLED=0` still holding.
  - Verify: `sh scripts/tychoc1_sweep.sh`, plus a DETERMINISM leg — any
    spawn/channel fixture must give identical output over at least 5 runs,
    because a golden that matches once proves nothing when scheduling is
    involved.

- [ ] **Phase 8c — const-generic size inference (`[$N]T`)**
  - `tests/const_generic_size.ty` and `tests/generic_many_typaram_names.ty`,
    both dying `unknown name 'N'`. `fn sumN(xs: [$N]int)` infers N from the
    argument's length, monomorphises, and uses N as an ordinary int const in
    the body.
  - **Why it is not another `_unify` arm, measured 2026-08-24.** The AST keeps
    the size — `ast.TFix(string, [Ty])`, "the size exactly as written", so
    `[$N]int` carries `"$N"` — but `compiler/emit/emit.ty@_ty`'s TFix arm
    lowers it to `"[" + elem + "]"` and **discards the size**. By the time
    `_unify` sees the concrete argument it is a plain `[int]`, so there is
    nothing to bind N from. Adding a TFix arm to `_unify` alone would bind
    nothing; the size has to survive in emit's type STRING first, which
    touches every array path (`_is_arr`, `_elem`, `_apfx`, the array defs).
  - Done when: both fixtures match, and the sweep's other counts do not fall.
  - Verify: `sh scripts/tychoc1_sweep.sh`, plus a control that makes the two
    instances share one N and shows the count drop — two call sites with
    DIFFERENT lengths must produce two instances, which is the whole feature.

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

- [ ] **Phase 7f — emit's C declaration order puts tuple bodies before map bodies**
  - Scope: `compiler/emit/emit.ty@program`. A tuple whose ELEMENT is a map holds
    it by value, so `struct TychoTup0_ { ... TychoMapM0 f1; ... }` names an
    incomplete type. `tests/newtype_over_aggregate.ty` is the first fixture to
    reach it (it was a clean error until Phase 7b's second pass lowered `[N]T`),
    and it is the sweep's only `compile` that does not `link`.
  - **The obvious fix is measured wrong.** Moving `msb` ahead of `ttd` — the map
    struct bodies hold only pointers, so it looks safe — fixes that one fixture
    and breaks FIVE others: `link 232 -> 227`, `MATCH 230 -> 225`, observed
    2026-08-23 and reverted. The order is a real dependency graph, not a list,
    and it needs solving as one.
  - Done when: `tests` `link` equals `compile` in the sweep, with MATCH >= 230.
  - Verify: `sh scripts/tychoc1_sweep.sh` only. Never an unbounded `make test`.

## Phase: close the compile-speed gap to ./tychoc

Scope: tychoc1 compiles 2.0-4.5x slower than ./tychoc (best-of-three, --emit-c:
compiler/main.ty 327ms vs 90ms, tycho-sheet 45 vs 10, mandelbrot 4 vs 2). The
cheap wins are taken (86dc4329, 822ad009, cb133eb0, 5d25b6d0: 663ms -> 327ms on
the self-compile). What is left is not a hot spot -- callgrind shows no caller
above 3.1% and nothing in the type checker above 0.25%, with ~48% of
instructions in arena_alloc + memcpy.

Two measured causes, both architectural:
  - 1,341,342 temp arenas per tycho-db compile, 1,134,759 of which never
    allocate: one per statement, from the codegen model.
  - Value-semantics AST: constructing a node copies its children, and every
    walker that binds a payload copies the subtree (22,032 Expr deep copies in
    one tycho-db typecheck). tychoc1 also runs ~2x the passes ./tychoc does.

PROGRESS. Two self-host miscompiles are fixed (d313844e, 39593d65 -- a
container literal's elements and a returned slice were still pinned to the
dying scope) and the self-built compiler now passes the whole 752-fixture
corpus. The match-arm payload borrow landed (1730220d). Ratio on
compiler/main.ty is ~3.1x, from 5.0x.

The two-stage build is ON (e4e0abbe) and the blocker is fixed (82aae400):
to_str/to_bool emit their argument unchanged, so their result is that place and
retaining it must copy -- core:io was handing back a pointer into the frame it
had just freed. Shipped compiler 341 ms -> 279 ms.

MEASURE WITH CALLGRIND, NOT THE CLOCK. This machine drifts +/-5% between runs,
which is the size of most of these wins; `valgrind --tool=callgrind` gives a
deterministic instruction count.

WHERE IT STANDS on compiler/main.ty (tychoc 89-91 ms alongside):

    5.0x   at the start of this work
    3.8x   341 ms
    3.1x   279 ms   two-stage bootstrap (e4e0abbe), unblocked by 82aae400
    2.8x   254 ms   concat chains flattened (c7f6ed37)
    2.7x   242 ms   arena bump inlined (cb76f804)
    2.6x   230 ms   same-arena strings and ENUMS shared (f1d14147 and after)
    2.4x   217 ms   push keeps a temporary already in the array's arena
    2.2x   200 ms   destructuring a call result, for-in snapshot, size-class guard
    2.0x   182 ms   arm-mutation test narrowed (05d0a710), after 8fc08efc
    1.95x  172 ms   string equality settles on the lengths (aa5d0909)
    1.87x  166 ms   short string copies move inline (23e2d69e)
    1.84x  164 ms   the short move shared by concat/substr (e1b2809b)
    1.81x  161 ms   the lexer stamps a token's file at creation (94add001)
    1.26x  113 ms   for-in snapshots only a collection the body WRITES (5a8df4e7)
    1.24x  110 ms   the scope-elision test scans once (5d4122c1)
    1.21x  108 ms   the parser compares a token's kind in place (a6ecc1ba)
    1.19x  106 ms   string equality checks one byte first (ad604f7a)
    1.18x  106 ms   the hottest token sites build their Tok inline (cc3a9d03)
    1.18x  105 ms   an ENUM out of an inout parameter returns uncopied (d99bc633)
    1.03x   94 ms   the binary parse levels write through an out parameter (622c4951)
    1.00x   92 ms   primary/postfix/unary and every expr() call site too (6588f1b9)
    0.99x   91 ms   unit and each function built into the slot that holds it (c89ec1bd)
    0.99x   91 ms   the Program written into the caller's own, not returned twice (0aa7fc11)
    0.98x   89 ms   short string equality without memcmp (fae3bfc4)

GOAL MET. 21 interleaved rounds, one session: ./tychoc min 91.0 / median 92.3 /
mean 96.9 ms, ./tychoc1 min 89.0 / median 91.2 / mean 94.8 -- 0.977 / 0.988 /
0.979. All three statistics agree, which the 1.00x row's did not (1.004 / 0.997
/ 0.994) and is why that row is not the finish line. Instructions 850.0e6 ->
789.6e6; tychoc still executes only 681.6e6, so the parity is bought by cheaper
instructions, not fewer.

THE RULE, in its final form. An out parameter pays where ONE tree is built up
and would otherwise be re-homed at every level; it LOSES where many small
values accumulate, because each push then pays an individual retention copy
instead of one bulk copy at the return. Measured both ways: the binary levels
and primary/postfix/unary win, tokenize_named's token array loses (830.8e6 ->
833.6e6), unary as a pass-through is flat, and postfix on top of a
tuple-returning primary is worse. The same rule read the other way is what the
slot-building wins are: push a placeholder, let the callee build in place.

A SHAPE WORTH KNOWING, since three of the wins above are instances of it:
passing a value through a small helper LOSES ITS ARENA IDENTITY and costs a
copy. A literal handed to _tk is a place once it is a parameter; a token read by
_expect is copied whole to look at two fields; `return cs.vcn[i]` duplicates a
field of a caller-owned struct into the caller's own arena. Building or
comparing in place at the call site is what removes them.

The general form of the last one does NOT work, and the reason is worth keeping:
teaching _shares that a place rooted in an INOUT parameter may be returned
without a copy -- its arena is the caller's, which is what _parent names -- is
right about LIFETIME and wrong about MUTATION. It reddens 15 fixtures for ~1%,
and const_local fails with `tycho: out of memory`, not a wrong value: the shared
string is a FIELD of the inout struct that its owner appends to IN PLACE, and
the in-place append grows a buffer the returned value still aliases.
_acc_rooted cannot see that -- it tests the root LOCAL, and here the accumulator
is cs.ccode[i]. Shipped for ENUMS only (d99bc633), which have no in-place write
at all: green everywhere, instructions flat because it fires rarely, 105 ms
against 106 over two runs of eleven each way.

    instructions   2.820e9 -> 1.016e9

WHAT IS LEFT, and why the two clean ways out are both measured losses: half the
remaining tycho_str_copy calls are inside AST deep copies, and those happen at
the RETURN boundary -- the parser handing a node up through its recursion. The
two ways to remove them are promotion (build the local in the caller's arena;
removes ZERO copies here, the rule never fires on this code shape) and adoption
(give the parent the scope's blocks; green and -4% instructions, +12% WALL).
Both are written up below with their numbers.

Also ruled out this round, each measured:
  - inlining arena_free's empty-arena case (1.87M of 2.75M arenas are freed
    empty) -- 1.016e9 -> 1.086e9 and 106 -> 111 ms. The call-site bloat costs
    more than the call.
  - raising the emitted C's optimisation level: it is ALREADY -O3
    (driver.ty@build) while ./tychoc itself is built -O2, so tychoc1 is the more
    aggressively compiled of the two. There is no free win there.
  - a cheaper map hash (siphash13 is 2.9%): rejected rather than measured. The
    seed is randomised per process on purpose and a weaker hash reopens HashDoS
    for 2%.

THE ONE THAT MATTERED: for-in was snapshotting its collection defensively, so
every AST walker in every pass deep-copied the child list AND every node under
it on entry. The snapshot only exists so a body can push to the array it walks;
the same mutation test the match arm already used decides it. -32% on its own.

THE RULE that did most of it: a value already in the destination arena is stored
rather than copied, when nothing can write through it -- strings and enums are
immutable, and a value built by a CALL was built in this arena to begin with,
since the call is handed cs.arena as its _parent. Arrays and maps are excluded
(push and element assignment write in place), and so is a place rooted in a live
string ACCUMULATOR (tests/value_semantics.ty).

STANDING: 1.15-1.18x. The gap is ONE problem, and it is scoped: 1.9M of the
4.0M string copies in a self-compile sit inside AST deep copies, and every one
of those crosses an arena boundary at a RETURN. That is ~17% of the profile,
which is the whole remaining gap. Four routes to it are closed with numbers:

    promotion / escape analysis   11 attempts; DEAD, and not for the reason
                                  everything above says -- see the last one
    scope adoption                green, -4% instructions, +12% WALL
    push-value arena              190 fixtures, then 254
    alias scope to caller's arena 19 fixtures; the accumulator guard made it 35

THE EIGHTH PROMOTION ATTEMPT WORKS, IS GREEN, AND IS STILL A LOSS -- and what
it measures is the useful part. Three gaps had to be closed:
  - a name declared by `l, i := mul(...)` (an MDecl) was never a candidate, and
    that is how every recursive-descent function declares the local it returns;
  - a read inside `l = ast.Binop(ln, op, [l, r])` -- the tree accumulator -- was
    counted as "outside a return", which disqualified exactly the target;
  - or_return pinned its subject to &_scope, so a promoted declaration took its
    Ok payload out of a scope the return then freed (tests/calc died on a
    non-exhaustive match, reading a tag out of released memory). Moving the
    subject to the current arena unconditionally leaks the statement's own
    scratch on the early-return path -- ASan caught one block in
    tests/result_void -- so it moves only for a promoted declaration.

With all three: 752/752, parse-check, corelib green, and

    tycho_str_copy   4,038,215 -> 3,707,039   (-331k, -8%)
    instructions     0.996e9 -> 1.020e9       (+2.4%)

The rule fires and removes copies; the census costs about three times what they
save. Two more attempts to make the census cheap: folding its four walks into
one (0.882e9 -> 0.947e9) and then removing the 4-tuple its recursion returned,
which allocates per node (-> 0.943e9). Neither helps, and together they measure
the thing worth knowing: ONE FULL AST WALK COSTS ABOUT 30M INSTRUCTIONS HERE,
3.4% of a whole compile. Any per-function analysis in emit starts 60M in debt.
That is the number a future attempt has to beat, and it is why the answer is
not a better census. AND THE ASSUMPTION UNDERNEATH ALL EIGHT ATTEMPTS IS WRONG: the AST copies
did NOT move (tycho_copy_E_ast__Expr 1,488,864 -> 1,484,873). The parser's
`return (l, i)` is not where they come from. Tracing the callers again puts the
roots in tycho_arr_K3_copy -- STATEMENT BODY arrays -- which this rule excludes,
because promoting arrays reddened every pkg_* fixture in an earlier attempt.
That is where a ninth attempt should start, and it should confirm the root by
call count BEFORE writing any analysis.

OUT PARAMETERS PAY WHERE A TREE ACCUMULATES, AND NOWHERE ELSE. The binary parse
levels (mul/add/isexp/cmp/notexp/andexp/expr) each returned their node, which
copies it into the caller's arena at EVERY precedence step -- O(depth x size)
for a deep expression. Writing through an `out: inout ast.Expr` builds the tree
where it will live: -3.3%, 95 -> 93 ms (622c4951).

The same transformation applied to levels that do NOT accumulate is a LOSS:
unary is a pass-through (0.850e9 -> 0.851e9) and postfix, which does accumulate
but whose base comes from primary through a shim, is worse still (-> 0.865e9,
93 -> 95 ms). The rule is: convert a level only where the node it returns is
built up across iterations from what the level below produced.

AN ARENA'S MEMORY IS NOT FRAME-SCOPED, and that is worth knowing before anyone
tries to make it so. Compiling compiler/main.ty asks the pool for 842k blocks to
bump 285 MiB -- about 350 bytes per block acquired -- so most arenas take a
64 KiB block, use a few hundred bytes and hand it back. Giving Arena a 256-byte
inline buffer (on the C stack for a scope arena) removes almost all of that
traffic and reddens 3 fixtures with ASan reporting STACK-USE-AFTER-RETURN. The
reason is deliberate: to_under aliases its argument, ./tychoc does too and
tests/newtype_agg records that answer, so a value can legitimately outlive the
frame whose arena holds it. Arena storage must stay off the stack.

THE ELEVENTH ATTEMPT SETTLES IT, AND CORRECTS THE DIAGNOSIS ABOVE. Promote
EVERY top-level local of a function that returns something heap -- no census, no
walk, no analysis cost whatsoever, just a mark. Green everywhere, and:

    instructions   0.879e9 -> 0.942e9   (+7%)
    allocations    7.59M -> 8.84M, bump 285 MiB -> 315 MiB
    wall           95 ms -> 107 ms

Allocations go UP. So the cost was never the census: building locals in the
caller's arena is itself a pessimisation here, because a promoted local's own
assignments then copy INTO _parent and nothing is freed until the caller
returns. Every earlier attempt blamed its walk; the walk was not the problem.
The family is dead.

ALSO MEASURED, both negative:
  - returning a PARAMETER-rooted place without copying, on the theory that the
    per-statement scratch arena is what makes a parameter's lifetime unknowable.
    Removing that arena (a statement's expression evaluated in &_scope) is GREEN
    on its own -- and costs 2.6%, 0.879e9 -> 0.902e9, 95 -> 97 ms: fewer arenas
    but worse locality. And the sharing it was meant to enable still reddens 23
    fixtures, so a parameter's buffer has other ways of being shorter-lived than
    the _parent it returns into (a loop's scratch arena resets, for one).

The FBIP recycling that aliasing would disturb is worth 2.5% on its own
(measured by making arena_recycle a no-op: 0.996e9 -> 1.021e9, 105 -> 109 ms),
so it cannot simply be dropped to make room.

What is left is not a patch. It is a decision about how the compiler assigns
arenas -- whether a callee can be told to build its result where the caller
wants it, rather than building it locally and copying. Everything above is an
attempt to infer that after the fact, and all four inferences are unsound,
too expensive, or both.

WHERE THE TIME IS BY PHASE, which reframes all of the above -- measured with the
compiler's own front-end modes on compiler/main.ty:

    --parse       2 ms   (one file)
    --resolve    78 ms   (load: read + lex + parse EVERY file, then resolve)
    --typecheck  79 ms   (+1)
    --emit-c    105 ms   (+26)

The BACK END IS 26 ms of 105. Everything above was aimed at emit's copies, and
emit is a quarter of the compile; the front end is three times it. A callgrind
of --resolve alone puts tycho_str_copy at 15.1%, tycho_str_eq 6.4%,
arena_alloc_i 5.9%, the Expr copy 5.4%, and -- the one that stands out --
tycho_arr_K17_copy at 5.1%: the whole TOKEN ARRAY, deep-copied once per file
with all four strings of every token, at tokenize_named's return.

That last one looks like the next lever and is NOT: rewriting tokenize_named to
fill an `inout` array moves the copy rather than removing it, because push into
an array owned by another arena copies each element anyway. It only pays if the
token is BUILT in the destination arena -- the push-value-arena change, now
tried TWICE and worse the second time: 190 fixtures the first way, 254 the
second (computing the target's owner before evaluating the value, so a pending
`?A` element type and a nested place both read the wrong arena). Do not try a
third shape of it without first understanding what _ownerof answers for a push
target that is a field, an element, or an inout parameter.

PROFILE NOW (exclusive): tycho_str_copy 13.1%, memcpy 11.7%, arena_alloc_i
7.4%, tycho_copy_E_ast__Expr 7.0%, the Expr-array copy 6.7%, memcmp 5.6%.

WHERE THE REST OF THE COPYING IS, traced through the call graph rather than
guessed: 4.6M of the 7.1M string copies are inside tycho_copy_E_ast__Expr; those
Expr copies come from tycho_arr_K0_copy; and 687k of THOSE come from
tycho_copy_E_ast__Stmt, which comes from tycho_arr_K3_copy -- copying an
[ast.Stmt] BODY. So the chain is: a body array is retained (parse builds `body`
then wraps it in ast.IfS), arrays are mutable so the sharing rule cannot touch
it, and the copy takes every statement, every expression and every string under
it. Eliding that needs the source array to be built in the destination arena,
which is the escape analysis -- measured four times, a loss every time.

ALSO RULED OUT: returning a shared singleton for an EMPTY string copy. Sound --
there are no bytes to alias and every write path reallocates -- but it measured
+1.6% (1.642e9 -> 1.669e9): the branch on every copy costs more than the
allocations it saves, so empty strings are not as common in copies as the AST's
shape suggests. Reverted.

RULED OUT, each measured against the baseline:
  - the full return-only escape analysis (src/tychoc.c@collect_ret_alias) --
    +7.7%. The name census is linear and still costs more than the copies it
    removes, and promoting locals to _parent defers every free.
  - the cheap half of it (promote a local returned BY NAME) -- the SELF-BUILT
    compiler fails 24 fixtures. Something else in this codegen assumes a local
    lives in _scope; the string accumulator's in-place append is the first
    suspect, since it needs its buffer to be the last allocation in its arena.
Both reverted. Do not re-port them without first finding what the promotion
breaks.
  - evaluating push()'s value directly in the ARRAY's arena (instead of the
    scope, then copying) -- 190 fixtures. The narrow version that only drops the
    copy when both are already in &_scope is what shipped.
  - (This one was WRONG and is now shipped as 9fbe9f35: widening the arena fast
    path to test the size CLASS rather than the bucket table's existence was
    dismissed on a wall-clock reading of 221 ms against 217, inside this
    machine's drift. Measured in instructions it removes 3.92M slow-path calls
    and 2%. Do not judge a change of this size by the clock.)
  - the return-only escape analysis, SECOND attempt, this time with a bounded
    census (candidates come from the return statements, only those names are
    counted over the body -- so the +7.7% of the first attempt is gone). It
    reddens 32 fixtures: every pkg_* program plus tests/subscript, and subscript
    fails with a WRONG VALUE (r=11,31,30 against 11,22,30), not a crash. So the
    promotion introduces aliasing somewhere the "every read is inside a return"
    rule believes it has excluded. Worth resuming from that fixture: it is small
    and it is the only non-package one.

    SEVENTH attempt, aimed squarely at why the rule never fires. The parser
    builds its tree with `l = ast.Binop(ln, op, [l, r])`, and that read of l is
    not inside a return, so "every read is inside a return" excludes it. Extended
    the rule to count a read inside an assignment BACK TO THE SAME NAME as
    return-only. It still does not pay -- 0.996e9 -> 1.019e9, +2% -- and it
    reddens tests/calc, whose own parser is that exact shape: the compiled
    program dies with "non-exhaustive match", so the promoted local holds the
    wrong value at run time. Seven attempts, seven negatives.

    SIXTH attempt, and the one that explains all the others. Counting the calls
    it removes: tycho_str_copy 4,398,272 without it and 4,473,320 WITH it;
    tycho_copy_E_ast__Expr 578,719 -> 586,274. It removes NOTHING -- the counts
    go up, by the analysis's own allocations. The rule never fires on this
    codebase, and the shape says why:

        l, i := mul(toks, i)
        l = ast.Binop(ln, op, [l, r])
        return (l, i)

    `l` is read in the Binop construction, which is not inside a return, so
    "every read is inside a return" excludes exactly the pattern the whole idea
    was aimed at. Dropping the census entirely (promote any local a return
    mentions) is still +1.4% AND reddens tests/calc. Six measurements: this line
    is closed, and it is closed on MECHANISM, not on cost.

    FOURTH attempt, after 05d0a710 made every AST walker cheap -- the reason
    the earlier ones were assumed to have failed. Still a loss: 1.765e9 ->
    1.976e9, 182 ms -> 199 ms. Four measurements now say the same thing, and the
    walkers being cheap does not change it. Stop trying this in emit.

    THIRD attempt, restricted to ENUM locals (arrays and maps still copy, which
    is what the pkg_* failures were about): GREEN everywhere -- 752/752,
    parse-check, corelib -- and 17% SLOWER. 1.924e9 -> 2.246e9, 200 ms -> 233 ms.

    So the conclusion is not "the rule is wrong" but "the analysis cannot live
    here". Three variants have now been measured: a full per-name census
    (+7.7%), a bounded one restricted to the names the returns mention (+17%
    with the enum restriction), and move-on-last-use (+8%). Every one of them
    costs more in emit than the copies it removes, because emit runs the walk
    per function on every compile.

    WHERE THE COPYING IS, so the next attempt starts from evidence: 5.5M of the
    8.2M string copies in a self-compile are inside tycho_copy_E_ast__Expr, and
    5.5M of the 5.9M AST copies come from tycho_arr_K0_copy -- an Expr ARRAY
    being deep-copied. That is the parser returning `(node, i)` up through its
    recursion, re-homing the subtree at every level.

    THE WAY OUT, if anyone takes this up: compute the escape set in a pass that
    ALREADY walks the body -- resolve or tcheck -- and hand emit a set to look
    up. Every failure here has been the cost of the walk, never the rule.

## Tried and failed: ALIAS the scope to the caller's arena

The cheapest possible form of adoption: for a function that returns a PLACE --
which is exactly the set whose returns copy -- give it no scope arena at all and
let its body allocate directly in _parent. No blocks to splice, no teardown
walk, and the return copy vanishes because the value is already there. The
qualifying test needs no types: a return whose expression is a place.

19 fixtures. Guarding it further -- never alias a body with an in-place string
accumulator, whose buffer must grow where it sits -- makes it WORSE, 35, so the
accumulator is not the (only) cause. Reverted, and this is the third shape of
"stop copying at the return" to fail after adoption (green but slower) and
promotion (seven attempts). The failures cluster on or_return, map parameters
and value_semantics, which is where to start if anyone tries a fourth.

## Tried and failed: ADOPT the scope instead of copying out of it

What is left of the copying is all at the RETURN boundary. Traced: the AST deep
copies are now almost entirely self-recursive (an Expr copy pulling its child
array, which pulls each child), and the ROOTS are the parser's own returns --
h_parse__mul, __postfix, __isexp -- handing a node up through the grammar. Each
level copies the whole subtree it just built.

Copying is the wrong instrument there. The scope is ABOUT TO DIE and the value
lives in it, so splicing the scope's blocks into the parent is O(1) where the
copy is O(the value), and every pointer stays valid. Sketched as
`arena_adopt(Arena *p, Arena *c)` in the runtime plus an `adopt` flag threaded
through the return's expression emit so tuple elements and payloads skip their
copies too.

BUILT AND GATED GREEN, AND STILL NOT WORTH IT. Two bugs had to be fixed first:

  - arena_reset pools every block after the head, which would hand back memory
    an adopted value still points into. Fixed with a `pinned` flag on HBlock
    that a reset keeps and only arena_free releases.
  - _ownerof answers "&_scope" for anything it cannot find, PARAMETERS included,
    so `return toks` adopted a buffer that belongs to the caller and handed the
    caller an alias of its own array. That is what reddened 14 fixtures, all of
    them slice/value-semantics ones. Fixed by requiring the root to be a local
    of the function being emitted.

With both fixed: 752/752, parse-check, corelib, conc all green, and

    instructions   1.016e9 -> 0.977e9   (-4%)
    allocations    11.1M -> 9.6M, bump 940 MiB -> 329 MiB
    wall           108 ms -> 121 ms     (+12%)

The instruction count improves and the WALL CLOCK gets worse, which is the whole
lesson. TYCHO_ARENA_STATS says where it goes: teardown is 19% of the run. Every
adopting scope hands its parent a whole block that is mostly empty, so the block
chains grow long and arena_free walks them. Adoption trades O(value) copying for
O(blocks) teardown plus the cache cost of a working set that no longer shrinks.

Reverted. Anyone taking it up needs a way to adopt only a scope whose live bytes
are worth a block -- which is a RUNTIME property, not a compile-time one.

## The biggest measured prize, and why it is not shipped

NARROWING THE ARM-MUTATION TEST IS WORTH 21%. `_mut_e` is conservative at ANY
unqualified call taking the binding first (emit.ty, the _mut_e Call arm). That
is exactly the shape of every recursive AST walker -- `_cn_es(k, nm)`,
`_mut_es(k, nm)`, `_names_in(kk, &out)` -- so every walker deep-copies its
payload at every node, and so does every analysis anyone writes on top of one.
Restricting it to the three builtins that really write their receiver
(push/pop/reserve; an `inout` parameter needs `&x` at the call site, which is an
Addr and is caught separately) gives:

    instructions   1.924e9 -> 1.521e9   (-21%)
    wall           200 ms -> 156 ms     -- a ratio of 1.7x, the best seen

SHIPPED as 05d0a710, once the real cause of the two map failures was found and
fixed (8fc08efc: an assignment built its value in &_scope and wrote the pointer
through an inout parameter, handing the caller memory the callee then freed).
The record of how it was found is kept below because the method is the lesson. A SEMANTICALLY INERT perturbation of emit.ty reddens the same two
fixtures:

    add a `curfn: string` field to struct C, set it in _fnbody around the body
    emit, add

        fn _bor_ok(cs: inout C) -> bool:
            return len(cs.curfn) > 0 and strings.starts_with(cs.curfn, "zzz__")

    and gate the two arm-binding copies on `not _bor_ok(&cs) or _mut_b(...)`.
    No function is named zzz__, so _bor_ok is always false and every binding
    copies exactly as it does at HEAD -- and tests/maps FAILS.

Controls, each observed: HEAD passes; the field ALONE passes; the field plus the
assignment passes. It takes the extra function and the two gated conditions --
still behaviourally identical -- to break it. So there is a LATENT,
LAYOUT-SENSITIVE miscompile in the self-built compiler, and the tree is green by
luck rather than by construction.

That reframes everything below it: the borrow narrowing was probably innocent
all along, and the 21% is blocked by this bug rather than by unsoundness.

What is known about the latent bug:
  - Stage 1 (built by ./tychoc) compiles both fixtures cleanly. Only the
    SELF-BUILT stage 2 fails, so the unsound borrow is somewhere in tychoc1's
    own source, not in the fixtures.
  - The symptom is a hoisted temp DECLARATION going missing: the emitted C says
    `h__mk4 undeclared`, so the `pre` accumulator in emit@_hoist_legs lost text
    it had appended. Once it emitted raw heap bytes into the middle of a line.
  - Restricting the borrow to ENUM payloads only does NOT fix it, and neither
    does requiring the match SUBJECT to be a place. Both were tried -- and both
    are explained by the perturbation result above.
  - Ruled out as the mechanism: the in-place string accumulator's length/cap
    shadow going stale across the recursive _hoist_legs call. `pre` is a
    parameter there, and accumulators are locals only, so that path never
    applies.
  - The 21% survives those restrictions largely intact, because copying an enum
    copies its payload arrays too -- tycho_arr_K0_copy is called BY
    tycho_copy_E_ast__Expr.

Next step for whoever picks this up: find which enum-payload binding in emit or
parse is written through. `_hoist_legs` and the compound-index-assign path are
where the corruption surfaces; the write itself is elsewhere.
  - MOVE-ON-LAST-USE itself, written and gated green (752/752, parse-check,
    corelib): a local read exactly once and not from inside a loop hands over
    its buffer instead of copying. It is a NET LOSS of 8% -- 217 ms -> 234 ms --
    and the split is the useful part: with the moves disabled but the census
    still running, 233 ms. So the read census costs ~16 ms and the moves it
    enables save ~1. Two reasons, both worth knowing before anyone tries again:
    the census is a per-function AST walk plus two maps, and the sites it
    reaches (declaration and assignment) are NOT where the copying is. The
    remaining tycho_copy_E_ast__Expr calls are deep copies ACROSS arenas --
    returns, and containers built in another arena -- which no last-use rule can
    elide. Reverted; the diff is recoverable from this commit's parent if the
    census is ever made free.
  - returning an IMMUTABLE value read out of a PARAMETER without copying it --
    12 fixtures. The premise is wrong: a parameter's buffer is not always in an
    arena that outlives the caller's _parent. `f(g())` passes g's result out of
    the caller's per-statement scratch, and f returning it lets the caller store
    into &_scope something that dies with that statement.
  - extending the same-arena sharing to STRUCTS with no array or map field
    (sound -- assigning a struct copies it by value and writing a field replaces
    the pointer) -- measured neutral, 1.9648e9 against 1.9625e9, because the
    struct copies that cost are inside array copies and at returns, neither of
    which the rule reaches. Not kept: a generalisation with no measured win is
    still code.

Done when: the ratio is <= 1.0 on compiler/main.ty and tycho-db.
Verify: best-of-three wall clock, both compilers, same input, --emit-c.
Gates: TYCHOC=./tychoc1 make test (752), make parse-check.
Not this: --native (measured slower, 341ms), -O3 (already used), per-token
field trimming (<1%), the copy_live lint (<1%), a per-loop scratch arena reset
per iteration (3 fixtures leak -- an early return escapes it), and one
per-function scratch reset per statement (307 fixtures: values DO outlive their
statement, and a reset reissues the same block). That last one is the useful
negative: the per-statement arena_new/arena_free is not removable without the
move-on-last-use liveness analysis that decides which values escape.

## Phase: the emitted-code performance gap to ./tychoc

Measured 2026-08-24, after `170f587c` and `67cd912c` made `bench/` honour
`TYCHOC`: benchmarks compiled by `./tychoc1` were run against the same source
compiled by `./tychoc` for the first time. Wall time is 1-4x worse; **peak RSS
is 100-400x worse** — strarr_build 1 MB -> 360 MB, inout_fill 1 MB -> 392 MB,
prongB iter-transform 4 MB -> 1536 MB, latency 4.5 MB -> 1533.7 MB, prongB
binary-trees 13 MB -> 767 MB, maptree 6 MB -> 504 MB. Two workloads are FASTER
under tychoc1: treewalk 38 ms -> 7 ms, prongB json-parse 1405 ms -> 1135 ms.
strarr_build, inout_fill and treewalk were reproduced independently.

**What is NOT established, stated before the ranking:**

- **No pass was isolated by rebuilding `./tychoc` without it.** The item-1
  ranking is inference from the shape of the emitted C, not measurement.
- **Items 6-9 have no measured workload at all** — they are gaps read out of
  `src/tychoc.c`, with no bench row attributable to them.
- **dbquery has no tychoc1 number**: `tychoc1: unknown option '--pkg'`, a CLI
  gap, not a codegen one.
- **The ms figures are best-of-1 on a non-quiesced box.** The RSS figures are
  deterministic and are the ones to trust.

The phases below are ranked by expected size of win. Each names the
`src/tychoc.c` pass and the emit site that stands in for it.

- [ ] **Perf 1 — per-iteration loop scratch arena**
  - `src/tychoc.c:12272` opens an `_scr<N> = arena_child(...)` per loop,
    `src/tychoc.c:12276` resets it at the top of each iteration and
    `src/tychoc.c:12287` frees it after. `compiler/emit/` has **zero**
    `arena_reset` and zero `_scr`: every transient goes to `&_scope`
    (`compiler/emit/emit.ty:2250`), which lives for the whole function, so a
    loop's garbage accumulates until the function returns.
  - Evidence: dominant cause. Every 100x+ RSS row above is a loop building
    transients — strarr_build, inout_fill, iter-transform, latency.
  - Known hazard, already measured under "close the compile-speed gap": a naive
    per-loop reset leaked 3 fixtures because an early `return` escapes the
    scratch. The escape analysis is the work, not the arena.
  - Done when: a loop's transients are freed per iteration and peak RSS on
    strarr_build is within 2x of `./tychoc`'s.
  - Verify: `TYCHOC=./tychoc1 make test` at 752, `make parse-check`, and the
    RSS of the four named benchmarks before and after.

- [ ] **Perf 2 — bounds-check elision for monotone indices**
  - `src/tychoc.c:9351-9540` proves an index in range, gated at
    `src/tychoc.c:9450`. `compiler/emit/` always emits the checked accessor
    (`compiler/emit/emit.ty:1625`).
  - Evidence: wall time only; no RSS component. Unquantified — no benchmark
    isolates it.

- [ ] **Perf 3 — nullary-variant singleton returned by copy**
  - `src/tychoc.c:9793-9806` returns a shared singleton for a payload-free enum
    variant. `compiler/emit/emit.ty:4256` copies one out.
  - Evidence: allocation count on any Option/Result-heavy loop; no isolated
    measurement.

- [ ] **Perf 4 — move-on-last-use**
  - `src/tychoc.c:9543-9713` hands a buffer over at its last read instead of
    copying. `compiler/emit/emit.ty:1320` (`_copy`) always copies.
  - Evidence: a standalone attempt on tychoc1's OWN compile speed was a net 8%
    LOSS (recorded above), because the census cost more than the moves saved.
    That is a cost measurement of the analysis, not of the win on the emitted
    programs, and it does not settle this item either way.

- [ ] **Perf 5 — construction-argument move**
  - `src/tychoc.c:10029-10050` moves an argument into the aggregate being built.
    `compiler/emit/emit.ty:2437` and `compiler/emit/emit.ty:3084` copy.
  - Evidence: binary-trees (13 MB -> 767 MB) is the shape; not isolated.

- [ ] **Perf 6 — map accumulator rewritten in place**
  - `src/tychoc.c:9928` and `src/tychoc.c:9938` recognise `m[k] = ...` /
    `delete` on the map being folded and write in place. `compiler/emit/` has
    **no** `map_set` fast path at all.
  - Evidence: **none measured.** maptree (6 MB -> 504 MB) is the plausible
    workload; nothing attributes it.

- [ ] **Perf 7 — push-cursor caching**
  - `src/tychoc.c:9643`, `:9671`, `:9698` (`fuse_gather`/`fuse_open`/
    `fuse_close`) hoist the destination cursor out of a push loop.
    `compiler/emit/emit.ty:3166` emits an independent push per iteration.
  - Evidence: **none measured.**

- [ ] **Perf 8 — loop-carried spine recycling**
  - `src/tychoc.c:11792` reuses a container's spine across iterations when the
    name is not an accumulator.
  - Evidence: **none measured.**

- [ ] **Perf 9 — sink-argument adopt**
  - `src/tychoc.c:9714-9733` adopts a `sink` argument's buffer instead of
    copying it. `is_sink` is parsed (`compiler/ast/ast.ty:30`) and **never read
    by `compiler/emit/`**.
  - Evidence: **none measured.**

- [ ] **Perf 0 — `tychoc1` has no `--pkg`, so dbquery cannot be scored**
  - `tychoc1: unknown option '--pkg'`. A CLI gap, not codegen; it is here
    because it is why one benchmark row is blank.
