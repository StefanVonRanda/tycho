# Element-wise arithmetic on arrays

Previous plan complete and archived at
[docs/internals/plan-postfreeze-rawstring-DONE.md](docs/internals/plan-postfreeze-rawstring-DONE.md)
(phases 1–8, 10, 11 done; 9 dropped with its measurement kept). Its four
unclosed discoveries are carried forward at the bottom of this file rather than
buried in an archive.

## Goal

`new_ideas.md` item 3: make `a * b` element-wise on arrays. Settled by the user,
2026-07-29:

- **Both array kinds.** `[N]T` mismatch is a **compile error** — `N` is static, so
  the compiler can see it. `[T]` mismatch is a **runtime abort**, the same shape
  as an out-of-bounds index.
- **Arithmetic only** — the operators the scalar rules already allow for that
  element type. Comparisons stay scalar: `==` already means whole-array equality
  (`docs/spec/12-aggregates.md:146`) and redefining it element-wise would be a
  silent breaking change to every program that compares two arrays.
- **Scalar broadcast both directions.** `a * 2` and `2 * a` scale every element.

Done looks like: `[1,2,3] * [2,2,2]` evaluates to `[2,4,6]`, `[1,2,3] * 2` to the
same, a length mismatch on `[T]` aborts with a diagnostic naming both lengths, a
mismatch on `[N]T` is refused at compile time, and every gate is green.

## Pre-flight

- **Worst case:** a silent wrong answer. This feature makes an expression that is
  a type error today start producing a value, so nothing in the tree can regress
  — but an off-by-one in the element loop or a mis-sized allocation produces
  plausible garbage rather than a crash. Verification is therefore by **golden
  output on real values**, not by "it compiles".
- **Reversibility:** fully. Each phase is a commit; no data touched; no
  destructive path.
- **Verified — `+` on arrays is free.** `docs/spec/09-expressions.md:35` gives `+`
  on two `string`s as concatenation and `:42` gives `bytes + bytes`; arrays are
  not in either. So element-wise `+` claims an operator that is currently a type
  error, not one with an existing meaning. This is the single most important
  fact in the plan and it is why the feature is additive.
- **Verified — the site.** The arithmetic arm of the binary-operator typechecker
  ends at `src/tychoc.c:5982` with `die_at(..., "arithmetic requires two ints or
  two floats (got %s, %s) ...")`, immediately after a `bytes`-specific arm at
  `:5980-5981`. The arms above it (`:5960-5975`) are the existing shape to copy:
  `T_CHAR`, `T_FLOAT`, newtypes over int/float, and the two literal-adaptation
  rules. A new array arm belongs in that chain, before the two `die_at`s.
- **Verified — two array kinds, and the precedent for element-wise semantics.**
  `docs/spec/12-aggregates.md:26` (growable `[T]`) and `:127` (fixed `[N]T`), and
  `:146` records that `==` on a fixed array already "compares element-wise" and
  requires the same static `N`. That is the rule this plan generalises to
  arithmetic, and its diagnostic is the one a mismatch message should read like.
- **Verified — element-wise arithmetic does not already exist.** `grep -rn
  'element-wise\|elementwise\|broadcast' docs/spec/ docs/*.md` returns four hits,
  all about tuple/array **equality** or a tuple literal being *checked*
  element-wise (`docs/spec/12-aggregates.md:146`, `:385`,
  `docs/spec/04-inference.md:51`, `docs/spec/appendix-e-conformance.md:84`).
  None is arithmetic.
- **Verified — the runtime already fails loudly in this style.** `runtime/tycho_rt.c`
  prints and dies for a float-to-int conversion out of range (`:187`), a bad
  `reserve` capacity (`:197`), an out-of-range string length (`:864`) and an
  out-of-bounds string index (`:1040`). A length-mismatch abort should be written
  to match those, not invented.
- **Verified — the fixtures have somewhere to live.** Frozen `tychoc0` will refuse
  every program in this plan, because the whole point is syntax it rejects as a
  type error. `tests/postfreeze/` exists for exactly this and is now covered by
  `tests/run.sh` and `scripts/asan_self.sh` and excluded from the two
  frozen-compiler lanes. This plan is the first real customer of the door phase 1
  of the previous plan opened.
- **Assuming — which operators apply per element type is whatever the scalar
  rules already allow, and I have not enumerated them.** `%` in particular is
  very likely int-only, and `/` on ints likely has a divide-by-zero path already.
  **Risk if wrong:** an arm that permits `[float] % [float]` where `float % float`
  is refused, i.e. the array form is more permissive than the scalar form. **Phase
  1 resolves this by reading `src/tychoc.c:5940-5982` and deriving the per-type
  operator set from the existing arms rather than asserting one**, and the rule it
  must satisfy is that `a OP b` on arrays is legal **iff** `a[i] OP b[i]` is legal.
