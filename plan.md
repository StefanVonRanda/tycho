# Front-door defects: diagnostics, compiler parity, emitted-C hygiene, doc drift

Follows the completed `tycho_int` migration (archived:
`docs/internals/plan-int64-DONE.md`; head commit `eaf0064`). That plan closed the
last *correctness* gap the spec tracked — punch-list #16 — and left the tree at
39 closed / 0 partial / 0 open with all seven gates green.

This plan addresses a different class, found by reading the language as a *user*
rather than as a maintainer: five defects on the surfaces a newcomer meets first
(error messages, example code, compiler warnings). None is a soundness hole. All
five sit in exactly the blind spot of the existing harness — `make ci` scores the
accept/reject decision and the golden output, and never scores the *text* of a
diagnostic, the *warning-cleanliness* of emitted C, or whether the examples still
describe the language they compile against.

## Goal

The front door matches the quality of the engine room. Done =
`type_name` names every reachable type (no type is ever misreported as `void`),
diagnostic-text divergence between the two compilers is measured and — if bounded
— gated, emitted C is warning-clean under the project's own `-Wall -Wextra`, and
no shipped example describes a restriction the language no longer has.

## Findings this plan closes (all verified by probe on 2026-07-25)

| # | Defect | Evidence | Class |
|---|---|---|---|
| 1 | `s[i]` yields `int`, not `char`, so char literals don't compose with string indexing | `docs/spec/03-types.md:86`; `examples/invindex.ty:24-27` compares to `65`/`90` with a `# 'A'..'Z'` comment | **design — needs a ruling, see Phase 6** |
| 2 | `type_name()` has no `case T_CHAR`; falls to `default: return "void"` | `src/tychoc.c:1291`; repro `s[1] == 'e'` → `cannot compare int with void` | defect (diagnostic) |
| 3 | The two compilers emit *different text* for the same rejected program | tychoc `…with void` vs tychoc0 `…with char` (`compiler/tychoc0.ty:6280`) — both reject | gap (unmeasured surface) |
| 4 | Emitted C is not warning-clean under the project's own flags | `tests/map_param_composite.ty` → `-Wdiscarded-qualifiers` at the `ekeys[e] = k` store; `Makefile:11` sets `-Wall -Wextra` | defect (hygiene) |
| 5 | Examples describe a map restriction that no longer exists | `examples/json.ty:11-12` and `examples/invindex.ty:5-6` claim maps are int/float-valued; `docs/spec/03-types.md:190-191` says value type is **unrestricted**, and a struct-valued map compiles and runs | doc drift |

## Pre-flight

- **Blast radius is small for #2/#4/#5, real for #3, and a language change for #1.**
  #2 is one `case` in a switch. #4 is one emitted string literal. #5 is comments
  plus the code that works around the phantom restriction. #3 adds a gate, which
  is new failure surface for every future diagnostic edit. #1 changes what an
  expression *means* and is parked behind a ruling.
- **#3 is the one with a trap.** The two compilers do not share a message
  *format* — tychoc emits `file:LINE: error: MSG` and tychoc0 emits `line LINE:
  MSG`. A naive text-parity gate fails on all 143 reject fixtures for reasons
  that have nothing to do with the defect. Phase 2 MEASURES first and reports the
  real divergence count; Phase 3 only builds a gate if that number is bounded and
  the normalization is honest. If Phase 2 finds divergence is broad and
  systematic, Phase 3 is re-scoped or dropped — that is a legitimate outcome, not
  a failure.
- **#4 may not be cosmetic.** `-Wdiscarded-qualifiers` at `m->ekeys[e] = k` means
  a `const`-qualified pointer is stored into a non-`const` slot. Phase 4 must
  first determine whether anything later *writes through* that slot. If it does,
  this is an aliasing bug wearing a warning's clothes and the phase escalates. If
  it does not, the fix is a qualifier. Do not silence it with a cast before
  answering that question.
- **The conformance oracle is NOT at risk from #3.** `docs/spec/00-conventions.md`
  §1.3 and `appendix-f-impl-defined.md:63-64` make the *accept/reject decision*
  normative, not the message text. Both compilers reject the probe program. This
  plan does not propose making message text normative; it proposes measuring it.
- Reversibility: git; one commit per phase; no user data, no persistent state.
- Assuming: the `-m32`/multilib toolchain remains available (used by `make ilp32`,
  which every phase runs). Risk if wrong: the ilp32 gate fails loudly, not
  silently.

## Phases

Strict order; one commit per phase; **NO commit trailers** (repo convention).
Each phase runs every relevant gate as its own foreground command and pastes the
summary line. A phase that fails a gate does NOT tick its box — it halts and
reports.

**ENVIRONMENT GOTCHA — run EVERY gate as `env -u LD_PRELOAD make …`.** The
interactive shell sets `LD_PRELOAD=/home/igzo/phonic/tools/block-nnp.so`, which
loads before `libasan.so.8` and makes every ASan fixture abort with "ASan runtime
does not come first in initial library list". That is a foreign preload in the
dev shell, NOT a code regression. Do **not** add `verify_asan_link_order=0` to
`run.sh` — that would blind the check for real link-order bugs.

**Gate set (all phases):** `make test`, `make corelib`, `make conc`,
`make fixpoint`, `make ilp32`, `make spec-check`, `make check-links`.
`make fixpoint` is the self-hosting tripwire: any change to `src/tychoc.c` that
alters emitted text must land together with its `compiler/tychoc0.ty` counterpart
or fixpoint goes red.

