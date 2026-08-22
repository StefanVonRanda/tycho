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

- [ ] **Phase 1 — skeleton and `print(str(1))` end to end**
  - Scope: `compiler/{lex,parse,ast,emit,driver}/`, `compiler/main.ty`, a
    `tychoc1` target in `Makefile`. No `types/` yet. `print(1)` does not
    compile under `./tychoc` — `print` takes a string — so the milestone
    program is `fn main(): print(str(1))`.
  - Done when: `make tychoc1` builds, and `./tychoc1` on that program emits C
    that `cc` compiles to a binary whose stdout is byte-identical to the binary
    `./tychoc` produces from the same source.
  - Verify: build both, run both, `cmp` the two stdouts; show the diff of the
    two emitted `.c` and state what differs and why.

- [ ] **Phase 2 — lexer complete**
  - Scope: `compiler/lex/`. Every token of the locked language: the 30 reserved
    words, `$T` type params, `bounded[N]`, string/char/byte/number literals with
    escapes, comments, and the significant-indentation INDENT/DEDENT rules.
  - Done when: a `--dump-tokens` mode lexes all 274 `tests/*.ty` plus every
    `.ty` under `corelib/` with zero unknown-token errors.
  - Verify: the dump over that corpus, plus a round-trip control — tokens
    re-joined must reproduce the source modulo whitespace on at least 50 files,
    and a deliberately corrupted token table must redden it.

- [ ] **Phase 3 — parser: expressions and statements**
  - Scope: `compiler/parse/`, `compiler/ast/`. Full expression grammar with
    precedence, `if`/`elif`/`else`, `for`/`in`, `while`, `match`, `select`,
    `spawn`, `return`/`break`/`continue`, destructuring, expression-valued
    `if`/`match`.
  - Done when: every `tests/*.ty` that `./tychoc` accepts parses without error,
    and every `tests/reject/` fixture that is a *syntax* rejection is rejected.
  - Verify: the parse sweep over both corpora, with the accept and reject counts
    printed separately; a reject leg that cannot fail is decoration, so show one
    deliberate break reddening it.

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
