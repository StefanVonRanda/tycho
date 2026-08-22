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

- [ ] **Phase 2b — three lexer parity gaps deferred out of Phase 2**
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

- [ ] **Phase 4 — parser: declarations**
  - Scope: `compiler/parse/`. `fn`, `struct`, `enum`, `newtype`, `handle`,
    `soa`, `bounded[N]T`, generics with `$T` and `where` (comma-separated, five
    closed predicates), `import`, `package`, `const`, `extern`, `subscript`,
    `sink`/`inout`, `# deprecated:`.
  - Done when: the whole tree parses — `tests/`, `corelib/`, `tools/`,
    `examples/`, `server/`, `bench/` — matching `./tychoc`'s accept/reject
    verdict on every file.
  - Verify: a per-file verdict differential against `./tychoc --emit-c`
    (parse-only comparison: both accept, or both reject), zero disagreements.

- [ ] **Phase 5 — `types/`: names, packages, scoping**
  - Scope: `compiler/types/`. Package and import resolution, the function table,
    variable scoping, const folding, corelib discovery.
  - Done when: every name in the tree resolves to the same declaration
    `./tychoc` resolves it to, and undefined-name rejections agree.
  - Verify: a symbol-table dump differential over `corelib/` and `tools/`,
    plus the reject corpus for undefined names.

- [ ] **Phase 6 — `types/`: checking and inference**
  - Scope: `compiler/types/`. Type resolve, inference, generics
    monomorphisation, newtype distinctness, affine (handle/task/channel) rules,
    `where` predicates, `bounded` capacity.
  - Done when: `tychoc1` agrees with `./tychoc` on accept-vs-reject for every
    file in `tests/`, `tests/reject/` and `corelib/`.
  - Verify: the verdict differential, counts printed; and the affine refusals
    checked individually, since a checker that refuses everything scores the
    same as a correct one on a reject corpus.

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