- **Assuming — a fresh array is the right result, not an in-place write.** Tycho
  is value-semantics (the previous plan's `FRICTION.md` notes call this out as one
  of the language's best properties), so `c := a * b` must not alias or mutate `a`.
  **Risk if wrong:** aliasing bugs that goldens would catch only by luck. Phase 1
  must include a fixture that mutates `a` after `c := a * b` and asserts `c` is
  unchanged.

## Phases

- [x] **Phase 1 — array ⊕ array, both kinds, with the two mismatch behaviours**
  - Scope: the arithmetic arm of the binary-operator typechecker in
    `src/tychoc.c` (the chain ending at `:5982`), the corresponding codegen, one
    new runtime helper if the emitted C wants one, and fixtures + goldens in
    `tests/postfreeze/`. **Not** touched: scalar broadcast (phase 2), the spec
    (phase 3), `tools/` and `editors/` — this feature adds **no new token**, so
    the formatter, the LSP and both grammars should need nothing. Confirm that
    claim by running `scripts/tools_check.sh` and `scripts/editors_check.sh`; if
    either reddens, that is a finding, not a licence to edit them here.
  - Derive the legal operator set per element type by reading `src/tychoc.c:5940-5982`,
    not by assuming. The rule: `a OP b` is legal **iff** `a[i] OP b[i]` is legal.
    State the derived set in the commit message.
  - `[N]T` mismatch: compile error, worded like the existing `==` rule at
    `docs/spec/12-aggregates.md:146`. `[T]` mismatch: runtime abort naming both
    lengths, written to match `runtime/tycho_rt.c:187`/`:197`/`:1040` in tone and
    exit behaviour.
  - Done when: `tests/postfreeze/` has a fixture whose golden shows `[1,2,3] *
    [2,2,2]` → `[2,4,6]` across every legal element type and operator; a fixture
    proving the result is a **fresh** array (mutate a source after the operation,
    assert the result is unchanged); a `tests/abort/` fixture for the `[T]`
    length mismatch; and a `tests/reject/` fixture for the `[N]T` mismatch with
    its diagnostic asserted.
  - Verify (gate budget, `CLAUDE.md`): `make test`, then `sh scripts/frontparity.sh`
    (`agreed` must not fall — a typechecker change that reddens `tools/*.ty` under
    the frozen compiler shows up here), then `sh scripts/asan_self.sh` (this adds
    a new allocation path and the postfreeze corpus is in that lane since phase 5
    of the previous plan). **Not** `make ci`.

  ### Evidence — phase 1, 2026-07-29

  **The derived operator set, and the lines that settled it.** Read
  `/home/igzo/github/tycho/src/tychoc.c:6020-6063` — the arithmetic chain — and
  derived the per-element-type set from the arms rather than assuming one. The
  rule enforced is `a OP b` on arrays is legal **iff** `a[i] OP b[i]` is legal:

  | element type | operators | the arm that settles it |
  |---|---|---|
  | `int` | `+ - * / %` | `/home/igzo/github/tycho/src/tychoc.c:6032` (arith), `/home/igzo/github/tycho/src/tychoc.c:6020-6026` (`%`) |
  | `u8 u16 u32 u64 i8 i16 i32 i64` | `+ - * / %` | `/home/igzo/github/tycho/src/tychoc.c:6036`; `%` likewise from `/home/igzo/github/tycho/src/tychoc.c:6020-6026` (`is_sized_int` passes) |
  | `float` | `+ - * /` | `/home/igzo/github/tycho/src/tychoc.c:6044`; `%` refused |
  | `f32` | `+ - * /` | `/home/igzo/github/tycho/src/tychoc.c:6037`; `%` refused |
  | numeric newtype over `int`/`float` | `+ - * /` | `/home/igzo/github/tycho/src/tychoc.c:6045`; `%` refused |
  | `char` | `+ -` | `/home/igzo/github/tycho/src/tychoc.c:6038-6043` (`char±char -> char`) |

  The pre-flight's open question was `%`. It is **not** simply int-only: the
  modulo/bitwise arm at `/home/igzo/github/tycho/src/tychoc.c:6021` tests
  `(lt != T_INT && !is_sized_int(lt)) || lt != rt`, so `%` is legal for `int`
  **and** every sized int, and refused for `float`, `f32` and for a newtype over
  `int` (a newtype's `Type` is not `T_INT`, and `is_sized_int` is false for it
  too). The array form matches the scalar form exactly in all six rows — the
  risk the pre-flight named (`[float] % [float]` legal where `float % float` is
  not) does not materialise. Both refusals verified live:

      $ ./tychoc e.ty            # a := [1.0, 2.0]; a % a
      error: `%` is not defined element-wise on [float], because `%` is not defined on float
      $ ./tychoc n.ty            # type Count = int; a := [Count(7), Count(2)]; a % a
      error: `%` is not defined element-wise on [Count], because `%` is not defined on Count

  `&`, `|`, `^`, `<<`, `>>` are **excluded**, and that was a decision, not an
  oversight. They are not arithmetic (the Goal above says "arithmetic only"),
  and the new arm at `/home/igzo/github/tycho/src/tychoc.c:5989` lists only the
  five arithmetic tokens, so an array operand still falls through to the bitwise
  arm at `/home/igzo/github/tycho/src/tychoc.c:6020` and the shift arm at
  `/home/igzo/github/tycho/src/tychoc.c:5965` and is refused there in their
  existing wording — behaviour unchanged by this phase:

      $ ./tychoc ...   # a := [1, 2]; a & a
      error: modulo / bitwise operators require two matching integers (got [int], [int])
      $ ./tychoc ...   # a := [1, 2]; a << a
      error: shift operators require integer operands (got [int], [int])

  The arm sits at `/home/igzo/github/tycho/src/tychoc.c:5973-6019`, i.e. **after**
  the shift arm and **before** the modulo/bitwise arm, precisely so that `%` on
  two int arrays reaches it while `&`/`|`/`^`/shifts do not.

  **`/` and `%` per element keep the scalar guards.** The scalar emit was
  factored out into `gen_arith_op`
  (`/home/igzo/github/tycho/src/tychoc.c:9062-9105`) and the element-wise arm
  calls it verbatim, so an element gets `tycho_idiv`/`tycho_imod`,
  `tycho_udiv`/`tycho_umod`, the shift width guard and `trunc_result` exactly as
  a scalar does — an element must not silently get fewer guards than a scalar.
  Verified: a zero **element** (not a literal, so the compile-time check at
  `/home/igzo/github/tycho/src/tychoc.c:6001` cannot see it) aborts cleanly
  instead of raising SIGFPE.

      $ ./tychoc ...   # a := [1, 2]; b := [1, 0]; println(str(a / b))
      tycho: division by zero          (exit 1)

  **The fixed/growable mixing decision: REFUSED, fail-closed.** A `[N]T` and a
  `[T]` cannot agree on a length at compile time, and the two kinds carry
  *different* mismatch rules (compile error vs runtime abort), so a mixed
  expression would have no single defensible behaviour. Nothing in the code
  argued for allowing it, so the fail-closed option stands
  (`/home/igzo/github/tycho/src/tychoc.c:6003-6008`). `bounded[N]T` and the
  template-only `[$N]T` are refused for the same reason
  (`/home/igzo/github/tycho/src/tychoc.c:5993-5997`): `bounded` carries a
  capacity *and* a separate live length, and no element-wise meaning for that
  pair has been settled. All three verified live:

      error: cannot mix a fixed array and a growable array in element-wise `*` (got [3]int, [int]) -- copy one side into the other's kind first
      error: element-wise `*` is defined for [T] and [N]T only (got bounded[3]int, bounded[3]int)
      error: element-wise `*` requires two arrays with the same element type (got [int], [float])

  Scalar broadcast (`a * 2`) deliberately gets **no** new diagnostic here: it
  falls through to the existing end-of-chain message at
  `/home/igzo/github/tycho/src/tychoc.c:6063`, unchanged, so phase 2 has nothing
  to unwrite.

  **The two mismatch behaviours.**

  - `[N]T`: **compile error**, worded off the `==` rule at
    `/home/igzo/github/tycho/docs/spec/12-aggregates.md:146` ("requires the same
    static `N`; `==` compares element-wise"). Locked byte-for-byte as
    `/home/igzo/github/tycho/tests/diag/array_arith_fixlen.err`:

          tests/diag/array_arith_fixlen.ty:7: error: element-wise `*` on a fixed array requires the same static length (got [3]int and [2]int)
               7 |     c := a * b

  - `[T]`: **runtime abort naming both lengths**, via one new runtime helper
    `tycho_ew_len` at `/home/igzo/github/tycho/runtime/tycho_rt.c:2418-2430`,
    written to match `/home/igzo/github/tycho/runtime/tycho_rt.c:187`,
    `/home/igzo/github/tycho/runtime/tycho_rt.c:197` and
    `/home/igzo/github/tycho/runtime/tycho_rt.c:1040` in tone and exit behaviour
    (`fprintf(stderr, "tycho: ...")` then `exit(1)`):

          $ ./tychoc tests/postfreeze/abort/array_arith_len.ty -o ab && ./ab
          tycho: element-wise arithmetic on arrays of different lengths (3 and 2)
          (exit 1)

  **The freshness proof.** `gen_ew_arith`
  (`/home/igzo/github/tycho/src/tychoc.c:9112-9137`) emits a GCC
  statement-expression that copies both operands into locals, allocates a **new**
  spine from the arena (`arena_alloc`), and writes every element by value; the
  element types the arm admits are all scalars, so a shallow element store is a
  deep copy. `/home/igzo/github/tycho/tests/postfreeze/array_arith_fresh.ty`
  proves it from the outside rather than by inspection — it mutates each source
  **after** the operation, pushes to a source, writes into the result, and
  chains two operations. Golden
  (`/home/igzo/github/tycho/tests/postfreeze/array_arith_fresh.out`), abridged:

        [2, 4, 6]        c := a * b
        [2, 4, 6]        after a[0] = 99      -- c unchanged
        [2, 4, 6]        after b[1] = 77      -- c unchanged
        [99, 2, 3]       a still holds its own values
        4 3              push(a, 4): len(a)=4, len(c)=3  -- no shared spine
        [2, 4, 42]       c[2] = 42 ...
        [99, 2, 3, 4]    ... did not reach back into a
        [9, 38]          a2 * b2 - b2, with a2 and b2 both intact afterwards

  **The fixture groups, and where the abort one had to go.**

  1. `/home/igzo/github/tycho/tests/postfreeze/array_arith.ty` — every legal
     element type × every legal operator, with real values in
     `/home/igzo/github/tycho/tests/postfreeze/array_arith.out`: `[1,2,3] *
     [2,2,2]` → `[2, 4, 6]`, `u8` wrap (`200+100` → `44`, `200*100` → `32`),
     `i8` wrap (`100+100` → `-56`), `char` (`'a'-'\t'` → `X`), `f32`, both
     newtype kinds, `[3]int` and `[2]float` fixed arrays, and the empty-`[T]`
     case.
  2. `/home/igzo/github/tycho/tests/postfreeze/array_arith_fresh.ty` — above.
  3. `/home/igzo/github/tycho/tests/postfreeze/abort/array_arith_len.ty` —
     **not** `tests/abort/`, and this is the conclusion the phase was asked to
     reach and state. `/home/igzo/github/tycho/scripts/frontparity.sh:164-165`
     globs `tests/abort/*.ty`, and that lane scores "tychoc accepted it, tychoc0
     refused it" as a **divergence**. An abort fixture is a program tychoc
     *accepts*, so a post-freeze one placed in `tests/abort/` would have
     reddened frontparity by construction — exactly the trap `tests/postfreeze/`
     was created to avoid. `tests/reject/` needed no such move: it appears in
     **no** frontparity glob (checked the whole loop at
     `/home/igzo/github/tycho/scripts/frontparity.sh:164-165`), and frontparity
     skips whatever tychoc itself refuses in any case. So a new lane was added at
     `/home/igzo/github/tycho/tests/run.sh:157-190`, mirroring the
     `tests/abort/` contract (build with tychoc, run native-only, require a
     nonzero exit plus a `tycho:` message). Neither `compiler/fixpoint.sh` nor
     `scripts/frontparity.sh` descends into it.
  4. `/home/igzo/github/tycho/tests/reject/array_arith_fixlen.ty` for the `[N]T`
     mismatch, plus `/home/igzo/github/tycho/tests/diag/array_arith_fixlen.ty`
     and its `.err` — because the reject lane
     (`/home/igzo/github/tycho/tests/run.sh:203-209`) asserts only that a
     diagnostic is *non-empty*, and this phase required the diagnostic itself to
     be asserted. `tests/diag/*.ty` is in frontparity's glob but skips there,
     because tychoc refuses it.

  Goldens are tracked, not ignored — confirmed rather than assumed:

      $ git check-ignore -v tests/postfreeze/rawstring.out ; echo "rc=$?"
      rc=1                                  (no match => not ignored)

  via `/home/igzo/github/tycho/.gitignore:100` (`!/tests/postfreeze/*.out`). The
  new abort fixture needs no `.out` — its lane asserts stderr, not stdout — so
  the fact that that exception does not cover `tests/postfreeze/abort/*.out`
  does not bite.

  **"No new token" — confirmed, not assumed.** Both cheap gates ran green, so
  `tools/tychofmt.ty`, `tools/lsp.ty` and `editors/` were not touched:

      $ sh scripts/tools_check.sh
      tools-check: ok
      $ sh scripts/editors_check.sh
      818 files parsed; the only failure is the enumerated known-bad set (tests/reject/rawstring_unterminated.ty )
      editors-check: ok

  **The three gates (foreground, one per command; `make ci` NOT run).**

      $ make test
      passed: 534   failed: 0
      all green
        ... including: ok postfreeze_array_arith / ok postfreeze_array_arith_fresh
                       ok pfabort_array_arith_len / ok reject_array_arith_fixlen
                       ok diag_array_arith_fixlen
      (the postfreeze lane builds and runs each fixture BOTH native and under
       ASan+UBSan and requires identical output, so the new allocation path is
       already sanitizer-clean on real values here.)

      $ sh scripts/frontparity.sh          # BEFORE, captured before any edit
      frontparity: agreed: 292   diverged: 0   (skipped, tychoc refused: 15)
      $ sh scripts/frontparity.sh          # AFTER
      frontparity: agreed: 292   diverged: 0   (skipped, tychoc refused: 16)
      frontparity: all green
      `agreed` did not fall — 292 before, 292 after. `skipped` rose by exactly 1:
      the new tests/diag/array_arith_fixlen.ty, which tychoc refuses. Nothing
      moved from `agreed` into `diverged`.

      $ sh scripts/asan_self.sh
      asan-self: compiled: 548   failed: 0
      asan-self: all green (tychoc's own execution is ASan+UBSan clean over the corpus)

  **Files changed:** `/home/igzo/github/tycho/src/tychoc.c`,
  `/home/igzo/github/tycho/runtime/tycho_rt.c`,
  `/home/igzo/github/tycho/tests/run.sh`,
  `/home/igzo/github/tycho/tests/postfreeze/array_arith.ty` + `.out`,
  `/home/igzo/github/tycho/tests/postfreeze/array_arith_fresh.ty` + `.out`,
  `/home/igzo/github/tycho/tests/postfreeze/abort/array_arith_len.ty`,
  `/home/igzo/github/tycho/tests/reject/array_arith_fixlen.ty`,
  `/home/igzo/github/tycho/tests/diag/array_arith_fixlen.ty` + `.err`.

  **Not verified.** `make ci` was not run (phase 4 owns it, once, per
  `/home/igzo/github/tycho/CLAUDE.md`), so the fuzzers, the ILP32 rebuild and
  the TSan lane have not seen this code. Risk if one of them is wrong: the
  emitted statement-expression sizes its spine with `tycho_int` and `size_t`
  arithmetic, the same shape as `tycho_arr_C%d_with_cap`
  (`/home/igzo/github/tycho/src/tychoc.c:11394`), so an ILP32 difference would
  be surprising — but it is unverified until phase 4.

  **A hazard caught and avoided, worth recording.** The runtime helper was first
  written next to its two siblings at
  `/home/igzo/github/tycho/runtime/tycho_rt.c:187` and `:197`, which is where it
  belongs by subject. That insertion pushed every line below 200 down by 11 —
  and about twenty `runtime/tycho_rt.c:N` citations point past that line, in
  `/home/igzo/github/tycho/FRICTION.md:189`, `:214`, `:225`, `:234`, `:316`,
  `/home/igzo/github/tycho/docs/rfc/value-lifetime-regions.md:36`, `:52`, `:54`,
  `:144`, `:212`, `:214`, `:269`, `:270`, `:271`, `:273`, `:301`,
  `/home/igzo/github/tycho/docs/internals/changelog-2026-06.md:24`, and this
  plan's own Pre-flight. `scripts/check_citations.py` only bounds-checks a
  source ref (carried-forward phase 13 says so in as many words), so the gate
  would have stayed **green** while all twenty silently became off-by-eleven.
  The helper was moved to the end of the file instead, making the diff
  append-only: `git diff --stat runtime/tycho_rt.c` is `1 file changed, 14
  insertions(+)`, and `runtime/tycho_rt.c:187`, `:197`, `:843` and `:1040` all
  still resolve to the lines their citers describe (checked with `sed -n`).

  **The same hazard in `src/tychoc.c`, which could NOT be avoided — so it was
  repaired.** The typechecker arm and the codegen helpers have to go in the
  middle of the file, so they shifted every line below them, and 110 anchored
  `src/tychoc.c:N@token` citations point in from the spec and the archives.
  `scripts/check_citations.py` is **not** merely a bounds check for these — it
  verifies the token is actually on the named line — so unlike the runtime case
  it reddened loudly rather than silently. Measured both sides rather than
  assumed: a clean `git worktree` at `HEAD` gives

      citation check: ok (110 anchored ..., 2009 bare in bounds, 83 source->doc ..., 121 source->source in bounds)

  and the working tree gave `FAILED (69 stale citation(s))`. All 69 were
  repaired by mapping each old line to its new one through the **actual
  `difflib` opcodes** between `HEAD:src/tychoc.c` and the working copy — not
  through the checker's "it appears at :N" candidate list, which is ambiguous
  for tokens like `parse_if` (three candidate lines) and would have picked wrong.
  Only the numbers moved; the `@token` half was never rewritten, and the diff is
  **59 insertions / 59 deletions across 10 files** — line-for-line neutral, so
  no citation *into* those documents moved either (phase 3's re-wrap rule,
  satisfied here for free). Re-verified green:

      $ python3 scripts/check_citations.py
      citation check: ok (110 anchored contain the token they name, 2013 bare in bounds, 83 source->doc citations resolve, 122 source->source in bounds)
      $ sh scripts/check_links.sh
      link check: ok (132 markdown files, no dead relative links)
      $ sh scripts/spec_check.sh
      spec-examples: 8 runnable example(s), all pass

  Files re-anchored: `/home/igzo/github/tycho/docs/spec/01-lexical.md`,
  `/home/igzo/github/tycho/docs/spec/02-grammar.md`,
  `/home/igzo/github/tycho/docs/spec/03-types.md`,
  `/home/igzo/github/tycho/docs/spec/10-statements.md`,
  `/home/igzo/github/tycho/docs/spec/12-aggregates.md`,
  `/home/igzo/github/tycho/docs/spec/15-program.md`,
  `/home/igzo/github/tycho/docs/spec/16-builtins.md`,
  `/home/igzo/github/tycho/docs/internals/frontend-restriction-audit-2026-07-25.md`,
  `/home/igzo/github/tycho/docs/internals/plan-front-door-DONE.md`,
  `/home/igzo/github/tycho/docs/internals/plan-postfreeze-rawstring-DONE.md`.
  This is line-number maintenance forced by phase 1's own edit, not spec
  authorship — phase 3 still owns every word of the spec's content.

- [x] **Phase 2 — scalar broadcast, both directions**
  - Scope: the same typechecker arm and codegen; fixtures in `tests/postfreeze/`.
  - `a * 2` and `2 * a` scale every element; the scalar may be on either side for
    commutative operators, and for non-commutative ones (`-`, `/`, `%`) both
    `a - 2` and `2 - a` must be defined and must not be quietly swapped. Write a
    fixture that would catch a swap: `2 - [5,1]` is `[-3,1]`, not `[3,-1]`.
  - The literal-adaptation rules at `src/tychoc.c:5966-5975` (an int literal
    adapts to a float side, but a variable never widens) must apply to the scalar
    the same way they apply in the all-scalar case — `[1.0,2.0] * 2` should
    behave exactly as `1.0 * 2` does. Read those lines and say whether it falls
    out or needs code.
  - Done when: fixtures cover both operand orders for every legal operator, the
    non-commutative swap test above, and the int-literal-against-float-array case;
    all match goldens.
  - Verify: `make test`, then `sh scripts/frontparity.sh`. Not `make ci`.

  ### Evidence — phase 2, 2026-07-29

  **Broadcast needed its OWN literal adaptation — it does NOT fall out of the
  existing code.** This was the phase's open question and the answer is
  unambiguous. Every literal-adaptation arm keys off the *other operand's scalar
  type*, and an array type is none of those, so not one of them fires for
  `[1.0, 2.0] * 2`:

  | rule | the test it makes | fires for `[float] * 2`? |
  |---|---|---|
  | sized-int literal | `/home/igzo/github/tycho/src/tychoc.c:5899` — `is_sized_int(rt)` | no: `rt`/`lt` is an array constructor, not a sized int |
  | f32 literal | `/home/igzo/github/tycho/src/tychoc.c:5901` — `rt == T_F32` | no |
  | B-1 int→float | `/home/igzo/github/tycho/src/tychoc.c:6100` — `lt == T_FLOAT` | no |

  So the three rules are re-applied inside the broadcast arm against the
  **element** type (`/home/igzo/github/tycho/src/tychoc.c:6057-6062`), keeping the
  value-directional restriction verbatim: a LITERAL adapts, a variable never
  widens. Verified as a matched pair against the all-scalar behaviour it must
  mirror — the array form and the scalar form agree in both directions:

      $ ./tychoc ...   # n := 2; f := [1.0, 2.0]; f * n
      error: element-wise `*` requires the scalar to have the array's element type (got [float] and int) -- a literal adapts, but a variable never widens
      $ ./tychoc ...   # n := 2; 1.0 * n                      -- the SCALAR reference
      error: arithmetic requires two ints or two floats (got float, int) -- convert one side, ...

  and the positive half is three fixture lines that must all print `[2.0, 4.0]`:
  `f * 2` (int literal adapts), `f * 2.0`, `2 * f`. Locked as
  `/home/igzo/github/tycho/tests/postfreeze/array_bcast.out:17-19`. The negative
  half is locked byte-for-byte in `/home/igzo/github/tycho/tests/diag/array_bcast_widen.err`,
  because the reject lane asserts only that a diagnostic is non-empty.

  **The scalar must land AT the element type — a deliberate narrowing, stated.**
  After adaptation the scalar's type must equal the element type, which makes
  `['a','b'] + 1` a compile error even though the all-scalar `'a' + 1` is legal
  at `/home/igzo/github/tycho/src/tychoc.c:6093-6096` (`char±int -> char`). This
  is not an oversight: phase 1's array⊕array arm already refuses `[char] + [int]`
  for the same reason (`/home/igzo/github/tycho/src/tychoc.c:5996`), so allowing
  the mixed case only in the broadcast direction would make broadcast *more*
  permissive than the arm it extends. Fail closed and consistent. It also avoids
  the question of what `[int] + 'a'` yields, which would be a `[char]` — a type
  carried-forward phase 16 records cannot even be spelled.

  **Divide / modulo by zero: NOT invented — it falls out, and was measured on
  both sides.** The scalar case has two distinct behaviours and broadcast
  reproduces each exactly, because the element emit calls `gen_arith_op`
  (`/home/igzo/github/tycho/src/tychoc.c:9115`) verbatim, exactly as phase 1's
  array⊕array arm does:

  | case | scalar behaviour | broadcast behaviour | mechanism |
  |---|---|---|---|
  | literal zero divisor | `error: division by zero` at compile time | identical | the literal check at `/home/igzo/github/tycho/src/tychoc.c:5956-5961` runs BEFORE the broadcast arm, so `[1,2] / 0` never reaches it |
  | zero VARIABLE divisor | `tycho: division by zero`, exit 1 | identical | `tycho_idiv` via `gen_arith_op` |
  | `%` by zero variable | `tycho: modulo by zero`, exit 1 | identical | `tycho_imod` |
  | zero ELEMENT, `s OP a` | n/a | `tycho: division by zero` / `tycho: modulo by zero`, exit 1 | same guard, per element |
  | float `/ 0.0` | `inf` (IEEE), exit 0 | `[inf, inf]`, exit 0 | float `/` stays a raw C operator |

  Measured, not reasoned:

      $ ...  # z := 0; [1, 2] / z      ->  tycho: division by zero   (exit 1)
      $ ...  # z := 0; 1 / z           ->  tycho: division by zero   (exit 1)   -- the reference
      $ ...  # z := 0; [1, 2] % z      ->  tycho: modulo by zero     (exit 1)
      $ ...  # 2 / [1, 0]              ->  tycho: division by zero   (exit 1)   -- zero ELEMENT
      $ ...  # 7 % [5, 0]              ->  tycho: modulo by zero     (exit 1)
      $ ...  # z := 0.0; [1.0,2.0] / z ->  [inf, inf]                (exit 0)
      $ ...  # z := 0.0; 1.0 / z       ->  inf                       (exit 0)   -- the reference

  Because a broadcast has no second length, **no abort fixture was needed** and
  none was added — `tests/postfreeze/abort/` is untouched. The runtime length
  check is emitted only when both sides are arrays
  (`/home/igzo/github/tycho/src/tychoc.c:9198`).

  **The swap test, and its output.** `-`, `/` and `%` are not commutative, so an
  implementation that normalised `s OP a` into `a OP s` would still typecheck and
  still return an array of the right length. Nothing in the typechecker reorders
  the operands, and `gen_ew_arith` keeps `_ewa` bound to the lhs and `_ewb` to the
  rhs unconditionally (`/home/igzo/github/tycho/src/tychoc.c:9175-9178`) — the
  array-ness of a side decides only whether its text is indexed, never which side
  it is. Proved from the outside, with operands chosen so a swap is visible:

      2 - [5, 1]   ->  [-3, 1]        [5, 1] - 2   ->  [3, -1]
      4 / [8, 2]   ->  [0, 2]         [8, 2] / 4   ->  [2, 0]
      3 % [7, 2]   ->  [3, 1]         [7, 2] % 3   ->  [1, 2]

  Each pair is a mirror image, so a swap turns one line into the other and the
  golden fails. Locked at
  `/home/igzo/github/tycho/tests/postfreeze/array_bcast.out:11-16`. Both orders
  are additionally exercised for every legal operator on every legal element type
  — `int`, `u8`, `i8`, `float`, `f32`, `char`, newtype-over-`int`,
  newtype-over-`float`, `[3]int`, `[2]float`, and the empty `[T]` — in
  `/home/igzo/github/tycho/tests/postfreeze/array_bcast.ty` (82 golden lines).
  The wrapping cases are the sharpest: `to_u8(100) - u` on `u = [200,100]` is
  `[156, 0]` where `u - to_u8(100)` is `[100, 0]`, and `to_i8(100) - s` on
  `s = [100,-9]` is `[0, 109]` against `[0, -109]`. Every golden line was checked
  against a hand computation before it was written, not accepted because it ran.

  **Freshness for the broadcast form.**
  `/home/igzo/github/tycho/tests/postfreeze/array_bcast_fresh.ty` is the twin of
  phase 1's array⊕array file, and it is not redundant: the broadcast emitter
  declares one local at the ARRAY type and one at the ELEMENT type and takes the
  result's length *and capacity* from whichever side is the array
  (`/home/igzo/github/tycho/src/tychoc.c:9183`), so sizing the result from the
  wrong side is a bug phase 1's fixture cannot reach. Golden
  (`/home/igzo/github/tycho/tests/postfreeze/array_bcast_fresh.out`), abridged:

        [2, 4, 6]        c := a * 2
        [2, 4, 6]        after a[0] = 99          -- c unchanged
        [-89, 8, 7]      d := 10 - a              -- scalar-on-the-left allocates too
        [-89, 8, 7]      after a[1] = 50          -- d unchanged
        4 3              push(a, 4): len(a)=4, len(c)=3   -- no shared spine
        [2, 4, 6, 8]     push(c, 8) ...
        [99, 50, 3, 4]   ... did not reach back into a
        [9, 19, 29]      (b * 10) - 1, chained, with b intact
        [10, 14, 18]     (b + g) * 2 -- the two forms compose
        10 20 30 / 77 2 3    fixed [3]T: q := p * 10, then p[0] = 77
        2                q[1] = 55 did not reach p[1]

  **Scope: no new runtime helper, no new allocation path, so
  `scripts/asan_self.sh` was NOT run.** The gate budget in
  `/home/igzo/github/tycho/CLAUDE.md` allows it only for a new allocation path
  phase 1 did not already cover, and there is none: `/home/igzo/github/tycho/runtime/tycho_rt.c`
  is **untouched** by this phase (`git diff --stat` lists no runtime file), and
  the emitted spine still comes from the single `arena_alloc` phase 1 added
  (`/home/igzo/github/tycho/src/tychoc.c:9194`). The broadcast change to that
  statement-expression is which C type each *operand local* gets, not how the
  result is allocated. The new code is nonetheless sanitizer-exercised: the
  postfreeze lane (`/home/igzo/github/tycho/tests/run.sh:148-153`) builds and runs
  every fixture BOTH native and under ASan+UBSan and requires identical output,
  so both new fixtures ran clean under both. Phase 1's end-of-file discipline for
  `runtime/tycho_rt.c` therefore never came up.

  **Citations shifted and repaired: 31.** Verified GREEN in a clean state first,
  so there was a real baseline rather than an assumption:

      $ python3 scripts/check_citations.py          # BEFORE any edit
      citation check: ok (110 anchored contain the token they name, 2013 bare in bounds, 83 source->doc citations resolve, 122 source->source in bounds)

  The typechecker arm and the codegen both sit mid-file, so they shifted every
  line below them and the gate reddened with `FAILED (29 stale citation(s))`.
  Repaired by mapping old→new through the **actual `difflib` opcodes** between
  `HEAD:src/tychoc.c` and the working copy — 12332 old lines, 12314 mapped
  one-to-one, 18 inside edited hunks — and NOT through the checker's
  "it appears at :N" candidate list, which is ambiguous for a token like
  `tycho_str_get` (three candidates) and would have picked wrong. The remapper
  replicates the checker's path-binding rule exactly (a bare `:N` inherits the
  last path named in the same paragraph; `cur` resets on a blank line,
  `/home/igzo/github/tycho/scripts/check_citations.py:211-215`).

  31 were rewritten, not 29: two more had shifted but their token happened to
  still appear on the old line, so the gate could not see them. Repairing only
  what the gate reports would have left those two silently pointing at the wrong
  line — the same class of silent rot phase 1 documented for
  `/home/igzo/github/tycho/runtime/tycho_rt.c`. One range,
  `docs/spec/04-inference.md`'s `src/tychoc.c:4715-5987`, straddles an edited
  hunk and has no faithful image; the remapper **refuses to guess** and left it,
  and the gate accepts it (it is bare, hence bounds-checked only, and still in
  bounds).

  Only digits changed. Proved rather than asserted — masking every run of digits
  in the diff makes the removed and added line sets identical:

      $ git diff --stat -- '*.md'
      8 files changed, 28 insertions(+), 28 deletions(-)
      $ <mask digits in the +/- lines, sort, compare>
      PROOF: with digits masked, removed lines == added lines -> ONLY line numbers changed

  So the diff is line-for-line neutral and nothing citing *into* those eight
  documents moved. Files re-anchored:
  `/home/igzo/github/tycho/docs/internals/frontend-restriction-audit-2026-07-25.md`,
  `/home/igzo/github/tycho/docs/internals/plan-front-door-DONE.md`,
  `/home/igzo/github/tycho/docs/internals/plan-postfreeze-rawstring-DONE.md`,
  `/home/igzo/github/tycho/docs/spec/01-lexical.md`,
  `/home/igzo/github/tycho/docs/spec/03-types.md`,
  `/home/igzo/github/tycho/docs/spec/12-aggregates.md`,
  `/home/igzo/github/tycho/docs/spec/15-program.md`,
  `/home/igzo/github/tycho/docs/spec/16-builtins.md`. This is line-number
  maintenance forced by this phase's own edit; phase 3 still owns every word of
  the spec's content.

  Only **anchored** refs were remapped. The ~300 bare `src/tychoc.c:N` refs that
  also shifted were deliberately left alone — see the new phase 17 below, filed
  rather than silently absorbed.

  **The gates (foreground, one per command; `make ci` NOT run).**

      $ make test
      passed: 537   failed: 0        (534 before -- +3, all new)
      all green
        ok postfreeze_array_bcast / ok postfreeze_array_bcast_fresh
        ok diag_array_bcast_widen

      $ sh scripts/frontparity.sh          # BEFORE, captured before any edit
      frontparity: agreed: 292   diverged: 0   (skipped, tychoc refused: 16)
      $ sh scripts/frontparity.sh          # AFTER
      frontparity: agreed: 292   diverged: 0   (skipped, tychoc refused: 17)
      frontparity: all green
      `agreed` did not fall -- 292 before, 292 after, diverged still 0. `skipped`
      rose by exactly 1: the new tests/diag/array_bcast_widen.ty, which tychoc
      refuses. Nothing moved from `agreed` into `diverged`.

      $ python3 scripts/check_citations.py
      citation check: ok (110 anchored contain the token they name, 2013 bare in bounds, 83 source->doc citations resolve, 122 source->source in bounds)
      (identical to the pre-edit baseline in all four counts)

      $ sh scripts/check_links.sh          # ran because 8 .md files were touched
      link check: ok (132 markdown files, no dead relative links)

  `cc -O2 -fwrapv -Wall -Wextra -std=c11` builds `src/tychoc.c` with **no
  warnings** after every edit.

  **Files changed:** `/home/igzo/github/tycho/src/tychoc.c`,
  `/home/igzo/github/tycho/tests/postfreeze/array_bcast.ty` + `.out`,
  `/home/igzo/github/tycho/tests/postfreeze/array_bcast_fresh.ty` + `.out`,
  `/home/igzo/github/tycho/tests/diag/array_bcast_widen.ty` + `.err`, plus the
  eight re-anchored documents above. `runtime/tycho_rt.c` and `tests/run.sh` were
  NOT touched — the broadcast form needed no new lane and no new helper.

  **Not verified.** `make ci` was not run (phase 4 owns it, once), so the
  fuzzers, the ILP32 rebuild and the TSan lane have not seen this code. Risk if
  ILP32 differs: the broadcast statement-expression sizes its spine with the same
  `tycho_int`/`size_t` arithmetic phase 1 used and this phase did not change that
  line, so the ILP32 exposure is phase 1's, not new. Unverified until phase 4.

- [x] **Phase 3 — the spec, and the conformance record**
  - Scope: `docs/spec/09-expressions.md` (the arithmetic section that currently
    gives `+` only for `string` at `:35` and `bytes` at `:42`),
    `docs/spec/12-aggregates.md` §16 (arrays — where `==`'s element-wise rule
    already lives at `:146`), `docs/spec/appendix-a-grammar.md` if the grammar
    needs a word (it should not — no new syntax), and
    `docs/spec/appendix-e-conformance.md` for the row recording that this is
    post-freeze and its fixture lives in `tests/postfreeze/`.
  - Every `> Provenance:` line you add obeys phase 10's rule from the previous
    plan, now gate-enforced: a **single-line** ref must be `path:N@token`; a
    **range** stays bare. Anchor by reading the line, not by guessing a token.
  - Re-wrap each block to its original line count where you edit an existing one.
    Phase 11 of the previous plan recorded 18 files at 1:1 and phase 10 recorded
    why: growing a spec file invalidates every citation into it.
  - Done when: the rule is stated for both array kinds, both mismatch behaviours
    and broadcast, with provenance; Appendix E carries the row; and the doc gates
    are green.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`,
    `sh scripts/spec_check.sh`. Not `make test`, not `make ci` — Markdown only.

  ### Evidence — phase 3, 2026-07-29

  **Every line number in phases 1 and 2's evidence was re-derived, not copied.**
  Phase 2 inserted into the same region phase 1 documented, so phase 1's
  `/home/igzo/github/tycho/src/tychoc.c:6020-6063` table and its `:6001`,
  `:5989`, `:5993-5997` refs had already moved by the time this phase read them.
  Every citation written below was checked by opening the cited line in the
  tree at `15c2235`.

  **What was written, and where.**

  1. `/home/igzo/github/tycho/docs/spec/09-expressions.md` — a new §13.2
     paragraph, **"Element-wise arithmetic on arrays"**, placed after the
     `bytes` paragraph and before **Comparison**. It states the operator set per
     element type, the `iff` rule, the exclusion of `& | ^ << >>`, freshness,
     broadcast with order preserved (`2 - [5, 1]` → `[-3, 1]`), and hands the
     detail to §16.8. Placement is deliberate: the plan's own
     `/home/igzo/github/tycho/plan.md:36-37` cites
     `/home/igzo/github/tycho/docs/spec/09-expressions.md:35` (`string` `+`) and
     `:42` (`bytes` `+`), and inserting *below* both leaves them pointing where
     they did. Re-verified after the edit: `:35` is still
     `**String concatenation.**` and `:42` still `**\`bytes\` concatenation.**`.
  2. `/home/igzo/github/tycho/docs/spec/12-aggregates.md` — a new **§16.8
     Element-wise arithmetic**, the full rule, between §16.7 and the `---`
     before §17. It carries: the two-array rule and the per-element-type
     operator table; the `[N]T` compile error vs the `[T]` runtime abort with
     the message and exit status quoted; the refusal to mix kinds and the
     refusal of `bounded[N]T` / `[$N]T`; broadcast in both directions with the
     order-preservation examples; literal adaptation against the ELEMENT type
     and the deliberate `['a','b'] + 1` narrowing; freshness; and the
     divide/modulo-by-zero table reduced to prose. It sits next to §16.5's
     `==`-compares-element-wise rule (`/home/igzo/github/tycho/docs/spec/12-aggregates.md:146`),
     which is the rule the `[N]T` mismatch is worded off.
  3. `/home/igzo/github/tycho/docs/spec/appendix-e-conformance.md` — the §16.8
     row in the §16–19 matrix, citing all eight fixtures, plus an E.2.1 bullet
     recording that this is the **fifth** time the freeze mechanism bites and
     the first time it costs nothing, because `tests/postfreeze/` already
     existed. The bullet states the one non-obvious placement: the abort fixture
     is at `tests/postfreeze/abort/`, not `tests/abort/`, because
     `/home/igzo/github/tycho/scripts/frontparity.sh:164` globs
     `tests/abort/*.ty` and `:157` scores "tychoc ACCEPTED it, tychoc0 REFUSED
     it" as a divergence. Both lines were opened and read before the citation
     was written; so were `/home/igzo/github/tycho/tests/run.sh:148-153` (the
     postfreeze golden loop) and `:172-189` (the postfreeze abort loop). Phase
     1's evidence gave the latter as `157-190`; the loop actually runs
     `172-189` (155-171 is its comment header), so the tighter true range was
     used rather than the inherited one.
  4. **`/home/igzo/github/tycho/docs/spec/appendix-a-grammar.md` — NOT touched,
     and this is the phase's stated conclusion, measured rather than asserted.**
     The feature adds no token and no production: `git diff 0180e69..HEAD --
     src/tychoc.c` (the two phases, exactly) has **four** hunks, at `base_of`,
     `resolve_expr_inner`, `op_str` and `gen_expr` — none inside `lex` or any
     `parse_*` function, so `parse_expr`
     (`/home/igzo/github/tycho/docs/spec/02-grammar.md:378`, citing
     `src/tychoc.c:2586-2654`) is untouched. `*` `/` `%` already sit at binding
     level 3 and `+` `-` at level 4
     (`/home/igzo/github/tycho/docs/spec/02-grammar.md:361-362`); `a * b` on two
     arrays parses today and always did — only its TYPING is new. Appendix A is
     additionally a *generated* projection of §3/§4
     (`/home/igzo/github/tycho/scripts/spec_check.sh:24-33`), so hand-editing it
     would have reddened check 1 for a change §3/§4 never made.

  **Provenance discipline.** 15 new anchored single-line refs, every anchor
  chosen by reading the line it names, and every range kept to its construct
  (`5987-6017` is the two-array arm exactly; `6046-6072` the broadcast arm
  exactly; `6057-6062` the three adaptation rules exactly; `1020-1027` was
  *not* used as a range — `1020@elem_arith_ok` is a tighter true statement).
  The anchored count moved 110 → 125 and nothing else changed:

      $ python3 scripts/check_citations.py
      citation check: ok (125 anchored contain the token they name, 2023 bare in bounds, 83 source->doc citations resolve, 122 source->source in bounds)

  **Line growth, and the one citation it moved.** This phase is append-only —
  three insertions, **zero deletions**, no existing block re-wrapped, so no
  citation *within* an edited paragraph could move:

      $ git diff --numstat          # the three spec files, final
      20      0       docs/spec/09-expressions.md
      96      0       docs/spec/12-aggregates.md
      20      0       docs/spec/appendix-e-conformance.md

  (`plan.md` also moves, but not as spec text: two 1-for-1 line rewrites — the
  phase-3 checkbox and the `:289` -> `:385` repair below — plus this evidence
  block and the new phase 18, both appends.)

  Growth still shifts everything *below* each insertion, so the shift was
  measured rather than hoped over. Enumerating every citation in the tree that
  binds to the three files (replicating the checker's paragraph-scoped path
  binding, `/home/igzo/github/tycho/scripts/check_citations.py:211-215`) found
  exactly **one live** ref below an insertion point:
  `/home/igzo/github/tycho/plan.md:55`'s `:289` into
  `/home/igzo/github/tycho/docs/spec/12-aggregates.md`, the tuple
  `**Equality.**` line, which the §16.8 insertion moved 289 → 385. Repaired
  here; only the digits changed. Everything else below an insertion is in a
  frozen `docs/internals/plan-*-DONE.md`
  (`/home/igzo/github/tycho/docs/internals/plan-int64-DONE.md:1583`, `:1681`
  into `09-expressions.md`;
  `/home/igzo/github/tycho/docs/internals/plan-front-door-DONE.md:606`, `:609`
  into `12-aggregates.md`;
  `/home/igzo/github/tycho/docs/internals/plan-postfreeze-rawstring-DONE.md:603`,
  `:622` into `appendix-e-conformance.md`), which the checker's own rule says
  must never be renumbered — "line numbers recorded as they stood when the work
  was done; renumbering them would falsify the record rather than repair it"
  (`/home/igzo/github/tycho/scripts/check_citations.py:63-66`). Left as records,
  deliberately.

  **The spec's own example is gate-verified, not hand-computed.** §16.8 carries
  a ` ```tycho `/` ```output ` pair, so `scripts/spec_examples.sh` compiles and
  runs it and requires the printed output to equal the block. The runnable count
  went 8 → 9 and the new one is the third line of the pair — the swap test —
  which is what makes it worth having:

      spec-examples: ok docs/spec/12-aggregates.md:216 (tychoc)
      spec-examples: 9 runnable example(s), all pass

  This is safe post-freeze only because that runner's `tychoc0` leg was cut on
  2026-07-26 (`/home/igzo/github/tycho/scripts/spec_examples.sh:12-16`); before
  that date a §16.8 example would have reddened the gate by construction, for
  the same reason its fixtures cannot live in `tests/*.ty`. Checked, not assumed.

  **The three gates (foreground, one per command; `make test` and `make ci` NOT
  run — this phase edited Markdown only and nothing it touched can reach a
  compiled artifact, per `/home/igzo/github/tycho/CLAUDE.md:23-25`).**

      $ python3 scripts/check_citations.py
      citation check: ok (125 anchored contain the token they name, 2023 bare in bounds, 83 source->doc citations resolve, 122 source->source in bounds)

      $ sh scripts/check_links.sh
      link check: ok (132 markdown files, no dead relative links)

      $ sh scripts/spec_check.sh
      spec-check: Appendix A grammar matches §3/§4 (ok)
      spec-check: all Appendix E fixture citations resolve (ok)
      spec-examples: 9 runnable example(s), all pass

  **Files changed:** `/home/igzo/github/tycho/docs/spec/09-expressions.md`,
  `/home/igzo/github/tycho/docs/spec/12-aggregates.md`,
  `/home/igzo/github/tycho/docs/spec/appendix-e-conformance.md`, and
  `/home/igzo/github/tycho/plan.md` (this evidence, the checkbox, the one
  repaired citation, and the new phase 18 below).

  **Not verified.** That the spec text matches *behaviour* is inherited from
  phases 1 and 2's fixtures, not re-measured here — no line-checker can see a
  wrong behavioural claim
  (`/home/igzo/github/tycho/scripts/check_citations.py:82-83` says so in as many
  words). The one behavioural claim this phase could and did test itself is the
  §16.8 example, above. `make ci` is still phase 4's.

- [x] **Phase 4 — the one full sweep**
  - `make ci`, once, at the end of the chain, per `CLAUDE.md`'s gate budget. This
    is the phase that runs the fuzzers, the ILP32 rebuild and the TSan lane over
    everything phases 1 and 2 added.
  - Done when: `CI GREEN`, exit 0. If it reddens, name the step and whether this
    plan caused it.

  ### Evidence — phase 4, 2026-07-29

  **The verdict.** One `make ci`, foreground, at `3a07a5c` with a clean tree
  (`git status --porcelain` = the pre-existing untracked `new_ideas.md` only).
  `LD_PRELOAD` confirmed empty before the run (`echo "[$LD_PRELOAD]"` → `[]`), so
  no sanitizer lane could fail spuriously.

      >>> [1/13]  build (make tychoc)
      >>> [2/13]  make test  (golden output + ASan/UBSan/LeakSanitizer)
                  passed: 537   failed: 0
      >>> [2b/13] make ilp32  (fixture suite rebuilt under -m32)
                  ilp32: -m32 toolchain OK (32-bit long, 64-bit int64_t verified)
                  passed: 537   failed: 0
      >>> [2c/13] make asan-self
                  asan-self: compiled: 551   failed: 0
      >>> [3/13]  make corelib        >>> [3b/13] make entrypoints
                                      entrypoints: ok (11 entry points compile with tychoc)
      >>> [4/13]  make conc           conc: passed 37   failed 0
      >>> [5/13]  make ffi            ffi: green
      >>> [6/13]  make fuzz N=200          ok=177 skip=23 timeout=0 FAIL=0
      >>> [7/13]  make fuzz-reject N=200   accepted=31 rejected=169 FAIL=0
      >>> [8/13]  make fuzz-leak N=150     ok=131 skip=19 FAIL=0
      >>> [9/13]  make tools-check    tools-check: ok
      >>> [9b/13] make editors-check  editors-check: ok
      >>> [10/13] bench-guard         >>> [11/13] make recursion  (all green)
      >>> [12/13] make spec-check     spec-examples: 9 runnable example(s), all pass
      >>> [13/13] make check-links
                  link check: ok (132 markdown files, no dead relative links)
                  citation check: ok (125 anchored contain the token they name,
                                      2024 bare in bounds, 83 source->doc citations
                                      resolve, 122 source->source in bounds)
      ================================================================
       CI GREEN -- tree is good
      ================================================================

  **Timing.** `/home/igzo/github/tycho/scripts/ci.sh` prints **no** per-step
  timing (`grep -c "elapsed\|took\|seconds"` over the captured log = 0), so only
  wall clock is available: started 16:37:28 CEST, `CI GREEN` and process exit at
  16:54:37 CEST — **17m09s**, against the ~19 min budgeted in
  `/home/igzo/github/tycho/CLAUDE.md:19`.

  **On "exit 0", stated precisely.** The run was launched detached so it could
  be waited on across the tool's 10-minute per-command cap, so its numeric exit
  status was not captured directly. What *is* proven: the log contains **zero**
  `make: ***` / `Error N` lines, and `/home/igzo/github/tycho/scripts/ci.sh:17`
  is `set -eu` with `printf ' CI GREEN -- tree is good\n'` as the last statement
  of the file (line 128, followed only by the closing `bar`). Under `set -e`,
  reaching that banner requires every one of the thirteen steps to have exited
  0, and nothing after it can fail. `make` also printed `Leaving directory`
  without an error line. Exit 0 is therefore derived, not observed.

  **The citation gate did NOT redden.** The task brief flagged phase 17's ~344
  deliberately-deferred bare refs as the likely suspect if it had; it did not
  fire, because `/home/igzo/github/tycho/scripts/check_citations.py` only
  bounds-checks a bare ref — which is exactly why phase 17 exists. `2024 bare in
  bounds` here against `2023` in phase 3's evidence: +1, this evidence block's
  own new refs. Nothing was fixed and nothing needed fixing; scope held.

  ---

  **The four never-before-covered lanes — what each actually exercised.**

  Method, so the claims below are falsifiable: `./tychoc <f> --emit-c` on a
  corpus, then grep the emitted C for `_ewa = (`, the opening of the
  statement-expression `gen_ew_arith` emits
  (`/home/igzo/github/tycho/src/tychoc.c:9185`, `:9191`). Calibrated on a known
  positive and a known negative before it was trusted:
  `tests/postfreeze/array_arith.ty` emits **34** occurrences; a fuzz program
  emits **0**. The runtime helper `tycho_ew_len` is NOT a usable marker — it is
  written into the prelude of *every* emitted program
  (`static void tycho_ew_len(...)` at line 2426 of every `.c` checked), so a
  grep for it matches 100% of programs and proves only that the helper compiles.

  1. **ILP32 (`-m32`) — PROVEN to exercise the new code.** This was the lane the
     brief called the real risk, because phases 1 and 2 added integer arithmetic
     over sized int types. Step 2b ran the full fixture suite rebuilt under
     `-m32` and the new fixtures are named green inside that step's own output
     (log lines 805-834 and 1068-1069, between the `[2b/13]` header at 551 and
     its `passed: 537` at 1092): `postfreeze_array_arith`,
     `postfreeze_array_arith_fresh`, `postfreeze_array_bcast`,
     `postfreeze_array_bcast_fresh`, `pfabort_array_arith_len`,
     `reject_array_arith_fixlen`, `diag_array_arith_fixlen`,
     `diag_array_bcast_widen`. Those are golden comparisons, so the `u8`/`i8`
     wrap values, the `f32` cases and the runtime length-mismatch abort all
     produced **byte-identical output under a 32-bit `long`** as under LP64. The
     pre-flight's unverified ILP32 risk (phase 1 evidence, "Not verified") is
     now closed. Note the lane's own caveat, printed at log line 553: the
     **ASan sub-lane is skipped under ilp32** (no 32-bit ASan runtime under
     multilib); 64-bit `make test` covers ASan.

  2. **`asan-self` — PROVEN to exercise the new typechecker arms.** `compiled:
     551  failed: 0`, against `548` in phase 1's evidence: **+3**, and
     `/home/igzo/github/tycho/scripts/asan_self.sh:150` globs
     `tests/postfreeze/*.ty`, so the four new fixtures are in that corpus. This
     is the first time the ASan+UBSan build of the compiler has run **phase 2's**
     broadcast typechecker (phase 1's own asan-self run predates it, as phase 1's
     evidence states). It proves `tychoc`'s *own* execution is clean while
     compiling the new arms; the emitted *program's* runtime cleanliness comes
     from step 2's postfreeze lane, which builds and runs each fixture both
     native and under ASan+UBSan and requires identical output
     (`/home/igzo/github/tycho/tests/run.sh:148-153`; log lines 261-267).
     `tests/postfreeze/abort/` is a subdirectory and is **not** in that glob, so
     the abort fixture is not part of the asan-self corpus — stated, not glossed.

  3. **The three fuzz lanes — GREEN, but they did NOT reach the new code.** This
     is the honest answer and it is measurable, not a guess. Regenerated the
     exact 200-seed corpus (`python3 fuzz/gen.py <seed>`, seeds 1-200,
     deterministic per `/home/igzo/github/tycho/fuzz/README.md`), emitted C for
     every one tychoc accepts (177/200 — matching the lane's own `ok=177
     skip=23`), and grepped:

         fuzz corpus: 0/177 emitted programs contain a real element-wise use site

     `/home/igzo/github/tycho/fuzz/gen.py` is type-directed and has no
     binary-arithmetic-over-typed-operands generator at all — no `["+","-","*"]`
     choice list exists in it; its only operator choices are the compound
     assignments at `:692` and `:817`, both on scalars. So the element-wise and
     broadcast arms are outside the generator's grammar. `fuzz-leak` (150 seeds)
     uses the same generator, so the same holds. `fuzz-reject` mutates text and
     asserts only fail-closed behaviour. **What the fuzz lanes DO prove:** the
     new arms caused no regression anywhere in the 177-program corpus, and every
     one of those programs compiled and ran with the new `tycho_ew_len` helper in
     its prelude under ASan/UBSan and under LeakSanitizer. **What they do not
     prove:** anything about element-wise arithmetic itself. Filed as phase 19.

  4. **`conc` / TSan — GREEN, but it did NOT reach the new allocation path
     either, by the same measurement.** All 11 `tests/conc/*.ty` fixtures emitted
     C; `0/11` contain a `_ewa = (` use site. `conc: passed 37 failed 0` (37 =
     the fixtures × the native/ASan/TSan legs) therefore says the new code did
     not break the concurrency lane, and that the new prelude helper compiles
     clean under TSan — not that an element-wise allocation was ever raced. The
     phase-1 arena allocation (`/home/igzo/github/tycho/src/tychoc.c:9194`) has
     **not** been exercised concurrently by any gate in this tree. Also filed as
     phase 19; it is the same gap in a second lane.

  **Summary of the four, in one line each.** ILP32: proven exercised, green.
  asan-self: proven exercised, green. Fuzzers: proven **not** exercised, green.
  TSan/conc: proven **not** exercised, green.

  **Files changed:** `/home/igzo/github/tycho/plan.md` only — this phase runs
  gates and records them. No source, test, or doc file was touched; nothing
  reddened, so nothing needed fixing and the scope lock never came under
  pressure.

## Status — PLAN COMPLETE, 2026-07-29

All four phases done. `new_ideas.md` item 3 is shipped.

```
0180e69  the plan itself
70fb00f  phase 1  element-wise arithmetic on arrays
15c2235  phase 2  scalar broadcast for array arithmetic
3a07a5c  phase 3  spec the element-wise array arithmetic rules
e2f4fff  phase 4  the full sweep -- CI GREEN, 17m09s, 13 steps
```

`[1,2,3] * [2,2,2]` is `[2,4,6]`; `[1,2,3] * 2` is the same; `2 - [5,1]` is
`[-3,1]` and that line is executable spec, run by `scripts/spec_check.sh`. Both
array kinds, arithmetic operators only, broadcast both directions, fresh result.
`make test` 537/0, `frontparity` `agreed: 292` unchanged across all four phases,
`asan_self` 551/0, `make ci` green.

**Three things the phases established that the plan got wrong or did not know:**

- **The Pre-flight's guess about `%` was wrong.** It assumed int-only; phase 1
  read `src/tychoc.c:6021` and found every sized int accepted, float/f32/newtypes
  refused. Deriving the operator set from the arms instead of asserting one is
  what kept the array form from out-permitting the scalar form.
- **Broadcast did not fall out of the existing literal adaptation.** Every
  adaptation arm keys off the *other operand's scalar type*, and an array type is
  none of those, so nothing fired for `[1.0,2.0] * 2`. Phase 2 re-applied all
  three against the element type. Had this been assumed rather than read, the
  feature would have silently refused a case the spec now guarantees.
- **An abort fixture could not go in `tests/abort/`.** That directory is inside
  `scripts/frontparity.sh`'s glob and an abort fixture is a program `tychoc`
  *accepts*, so a post-freeze one would have reddened the frozen-compiler lane by
  construction. Phase 1 built `tests/postfreeze/abort/` instead. The lane the
  previous plan opened needed a second room, and this is it.

**What the full sweep proved, and what it did not.** Phase 4 measured coverage
rather than assuming it: the ILP32 `-m32` rebuild and the `asan-self` lane
provably exercise the new code (all eight fixtures run as golden comparisons
under a 32-bit `long`; `asan_self` 551 compiled). The fuzz lanes and the
TSan/`conc` lane provably do **not** — 0 of 177 generated programs and 0 of 11
concurrency fixtures contain a use site, because `fuzz/gen.py` has no generator
for binary arithmetic over typed operands. No seed count fixes that. Filed as
phase 19 rather than papered over.

One honest caveat, recorded in phase 4's evidence: `make ci`'s exit status is
*derived*, not observed — the run was detached to outlive the per-command cap, so
the numeric status was lost. `scripts/ci.sh:17` is `set -eu`, the banner is its
last statement, and the log has zero `make: ***` lines.

## Carried forward from the previous plan

Filed by phase agents as they ran; none blocking, none closed.

- [ ] **Phase 12 — `editors/zed/README.md`'s corpus count is hand-typed and
      unguarded.** It read "462 committed `.ty` files" until phase 3 of the
      previous plan re-measured it to 813; nothing stops it rotting again.
      `scripts/editors_check.sh` already computes the count — grep the README for
      it and fail on a mismatch.
- [ ] **Phase 13 — an anchored form for source→source citations.** Phase 8 added
      a bounds check for the 121 of them and stated honestly that it would have
      caught **none** of the 17 wrong-line refs it repaired by hand. The
      wrong-line class needs `path:N@token`, which the checker cannot currently
      see in bare comment prose.
- [ ] **Phase 14 — a `> Provenance:` block that names no path escapes the
      mandatory-anchor rule by accident.** Phase 10's rule keys off a named path;
      12 refs in such blocks are exempt without anyone deciding they should be,
      including 8 stale ones in `docs/spec/02-grammar.md:272-274`. Phase 11 held
      two ranges path-less on purpose to avoid reddening the gate — a workaround
      that should not need to exist.
- [ ] **Phase 16 — `char` is a scalar type with no type NAME and no `int -> char`
      conversion.** Found while building phase 1's element-type matrix. `char`
      has arithmetic (`/home/igzo/github/tycho/src/tychoc.c:6038-6043`,
      `char±char -> char`) and `str()` prints it, but `[]char` is refused with
      `error: unknown type 'char'`, so a `[char]` can only be built from a
      literal of char literals, never annotated, returned by signature, or made
      empty. There is no `to_char(n)` either (`error: unknown procedure
      'to_char'`), and the char escape set is `\n \t \r \0 \\ \'` only — no
      `\xNN` (`error: unsupported char escape`) — so a char outside that set and
      the printable range cannot be written at all. Net effect: the one element
      type in phase 1's derived table whose operator set is *narrower* than the
      others is also the one hardest to test, and phase 1's fixture had to reach
      it through `['a', 'b'] - ['\t', '\t']`. Not blocking; the escape half is
      deliberate and already reasoned out in `/home/igzo/github/tycho/FRICTION.md:214`
      (a string literal's text is pasted verbatim into the emitted C), but the
      missing *type name* is a separate and much smaller question.

- [ ] **Phase 17 — bare `src/tychoc.c:N` refs drift silently on every edit to
      that file, and there are ~300 of them.** Found while repairing phase 2's
      citation fallout. `scripts/check_citations.py` verifies the token only for
      the **anchored** `path:N@token` form; a bare `:N` is bounds-checked and
      nothing more, so it stays "green" while pointing at the wrong line. Phase 2
      measured the size of this: remapping every citation that binds to
      `src/tychoc.c` through a `difflib` line map moves **375** refs across **27**
      files, of which only **31** are anchored. The other ~344 are bare, and
      phases 1 and 2 have now each shifted them without repair — they are
      cumulatively wrong by both phases' insertions. Phase 2 deliberately did not
      touch them: most live in frozen `docs/internals/plan-*-DONE.md` evidence
      that describes the tree as it was at that commit, so silently rewriting
      their numbers is not obviously correct, and it is a large unreviewable diff
      to smuggle into a codegen phase. The decision needed is a policy one — do
      frozen archives get re-anchored, or pinned to a commit? — which is why this
      is a phase and not a fix. Related to but distinct from carried-forward
      phase 13, which is about source→source refs; this is doc→source.
      A remapper that does the mechanical half correctly (replicating the
      checker's paragraph-scoped path binding, refusing to guess inside edited
      hunks) was written for phase 2 and its approach is described in that
      phase's evidence.

- [ ] **Phase 18 — doc→doc citations drift exactly like doc→source ones, and one
      is already wrong.** Found by phase 3 while measuring what its own
      insertions would shift. `/home/igzo/github/tycho/docs/internals/spec-plan.md:605`
      cites `/home/igzo/github/tycho/docs/spec/appendix-e-conformance.md:187` as
      the cross-reference for "§9.5 is evidenced by the whole differential suite
      + `make fixpoint`, not one fixture". That line is the **§24.2 linking / cc
      invocation** row, and was already so before phase 3 touched anything —
      verified against the tree at `15c2235`, not inferred:
      `git show HEAD:docs/spec/appendix-e-conformance.md | sed -n '187p'` prints
      the `§24.2` row. The §9.5 claim actually lives in the E.2.1 bullet near
      `:218`. The citation is **bare**, so `scripts/check_citations.py` only
      bounds-checks it and has stayed green over the whole drift — the same
      silent class as carried-forward phases 13 and 17, in a third direction
      (doc→doc). Phase 3 deliberately did **not** renumber it: moving `187` to
      `188` would preserve the wrongness while looking like a repair, and
      choosing the *right* target is a content decision about what spec-plan.md
      meant, outside a Markdown-only phase's scope. Two things to settle
      together: repoint this one, and decide whether the anchored `path:N@token`
      form should be **mandatory** for a doc→doc ref into `docs/spec/`, which is
      what would have caught it.
- [ ] **Phase 19 — no fuzz lane and no concurrency lane can reach element-wise
      array arithmetic, and the gap is in the generator, not the seeds.** Found
      by phase 4 while measuring what the full sweep actually covered, and
      measured rather than assumed: regenerating the 200-seed corpus and
      grepping the emitted C for `_ewa = (` (the `gen_ew_arith`
      statement-expression, `/home/igzo/github/tycho/src/tychoc.c:9185`) gives
      **0/177**, and `tests/conc/*.ty` gives **0/11**. The cause is structural —
      `/home/igzo/github/tycho/fuzz/gen.py` is type-directed and contains no
      binary-arithmetic-over-typed-operands generator at all (its only operator
      choices are the compound assignments at `:692` and `:817`, both scalar), so
      no seed count fixes it. Two separable pieces of work: (a) teach `gen.py` to
      emit `arr OP arr` and `arr OP scalar` / `scalar OP arr` when it has two
      array variables of the same element type in scope — the differential and
      leak lanes would then cover the new allocation path, and the `[T]`
      length-mismatch abort becomes a shape the fuzzer can find on its own; (b)
      add one `tests/conc/` fixture that builds an element-wise result inside a
      `spawn`/`parallel-for` body, so the arena allocation at
      `/home/igzo/github/tycho/src/tychoc.c:9194` is exercised under TSan. Not
      blocking and not a defect — the feature is covered by goldens, ASan/UBSan,
      ILP32 and asan-self — but "green" in those three lanes currently carries no
      information about this feature, and that should be written down rather than
      assumed away. Related to nothing else in this list; it is a coverage gap,
      not a citation one.
- [ ] **Phase 15 — `docs/corelib.md` does not exist.** Moved to
      `docs/guides/corelib.md` by `68e5b39`; a dead backticked doc path in prose
      is invisible to `scripts/check_links.sh`, which only validates real
      Markdown links.

## Out of scope

- **`new_ideas.md` item 4 — Odin-style `for i := 0; i < n; i += 1:`.** A
  replacement for three shipped forms (`docs/spec/10-statements.md:87`, `:90`,
  `:98`), so a breaking change across the whole tree. Its own plan.
- **The two concurrency items in `FRICTION.md`** — no storable task handles, no
  way to hand a connection to whichever worker is free. They want a type-system
  answer before any code.
- **Comparisons on arrays.** Deliberately excluded above: `==` already means
  whole-array equality and changing it is silently breaking.
- **`soa` and maps.** This plan is arrays only. If `soa` wants the same treatment
  it is a separate, later question.