- [x] **Phase 1 — `type_name` names every reachable type (#2)**
  - Scope: `src/tychoc.c` `type_name()`. Add `case T_CHAR: return "char";`.
    Then AUDIT the rest: 22 of the 47 `Type` tags are absent from the switch, but
    most are `*_BASE` families already handled by the `IS_NEWTYPE`/`IS_TASK`/
    `IS_CHAN`/`IS_HANDLE`/`IS_STRUCT`/`IS_ARRC`/`IS_MAPC`/`IS_OPT`/`IS_RES`/
    `IS_TUP`/`IS_FUNC`/`IS_ENUM`/`IS_SOA` predicates *before* the switch. For each
    of the remaining tags (`T_VOID`, `T_MAP_S`, `T_ID`, `T_TYPE`, `T_PENDING`,
    `T_TYPARAM`, `T_MAX`), determine by reading the source whether it can reach a
    user-visible diagnostic. Name each one REACHABLE (→ add a case) or
    UNREACHABLE (→ say why, in a comment above `default:`).
  - Non-scope: `compiler/tychoc0.ty` builds type names as strings and already
    prints `char` correctly — do not touch it here. Phase 2 measures the rest.
  - Done when: `s[1] == 'e'` reports `cannot compare int with char` from tychoc;
    every reachable tag is either cased or documented-unreachable; `default:`
    carries a comment saying what still lands there and why that is safe.
  - Verify: paste the before/after diagnostic for the probe; paste the
    reachability verdict per tag; full gate set green.
  - **DONE 2026-07-25.** Changes, all in `type_name()` (`src/tychoc.c:1235`):
    added `case T_VOID`, `case T_CHAR`, an `IS_TYPARAM` branch ahead of the
    predicate chain, and a comment above `default:`.

    **Before / after — the plan's probe** (`s[1] == 'e'`):
    ```
    BEFORE  /tmp/p1.ty:3: error: cannot compare int with void
    AFTER   /tmp/p1.ty:3: error: cannot compare int with char
    ```

    **Second defect found by the audit — a `$T` was misreported as `void` too.**
    Probe (`fn f(a: $T) -> $T:` / `y: $U = a` / `return y`):
    ```
    BEFORE  :2: error: declared type void but value is int
    AFTER   :2: error: declared type $U but value is int
    ```

    **Correction to this phase's own scope text.** It named seven "remaining
    tags"; four of them do not exist in `src/tychoc.c`. `grep -nw 'T_MAP_S|T_ID|
    T_TYPE|T_MAX'` over the file returns only two *comment* mentions
    (`:998` "T_MAP_S?/I?", `:1388` "a T_TYPARAM") and no declaration. The base
    enum (`:479-500`) has **28** tags, not 47; the switch cased **25** of them,
    so the true gap was three tags plus the un-predicated `IS_TYPARAM` range.

    **Reachability verdict, per tag.** Method: an instrumented build (a scratch
    copy of `tychoc.c` printing a marker at `type_name`'s head) run over all
    **370** `.ty` sources in `tests/`, `tests/reject/`, `examples/`, `corelib/`
    and `compiler/`, plus hand-written probes per tag. Corpus totals: `CHAR` 2,
    everything else 0.

    | Tag | Verdict | Evidence |
    |---|---|---|
    | `T_CHAR` | **REACHABLE** → cased | The plan's probe; 2 corpus arrivals |
    | `T_VOID` | **REACHABLE** → cased | 3 probes reach it: `1 + nop()`, `nop() == 1`, `takes(nop())`. Already printed `void` via `default:`; now explicit so `default:` means "cannot happen" |
    | `IS_TYPARAM` (`T_TYPARAM_BASE`, `:637`) | **REACHABLE** → branch added | `y: $U = a` inside a generic template; an annotation naming a typaram no argument binds keeps the raw tag through resolve. Was printed as `void` |
    | `T_PENDING` | **UNREACHABLE** → documented above `default:` | Fenced on every path: `resolve_expr` dies with its own message at the first use needing the type (`:4592`), `pend_ground` rejects a pending/void/None/partial grounding type *before* either of its `type_name` calls (`:4396`), `resolve_block` audits a still-pending decl at block end. 7 targeted probes (`xs := []` used / never grounded / grounded to `int` / `x := None` variants / `xs == ys`) all produced a dedicated message; 0 corpus arrivals |
    | `T_MAP_S`, `T_ID`, `T_TYPE`, `T_MAX` | **DO NOT EXIST** | No declaration anywhere in `src/tychoc.c` |

    Every other absent tag is a `*_BASE` family already handled by the
    `IS_NEWTYPE`/`IS_TASK`/`IS_CHAN`/`IS_HANDLE`/`IS_STRUCT`/`IS_ARRC`/`IS_MAPC`/
    `IS_OPT`/`IS_RES`/`IS_TUP`/`IS_FUNC`/`IS_ENUM`/`IS_SOA` predicates before the
    switch, as the phase text predicted.

    **Why this could not move emitted C** (the `make fixpoint` question, answered
    before running the gate, not after). `type_name` has exactly one codegen call
    site: `:7941`, `gen_str`'s fall-through for a fn/soa/handle/ptr *nested* field,
    which emits `"<%s>"` into the generated C. It cannot see the tags that
    changed: `gen_str` returns `tycho_chr(...)` for `T_CHAR` at `:7908`, 33 lines
    earlier; `T_VOID` produced `"<void>"` before and after (same string); and a
    `T_TYPARAM` never reaches codegen — templates emit nothing, only their
    concrete instances do (`:7052`, `:10962`, `:11040`; design note at `:705`).
    `make fixpoint` green below confirms it empirically.

    Compiler warnings: 3 before, 3 after — the same pre-existing
    `-Wmissing-field-initializers` on `Param` initializers (`:6095` et al.), none
    from this edit. Verified by `cc -Wall -Wextra -fsyntax-only` across a
    `git stash` / `git stash pop`.

    **Gate set — all seven green:**
    ```
    make test         passed: 427   failed: 0   / all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 36   failed 0
    make fixpoint     ok  B == C : tychoc0 reproduces itself byte-identically (34769 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32        passed: 427   failed: 0   / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (120 markdown files, no dead relative links)
    ```

- [x] **Phase 2 — MEASURE diagnostic-text divergence across all 143 reject fixtures (#3, part 1)**
  - Scope: docs only — write `docs/internals/diagnostic-parity-2026-07-25.md`.
    Run every `tests/reject/*.ty` (143 files) through both compilers. Normalize
    away the known format difference (`file:LINE: error: ` vs `line LINE: `) and
    compare the message *bodies*. Classify every fixture: IDENTICAL / DIVERGENT
    (both reject, text differs) / DECISION-DIVERGENT (one accepts — **this would
    be a conformance-oracle bug and must halt the plan and be reported
    immediately**). Record the exact normalization used and its assumptions, so a
    future reader can tell what the number does and does not cover.
  - Non-scope: no source change, no gate added. Measurement only.
  - Done when: the doc gives a per-fixture table and three totals; the
    normalization is stated as code, not prose; any DECISION-DIVERGENT fixture
    halts the plan.
  - Verify: `git diff --stat` shows `docs/` only; paste the three totals; full
    gate set green (no source touched).
  - **DONE 2026-07-25.** Full measurement:
    `docs/internals/diagnostic-parity-2026-07-25.md`. No source touched
    (`git status --short` showed only the new doc + this file).

    **The three totals, over all 143 `tests/reject/*.ty`:**
    ```
    IDENTICAL           68     both reject, normalized bodies match
    DIVERGENT           75     both reject, bodies differ
    DECISION-DIVERGENT   0     one accepts, one rejects
    ------------------------
    TOTAL              143
    ```
    **DECISION-DIVERGENT is 0 — the conformance oracle is intact and the plan
    continues.** All 143 exit non-zero from both compilers with a non-empty
    diagnostic. (`docs/spec/appendix-f-impl-defined.md:63-64` makes only the
    accept/reject decision normative; it is unbroken.)

    **How they were run** — mirrors `tests/run.sh:155` and `:159`. tychoc0 built
    with `./tychoc compiler/tychoc0.ty -o <outside-the-tree>/tychoc0` (no
    `make tychoc0` target exists), outside the repo so no `.c` spilled:
    ```sh
    ./tychoc  "$f" --emit-c -o "$W/rj.c" >"$OUT/$b.tychoc"  2>&1   # C to -o, so 2>&1
    "$W/tychoc0" "$f" --emit-c >/dev/null 2>"$OUT/$b.tychoc0"      # C to stdout, diag on stderr
    ```

    **Normalization, as code** (full version with its rationale table in the doc):
    ```python
    ECHO_C  = re.compile(r'^\s*\d+ \| ')        # tychoc echo   "     5 |     x := 1e"
    CARET_C = re.compile(r'^\s*\|\s*\^')        # tychoc caret  "       |           ^"
    CARET_0 = re.compile(r'^\s*\^\s*$')         # tychoc0 caret "        ^"
    LOC_C   = re.compile(r'^tests/reject/[A-Za-z0-9_]+\.ty(:(?P<ln>\d+))?: error: ')
    PHASE_0 = re.compile(r'^(lex|parse|type|resolve|generics|codegen): ')
    LINE_0  = re.compile(r'^line (?P<ln>\d+): ')
    WARN_C  = re.compile(r': warning: ')
    # headers = lines that are neither an echoed source line nor a caret row
    #   (tychoc0's echo has no gutter: it is the line directly above a caret row)
    # fatal   = the LAST non-warning header (die() prints last, then exits)
    # body    = fatal with LOC_C stripped (tychoc) / LINE_0+PHASE_0 stripped in a
    #           loop (tychoc0 — both orders occur: "parse: line 8: m" and
    #           "line 3: type: m")
    ```
    **The echoed source line is EXCLUDED from the comparison.** It is the
    fixture's own source text, byte-identical by construction; only the gutter
    differs (`     5 | ` + original indent, `src/tychoc.c:52-55`, vs an 8-space
    indent, `compiler/tychoc0.ty:5448`). Including it would score gutter
    formatting, not diagnostics.

    **The one judgement call: stripping tychoc0's phase tag.** 59 of 143 tychoc0
    fatal lines carry `lex:`/`parse:`/`type:`/`resolve:`/`generics:`/`codegen:`,
    baked into the message string (`compiler/tychoc0.ty:622`, `:142`). tychoc has
    no such field. Kept rather than stripped, 27 IDENTICAL fixtures flip and the
    totals read **41 / 102 / 0**. Both are defensible; 68/75 is the headline
    because the tag is a per-compiler structural field. **Phase 3 must pick one
    explicitly if it gates.**

    **What the number does NOT cover:** only `tests/reject/*.ty` — not the 1
    package-reject dir nor the 15 `tests/abort/` fixtures; only a minority of the
    diagnostic surface (`src/tychoc.c` has 455 `die_at(` sites, `tychoc0.ty` has
    256 `die(` + 92 `die_at(` + 18 `lex_err(`); only the first fatal diagnostic;
    one build, native x86-64, default flags.

    **Secondary metric (not one of the three totals):** where both report a line
    number they agree 87/93. tychoc0 reports none at all on 50/143, tychoc on 1.

    **DIVERGENT grouped by cause — what Phase 3 needs:**

    | Group | Cause | Count |
    |---|---|---|
    | G1 | Cosmetic rendering of the same message (`str` vs `string` ×10, backticks ×6, em dash ×1, tuple spacing ×1, `bounded[4]int` vs `[b4]int` ×1, leading `.` on a tuple index ×1) | 20 |
    | G2 | Same rule, same types — one side carries an extra hint/detail (tychoc richer ×10, tychoc0 ×6) | 16 |
    | G3 | Newtype-identity family — two templates for one rule | 6 |
    | G4 | Sum-payload naming — tychoc names `Option(string)`/`Some(int)`, tychoc0 names only `str`/`int` | 7 |
    | G5 | Reworded, same rule, same types (`proc returns` vs `this function returns`, etc.) | 17 |
    | G6 | **Different diagnosis — the compilers disagree about *why*** | 9 |
    | | **Total** | **75** |

    G1+G3+G4 = 33 fixtures are three shared sentence shapes plus one type-name
    spelling — reachable by editing a handful of format strings. G2+G5 = 33 are
    independent hand-written strings, 33 separate edits in *both* compilers
    (`make fixpoint` forbids landing one without the other) for no correctness
    payoff. **G6 (9) is the only group where divergence means something other
    than wording**: `bare_expr_stmt`, `chan_reassign`, `char_as_type`,
    `explicit_count`, `explicit_nongeneric`, `fixed_array_nonconst_size`,
    `genenum_bare_nullary`, `infer_use_before_ground`, `base_mismatch_inout`.
    Sharpest: `explicit_count` — tychoc says `unknown type 'str'`, tychoc0 says
    `'empty' has 1 type parameter(s), but 2 explicit type argument(s) were given`.
    `base_mismatch_inout` is the one where they disagree about a *type*: tychoc
    calls the argument `float`, tychoc0 calls it `int`. Both still reject; no
    soundness hole. **No Phase 3 decision is pre-judged here.**

    **Gate set — all seven green (no source touched):**
    ```
    make test         passed: 427   failed: 0   / all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 36   failed 0
    make fixpoint     ok  B == C : tychoc0 reproduces itself byte-identically (34769 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32        passed: 427   failed: 0   / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (121 markdown files, no dead relative links)
    ```

- [x] **Phase 3 — gate diagnostic parity, IF Phase 2 says it is bounded (#3, part 2)**
  - **Conditional.** Read Phase 2's totals first. If DIVERGENT is small and each
    case is a genuine defect, fix them and add a `make diag-parity` lane that
    locks the normalized bodies. If DIVERGENT is large or the differences are
    deliberate (one compiler legitimately gives a richer message), do NOT force
    them equal — instead record the divergence as accepted, write the rationale,
    and drop the gate. Say which branch was taken and why.
  - Non-scope: message text does not become normative in the spec. This is an
    implementation-parity gate, not a conformance requirement — state that in the
    lane's own header comment so nobody later mistakes it for spec.
  - Done when: either the lane exists and is green and wired into `scripts/ci.sh`,
    or the doc records the decision not to gate with its reasoning.
  - Verify: if gated, paste the lane's summary line and show it fails on a
    deliberately perturbed message; full gate set green either way.
  - **DONE 2026-07-25. Branch taken: the "large / deliberate" one — NO blanket
    text-parity gate.** Phase 2 measured 75/143 = 52% DIVERGENT with
    DECISION-DIVERGENT = 0. The decision and its four reasons are written up in
    `docs/internals/diagnostic-parity-2026-07-25.md` § "Decision (Phase 3)". The
    short form: the spec makes only the accept/reject decision normative
    (`docs/spec/00-conventions.md` §1.3, `appendix-f-impl-defined.md:63-64`);
    G2+G5 (33 fixtures) are *deliberately* asymmetric because the C compiler is
    the user-facing one; and `tests/run.sh:248-254` already says in code that
    tychoc0's wording is held to a SEPARATE golden on purpose, so a shared-text
    gate would contradict a decision the harness documents.

    **Part A — the normative property was ALREADY gated. No lane added.**
    `tests/run.sh:148` builds tychoc0 (`"$TYCHOC" compiler/tychoc0.ty -o
    "$TMP/h0"`, hard `exit 2` if that fails) and the loop at `:150-163` asserts,
    per fixture, that tychoc rejects (`:155-156`), that its diagnostic is
    non-empty (`:157-158`), and that **tychoc0 does not fail-open** (`:159-160`,
    "tychoc0 ACCEPTED an invalid program (fail-open)"). `H0_REJECT_SKIP` (`:149`)
    is empty, so all 143 are covered on both sides; `:169-183` does the same for
    the package-reject dirs. `make test` runs `tests/run.sh` (`Makefile:88-89`),
    and `scripts/ci.sh` step `[2/19]` runs `make test`. It is decision-only and
    compares no text — exactly the lane this phase would have written. A second
    lane would have been duplicate coverage, so none was added.

    **Part B — the 9 G6 fixtures, classified against both sources.** Fixed only
    category (ii). All four fixes are in `compiler/tychoc0.ty` and are message
    paths (a `die_at` that already fired); none touches emitted C, which
    `make fixpoint` green confirms empirically.

    | Fixture | Class | Verdict and evidence |
    |---|---|---|
    | `base_mismatch_inout` | **(ii) FIXED** | `tychoc0.ty:10737` printed `base_ty(pp[k])` — the PARAMETER's type — in the argument's slot, so a `float` argument was reported as `int`: the one type it does not have. The sibling by-value/sink arms (`:10745`, `:10752`) already use `type_of(args[k], …)`, and `type_of` on an `EAddr` recurses to the place (`:5033`). Now `argument 1 of 'f' is float, expected int`, byte-identical to tychoc (`src/tychoc.c:5500`) |
    | `explicit_nongeneric` | **(ii) FIXED** | `f$(int)` parses to the encoded callee `f$<int>` (`tychoc0.ty:866`); when `f` is not generic the generics pass leaves it and the codegen-position unknown-callee die (`:6710`) blamed a **missing declaration** for a function declared two lines up, quoting a name the user never wrote. New helper `explicit_ta_check` re-uses this compiler's own wording from `:14371` and tychoc's from `src/tychoc.c:5455` |
    | `genenum_bare_nullary` | **(ii) FIXED** | tychoc0 said `unknown variable 'Empty'`; `Empty` is declared 3 lines above. Root cause: Stage 2b **drops every generic-enum template** from the enum list (`tychoc0.ty:14890`), so by checker time `Empty` is genuinely not a known variant. Caught earlier instead, in `mono_expr`'s `EVar` arm (`:14332`) where the templates are still present, guarded by `not has_str(names, s)` so a local of that name still wins. Now matches tychoc (`src/tychoc.c:4628`) |
    | `fixed_array_nonconst_size` | **(ii) FIXED** | tychoc0 said `declared type [#n]int but value is [int]` — blaming the array literal and printing `[#`, the parser's internal encoding for a const-sized array (`tychoc0.ty:1692`). Every *real* const is resolved to `[N]T` before the checker (`:2812`; verified: `const W = 3` / `v: [W]int = [1, 2]` → "a fixed-size array of length 3 needs 3 elements, got 2"), so a surviving `[#` means the name is not a const. Guard added at the `STypedDecl` arm; wording matches tychoc, which dies at parse (`src/tychoc.c:1818-1820`) |
    | `bare_expr_stmt` | (i) both correct — left | tychoc rejects semantically (`src/tychoc.c:3190`), tychoc0's parser rejects the same line one phase earlier (`tychoc0.ty:1622`, "expected ':=', '=', or '('"). Both statements are true of `x` on a line by itself; neither names a wrong type, rule, or operand |
    | `chan_reassign` | (i) both correct — left | The program violates two rules at once. tychoc cites "a channel variable cannot be reassigned" (`src/tychoc.c:6418`); tychoc0 cites "a channel must be created directly in a declaration" (`tychoc0.ty:10916`) — the same string tychoc uses on the sibling fixture `chan_in_container`, where the two agree. Both compilers own both rules; they pick different true ones |
    | `char_as_type` | (iii) tychoc0 strictly better — left | `tychoc0.ty:1799-1800` carries an explicit AUDIT comment: it knows tychoc says `unknown type 'char'` and deliberately says more ("there is no `char` type keyword — a char arises by inference"). Same rule, one side actionable. Converging would mean deleting the better message. (Improving *tychoc's* side is Phase 6 territory — it is the `char` ruling's surface, not a defect) |
    | `explicit_count` | (i) both correct — left | `empty$(int, str)` violates two rules: wrong arity **and** a bad type name. tychoc resolves type arguments first and dies `unknown type 'str'`; tychoc0 checks arity first (`:13820`). Verified `str` genuinely is not a tychoc type name: probe `x: str = "h"` → `unknown type 'str'`. Order of checks, not a misdiagnosis. (The probe did turn up a real defect on the tychoc0 side — see the new phase below) |
    | `infer_use_before_ground` | (iii) same rule, tychoc more actionable — left | Both say the type of `xs` cannot be inferred (`src/tychoc.c:4607` vs `tychoc0.ty:12851`). tychoc adds "assign/push/pass it first" and points at the use; tychoc0 points at the block. Same rule, no wrong claim — a wording/detail gap of the G2 kind, out of scope by this phase's own non-scope rule |

    **Fixtures locking the four corrected diagnoses.** `tests/reject/` scores the
    decision only, so the corrected *text* is locked with the mechanism that
    exists for exactly that (`tests/run.sh:227-278`): four new
    `tests/diag/g6_*.ty` with a `.err` (tychoc) and a `.h0err` (tychoc0) golden
    each. They fail the build if either message regresses:
    ```
    tests/diag/g6_inout_arg_type.h0err          line 10: argument 1 of 'f' is float, expected int
    tests/diag/g6_explicit_ta_nongeneric.h0err  line 8: type: explicit type arguments given, but 'f' is not a generic function
    tests/diag/g6_genenum_bare_nullary.h0err    line 9: generics: Empty is a variant of generic enum Box; supply the type explicitly, e.g. Empty$(int)
    tests/diag/g6_fixarr_nonconst_size.h0err    line 6: a fixed-size array length must be an integer literal or an int `const` -- 'n' is not
    ```
    `make test` rose 427 → **435** (4 fixtures × 2 lanes: `diag_` + `diag0_`).

    **A dead guard was written and then removed, on evidence.** The first version
    also called `explicit_ta_check` ahead of the type_of-side unknown-function
    die (`tychoc0.ty:4713`). A build with that call stubbed out produced
    byte-identical diagnostics for both `println(str(f$(int)()))` and
    `x := f$(int)()`, proving that path never fires — so it was deleted rather
    than shipped unexercised. The reason is recorded above the helper.

    **No-regression probes (all still accepted, rc=0):** `const W = 3` /
    `v: [W]int = [1, 2, 3]` (the const-sized array the new `[#` guard must not
    touch); `Empty$(int)` written out with a `match` over `Box(int)`; a bare
    `Red` of a non-generic enum. And `nosuch$(int)()` still reports an unknown
    function, so the new guard does not swallow a genuinely undefined name.

    **Gate set — all seven green:**
    ```
    make test         passed: 435   failed: 0   / all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 36   failed 0
    make fixpoint     ok  B == C : tychoc0 reproduces itself byte-identically (34839 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32        passed: 435   failed: 0   / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (121 markdown files, no dead relative links)
    ```
    `git status --short` after the run: `M compiler/tychoc0.ty` plus the 12 new
    `tests/diag/g6_*` files. No build spill (tychoc0 was built to a scratch dir
    outside the tree, per Phase 2's note that `-o` drops a sibling `.c`).

- [ ] **Phase 4 — emitted C is warning-clean under `-Wall -Wextra` (#4)**
  - Scope: `src/tychoc.c` map-append emitter (`:10701` emits the store;
    `:10272` emits `struct TychoMapC%d_` with `%s*ekeys` from `mapc_kslot`,
    `:10150`) and its `compiler/tychoc0.ty` twin — **both, in one commit**, or
    `make fixpoint` goes red.
  - **First, answer the safety question** (Pre-flight): trace whether any emitted
    code writes through `ekeys[e]` after the store. Record the answer with line
    numbers BEFORE writing the fix. Escalate if a write exists.
  - Then: sweep for other warnings. Compile the emitted C of the whole fixture
    suite with `-Wall -Wextra` and count. Fix what the emitters own; for anything
    that is genuinely a false positive, document it rather than silence it.
  - Done when: `tests/map_param_composite.ty` and the full suite emit zero
    warnings; the safety trace is written down; both compilers changed together.
  - Verify: paste the pre-fix and post-fix warning counts over the whole suite;
    `make fixpoint` green (proves the two emitters stayed byte-identical); full
    gate set green.

- [ ] **Phase 5 — examples stop describing a restriction that no longer exists (#5)**
  - Scope: `examples/json.ty` and `examples/invindex.ty`. Both state maps are
    int/float-valued and both build **parallel key/value arrays** to route around
    it (`json.ty` `JObj([string], [Json])`; `invindex.ty` `[string: int]` into a
    `[Posting]` side array). Correct the comments. Then decide, per file, whether
    the parallel-array *code* should be simplified to a direct composite-valued
    map — and if a file keeps the workaround (e.g. `json.ty`'s recursive `[string:
    Json]` may hit a real limit, or the parallel form may be genuinely clearer),
    say so in the comment instead of implying the language forbids it.
  - Non-scope: do not restructure an example beyond what the stale claim touches.
    These are teaching programs; a rewrite is a separate decision.
  - **Check the rest**: grep every `examples/*.ty` and `docs/` for other claims
    about map value types, `char`, or `while`, and list any further drift found as
    a new unchecked phase rather than absorbing it here.
  - Done when: no example asserts a restriction the compiler does not enforce;
    each remaining workaround carries an honest reason.
  - Verify: paste a probe showing the composite-valued map form compiling and
    running; `make test` and `make corelib` green (examples are gated); full gate
    set green.

- [ ] **Phase 6 — PARKED, NEEDS A USER RULING: `s[i]` → `int` vs `char` (#1)**
  - **Do not start this phase without an explicit ruling.** It changes what an
    expression means, and every option has a real cost.
  - The situation: `char` is a real type with real literals (`'e'` works, `char ==
    char` works, `str(char)` yields the glyph), but `s[i]` yields `int` by spec
    (`03-types.md:86`), so the two never meet. Idiomatic string code compares
    against numeric byte values with a comment translating them
    (`examples/invindex.ty:24-27`). There is also no `char` type *keyword*
    (`01-lexical.md:114-117`), so a `char` cannot be annotated, only inferred.
  - Options, with the honest cost of each:
    - **(a) `s[i]` yields `char`.** Most ergonomic; `s[i] == 'e'` reads right.
      Breaking: every `s[i]` arithmetic/comparison against `int` in corelib,
      examples, and both compilers' own source must be revisited, and `char ± int
      → char` wraps to `0..255` where `int` did not — a silent semantic change in
      existing code. Largest blast radius by far.
    - **(b) Keep `int`, allow `char`/`int` comparison.** `s[i] == 'e'` compiles;
      nothing existing changes meaning. Costs a small hole in the type system's
      strictness (mixed-type comparison, which §13.2 currently forbids).
    - **(c) Keep it, document it, add a helper.** No semantic change at all; e.g.
      `char_at(s, i) -> char` alongside `s[i] -> int`, plus a spec note. Cheapest
      and safest; leaves the wart visible but explained.
  - **Recommendation: (c), then reconsider (b) if it still grates.** (a) is a
    language change to fix a papercut, and this codebase has just spent 16 phases
    proving how expensive a silent width/semantic change is to chase down.
  - Done when: the ruling is recorded here verbatim, then the chosen option is
    implemented in its own phase(s) appended below with their own gates.

- [ ] **Phase 7 — a `void` value grounds a pending `[]` and escapes into codegen (found by Phase 1, NOT fixed there)**
  - Found while probing `T_PENDING` reachability. This program produces no tycho
    diagnostic at all — it reaches the C compiler and fails there:
    ```
    fn nop():
        print("x")

    fn main():
        xs := []
        push(xs, nop())
    ```
    ```
    /tmp/pd_pend_ground_void.c:2426:52: error: 'void' must be the only parameter and unnamed
     2426 | static void tycho_arr_C0_push(Arena*, TychoArrC0*, void );
    ```
  - The guard exists but this path bypasses it. `pend_ground` (`src/tychoc.c:4396`)
    already rejects a `T_VOID` grounding type with "cannot infer the type of '%s'
    from this use" — but `push` on a pending variable is special-cased earlier
    (`:5262`, `:5322`, and the `resolve_expr` head grounding at `:5696`) and
    grounds the element type without going through that check. Compare
    `xs = 3` / `x = 3`, which *do* produce clean tycho diagnostics.
  - Class: front-end escape (a raw C error is user-visible and unactionable), not
    a soundness hole — the program never builds either way.
  - Scope when taken: the pending-grounding paths in `src/tychoc.c` and their
    `compiler/tychoc0.ty` twins, plus a `tests/reject/` fixture. Check the whole
    `void`-as-a-value family, not only `push`: array literal elements, map
    values, tuple elements, channel sends.
  - Done when: the probe above is rejected by tychoc *and* tychoc0 with a tycho
    diagnostic naming the line; a reject fixture locks it; full gate set green.

- [ ] **Phase 8 — two concrete diagnostic defects surfaced by Phase 2's G6 (NOT fixed there)**
  - **(a) IS CLOSED BY PHASE 3** (2026-07-25), together with three more G6
    misdiagnoses the phase's non-scope note had assumed were defensible
    (`explicit_nongeneric`, `genenum_bare_nullary`, `fixed_array_nonconst_size`).
    See Phase 3's classification table. **What remains of this phase is (b)
    only** — the six line-number disagreements, none of which Phase 3 touched.
  - Phase 2 was measurement-only, so these were recorded, not touched. They are
    *message* defects, not soundness holes — both compilers still reject both
    programs. Listed separately from Phase 3 because they are worth fixing
    whether or not Phase 3 builds a parity gate.
  - **(a) `tychoc0` names the wrong side of an inout mismatch.**
    `tests/reject/base_mismatch_inout.ty` passes a `float` variable to
    `fn f(x: inout int)`. tychoc: `argument 1 of 'f' is float, expected int`.
    tychoc0: `argument 1 of 'f' is int but the &place is a different type` — it
    reports the *parameter's* type as the argument's and never names `float`.
    Scope when taken: the inout/&place arm in `compiler/tychoc0.ty` only; tychoc
    is already right, so this is a one-sided fix and `make fixpoint` is not at
    risk (tychoc0's own diagnostics are not emitted C).
  - **(b) Six fixtures where the two compilers point at different lines.**
    `if_expr_no_else` (tychoc 3 / tychoc0 5), `if_expr_type_mismatch` (6 / 3),
    `len_scalar` (1 / 6), `match_dup_arm` (10 / 8), `match_wildcard_not_last`
    (12 / 10), `subscript_type_mismatch` (5 / 8). Four of these are scored
    IDENTICAL on text — the divergence is purely the reported location. Decide
    per fixture which line is the *right* one (the one a user would look at)
    before changing either compiler; a wrong "fix" here makes the message worse.
  - Non-scope: the other seven G6 fixtures. Those are ordering/phase differences
    where each compiler's message is defensible on its own (e.g.
    `explicit_count`: `str` genuinely is not a type name — verified by probe
    `x: str = "h"` → `unknown type 'str'` — so tychoc rejecting the bad type
    before checking arity is a legitimate order, not an error).
  - Done when: (a) tychoc0 names the argument's actual type; (b) each of the six
    has a recorded ruling and the compilers agree where a ruling says they
    should; full gate set green.

- [x] **Phase 9 — DECISION divergence: tychoc0 accepts `str` as a written type annotation, tychoc rejects it (found by Phase 3, NOT fixed there)**
  - Found while checking whether tychoc's `unknown type 'str'` on
    `tests/reject/explicit_count.ty` was itself a misdiagnosis. It is not — but
    the probe that proved it exposed something bigger than a message:
    ```
    fn main():
        x: str = "h"
        println(x)
    ```
    ```
    tychoc   probe.ty:2: error: unknown type 'str'      (rc 1)
    tychoc0  <no diagnostic>                            (rc 0)
    ```
  - **This is an accept/reject divergence, i.e. the one property the spec makes
    normative** (`docs/spec/00-conventions.md` §1.3,
    `appendix-f-impl-defined.md:63-64`) — unlike everything else in the
    diagnostic-parity work, which is deliberately non-normative text. tychoc0
    fail-opens: `str` is its *internal* spelling of the string type
    (`compiler/tychoc0.ty:2116`, `:2844` list it alongside `int`/`float`/`bool`
    as a base type name), so a user annotation reading `str` sails through.
  - Phase 2 reported DECISION-DIVERGENT = 0, and that stands: it measured only
    `tests/reject/*.ty`, and no fixture writes `str` in type position. This is a
    hole in the fixture set, not a contradiction of the measurement.
  - Scope when taken: decide first which spelling the language has —
    `docs/spec/03-types.md` and `01-lexical.md` are the authority, and the answer
    determines whether tychoc0 must reject `str` or tychoc must accept it (do NOT
    assume tychoc is right merely because it is stricter). Then the type-name
    validation in whichever compiler is wrong, plus a `tests/reject/` fixture so
    the reject lane covers it on both sides.
  - Also sweep the same shape: every other name tychoc0 accepts in type position
    that tychoc does not (`tychoc0.ty:2844` is the list to walk — `ptr`, `bytes`,
    the sized ints), since each is the same class of fail-open.
  - Done when: both compilers agree on the accept/reject decision for a written
    `str` annotation, a reject fixture locks it, full gate set green.

  **DONE 2026-07-25 — tychoc0 was wrong; 2 of 23 swept names diverged (`str`, `void`).**

  **1. The spelling, from the spec (which is the authority).** The answer is
  `string`, and `str` is NOT a type at all:
  - `docs/spec/appendix-b-keywords.md:18-20` — "Of these, the **type keywords**
    are `int`, `float`, `bool`, `string`, `ptr`, `bytes`, the fixed-width
    integers `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, and `f32`.
    There is no `while`, `char`, or `void` keyword." That is the closed list.
  - `docs/spec/appendix-b-keywords.md:38` — "all builtin names (`len`, `push`,
    `str`, `wait`, …) | resolved as calls; **never reserved**". `str` is the
    string-conversion BUILTIN FUNCTION, not a type name.
  - `docs/spec/03-types.md:81` — the string type's own section heading is
    "### 5.2.5 `string`".
  - `docs/spec/03-types.md:74` — "there is no `char` type keyword";
    `docs/spec/01-lexical.md:116-117` — "there is no `char` or `void` type
    keyword (the `char` type arises only from character literals and inference,
    and `void` is the implicit …)"; `01-lexical.md:307` names a doc bug as
    "it lists `char` and `void` as type keywords — **neither is spellable as a
    type**".
  So tychoc is RIGHT to reject `str`, and tychoc0 fail-opened. This was checked
  against the spec first, not presumed from tychoc being stricter.

  **2. Root cause (source trace).** `parse_type_d`,
  `compiler/tychoc0.ty:1754-1817`. It matches the user-facing spellings
  explicitly — `:1767-1768` maps written `string` → internal `"str"` — and any
  unmatched bare name falls through to `:1817 return name` (intended for struct/
  enum names). Because `str`/`void`/`char` ARE tychoc0's own internal spellings
  (listed as base type names at `:2116` `ffi_scalar` and `:2844` `mangle_type`),
  that fall-through handed a *written* `str` straight back as the real internal
  string type and nothing downstream ever objected. The internal name and the
  user-facing name were conflated by omission: the guard existed for `char` only
  (`:1799`, added by earlier audit work) and was never generalized.
  Confirmed `parse_type_d` only ever sees user source, never internal type text:
  every caller passes a `[Token]` lexed from the input file, and the shipped
  `char` guard at `:1799` already rejects an internal spelling with all gates
  green — if internal type text reached this function, that guard would already
  be firing.

  **3. Fix.** `compiler/tychoc0.ty:1799-1812` — generalized the one-name `char`
  guard into the full internal-spelling guard (`char`, `void`, `str`), with the
  spec citations inline. Front-end reject path only; nothing reaches codegen
  (corroborated by `make fixpoint` staying green, below).

  **Before/after probe** (`fn main(): x: str = "h"; println(x)`):
  ```
  BEFORE  tychoc   probe.ty:2: error: unknown type 'str'      (rc 1)
          tychoc0  <no diagnostic>                            (rc 0)   FAIL-OPEN
  AFTER   tychoc   probe.ty:2: error: unknown type 'str'      (rc 1)
          tychoc0  type: 'str' is not a type keyword (...)    (rc 1)   agree
  ```

  **4. FULL base-type-name sweep.** Every name on the `:2844` list plus the
  spec's type keywords plus negative controls, each in user type-annotation
  position. Probe isolates the NAME lookup — an identity fn needs no literal, so
  no assignment-type noise: `fn f(x: N) -> N: return x`.

  | name | tychoc | tychoc0 BEFORE | tychoc0 AFTER | verdict |
  |---|---|---|---|---|
  | `int` | accept | accept | accept | agree |
  | `float` | accept | accept | accept | agree |
  | `bool` | accept | accept | accept | agree |
  | `string` | accept | accept | accept | agree |
  | **`str`** | REJECT | **accept** | REJECT | **was DIVERGENT — fixed** |
  | `char` | REJECT | REJECT | REJECT | agree (guarded since earlier audit) |
  | **`void`** | REJECT | **accept** | REJECT | **was DIVERGENT — fixed** |
  | `ptr` | accept | accept | accept | agree |
  | `bytes` | accept | accept | accept | agree |
  | `u8` `u16` `u32` `u64` | accept | accept | accept | agree (4 names) |
  | `i8` `i16` `i32` `i64` | accept | accept | accept | agree (4 names) |
  | `f32` | accept | accept | accept | agree |
  | `bounded` `soa` `inout` `fn` `nosuchtype` | REJECT | REJECT | REJECT | agree (negative controls) |

  23 names swept, **2 divergent, both fixed**. Both were the same root cause and
  the same check (the `return name` fall-through at `:1817` returning an internal
  spelling), which is why one guard closes both. `ptr` and `bytes` — flagged as
  suspects when the phase was written — are genuine spec type keywords
  (`appendix-b-keywords.md:18-19`) and correctly accepted by BOTH; no change.

  **5. Fixtures** (reject lane, `tests/run.sh:148-163`, runs BOTH compilers and
  fails on "tychoc0 ACCEPTED an invalid program (fail-open)"):
  - `tests/reject/str_as_type.ty` — the exact probe program.
  - `tests/reject/void_as_type.ty` — `void` in param + return position.
  One file per name, though both funnel through the same guard, because the
  compiler stops at the first error so a single file cannot lock both. Joins the
  pre-existing `tests/reject/char_as_type.ty`. Test count 435 → **437**.

  **6. NOT over-tightened — legal base types still compile AND RUN.** A program
  exercising every one of the 15 spec type keywords in annotation position
  (identity fn per type + live calls in `main`) compiles and runs on both, with
  byte-identical output:
  ```
  tychoc  -> 7 1.5 true / hi 2 / 1234 / 5678 / 2.5   (rc 0)
  tychoc0 -> 7 1.5 true / hi 2 / 1234 / 5678 / 2.5   (rc 0)
  ```

  **7. Gates** (each its own foreground `env -u LD_PRELOAD make …`):
  ```
  test        passed: 437   failed: 0  /  all green        (435 + 2 new fixtures)
  corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
  conc        conc: passed 36   failed 0
  fixpoint    ok B == C : tychoc0 reproduces itself byte-identically (34839 lines C)
              fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
  ilp32       passed: 437   failed: 0  /  all green
  spec-check  spec-examples: 7 runnable example(s), all pass
  check-links link check: ok (121 markdown files, no dead relative links)
  ```
  `fixpoint` green is the load-bearing one: it asserts byte-identical emitted C,
  so a green B==C proves the change stayed in the front-end reject path and never
  moved codegen.
  `git status --short` clean of spill (only the intended `compiler/tychoc0.ty` +
  the two new fixtures). tychoc0 was built at `/tmp/ph9/` throughout, outside the
  repo tree.

  **Residual uncertainty:** none for base type NAMES — the sweep is exhaustive
  over the `:2844` list and the spec's closed type-keyword list. Not swept:
  internal spellings of COMPOUND type forms (tychoc0 writes e.g. `&T` for inout
  and `~T` for sink as type-string prefixes, `:1832-1838`). Those are not bare
  identifiers so they cannot reach the `:1817` fall-through, and the negative
  controls `inout`/`bounded`/`soa`/`fn` all reject on both — but a written
  compound-prefix form was not probed. Class is the same; exposure is not
  demonstrated. Logged as Phase 10 rather than absorbed here.

- [x] **Phase 10 — sweep the COMPOUND-type internal spellings for the same fail-open shape (discovered by Phase 9, out of its scope)**
  - Phase 9 closed the bare-NAME fall-through in `parse_type_d`
    (`compiler/tychoc0.ty:1817`) and swept all 23 base type names. It did NOT
    sweep tychoc0's internal spellings of *compound* type forms: `parse_param`
    at `:1832-1838` encodes `inout T` as the type string `"&T"`, sink as `"~T"`,
    and variadic as `"...T"`; other internal forms include the `[bN]T` bounded
    encoding (`:2825-2826`) and the `fn(P->R)` shape (`:2837-2843`).
  - Question to answer: can a user *write* any of these internal encodings
    directly in type position (e.g. `x: &int`, `x: ~int`, a literal `[b4]int`)
    and have tychoc0 accept where tychoc rejects? Phase 9 argued exposure is
    unlikely — these are not bare identifiers, so they cannot reach the `:1817`
    fall-through, and the `inout`/`bounded`/`soa`/`fn` negative controls reject
    on both compilers — but it PROBED only the bare-identifier position. The
    argument is a source trace, not a measurement.
  - Method: mirror Phase 9's harness — probe each written compound form against
    both compilers in user annotation position, tabulate accept/reject, fix any
    divergence in whichever compiler the spec says is wrong (`02-grammar.md` /
    `appendix-a-grammar.md` are the authority for what a type *form* may be),
    add a `tests/reject/` fixture per divergence.
  - Fail closed: any form that cannot be confidently shown user-writable per the
    grammar gets REJECTED, never accepted on a guess.
  - Done when: the compound-form sweep table is recorded, every divergence fixed
    and fixture-locked, legal compound annotations still compile on both, full
    gate set green. A table with zero divergences is a legitimate outcome and
    closes the phase — the measurement is the deliverable.

  **DONE 2026-07-25 — MEASURED, not argued: 45 probes, 0 divergences. No source
  change. Phase 9's residual-uncertainty note is now closed by measurement.**

  **1. Correction to this phase's own line numbers.** Phase 9's edit shifted the
  file, and two of the cited ranges were the *mangler*, not the *producer*:

  | Plan text said | Actually at | What is there |
  |---|---|---|
  | `parse_param` `:1832-1838` | **`compiler/tychoc0.ty:1835-1851`** | `:1844` `"&" + parse_type(...)`, `:1847` `"~" + …`, `:1850` `"..." + …`. (`:1832-1833` is now `parse_type`, the public entry point.) |
  | bounded `[bN]T` `:2825-2826` | **`:1718-1731`** (produced), `:2825-2826` is `mangle_type` consuming it | `:1731 return "[b" + str(bcap) + "]" + belem` |
  | `fn(P->R)` `:2837-2843` | **`:1732-1753`** (produced), `:2837-2843` is `mangle_type` | `:1753 return out + "->" + ret + ")"` |

  Two further internal compound encodings the phase text did not name were found
  by reading `parse_type_d` and swept as extras: the map spelling `{K:V}`
  (`:1709`, user writes `[K:V]`) and the const-sized-array spelling `[#N]T`
  (`:1692`, user writes `[C]T`).

  **2. THE SWEEP TABLE — 7 forms × 5 positions = 35 probes.** Each probe is a
  complete program whose only questionable token is the annotation; the `param`,
  `field` and `arrelem` probes never call the function or build the struct, so
  they isolate the annotation alone with no value-type noise. Messages are the
  last non-warning fatal line, location prefix stripped.

  | form (as WRITTEN) | position | tychoc | tychoc message | tychoc0 | tychoc0 message | verdict |
  |---|---|---|---|---|---|---|
  | `&int` | param | REJECT | expected a type (int, float, bool, string, [int], or a struct) | REJECT | expected a type | agree |
  | `&int` | local | REJECT | " | REJECT | " | agree |
  | `&int` | return | REJECT | " | REJECT | " | agree |
  | `&int` | field | REJECT | " | REJECT | " | agree |
  | `&int` | arrelem | REJECT | " | REJECT | " | agree |
  | `~int` | param | REJECT | expected a type (int, float, bool, string, [int], or a struct) | REJECT | expected a type | agree |
  | `~int` | local | REJECT | " | REJECT | " | agree |
  | `~int` | return | REJECT | " | REJECT | " | agree |
  | `~int` | field | REJECT | " | REJECT | " | agree |
  | `~int` | arrelem | REJECT | " | REJECT | " | agree |
  | **`...int`** | **param** | **accept** | — (emits C) | **accept** | — (emits C) | **agree — and CORRECT, see §3** |
  | `...int` | local | REJECT | expected a type (int, float, bool, string, [int], or a struct) | REJECT | expected a type | agree |
  | `...int` | return | REJECT | " | REJECT | " | agree |
  | `...int` | field | REJECT | " | REJECT | " | agree |
  | `...int` | arrelem | REJECT | " | REJECT | " | agree |
  | `[b4]int` | param | REJECT | a fixed-size array length must be an integer literal or an int `const` -- 'b4' is not | REJECT | unknown type '#b4]in' | agree |
  | `[b4]int` | local | REJECT | " | REJECT | a fixed-size array length must be an integer literal or an int `const` -- 'b4' is not | agree |
  | `[b4]int` | return | REJECT | " | REJECT | returning [int] but this function returns [#b4]int | agree |
  | `[b4]int` | field | REJECT | " | REJECT | unknown type '#b4]in' | agree |
  | `[b4]int` | arrelem | REJECT | " | REJECT | unknown type '#b4]in' | agree |
  | `fn(int->int)` | param | REJECT | expected ')' | REJECT | unexpected token | agree |
  | `fn(int->int)` | local | REJECT | " | REJECT | " | agree |
  | `fn(int->int)` | return | REJECT | " | REJECT | " | agree |
  | `fn(int->int)` | field | REJECT | " | REJECT | " | agree |
  | `fn(int->int)` | arrelem | REJECT | " | REJECT | " | agree |
  | `{int:int}` (extra) | param | REJECT | unexpected character '{' | REJECT | unexpected character | agree |
  | `{int:int}` | local | REJECT | " | REJECT | " | agree |
  | `{int:int}` | return | REJECT | " | REJECT | " | agree |
  | `{int:int}` | field | REJECT | " | REJECT | " | agree |
  | `{int:int}` | arrelem | REJECT | " | REJECT | " | agree |
  | `[#4]int` (extra) | param | REJECT | unknown type 'println' | REJECT | expected a type | agree |
  | `[#4]int` | local | REJECT | unknown type 'println' | REJECT | expected a type | agree |
  | `[#4]int` | return | REJECT | expected a type (int, float, …) | REJECT | unexpected token | agree |
  | `[#4]int` | field | REJECT | expected '(' after fn in a function type | REJECT | unexpected token | agree |
  | `[#4]int` | arrelem | REJECT | unknown type 'println' | REJECT | expected a type | agree |

  **Plus 10 RECURSIVE-nesting probes** (the 5 named forms inside a map value
  `[string: FORM]` and inside `Option(FORM)`, which re-enter `parse_type_d`
  through a different call site): **10 probes, 0 DIVERGENT**, same messages as
  the `arrelem` row of each form.

  ```
  TOTAL 45 probes (35 positional + 10 nested)   DIVERGENT 0
  ```

  **3. The one `accept` is not a fail-open — the grammar says it is legal.**
  `docs/spec/appendix-a-grammar.md:66`:
  ```
  Param      ::= IDENT ":" ( "inout" | "sink" )? "..."? Type
  ```
  `...` is a PARAM-position prefix in the user grammar, so `a: ...int` is
  grammatical and both compilers are right to accept it. `...T` differs from the
  other four forms in kind: it is the one whose internal encoding *coincides
  with* the legal user spelling (`compiler/tychoc0.ty:1848-1850` reads a real
  `TEllipsis` token, it does not fall through a name lookup). Both compilers
  reject it in all four NON-param positions, which the grammar also requires —
  `Type` (`appendix-a-grammar.md:81-98`) has no `"..."` alternative.

  **4. Why the other four cannot be the Phase 9 shape — now with the measurement
  behind it, not instead of it.** `Type ::=` at `appendix-a-grammar.md:81-90`
  enumerates every type form; none begins with `&` or `~`, so `&int`/`~int` die
  at "expected a type" in both. `:83` spells the function type
  `"fn" "(" ( Type ( "," Type )* )? ")" ( "->" Type )?` — the arrow is OUTSIDE
  the parens, so the internal `fn(int->int)` is ungrammatical in both. `[b4]int`
  matches `ArrayOrMap ::= IDENT "]" Type` (`:93`, the `[C]T` const-sized array),
  so `b4` is read as a const NAME and both reject it as a non-const — tychoc at
  parse, tychoc0 at the guard Phase 3 added. `{` never appears in `Type` and is
  not a tycho lexeme at all. `[#4]int` cannot even reach the parser: `#` opens a
  comment (`docs/spec/01-lexical.md`), so `[#4]int` lexes as `[` + comment-to-EOL
  and the parse resumes on the following line — hence tychoc's `unknown type
  'println'`. Fail-closed on all five; nothing was accepted on a guess.

  **5. Nothing fixed, because nothing diverged.** No `tests/reject/` fixture was
  added: the phase's own text makes a zero table a legitimate close, and a
  fixture can only lock a *decision*, which is already identical on both sides
  for all 45 probes. Test count stays **437**. `git status --short` is empty
  apart from this file.

  **6. NOT over-tightened — every LEGAL compound spelling still compiles AND
  RUNS, identically on both.** One program using `inout int`, `sink string`,
  `...int` variadic, `bounded[4]int` (struct field), `bounded[8]int` (return +
  local), `fn(int) -> int` (param, passed a named function), `soa [Pt]`,
  `[string: int]` and `Option(int)`. tychoc via `-o`; tychoc0 via `--emit-c`
  piped through `cc -O2 -fwrapv -std=c11 … -lm` (mirrors `tests/run.sh:72`):
  ```
  tychoc   -> 42 5 10 42 2 16 3 12 1 3   (rc 0)
  tychoc0  -> 42 5 10 42 2 16 3 12 1 3   (rc 0)
  diff out_c.txt out_0.txt  ->  BYTE-IDENTICAL
  ```
  (No compiler source was edited, so over-tightening was structurally impossible;
  this is run anyway because the phase text asks for it explicitly.)

  **7. Gates — all seven green** (each its own foreground `env -u LD_PRELOAD
  make …`; tychoc0 built at `/tmp/ph10/`, outside the repo tree):
  ```
  test        passed: 437   failed: 0  /  all green
  corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
  conc        conc: passed 36   failed 0
  fixpoint    ok  B == C : tychoc0 reproduces itself byte-identically (34839 lines C)
              fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
  ilp32       passed: 437   failed: 0  /  all green
  spec-check  spec-examples: 7 runnable example(s), all pass
  check-links link check: ok (121 markdown files, no dead relative links)
  ```
  `git status --short` after the run: empty except `plan.md`. No build spill.

  **Residual uncertainty:** the sweep covers the five compound encodings the
  phase named plus two found by reading `parse_type_d`, in five syntactic
  positions plus two nested ones. Not swept: tuple `(A,B)` and `soa[T]` internal
  spellings, which are *identical* to their user spellings (`:1717`;
  `appendix-a-grammar.md:82,:84`) and so cannot diverge by construction.

- [ ] **Phase 11 — `bounded[N]T` is implemented by both compilers but absent from the spec grammar (found by Phase 10, out of its scope)**
  - Both compilers accept `bounded[N]T` as a type form — `compiler/tychoc0.ty:1718-1731`
    parses it and `tests/bounded.ty:6,:8,:34` exercises it in a struct field, a
    return type and a local, all green under `make test`.
  - But the type grammar does not have it. `Type ::=` at
    `docs/spec/appendix-a-grammar.md:81-90` lists `$IDENT`, `soa[T]`, `fn(…)`,
    tuple, `[`-forms, `Option`/`Result`/`Channel`, `QualName TypeArgs?` and
    `PrimType` — no `bounded`. `grep -rn 'bounded' docs/spec/` returns 4 hits in
    3 files (`16-builtins.md:214`, `13-concurrency.md:100,:115`,
    `07-memory-model.md:136`) and every one is about a *bounded channel* or
    bounded memory, not the type form. `docs/spec/02-grammar.md` has none.
  - Class: **doc drift / spec incompleteness, NOT a compiler divergence** — both
    compilers agree on accept, so the normative accept/reject property
    (`00-conventions.md` §1.3) is intact. Logged rather than absorbed because
    Phase 10's scope was compound-form *validation*, not spec authoring.
  - Scope when taken: add the `bounded` alternative to `Type` in
    `appendix-a-grammar.md` and to `02-grammar.md`, and give it a section in
    `03-types.md` (capacity must be a positive integer literal — `:1721-1725`;
    element may not be `void`/`bool` — `:1729`). Docs only; no compiler change.
  - Done when: the grammar admits every type form both compilers accept, and
    `make check-links` + `make spec-check` stay green.

## Out of scope

Not addressed by this plan; listed so they are not silently absorbed:

- `while`, traits, package manager, ternary, HM inference, GC — permanent
  non-goals (`docs/architecture.md#decided-non-goals`).
- The two evidence limits F.3 already states: LLP64 is asserted architecturally
  and never measured (no Windows hardware), and the ILP32 lane omits sanitizers
  (no 32-bit ASan under multilib). Closing either needs hardware or a toolchain
  this box does not have.
- `tests/run.sh` leaves a ~57MB `tmp.XXXXXXXX/` scratch dir in the repo root on
  every run. Cleaned manually on 2026-07-25; the harness still does not clean up
  after itself. Cosmetic, but it is why the root accumulates spill.
