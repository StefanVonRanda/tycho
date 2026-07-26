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
**CORRECTION (Phase 4, 2026-07-25) — an earlier version of this preamble said
`make fixpoint` asserts that *tychoc and tychoc0* emit byte-identical C, and that
any emitted-text change must therefore land in both compilers in one commit. That
was FALSE, and it was repeated in five phase prompts before Phase 4 checked it.**
`compiler/fixpoint.sh:16-21` builds `A` with tychoc, then compares `cA` against
`cB` — **both emitted by tychoc0-derived binaries**. Its own header states the
purpose: "cB == cC proves the Tycho compiler reproduces itself exactly." tychoc is
compared only **behaviourally**, by golden output over `tests/` + `examples/`
(`:23-30`). So fixpoint is a tychoc0 **self-reproduction** check, not a
cross-compiler byte-identity check, and the two emitters legitimately differ in
text today (`char **ekeys` vs `char** ekeys`) while green.

Consequence for planning: a change to `src/tychoc.c`'s emitted text does **not**
mechanically require a paired `compiler/tychoc0.ty` edit. It requires that the two
stay *behaviourally* equivalent. Verify which of the two you actually need before
assuming the twin must change — Phase 4 found tychoc0 never had the defect it was
told to fix there.

`make fixpoint` remains in the gate set: it is the self-hosting tripwire, and a
front-end-only change leaving it green is real evidence the change did not reach
codegen.

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
    exists for exactly that (`tests/run.sh`'s negative lanes (as they stood before the 2026-07-26 tychoc0 freeze)): four new
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

- [x] **Phase 4 — emitted C is warning-clean under `-Wall -Wextra` (#4)**
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
  - **DONE-WHEN AMENDED after the fact — read this before trusting the tick.**
    Two clauses above were mis-specified by the plan's author, and the box is
    ticked against the amended form, not the original:
    (a) *"the full suite emits zero warnings"* conflated two different compile
    lines. `Makefile:11`'s `-Wall -Wextra` builds **tychoc itself**; the line
    tychoc emits to compile a user's program (`src/tychoc.c:11532`) carries no
    `-Wall`. Under the flags emitted C is actually compiled with, the count went
    **24 → 4** and `-Wdiscarded-qualifiers` **20 → 0**. Under opt-in
    `-Wall -Wextra` the residue is 13346, ~89% unused-symbol noise inherent to
    emitting the whole runtime into every program — a design change, out of
    scope here, filed as Phase 13. That clause is **NOT met** and is deferred,
    not waived.
    (b) *"both compilers changed together"* rested on the false fixpoint premise
    corrected in the Phases preamble above. tychoc0 never had this defect
    (`tychoc0.ty:10475` states the invariant directly), so only tychoc changed.
    The phase's substantive goal — the const-qualifier defect and its safety
    verdict — was met in full.

  **EVIDENCE (2026-07-25).** Two premises in the phase text above were wrong and
  are corrected here rather than edited away.

  *Line citations.* All three were off by ~20: the store is `src/tychoc.c:10721`
  (not `:10701`), the `struct TychoMapC%d_` emitter is `:10292` (not `:10272`),
  `mapc_kslot` is `:10170` (not `:10150`).

  *`make fixpoint` does NOT compare tychoc against tychoc0 byte-for-byte.*
  `compiler/fixpoint.sh:16-21`: `A = ./tychoc` building `tychoc0.ty`; `cA` = A
  emitting; `B = cc(cA)`; `cB` = B emitting; `cmp cA cB`. Both `cA` and `cB` come
  from **tychoc0**-derived binaries — it proves tychoc0 is a fixed point of
  itself. tychoc's emitted text is only compared *behaviourally* (`ref` output
  equality over `tests/*.ty` + `examples/*.ty`, `:23-30`). The two emitters
  already differ in text today and are green: tychoc emits `char **ekeys`
  (`%s*ekeys`, `src/tychoc.c:10292`, `ks="char *"`) while tychoc0 emits `char** ekeys`
  (`compiler/tychoc0.ty:10400`). So this phase did NOT have to change tychoc0 —
  and did not need to, because **tychoc0 never had the defect** (below).

  **STEP 1 — write-through trace. VERDICT: NO WRITE-THROUGH. Qualifier bug, not
  an aliasing bug.** Every use of `ekeys[…]` in the emitted string-key family
  (`TychoMapC0` in `/tmp/ph4/mpc.c`, from `tests/map_param_composite.ty`), with
  the callee signature that decides it:

  | emitted site | use | verdict |
  |---|---|---|
  | `:2458` `strcmp(m.ekeys[e-1], k)` | `const char *` params | read |
  | `:2462`, `:2502` `tycho_si_hash(m->ekeys[…])` | `:1734` `(const char *s)` | read |
  | `:2497` `strcmp(m->ekeys[m->idx[i]-1], k)` | `const char *` params | read |
  | `:2508` `tycho_mapc0_put(a,&r,src.ekeys[e],…)` | `const char *k` → `tycho_str_copy` `:971 (const char *s)` | read |
  | `:2523` `tycho_arr_str_push(a,&r,m.ekeys[e])` | `:1487 (const char *v)`, body `:1496` stores `tycho_str_copy(a,v)` — a **copy**, pointer does not escape | read |
  | `:2528` `tycho_mapc0_find(y, x.ekeys[e])` | `const char *k` | read |
  | `:2629` `tycho_str_concat(a,r,m.ekeys[e])` | `:874 (const char *x, const char *y)` | read |
  | `:2471` `m->ekeys[w] = m->ekeys[r]` (compact) | writes the **slot**, not the pointee | slot write |
  | `:2480` `nk[e] = m->ekeys[e]` (grow) | slot write | slot write |
  | `:2481` `m->ekeys = nk` | array write | array write |
  | `:2482` `m->ekeys[e] = k` (the warning) | slot write | slot write |

  Nothing dereferences a key for mutation and nothing hands a key to a callee
  that could; the only non-`const` escape (`tycho_arr_str_push`) copies the bytes.
  So the warning is a missing/incorrect qualifier, and the fix is a qualifier —
  **no cast was used anywhere.**

  **STEP 2 — the fix, and why it is on the parameter and not the slot.**
  The defect is **tychoc-only**. tychoc0 states the invariant directly —
  `compiler/tychoc0.ty:10475` `kpar := kslot + " k"`, so its key parameter *is*
  its slot type and its `_app` (`:10490`) never discards anything. tychoc broke
  that invariant by deriving the two independently: `mapc_kslot` → `"char *"`
  (`:10172`) but `mapc_kparam` → `"const char *k"` (`:10177`).

  The slot was **not** made `const`, deliberately: `c_type(T_STRING)` is
  `"char *"` (`:1223`), and every string container in the system is `char **` —
  `TychoArrStr.data` (emitted `:1468`) and the hand-written runtime's own
  `TychoMapSI.ekeys` (`:1652`), whose `tycho_map_si_append` (`:1786`) takes
  `char *k` for exactly this reason. Const-ifying the slot would make the emitted
  map the only const string container in the language and would contradict the
  runtime family this emitter explicitly mirrors (`src/tychoc.c:10680-10681`).
  `const` is kept on every read-only entry point (`find`/`get`/`has`/`del`/`set`/
  `del_pure`/`put`/`slotptr`), where it is true and still catches a future
  write-through; it is dropped only on `_append` (`:10732`), whose contract is
  ownership transfer — both callers (`_put` `:10742`, `_slotptr` `:10748`) pass
  the freshly arena-owned copy from `mapc_kcopy`, i.e. `tycho_str_copy(a,k)`
  returning `char *`.

  Emitter sites actually changed, `src/tychoc.c`, **tychoc only**:
  - `:10175` — `mapc_kparam` kept as-is, comment marks it the READ-only param.
  - `:10180-10196` — new `mapc_kparam_own(Type)` returning
    `sfmt("%sk", mapc_kslot(k))` (tychoc0's `kpar := kslot + " k"`), with the
    ownership rationale in its header comment.
  - `:10693` — `char *kpo = mapc_kparam_own(keyt);`
  - `:10740` — the `_append` `fprintf` arg list takes `kpo` in place of `kp`.

  Emitted text delta is exactly one token on one line, only for string keys:
  `_append(Arena *a, TychoMapC0 *m, const char *k, …)` → `… char *k, …`.
  The int-rep and composite branches are byte-unchanged (`mapc_kslot` and
  `mapc_kparam` already agreed there) — which is why `make corelib`
  ("tychoc and tychoc0 agree, match goldens") stayed green.

  **STEP 3 — suite sweep.** Emitted C for all `tests/*.ty examples/*.ty
  corelib/*/*.ty compiler/*.ty` via `--emit-c` (263 seen, 227 emitted, 36 are
  reject fixtures that correctly refuse to emit), each `cc -O2 -fwrapv -std=c11
  -c`, 0 errors both runs.

  | | pre-fix | post-fix |
  |---|---|---|
  | **default-on** (what `tychoc`'s own `cc` line uses — `src/tychoc.c:11532` passes **no** `-Wall`/`-Wextra`) | **24** | **4** |
  | ` └ -Wdiscarded-qualifiers` | **20** | **0** |
  | ` └ integer-literal (2 fixtures)` | 4 | 4 → new Phase 12 |
  | **opt-in `-Wall -Wextra`** | 13366 | 13346 |
  | ` └ -Wunused-function` | 8507 | 8507 → new Phase 13 |
  | ` └ -Wunused-variable` | 3363 | 3363 → new Phase 13 |
  | ` └ -Wmisleading-indentation` | 1286 | 1286 → new Phase 13 |
  | ` └ -Wunused-parameter` | 149 | 149 → new Phase 13 |
  | ` └ -Wmissing-field-initializers` | 23 | 23 → new Phase 13 |
  | ` └ -Wunused-but-set-variable` | 13 | 13 → new Phase 13 |
  | ` └ -Woverflow / -Wmissing-braces` | 1 / 1 | 1 / 1 → new Phase 12 |

  Direct repro, before and after (`./tychoc tests/map_param_composite.ty -o …`):
  pre — `mpc.c:2482:44: warning: assignment discards 'const' qualifier from
  pointer target type [-Wdiscarded-qualifiers]`; post — no diagnostic.

  **Done-when clause "the full fixture suite emits ZERO warnings" is NOT met, and
  is not meetable by this phase.** The plan's premise ("#4 is one emitted string
  literal", Pre-flight) held for the named defect — that one *is* closed, 20 → 0 —
  but the `-Wall -Wextra` surface underneath it is 13346 warnings, ~89% of them
  `-Wunused-function`/`-Wunused-variable` fired on the whole `runtime/tycho_rt.c`
  prelude and the whole per-type family (`tycho_arr_C12_pop`, `_sing_Tok_3`, …)
  that every program pastes in and mostly does not call. Removing those means
  demand-driven emission or `__attribute__((unused))` across the family emitters —
  a design change, squarely outside "smallest change / do not restructure the map
  emitter". Filed whole as Phase 13 rather than absorbed or silenced. What this
  phase actually closes is the surface a user sees: `tychoc` compiles emitted C
  with no `-Wall`/`-Wextra`, so its user-visible warning count went **24 → 4**,
  and the 4 remaining are the unrelated literal emitter (Phase 12).

  **Gates** (each its own foreground command, `env -u LD_PRELOAD make …`):
  - `make test` → `passed: 437   failed: 0` / `all green`
  - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`
  - `make conc` → `conc: passed 36   failed 0`
  - `make fixpoint` → `ok   B == C : tychoc0 reproduces itself byte-identically (34839 lines C)` / `fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)`
  - `make ilp32` → `passed: 437   failed: 0` / `all green`
  - `make spec-check` → `spec-examples: 7 runnable example(s), all pass`
  - `make check-links` → `link check: ok (121 markdown files, no dead relative links)`
  - `git status --short` → `M src/tychoc.c` only; no build spill.

- [x] **Phase 5 — examples stop describing a restriction that no longer exists (#5)**
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
  - **DONE 2026-07-25. Three examples changed; the sweep found a THIRD false
    restriction the phase text did not know about (`examples/inout.ty:5`).**

    **PROBE 1 — a map's value type really is unrestricted** (array-, string- and
    struct-valued in one program; `/tmp/ph5/p1.ty`):
    ```
    $ ./tychoc /tmp/ph5/p1.ty -o /tmp/ph5/p1 && /tmp/ph5/p1
    built /tmp/ph5/p1
    3 hello x
    ```
    Matches `docs/spec/03-types.md:189-191` (§5.3.5): "A map `[K: V]` … The
    **value type `V` is unrestricted** (any type)." Only the KEY type is
    constrained (`:193-196`).

    **PROBE 2 — the `[string: Json]` question the phase flagged as "may or may
    not compile". It COMPILES.** A map valued by the recursive enum that
    contains it (`/tmp/ph5/p2.ty`, `enum Json: JNull / JNum(int) /
    JObj([string: Json])`), built and run: `rc=0`, prints `obj`. So there is no
    limit to document — `json.ty` was converted rather than annotated.

    **PROBE 3 — `[string: Posting]` with in-place field growth through the map
    place** (`/tmp/ph5/p4.ty`): `push(idx[term].docs, doc)` through an
    `inout [string: Posting]` works —
    ```
    a -> 2
    b -> 1
    ```
    (First attempt used `idx.has(term)` → `'.has' on a non-struct value`;
    membership is spelled `k in m`, `docs/spec/12-aggregates.md:401`.)

    **PROBE 4 — `keys()` order, which decides whether json.ty's round-trip can
    change.** `docs/spec/12-aggregates.md:400-402` and `17-runtime.md:75` make
    `keys(m)` **insertion order**, normatively (`appendix-e-conformance.md:183`,
    §30.4). So a `[string: Json]` object round-trips member order unchanged —
    confirmed empirically by the golden diff below, not assumed.

    ### DECISION 1 — `examples/json.ty`: CONVERTED to `JObj([string: Json])`
    The parallel arrays existed *only* to route around the phantom restriction
    (the old comment said so in as many words), and Probe 2 shows the direct
    form compiles. The change is confined to the `JObj` payload: the enum
    variant, `parse_object`, and the four `JObj` match arms (`to_json`, `get`,
    `as_num`, `count_nums`). `get` collapses from a linear scan over `ks` to
    `m.get(key, JNull)`. 21 insertions / 21 deletions; no other function touched.
    Header now reads (`examples/json.ty:10-15`):
    > Scope: … objects, plus whitespace; floats are left as an exercise. An
    > object is simply a `[string: Json]` map: a map's VALUE type is unrestricted
    > (docs/spec/03-types.md 5.3.5), so it may be the recursive enum itself, and
    > `keys()` yields keys in insertion order (12-aggregates.md 18.6) so a
    > round-trip preserves member order. (A repeated member name keeps the last
    > value, as most JSON readers do.)

    The last sentence is the one honest behaviour change: parallel arrays kept
    both entries for a duplicate member name and `get` returned the first; a map
    keeps the last. The fixture has no duplicate keys, so **output is
    byte-identical and `tests/json.out` was NOT touched** —
    ```
    $ ./tychoc examples/json.ty -o /tmp/ph5/j2 && /tmp/ph5/j2 | diff tests/json.out -
    (no output)
    round-trip: {"name":"tycho","version":7,"tags":["systems","arena"],"nested":{"ok":true,"n":-3},"empty":[],"nothing":null}
    version    = 7
    nested.n   = -3
    sum of nums = 4
    ```

    ### DECISION 2 — `examples/invindex.ty`: KEEPS the slot form, comment made honest
    **The alternative was BUILT AND RUN before deciding, not reasoned about.**
    `/tmp/ph5/inv_new.ty` is the full `[string: Posting]` rewrite (drops the
    `postings` array and `term_slot`, rewrites `add`, `index_doc`, `query_and`
    and `main`): it compiles, runs, and `diff tests/invindex.out` is **empty** —
    it is a perfectly legal program. It was still rejected, for two reasons that
    are about the example and not the language:
    1. it deletes the file's own second teaching point — growing an element's
       array field in place through an `inout [Posting]` borrow
       (`push(postings[i].docs, …)`), advertised in the header;
    2. it restructures 5 of 8 functions and removes one, which this phase's
       **non-scope** rule forbids ("do not restructure an example beyond what
       the stale claim touches").
    The comment now gives the real reason and explicitly denies the language
    limit (`examples/invindex.ty:5-10`):
    > a [string: int] map maps each term to its slot in a [Posting] array. This
    > indirection is a DELIBERATE choice, not a language limit: a map's value
    > type is unrestricted (docs/spec/03-types.md 5.3.5) and `[string: Posting]`
    > compiles and runs fine. The slot form is kept because it is how a real
    > inverted index is laid out — a small term dictionary over one contiguous
    > posting array — and because it is what lets the next bullet exist;

    ### DECISION 3 — `examples/inout.ty:5`: a THIRD false restriction, deleted
    Found by the sweep, not named in the phase text. The line said
    *"Restricted to non-heap types (int, bool, pure-value structs)."* That is
    false twice over: `examples/invindex.ty` has itself been passing
    `inout [Posting]` and `inout [string: int]` since it shipped, and
    `docs/spec/07-memory-model.md:182-186` has a section titled **"11.3 Heap
    `inout`"** — "`inout` extends to heap-bearing values". Probe
    (`/tmp/ph5/p6.ty`, one call taking `inout string`, `inout [int]`,
    `inout [string: int]`): builds, prints `hi! 1 1`. Replaced with a pointer to
    §11.3 and to invindex.ty. The line ABOVE it (`:4`, "the same variable can't
    be passed to two inout params at once") was probed too and is **TRUE** —
    `f(&x, &x)` → `error: variable 'x' passed to two inout parameters of 'f'
    (overlapping mutable access)` — so it was left alone.

    ### DRIFT SWEEP — method and findings
    Searched every `examples/*.ty` and `examples/*/*.ty` for restriction-shaped
    assertions (`no X` / `not supported` / `only …` / `can't be` / `lacks` /
    `maps are|hold`), then every `#` comment mentioning `char`, `while`, `str`,
    `void`; then all of `docs/` + `README.md` for map-value-type, `inout`-scalar
    and `char`/`void`/`while`/`str`-keyword claims.

    | Finding | Verdict |
    |---|---|
    | `examples/json.ty:12` "maps are int/float-valued only" | **FALSE — fixed** (Decision 1) |
    | `examples/invindex.ty:6` "maps hold int/float values" | **FALSE — fixed** (Decision 2) |
    | `examples/inout.ty:5` "Restricted to non-heap types" | **FALSE — fixed** (Decision 3) |
    | `examples/inout.ty:4` "same variable can't be passed to two inout params" | TRUE — probed, left |
    | `examples/json.ty:44` `'\0' + byte: int -> char` | TRUE — that is exactly how a `char` is obtained (no `char` keyword) |
    | `examples/fetch/main.ty:34` "as a void helper" | TRUE — describes a fn with no return type, not a written `void` annotation |
    | `docs/spec/01-lexical.md:115-116`, `02-grammar.md:168`, `03-types.md:74`, `appendix-b-keywords.md:20` — "there is no `while`/`char`/`void` keyword", "`str` is not a type" | **TRUE on both compilers, re-probed after Phases 1 and 9.** `x: str/void/char = …` → tychoc `unknown type 'X'`, tychoc0 `'X' is not a type keyword (…)`, both rc≠0; and `while := 3` compiles and prints `3`, so `while` really is an ordinary identifier |
    | `docs/spec/appendix-h-differences.md:22` (H2) | Already records the removed "int-keyed maps support only int/float values" diagnostic and states V is unrestricted — consistent |
    | `docs/spec/14-ffi.md:43` "`inout` out-parameters — a numeric scalar or `ptr` only" | TRUE and unrelated: an **FFI** restriction, not a language one |

    **No out-of-scope drift was found, so no new phase is filed by this one.**
    The `docs/` side of the map claim was already correct (H2 above), and the
    `char`/`void`/`while`/`str` statements Phases 1 and 9 could have falsified
    were all re-probed and still hold.

    **Gate set — all seven green** (each its own foreground
    `env -u LD_PRELOAD make …`):
    ```
    make test         passed: 437   failed: 0   / all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 36   failed 0
    make fixpoint     ok  B == C : tychoc0 reproduces itself byte-identically (34839 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32        passed: 437   failed: 0   / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (121 markdown files, no dead relative links)
    ```
    Per-example, both compilers, against the recorded goldens (tychoc0 built to
    `/tmp/ph5/` so no `.c` spilled into the tree):
    ```
    json:     OK  tychoc==tychoc0==golden
    invindex: OK  tychoc==tychoc0==golden
    inout:    OK  tychoc==tychoc0==golden
    ```
    `git status --short` after the run: `M examples/inout.ty`,
    `M examples/invindex.ty`, `M examples/json.ty` — no build spill. **No golden
    file was modified.**

- [x] **Phase 6 — RULED AND CLOSED: `s[i]` stays `int`; `char_at` added (#1)**
  - **CLOSED 2026-07-25.** Its Done-when was "the ruling is recorded here
    verbatim, then the chosen option is implemented in its own phase(s)". Both
    hold: the ruling is recorded below verbatim, and option (c) shipped as
    **Phase 26** (`bb396dc`) — `char_at(s, i) -> char` on both compilers, with
    `s[i]` still yielding `int` (`s[1]` on `"hello"` → `101`, probed on both) and
    an out-of-bounds abort byte-identical to `s[i]`'s. Point #1 of the original
    five is therefore closed as a documented-and-mitigated design choice, not as
    a semantic change.
  - *(Original parked text follows, kept for the reasoning that produced the
    ruling.)*
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
  - **USER RULING, 2026-07-25, verbatim: "keep int, add char_int, do the frontend
    audit".** Option **(c)** is taken: `s[i]` keeps yielding `int` — no semantic
    change to any existing program — and a helper is added alongside it. The
    "frontend audit" half of the ruling is Phase 25 below.
  - **OPEN QUESTION on the helper's name/shape — asked, not assumed.** The ruling
    says `char_int`; the recommendation it accepted said `char_at(s, i) -> char`.
    These are not the same function, and the difference matters:
    - a *string-index* helper returning `char` (recommendation (c)'s `char_at`) is
      what makes `s[i] == 'e'` expressible, which is the papercut #1 is about;
    - a *char→int* conversion already exists — `to_int(char)` / `to_u32(char)`,
      `docs/spec/03-types.md:78-79` — so a helper by that meaning would be a
      no-op, and RULE 11 says do not add what is already there.
    Implementation is Phase 26, held until the name/shape is confirmed. Nothing
    is guessed: a wrong helper is a permanent public API.

- [x] **Phase 7 — a `void` value grounds a pending `[]` and escapes into codegen (found by Phase 1, NOT fixed there)**
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
  - **DONE 2026-07-25.**

    **Line citations in this phase's own text were wrong — corrected.** All four
    were ~13 lines short of the real sites (the file moved under Phase 1's edit):

    | This phase said | Actually at | What is there |
    |---|---|---|
    | `pend_ground` at `:4396` | **`:4409`** (guard on `:4410`) | correct mechanism, wrong line |
    | push special case at `:5322` | **`:5343`** | `pend_ground(args[0]->sval, arr_of(resolve_expr(args[1])), line)` |
    | map_set special case at `:5262` | **`:5286`** | `pend_ground(args[0]->sval, gm, line)` |
    | resolve_expr head grounding at `:5696` | **`:5716`** | `if (vars_find(...) && vt == T_PENDING) pend_ground(e->sval, want, e->line)` |

    (Line numbers above are pre-fix. There is also a fourth call site the phase
    text never mentioned: the assignment grounding at `:6423`.)

    **Root cause — the guard tested the wrong thing, not the wrong place.**
    `pend_ground`'s guard was `t == T_VOID || t == T_NONE || ...`, i.e. a test on
    the TOP-LEVEL tag. But the push path does not hand it `void`; `:5343` calls
    `arr_of(resolve_expr(args[1]))` first, and `arr_of` (`:1327`) falls through to
    `arrc_of(elem)` for any non-int/float/string element — so a `void` element
    became a perfectly well-formed *array* type whose top tag is `IS_ARRC`, sailed
    past the guard, and reached codegen as `static void tycho_arr_C0_push(Arena*,
    TychoArrC0*, void )`. The composition, not the special-casing, was the hole.

    **Fix site — one generalized predicate per compiler, no parallel guard.**
    - `src/tychoc.c:4409` — new `static int uninferrable(Type t)` holding the old
      five-way test, and `pend_ground`'s guard now reads it THROUGH one level of
      composition: `uninferrable(t) || (is_array(t) && uninferrable(arr_elem(t)))
      || (is_map(t) && (uninferrable(map_key(t)) || uninferrable(map_val(t))))`.
      This is the only change to tychoc; all four `pend_ground` call sites are
      covered by it, exactly as the phase brief asked.
    - `compiler/tychoc0.ty` — the twin `fn ty_has_void(ty: string) -> bool`
      (recursive over array / map / tuple type strings), applied at three sites.

    **tychoc0 was PROBED before being patched (Phase 4's lesson), and it DID have
    the defect** — plus three more the phase brief had not predicted. Probing
    protocol copied from `tests/run.sh:150-164`: `tychoc <f> --emit-c -o <out>`
    and `tychoc0 <f> --emit-c` (tychoc0 has no `-o`; it writes C to stdout).
    `--emit-c` stops before the C compiler, so exit 0 means *the front end
    accepted it* — the escape, isolated from the C toolchain.

    **Sweep table — the whole `void`-as-a-value family, both compilers.**
    21 probes (18 illegal + 3 legal controls), `/tmp/ph7/probes/`.

    | Shape | tychoc before | tychoc after | tychoc0 before | tychoc0 after |
    |---|---|---|---|---|
    | `xs := []` ; `push(xs, nop())` | **ESCAPE** | reject `:6` | **ESCAPE** | reject |
    | `xs := []int` ; `push(xs, nop())` | reject | reject | **ESCAPE** | reject |
    | `ys := [nop()]` | reject | reject | **ESCAPE** | reject |
    | `t := (1, nop())` | reject | reject | **ESCAPE** | reject |
    | `x := nop()` | reject | reject | **ESCAPE** | reject |
    | `ys := [1, nop()]` | reject | reject | reject | reject |
    | `m["k"] = nop()` (map value) | reject | reject | reject | reject |
    | `m[nop()] = 1` (map key) | reject | reject | reject | reject |
    | `send(c, nop())` | reject | reject | reject | reject |
    | `str(nop())` | reject | reject | reject | reject |
    | `"a" + nop()` | reject | reject | reject | reject |
    | `return nop()` from `-> int` | reject | reject | reject | reject |
    | `takes(nop())` (user fn arg) | reject | reject | reject | reject |
    | `x: int = nop()` | reject | reject | reject | reject |
    | `x = nop()` (assign) | reject | reject | reject | reject |
    | `print(nop())` | reject | reject | reject | reject |
    | `xs[0] = nop()` (element) | reject | reject | reject | reject |
    | `if nop():` | reject | reject | **ESCAPE** | **still escapes → Phase 14** |
    | *control* `nop()` as a statement | accept | accept | accept | accept |
    | *control* `nop()` in a `for` body | accept | accept | accept | accept |
    | *control* void fn + bare `return` | accept | accept | accept | accept |

    Escapes: tychoc **1**, tychoc0 **6**. Fixed here: tychoc 1/1, tychoc0 5/6.
    The sixth (`if nop():`) is **not** a void defect — probes `if 1:` and
    `if "s":` escape tychoc0 identically, so it is a general missing
    bool-condition check. Filed as Phase 14 rather than absorbed (scope lock).

    **Before / after — the phase's repro, both compilers:**
    ```
    BEFORE  tychoc   (no tycho diagnostic; escapes to cc)
                     /tmp/…/push_pending.c:2426:52: error: 'void' must be the
                     only parameter and unnamed
                      2426 | static void tycho_arr_C0_push(Arena*, TychoArrC0*, void );
    BEFORE  tychoc0  (no diagnostic at all; exit 0, emitted C)

    AFTER   tychoc   /tmp/…/push_pending.ty:6: error: cannot infer the type of 'xs' from this use
                          6 |     push(xs, nop())
    AFTER   tychoc0  line 5: cannot infer the type of 'xs' from this use
                             xs := []
                             ^
    ```
    Known, honest divergence: tychoc points at the grounding **use** (line 6),
    tychoc0 at the **decl** (line 5). tychoc0's `pend_seek` returns only a type
    string, not the use's location; threading a location up through it is
    disproportionate to the benefit, and both messages name real, adjacent code.
    (This is a seventh instance of the Phase 8(b) line-number family.)

    **Why the three tychoc0 sites, and why not more.** `lift_program`
    (`compiler/tychoc0.ty:15021`) only lifts a function when
    `block_has_lambda(f.body)` is true — and `stmt_has_lambda` returns true for a
    bare pending decl (`pend_init_kind(e) != 0`), which is why the B-3 grounding
    fix lands reliably. It is ALSO why a first attempt to guard `lift_stmt`'s
    `SDecl` arm was **reverted**: that arm only runs inside lambda-bearing or
    pending-bearing functions, so `x := nop()` would have been rejected or
    accepted depending on whether the enclosing function happened to contain a
    lambda. Verified empirically before reverting: an instrumented build
    (`eprint` in each arm) printed for `push_pending` and printed **nothing** for
    `bind_void`. The guard was moved to `gen_stmt`'s `SDecl` arm, which every
    function reaches. Final sites:
    - `lift_block` (`:12938-12948`) — the pending-grounding install, twin of
      `pend_ground`; `pend_probe` gained an `sl: inout int` so the diagnostic can
      name real source instead of `die()`ing without a location.
    - `gen_stmt` `SDecl` (`:8452`) — universal; covers `x := nop()`,
      `ys := [nop()]`, `t := (1, nop())` via the composed type string.
    - `gen_call` `push` arm (`:6504`) — next to the existing fail-closed
      "push's first argument must be an array or soa" guard; covers the
      already-typed array. Deliberately checks *only* for void, not full
      element-type equality: tychoc0's checker is thinner than tychoc's by
      design, and asserting general equality here could reject legal newtype /
      generic coercions. Fail-closed on the shape actually proven broken.

    **Legal-usage control — void in statement position still compiles AND RUNS**
    (over-tightening would be worse than the escape). Built with each compiler
    and executed:
    ```
    ctl_stmt         tychoc-run: xok      tychoc0-run: xok
    ctl_stmt_loop    tychoc-run: xxxok    tychoc0-run: xxxok
    ctl_void_return  tychoc-run: g xok    tychoc0-run: g xok
    ```
    Identical output from both, and all three still classify as "front end
    accepted" in the sweep above.

    **Fixtures** (one per distinct rejection — Phase 9's note that the compiler
    halts at the first error):
    - `tests/reject/void_grounds_pending_push.ty` — the repro.
    - `tests/reject/void_push_value.ty` — push onto an already-typed array.
    - `tests/reject/void_bound_to_decl.ty` — `x := nop()`.
    Each verified rejected by BOTH compilers with a located diagnostic.

    **Gates — each its own foreground command, `env -u LD_PRELOAD make …`:**
    ```
    make test         passed: 440   failed: 0 / all green      (437 + 3 fixtures)
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 36   failed 0
    make fixpoint     ok B == C : tychoc0 reproduces itself byte-identically (34892 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32        passed: 440   failed: 0 / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (121 markdown files, no dead relative links)
    ```
    `git status --short`: only the two sources + the three new fixtures. No spill.
    `make fixpoint` green is the evidence the change never reached codegen: both
    guards `die` before any type is installed, so emitted C is byte-identical.

- [x] **Phase 8 — two concrete diagnostic defects surfaced by Phase 2's G6 (NOT fixed there)**
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

  **DONE 2026-07-25 — part (b) only ((a) was closed by Phase 3). 3 of 6 changed,
  1 ruled both-defensible, 2 ruled preferable-but-disproportionate → Phase 17.**

  **1. Re-verified line table.** Both compilers rebuilt from HEAD (`make tychoc`;
  `./tychoc compiler/tychoc0.ty -o /tmp/ph8/tychoc0`) and all six run through
  both. **The plan's table was correct in all six pairs** — the first plan table
  in this document to survive verification intact (contrast Phases 4/7/10/16).

  | fixture | tychoc BEFORE | tychoc0 BEFORE | plan claimed | ruling | AFTER |
  |---|---|---|---|---|---|
  | `if_expr_no_else` | 3 | 5 | 3 / 5 ✓ | **line 3** | 3 / **3** |
  | `if_expr_type_mismatch` | 6 | 3 | 6 / 3 ✓ | both defensible | 6 / 3 (unchanged) |
  | `len_scalar` | 1 | 6 | 1 / 6 ✓ | **line 6** | **6** / 6 |
  | `match_dup_arm` | 10 | 8 | 10 / 8 ✓ | 10 preferred, 8 defensible | 10 / 8 (unchanged, → Phase 17) |
  | `match_wildcard_not_last` | 12 | 10 | 12 / 10 ✓ | 12 preferred, 10 defensible | 12 / 10 (unchanged, → Phase 17) |
  | `subscript_type_mismatch` | 5 | 8 | 5 / 8 ✓ | **line 5** | 5 / **5** |

  Governing principle applied throughout: **point at the line the user must EDIT
  to fix the program, not the line where the compiler happened to notice.**

  **2. The six rulings.**

  **(i) `if_expr_no_else` → line 3 (the `if` head). tychoc0 was WRONG.**
  Source: `3: x := if true:` / `4: 1` / `5: println(str(x))`. The incomplete
  construct is the `if` on line 3; line 5 is a well-formed `println` the parser
  merely reached while looking for `else`. Sending the user to edit line 5 points
  at code with nothing wrong with it. tychoc already reported 3
  (`src/tychoc.c:2620`). tychoc0 reported the lookahead token's line
  (`compiler/tychoc0.ty:2537`, `t.line` where `t := toks[pos]` at `:2528`).
  Fix: report the head, which `parse_value_if` already receives as `sl`
  (`:2527`, passed from `parse_value_ctrl` `:2544`). **Trap found by running it:**
  `sl` is not a raw line — `tloc` packs `line * 100000 + col` (`:123-124`), which
  `die_at` decodes at `:5528-5529`. The naive `str(sl)` printed `line 300009`.
  Corrected to `str(sl / 100000)`, keeping this site's existing `parse: line N:`
  text (no wording churn). An `elif` chain still reports the *elif* that lacks
  the else, because the recursive call passes `esl` (`:2530-2531`).

  **(ii) `if_expr_type_mismatch` → BOTH DEFENSIBLE, no change.**
  Source: head at 3, arms at 4 (`1`) and 6 (`"two"`). The message already names
  both types. **Neither line is uniquely the edit site** — the user fixes this by
  editing arm 4 *or* arm 6; there is no declaration to make one side
  authoritative and the other the violator. tychoc names the arm that broke
  agreement (6, `src/tychoc.c:6325`, `tails[i]->line`); tychoc0 names the
  construct whose arms disagree (3, `compiler/tychoc0.ty:8429/8462`, `sl`). Both
  land the user inside the same four-line construct and each is internally
  consistent with that compiler's policy. Changing either would be preference
  dressed as a fix, so neither was touched.

  **(iii) `len_scalar` → line 6 (the `len(x)` call). tychoc was WRONG — and this
  was a real bug, not a policy difference.** tychoc reported **line 1, a
  COMMENT**. Root cause traced, not guessed: `desugar_interp`
  (`src/tychoc.c:1995`) re-lexes each `{…}` hole with `lex(sub)` (`:2025`), and
  `lex` restarts its own counter at `int line = 0;` (`:217`), so every node parsed
  out of a hole carried line 1 regardless of where the f-string sat.
  **Generalized by probe** — this hit *every* diagnostic inside an f-string hole,
  not only `len`:
  ```
  # two comment lines, then fn main(): / x := 5 / <the call>
  plain   y := len(x)         on line 5  ->  p_len_plain.ty:5: error: len(...) ...
  f-str   print(f"{len(x)}")  on line 5  ->  p_len_fstr.ty:1: error: len(...) ...   <-- line 1 = a comment
  ```
  Fix (`src/tychoc.c:2025-2033`): stamp the f-string's real line onto the
  sub-tokens before parsing —
  `for (int k = 0; k < tv.n; k++) { tv.v[k].line = line; tv.v[k].col = 0; }`.
  `col` is deliberately zeroed: a hole-relative column is meaningless against the
  real source line, and `die_at` treats `0` as "no caret" (`:36`, `:54-55`), so
  the fix reports the right line rather than the right line with a lying caret.

  **(iv) `match_dup_arm` → line 10 preferred (the duplicate arm); tychoc0's line 8
  defensible; NOT changed — the honest fix is disproportionate, filed as Phase 17.**
  Line 10 is the arm the user deletes, so it wins on the edit-site principle.
  tychoc0's `match c:` (line 8) is still defensible: the message names the variant
  ("duplicate arm for Red"), so it does lead to the fix.
  **Why not fixed:** `check_match` (`compiler/tychoc0.ty:10946`, single caller
  `:11207`) receives arm *names* as `[string]` plus one line. The per-arm lines do
  not exist anywhere — the AST variant itself is
  `SMatch(Expr, [string], [[string]], [[Stmt]], int)` (`:493`): one loc for the
  whole construct, none per arm. Supplying them means widening `SMatch`, which
  `grep -n "SMatch("` shows is destructured or constructed at **43 sites** in the
  self-hosting compiler, all of which must then round-trip `make fixpoint`. A
  43-site AST change to a bootstrap compiler **to move a caret two lines** is not
  proportionate; per this phase's own method step 4, it is filed rather than
  forced. The cheap proxy (use `bodies[j][0]`'s line) was considered and
  **rejected**: it is correct only when the arm body sits on the arm's own line
  and reports the *following* line for the block form
  (`Red:` newline `    println("b")`), i.e. it would be wrong in a way the current
  behaviour is not.

  **(v) `match_wildcard_not_last` → line 12 preferred (the misplaced `_` arm);
  tychoc0's line 10 defensible; NOT changed — same blocker, same filing.**
  Identical analysis: same `check_match`, same missing per-arm lines, same 43-site
  cost. Line 12 is the arm the user moves; line 10 names the construct and the
  message names the offending arm kind.

  **(vi) `subscript_type_mismatch` → line 5 (the `yield`). tychoc0 was WRONG.**
  Source: `4: subscript at(...) -> inout string:` / `5: yield &r.cells[i]` /
  `8: print(r.at(0))`. The defect lives entirely in the *definition*; line 8 is a
  correct call, so tychoc0 sent the user to edit innocent code at a site that
  cannot be fixed there. **Why line 5 and not line 4, when (ii) ruled two lines
  both-defensible:** unlike (ii), one side here IS authoritative. Line 4 is the
  declaration; line 5 is the expression that violates it. Pointing the caret at
  the violator (with the declaration named in the message text, which it already
  is) is the standard declaration-vs-actual convention. That is also what tychoc
  does (`src/tychoc.c:4857`, `e->line`).
  Fix, two edits in `compiler/tychoc0.ty`: (1) `parse_subscript` stamped the body
  stmt with the `subscript` keyword's line (`sline * 100000`), so the yield's own
  loc was being *discarded at parse time* — capture it instead
  (`yloc := tloc(toks[pos])` before `expect_ident(toks, &pos, "yield")`) and build
  `[SExpr(place, yloc)]`; (2) `subscript_call_type` now reads it through a new
  `subscript_place_line(f)` helper (sibling of the existing `subscript_place`)
  instead of using `_el`, the call-site loc. **Fails open**: a `0` line falls back
  to `_el`, so a body shape the helper cannot read still produces a located
  message rather than a bare `line 0`.
  Note this was an *intermediate* wrong answer caught by re-running rather than
  assuming: the first version read the body stmt's stored loc and reported line
  **4**, because the parser had stamped the head line there. The `yield` line was
  never lost downstream — it was thrown away at construction.

  **3. What changed vs what deliberately did not.**
  - `src/tychoc.c:2025-2033` — f-string hole tokens carry the f-string's real line
    (fixes (iii), and every other diagnostic inside an interpolation hole).
  - `compiler/tychoc0.ty:2537` — value-`if` missing-`else` reports the head,
    decoded out of the packed tloc (fixes (i)).
  - `compiler/tychoc0.ty:2095`, `:2123`, `:3830-3840`, `:5246-5249` — the
    subscript body carries the `yield` loc and the mismatch diagnostic uses it
    (fixes (vi)).
  - **Deliberately unchanged:** all message TEXT. Phase 3 declined a blanket
    text-parity gate and the pre-flight pre-registered that; wording edits here
    would add fresh entries to Phase 2's divergence set for no gain. The
    surviving `string` vs `str` wording split in (vi) is therefore left as-is —
    it is a text divergence, not a location one, and out of this phase's scope.
  - **Deliberately unchanged:** `if_expr_type_mismatch` (ruled both-defensible)
    and the two `match` arm-line cases (ruled preferable but disproportionate).

  **4. Golden updates: NONE — and that was checked, not assumed.** All 17
  `tests/diag/*.ty` fixtures were run through both rebuilt compilers and `cmp`'d
  against their `.err` and `.h0err` goldens; every one matched byte-for-byte, so
  nothing was re-recorded. None of the six fixtures in this phase has a recorded
  golden (the reject lane asserts only that a non-empty diagnostic is produced,
  `tests/run.sh:150-158`), which is why changing three reported lines needed no
  golden churn. `tests/warn/*.err` likewise unchanged (covered by `make test`).

  **5. Gate set — each its own foreground command, `env -u LD_PRELOAD`.**
  ```
  make test         passed: 447   failed: 0   /  all green
  make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
  make conc         conc: passed 37   failed 0
  make fixpoint     ok   B == C : tychoc0 reproduces itself byte-identically (34929 lines C)
                    fixpoint: all green (self-hosting; B==C; single files + packages;
                                         tychoc0 self-split dogfood)
  make ilp32        passed: 447   failed: 0   /  all green
  make spec-check   spec-examples: 7 runnable example(s), all pass
  make check-links  link check: ok (121 markdown files, no dead relative links)
  ```
  `git status --short`: only `src/tychoc.c` and `compiler/tychoc0.ty` (plus this
  file). No build spill. `make fixpoint` green is the evidence the tychoc0 edits
  never reached codegen: the subscript loc is diagnostic-only and `B == C` still
  holds at the same 34929 lines of C.

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

  **RE-VERIFIED 2026-07-25 by Phase 18 — the method was ALREADY sound, and the
  zero-divergence conclusion SURVIVES.** Phase 11 later found that comparing
  `rc` of `tychoc f -o out` against `tychoc0 f` conflates frontend and `cc`
  status, and Phase 18 was told to assume Phase 10 might have used it. It did
  not. Phase 10's harness is still on disk and was read: `/tmp/ph10/sweep.py:61-67`
  runs **`--emit-c` on both sides** —
  `[TYCHOC, path, "--emit-c", "-o", out_c]` and `[TYCHOC0, path, "--emit-c"]` —
  and never invokes `cc`, so both columns are the FRONTEND verdict, the same
  surface `tests/run.sh:148-212` compares. The nesting harness
  `/tmp/ph10/sweep2.py:28-29` does the same. Both were re-run by Phase 18:
  ```
  pre-Phase-18 tychoc0   sweep.py  TOTAL 35 probes, DIVERGENT 0
                         sweep2.py nesting probes: 10, DIVERGENT 0
  post-Phase-18 tychoc0  sweep.py  TOTAL 35 probes, DIVERGENT 0
                         sweep2.py nesting probes: 10, DIVERGENT 0
  ```
  So the 45/0 table stands, and Phase 18's compiler change did not perturb it.
  One caveat now recorded honestly: because both columns are frontend-only, a
  row reading `accept` means "the frontend accepted", **not** "the emitted C
  compiles" — the `...int` param row is accept/accept on the frontend and that
  is exactly what the phase was measuring. Nothing in the table was ever an
  `rc`-conflation artefact.

- [x] **Phase 11 — `bounded[N]T` is implemented by both compilers but absent from the spec grammar (found by Phase 10, out of its scope)**
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
  - **DONE 2026-07-25.** Docs only; neither compiler touched.
  - **Both plan citations verified correct**: `compiler/tychoc0.ty:1718-1731` is
    the `bounded` parse branch; `docs/spec/appendix-a-grammar.md:81-90` was the
    `Type ::=` block with no `bounded` alternative. The tychoc twin is
    `src/tychoc.c:1727-1743`.
  - **METHOD CORRECTION — the first probe round was invalid and was discarded.**
    `tychoc0` emits C to **stdout and never invokes `cc`**, so its exit status is
    a *frontend* verdict, while `tychoc file -o out` folds in the `cc` run. A
    naive rc-vs-rc comparison scored 5 "divergences", 4 of which were `cc`
    failures on one side only. The table below separates three columns per
    compiler — FRONT (frontend accept/reject; `tychoc --emit-c`, which writes
    `<out>.c` and prints `wrote <path>`, `src/tychoc.c:11576-11579`), CC (does
    the emitted C build), RUN (stdout). `tests/run.sh:148-212` compares the
    **frontend** exit status across the two compilers, so FRONT is the column
    that decides normative divergence. Harness: `/tmp/ph11/probe3.py`.

    | probe | form | tychoc FRONT/CC/RUN | tychoc0 FRONT/CC/RUN |
    |---|---|---|---|
    | cap_lit_4 | `bounded[4]int` | ACCEPT/ok/ | ACCEPT/ok/ |
    | cap_lit_1 | `bounded[1]int` | ACCEPT/ok/ | ACCEPT/ok/ |
    | cap_lit_0 | `bounded[0]int` | REJECT `a bounded capacity must be positive` | REJECT (same text) |
    | cap_neg | `bounded[-1]int` | REJECT `bounded needs a capacity` | REJECT `bounded needs an integer capacity` |
    | cap_float | `bounded[2.5]int` | REJECT | REJECT |
    | cap_empty | `bounded[]int` | REJECT | REJECT |
    | cap_hex | `bounded[0x10]int` | REJECT `must be positive` | REJECT (same) |
    | cap_space | `bounded[ 4 ]int` | ACCEPT/ok/ | ACCEPT/ok/ |
    | cap_1e6 | `bounded[1000000]int` | ACCEPT/ok/ | ACCEPT/ok/ |
    | cap_i64max | `bounded[9223372036854775807]int` | ACCEPT/CCFAIL | ACCEPT/CCFAIL (both: `size of array 'v' exceeds maximum object size`) |
    | cap_unknown_ident | `bounded[NOPE]int` | REJECT | REJECT |
    | cap_const_neg | `const CAP = -1` | REJECT | REJECT |
    | cap_const_str | `const CAP = "x"` | REJECT | REJECT |
    | cap_sizeparam | `bounded[$N]int` | REJECT `expected a parameter name` | REJECT (same) |
    | **cap_const** | `const CAP = 4` → `bounded[CAP]int` | **ACCEPT/ok/** | **REJECT** ⇐ **DIVERGENCE** |
    | **use_const_cap** | same, value really used | **ACCEPT/ok/`n 3`** | **REJECT** ⇐ same cause |
    | el_int/float/string | `bounded[4]{int,float,string}` | ACCEPT/ok/ | ACCEPT/ok/ |
    | el_i32/u8/f32/ptr | fixed-width + `ptr` | ACCEPT/ok/ | ACCEPT/ok/ |
    | el_enum | `bounded[4]Color` | ACCEPT/ok/ | ACCEPT/ok/ |
    | el_dynarr | `bounded[4][int]` | ACCEPT/ok/ | ACCEPT/ok/ |
    | el_fn | `bounded[4]fn(int)->int` | ACCEPT/ok/ | ACCEPT/ok/ |
    | el_bool | `bounded[4]bool` | REJECT `bounded elements must be int, float, string, a struct, or an array` | REJECT (same text) |
    | el_void / el_char | `bounded[4]void`, `…char` | REJECT (no such type spelling) | REJECT (same) |
    | el_bytes | `bounded[4]bytes` | ACCEPT/ok/ | ACCEPT/**CCFAIL** |
    | el_fixarr | `bounded[4][2]int` | ACCEPT/ok/ | ACCEPT/**CCFAIL** |
    | el_map | `bounded[4][string:int]` | ACCEPT/ok/ | ACCEPT/**CCFAIL** |
    | el_bounded | `bounded[4]bounded[2]int` | ACCEPT/ok/ | ACCEPT/**CCFAIL** |
    | el_struct | `bounded[4]Pt` | ACCEPT/**CCFAIL** | ACCEPT/**CCFAIL** |
    | el_tuple | `bounded[4](int,int)` | ACCEPT/**CCFAIL** | ACCEPT/**CCFAIL** |
    | el_soa | `bounded[4]soa[Pt]` | ACCEPT/**CCFAIL** | ACCEPT/**CCFAIL** |
    | el_option / el_result | `bounded[4]Option(int)`, `Result(int,string)` | ACCEPT/**CCFAIL** | ACCEPT/ok/ |
    | **el_chan** | `bounded[4]Channel(int)` | **REJECT** `a channel handle cannot be stored in a container` | **ACCEPT** (then CCFAIL) ⇐ **DIVERGENCE (tychoc0 fail-open)** |
    | pos_param / pos_local / pos_field / pos_return | the four core positions | ACCEPT/ok/(`1`,`5`,`2`) | ACCEPT/ok/(same) |
    | pos_arr_elem / pos_map_value / pos_tuple / pos_fnty | `[bounded[4]int]`, `[string:bounded[4]int]`, … | ACCEPT/ok/ | ACCEPT/ok/ |
    | pos_map_key | `[bounded[4]int:string]` | ACCEPT/ok/ | ACCEPT/**CCFAIL** |
    | pos_fixarr_of / pos_bounded_of | `[2]bounded[4]int`, `bounded[2]bounded[4]int` | ACCEPT/ok/ | ACCEPT/**CCFAIL** |
    | pos_option_paren / pos_chan_paren | `Option(bounded[4]int)`, `Channel(bounded[4]int)` | ACCEPT/ok/ | ACCEPT/ok/ |
    | pos_global | `g: bounded[4]int` at top level | REJECT `expected 'fn'` | REJECT `expected a keyword` |
    | use_overflow | 3rd `push` into `bounded[2]` | ACCEPT/ok/**rc=1** `tycho: push to a full bounded[2]` | identical |
    | use_litover | `bounded[2]int = [1,2,3]` | REJECT `a bounded[2] holds at most 2 elements, got 3` | REJECT (same text) |
    | pop / slice / reserve | on a `bounded` value | REJECT (3 distinct messages) | REJECT |
    | `for v in a` | iteration | ACCEPT | ACCEPT |
    | bounded_as_var | `bounded := 3` | ACCEPT/ok/`3` | ACCEPT/ok/`3` — **`bounded` is not a reserved word** |
    | bounded_bare | `a: bounded` (no `[`) | REJECT `expected '[' after bounded` | REJECT |

  - Incidental finding that kept the spec honest: **`0x10` is not a valid integer
    literal anywhere in Tycho** (`x := 0x10` is rejected by both), so the
    `cap_hex` rejection is not a `bounded`-specific rule and is NOT written up as
    one.
  - **New production** (identical text added to `docs/spec/appendix-a-grammar.md`
    and `docs/spec/02-grammar.md`, immediately after the `soa` alternative; the
    `spec-check` "Appendix A grammar matches §3/§4" assertion confirms the two
    copies stayed in sync):

    ```ebnf
                | "bounded" "[" INT "]" Type                       /* bounded[N]T inline fixed-capacity */
    ```

    The capacity is spelled `INT` — **not** `INT | IDENT` — because the
    const-name form is accepted by only one of the two compilers (see Phase 18).
    The grammar therefore admits exactly what both accept.
  - **Sections added:**
    - `docs/spec/02-grammar.md` — the production plus a §4.2 note bullet: capacity
      MUST be a positive literal, the `const` form is not portable, and `bounded`
      is *contextual* (a type constructor only before `[`).
    - `docs/spec/03-types.md` — new **§5.3.10 `bounded[N]T`**, appended after
      §5.3.9 rather than inserted next to the array sections: §5.3.1/2/4/5/6/7/8/9
      anchors are cross-referenced from `12-aggregates.md`, `11-functions.md`,
      `05-generics.md`, `16-builtins.md` and `appendix-e-conformance.md`, so
      renumbering would break live links. Carries a `> Provenance:` line in the
      §5.3.5 house style, every citation of which was opened and verified:
      `src/tychoc.c:1727-1743` (capacity `:1731-1738`, element restriction
      `:1741-1742`), `compiler/tychoc0.ty:1718-1731`, slice `:4754-4755`, `pop`
      `:5396-5397`, `reserve` `:5418`, over-long literal `:5759-5761`, push trap
      `:10593-10595`.
    - A second `> Note:` states plainly that aggregate element types parse but are
      not reliably code-generated, and names the portable subset. Written that way
      because the compilers' own diagnostic promises "a struct" while
      `bounded[4]Pt` fails to compile on **both** — the spec must not repeat a
      promise the implementations do not keep.
    - `docs/spec/appendix-e-conformance.md` — yes, a row was warranted: the
      appendix already carries §5.3.2, §5.3.5 and §5.3.9 rows in the same §5
      table, so the form fits the existing structure. Added
      `| §5.3.10 | bounded[N]T … | tests/bounded, reject/fixarr_into_bounded_arg |`.
      `spec-check`'s "all Appendix E fixture citations resolve" confirms both
      fixtures exist.
  - **Gates, each its own foreground command, `env -u LD_PRELOAD`:**
    - `make test` → `passed: 447   failed: 0` / `all green`
    - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`
    - `make conc` → `conc: passed 37   failed 0`
    - `make fixpoint` → `ok B == C : tychoc0 reproduces itself byte-identically (34929 lines C)` / `fixpoint: all green`
    - `make ilp32` → `passed: 447   failed: 0` / `all green`
    - `make spec-check` → `spec-check: Appendix A grammar matches §3/§4 (ok)`,
      `spec-check: all Appendix E fixture citations resolve (ok)`,
      `spec-examples: 7 runnable example(s), all pass`
    - `make check-links` → `link check: ok (121 markdown files, no dead relative links)`
    - `git status --short` → only the 4 edited `docs/spec/` files; no build spill.

- [x] **Phase 12 — the integer-literal emitter produces two default-on C warnings (found by Phase 4, out of its scope)**
  - These are the only emitted-C warnings left on the **default-on** path after
    Phase 4 (4 of the post-fix 4). Both are the literal/arith emitter, not the
    map emitter Phase 4 was scoped to, so they were filed rather than absorbed.
  - **(a) i64 MIN is emitted as a negated out-of-range literal.**
    `tests/boundary_fold.ty` emits, three times,
    `tycho_int_to_str(&_t, -9223372036854775808LL)` (emitted C `:2435 :2437
    :2440`) → `warning: integer constant is so large that it is unsigned`.
    In C the `-` is a *unary operator on* `9223372036854775808LL`, which does not
    fit `long long`; the constant's type is decided before the negation. The
    printed value is right on two's-complement, so `make test` is green and this
    is not a live miscompile — but the emitter is relying on a construct the C
    standard does not give it. Conventional fix: emit `(-9223372036854775807LL - 1)`
    (or `INT64_MIN`) when the folded value is the minimum of its width.
  - **(b) a deliberate i32 wrap is constant-folded at translation time.**
    `tests/sized_family.ty` emits
    `(int )((((int )1000000LL) * ((int )1000000LL)))` (emitted C `:2438`) →
    `-Woverflow: integer overflow in expression of type 'int' results in
    '-727379968'`. The wrap is the *point* of the fixture (the `-fwrapv`
    contract), so the diagnostic is arguably a false positive — but it fires on
    every user build of such a program, unsuppressed. Decide explicitly: either
    emit the fold already reduced (no overflowing expression survives into C), or
    document it as accepted noise. Do not silence it with a cast.
  - Done when: `cc -O2 -fwrapv -std=c11` over the emitted C of the whole fixture
    suite reports **0** warnings (Phase 4 left it at 4), or each survivor carries
    a written justification; full gate set green.

  **EVIDENCE (2026-07-25). 4 → 1; the survivor is (b), accepted under decision
  (ii) with the reasoning below.**

  *Corrected citations.* The phase text's emitted-C line numbers still hold
  (`bf.c:2435 :2437 :2440`, `sf.c:2438`), but the compiler-side citations it did
  not give are:
  - tychoc: the literal emitter is `gen_expr`'s `case E_INT` / `case E_CHAR`,
    now `src/tychoc.c:8490-8498` (the `%lldLL` line was `:8466` before this
    phase). The only other `%lld…LL`-family emission is `:8118`
    (`((void)(%s), %lldLL)` over `fixarr_size`) — an array length, never
    `INT64_MIN`. Every remaining `%lld` in the file (`:1249 :1250 :4592 :4594
    :5749 :5761 :6728 :6729 :8479 :8520 :8922 :10060 :10063 :10644 :10681 :10685
    :10689 :10694 :10698 :10704 :11051`) is a type name, a diagnostic string, a
    tuple index, a bool 0/1, or an array size — none carries a user integer
    value, so `case E_INT`/`case E_CHAR` is the whole surface.
  - tychoc0: `gen_expr`'s `EInt`/`EChar` arms, `compiler/tychoc0.ty:6300-6307`
    (was `:6280-6285`, `return t + "LL"`).
  - The `-fwrapv` width contract this phase had to preserve: `src/tychoc.c:494-497`
    ("i32/i64 map to C int/long long and wrap natively (-fwrapv)") and
    `trunc_result` at `:8423-8426`.
  - `<stdint.h>` **is** already in both emitted preludes (`runtime/tycho_rt.c:50-51`;
    `compiler/tychoc0.ty:10060` emits `#include <stdint.h>\n#include <inttypes.h>`),
    so `INT64_MIN` was available — see the spelling choice below for why it was
    not used.

  **BOTH compilers had BOTH defects** — probed before patching, not assumed.
  Pre-fix, from `./tychoc0 tests/…` (tychoc0 built per the prompt into `/tmp/ph12`):
  `bf0.c:217 :219 :222` carry `i2s(&_t, -9223372036854775808LL)` and
  `sf0.c:220` carries `(int)((((int)(1000000LL)) * ((int)(1000000LL))))`. So unlike
  Phase 4 (tychoc-only) and Phase 16 (tychoc0-only), this one is genuinely paired.

  ### (a) the 2^63 minimum — FIXED in both compilers

  C has no negative integer constants. `-9223372036854775808LL` is unary `-`
  applied to the constant `9223372036854775808LL`, and **the constant's type is
  chosen before the negation** (C11 6.4.4.1p5). 2^63 has no `long long`, so the
  standard gives the token no type at all; GCC/clang accept it as an extension and
  emit the default-on `integer constant is so large that it is unsigned`. The
  printed value came out right on two's-complement, so `make test` never caught
  it — the emitter was leaning on a construct C does not grant it.

  Fix: a single new helper in each compiler, called from both the `E_INT`/`EInt`
  and `E_CHAR`/`EChar` arms.
  - `src/tychoc.c:8419-8459` — new `c_int_lit(int64_t v)`:
    `v == INT64_MIN` → `"(-9223372036854775807LL - 1)"`, else `"%lldLL"`.
    Call sites `:8497` (`E_INT`, signed path) and `:8498` (`E_CHAR`).
  - `compiler/tychoc0.ty:6278-6298` — new `fn c_int_lit(t: string) -> string`
    keyed on the decimal text `"-9223372036854775808"` (tychoc0's `EInt` carries
    the literal as a *string*, produced by `str()` in its folder at `:3134 :3144
    :3151…`). Call sites `:6303`, `:6307`.

  *Why `(-9223372036854775807LL - 1)` and not `INT64_MIN`,* even though
  `<stdint.h>` is present: (1) the surrounding invariant is an explicit **`LL`
  rank** guarantee (`src/tychoc.c:8421-8423`: a plain `L` is 32-bit under
  ILP32/LLP64, so `100000L * 100000L` truncates in the multiply) — `LL` states
  that in the emitted text instead of delegating it to a macro whose expansion is
  `long` on LP64; (2) the standard does not promise `INT64_MIN` is parenthesized,
  and this string is pasted into arbitrary operand positions, so the emitter
  supplies its own parens; (3) it is header-independent, which matters because the
  same emitter feeds the `--emit-c` path. It is the same shape `<stdint.h>` itself
  uses for the macro.

  *Whole-family coverage — the reason ONE test is enough.* Only the **64-bit**
  minimum can hit this, established by width rather than by fixture:
  | rep | MIN | magnitude negated | fits `long long`? |
  |---|---|---|---|
  | i8 | -128 | 128 | yes → already legal |
  | i16 | -32768 | 32768 | yes → already legal |
  | i32 | -2147483648 | 2147483648 | yes → already legal |
  | i64 / `int` | -9223372036854775808 | 9223372036854775808 | **NO** → fixed here |
  | u32 | n/a | takes the `%lldU` arm (`:8493`) — an unsigned suffix makes even a top-bit-set value a legal constant | n/a |
  | u64 | n/a | takes the `%lldULL` arm (`:8494`); e.g. 2^63 emits `-9223372036854775808ULL`, whose constant *has* a type (`unsigned long long`) and whose negation is defined modulo 2^64 → right value, no warning | n/a |
  | `char` rep | 0..255 | cannot be negative | n/a |
  | newtype over `int` | reaches the signed arm via `base_of` | same `int64_t` field | covered by the same test |
  The narrow signed reps and the newtype/char reps therefore need no separate
  case, and the empirical sweep below confirms it: zero
  `integer constant is so large` warnings remain anywhere in 231 emitted files.

  ### (b) the deliberate i32 wrap — **DECISION (ii): ACCEPTED NOISE, documented**

  `to_i32(1000000) * to_i32(1000000)` emits
  `(int )((((int )1000000LL) * ((int )1000000LL)))` and GCC reports
  `-Woverflow: integer overflow in expression of type 'int' results in
  '-727379968'`. Recorded decision: **(ii)**. Four reasons, in order of weight:

  1. **The premise behind (i) is false, and this had to be checked before
     choosing.** The prompt offered (i) as "the compiler has already computed the
     value — emitting `-727379968` directly is honest". **It has not.** tychoc's
     only constant folder is `const_fold` (`src/tychoc.c:3901-3940`), which runs
     at *parse* time, is reached only from `parse_const` (`:3953`) and a local
     `const` (`:2860`), and folds **`int` (64-bit) literals only** —
     `:3919` `if (a->kind != E_INT || b->kind != E_INT) return e;`. It has no
     notion of a sized type and never sees `to_i32(…)`. The `1000000` values here
     are not `const`s at all; they are ordinary expressions in a function body.
     So (i) is not "emit what we already know" — it is **building a new
     sized-integer constant evaluator**, in both compilers.
  2. **That evaluator is a miscompile risk out of all proportion to one
     warning.** It would have to reproduce `-fwrapv` wrapping for
     `+ - * / % << >> & | ^` across 8/16/32/64 bits, signed and unsigned, twice,
     byte-exactly — including the traps the existing folder already had to be
     told about (`INT64_MIN / -1`, negative and over-width shift counts:
     `:3928-3936`). A wrong fold is a **silent wrong value in every user
     program**; the warning is noise on one. Trading a cosmetic defect for a
     correctness surface is the wrong direction, and it is squarely the
     "don't build a subsystem for a 5-line problem" case.
  3. **The warning is currently a free cross-check of the `-fwrapv` contract, and
     (i) would destroy it.** GCC's text names the value: `results in
     '-727379968'` — which is exactly what `tests/sized_family.out` asserts and
     what `tests/sized_family.ty:16` documents (`# 10^12 wraps i32 ->
     -727379968`). An independent implementation confirming the fixture's
     expected value, on every build, is *evidence*, not noise. Folding it in the
     emitter would move that computation inside the thing being tested.
  4. **It is bounded and it is a true statement.** One warning, one fixture, and
     what it says is accurate: an `int` expression overflows. The program is
     nonetheless well-defined because `tychoc` compiles emitted C with `-fwrapv`
     (`src/tychoc.c`'s cc line, and `Makefile:11` for the compiler itself), per
     `docs/spec/03-types.md` §5.2.1. A reader who follows the warning finds a
     documented, intended wrap.

  **No cast was added** — the prompt's prohibition is honoured, and note that a
  cast would have been the *worst* option here: it would have suppressed
  `-Woverflow` for every genuinely-wrong constant expression the emitter produces
  later, which is exactly the class of bug this diagnostic exists to catch.
  The justification is written where a maintainer meets the construct, not only
  here: `src/tychoc.c:8419-8459` (`c_int_lit`'s header) and
  `compiler/tychoc0.ty:6278-6294`.

  **One adjacent difference probed and DISMISSED — no new phase filed.** tychoc
  has unsigned-suffix arms (`src/tychoc.c:8493-8494`, `%lldU` / `%lldULL`) that
  tychoc0 does **not**: tychoc0's `EInt` emits `…LL` for every literal regardless
  of type. That looked like a u32/u64 width divergence, so it was probed rather
  than filed on suspicion:
  - `x: u32 = 3000000000` / `x + 3000000000` — tychoc emits `3000000000U`,
    tychoc0 emits `3000000000LL`, and **both print `1705032704`**. tychoc0 is
    saved by its own result-truncating cast, which re-narrows before the value is
    observed.
  - `to_u64(1) << to_u64(63)` then `/ to_u64(3)` — both print
    `3074457345618258602`, tychoc0's emitted C compiles with 0 warnings.
  - A literal that would actually need `ULL` (`c: u64 = 18446744073709551615`) is
    **rejected by both**: tychoc `integer literal out of range`, tychoc0
    `lex: line 5: integer literal out of range`. A top-bit-set u64 is reachable
    only by computation, which never passes through the literal emitter.
  No divergence in emitted *value* was demonstrated, so per RULE 11 nothing is
  asserted about it and no phase is opened — the difference is in emitted text
  only, which the corrected fixpoint premise (Phases preamble) already permits.

  **Pre/post default-on warning counts, whole suite.** Method mirrors Phase 4's
  STEP 3: emit C for `tests/*.ty examples/*.ty corelib/*/*.ty compiler/*.ty`,
  compile each with the flags `tychoc` actually uses for user programs —
  `cc -O2 -fwrapv -std=c11 -c`, **no `-Wall`/`-Wextra`**. Pre-fix binary built
  from `git show HEAD:src/tychoc.c` so the two runs differ only by this patch
  (script: `/tmp/ph12/sweep.sh`).

  ```
  PRE   seen=267 emitted=231 cc_errors=0
        default-on warnings: 4
              3 integer constant is so large that it is unsigned
              1 integer overflow in expression of type ‘int’ results in ‘-727379968’ [-Woverflow]

  POST  seen=267 emitted=231 cc_errors=0
        default-on warnings: 1
              1 integer overflow in expression of type ‘int’ results in ‘-727379968’ [-Woverflow]
  ```
  (267/231 vs Phase 4's 263/227: four fixtures were added by Phases 15-20.)

  **Emitted line, before and after.**

  (a) `tests/boundary_fold.ty`, tychoc, emitted `:2435` (identically at `:2437`,
  `:2440`):
  ```
  BEFORE  { Arena _t = arena_new(0); (tycho_print_s(tycho_int_to_str(&_t, -9223372036854775808LL)), tycho_print("\n")); arena_free(&_t); }
  AFTER   { Arena _t = arena_new(0); (tycho_print_s(tycho_int_to_str(&_t, (-9223372036854775807LL - 1))), tycho_print("\n")); arena_free(&_t); }
  ```
  the same fixture through tychoc0, emitted `:217` (also `:219`, `:222`):
  ```
  BEFORE  { Arena _t = arena_new(0); hi_puts(sc(&_t, i2s(&_t, -9223372036854775808LL), …)); arena_free(&_t); }
  AFTER   { Arena _t = arena_new(0); hi_puts(sc(&_t, i2s(&_t, (-9223372036854775807LL - 1)), …)); arena_free(&_t); }
  ```
  (b) `tests/sized_family.ty`, emitted `:2438` — **unchanged by decision**:
  ```
  BEFORE  … tycho_int_to_str(&_t, (int )((((int )1000000LL) * ((int )1000000LL)))) …
  AFTER   … tycho_int_to_str(&_t, (int )((((int )1000000LL) * ((int )1000000LL)))) …
  ```

  **Runtime output unchanged — demonstrated by running, and by a byte-diff of the
  whole emitted suite.**
  - Byte-diff of all 231 emitted `.c` files, pre vs post
    (`diff -rq /tmp/ph12/pre /tmp/ph12/post`): **exactly one file differs**,
    `tests_boundary_fold.c`, and its entire delta is the three lines above.
    Every other emitted file in the suite is byte-identical, so no other
    fixture's behaviour can have moved.
  - Both versions of that one file compiled and **run**:
    `diff out_old.txt out_new.txt` → identical;
    tychoc0's post-fix emission run too, `diff out_new.txt out_h0.txt` →
    identical; and `diff out_new.txt tests/boundary_fold.out` → **GOLDEN MATCH**
    (the `-9223372036854775808` lines print unchanged).
  - `make test` / `make ilp32` re-ran all 478 fixtures against their goldens on
    both data models — `failed: 0` on each.

  **Gates** (each its own foreground command, `env -u LD_PRELOAD make …`):
  - `make test` → `passed: 478   failed: 0` / `all green`
  - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`
  - `make conc` → `conc: passed 37   failed 0`
  - `make fixpoint` → `ok   B == C : tychoc0 reproduces itself byte-identically (35141 lines C)` / `fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)` — load-bearing here: the tychoc0 edit changed tychoc0's own emitted text, and B==C proves tychoc0 still reproduces itself through it.
  - `make ilp32` → `passed: 478   failed: 0` / `all green`
  - `make spec-check` → `spec-examples: 7 runnable example(s), all pass`
  - `make check-links` → `link check: ok (121 markdown files, no dead relative links)`
  - `git status --short` → `M compiler/tychoc0.ty` + `M src/tychoc.c` only; no build spill.

  **What is NOT closed.** The Done-when's first branch (0 warnings) is met for
  (a) and taken via its second branch ("or each survivor carries a written
  justification") for (b). The one survivor is `-Woverflow` on
  `tests/sized_family.ty`; if a future maintainer wants it gone, the only
  non-silencing route is a real sized-integer constant evaluator, and reason 3
  above is the argument against wanting that.

- [x] **Phase 13 — CLOSED, WILL NOT DO (user decision 2026-07-25): emitted C is not clean under opt-in `-Wall -Wextra`: 13346 warnings, ~89% unused-symbol (measured by Phase 4)**
  - **Closed without doing it, deliberately.** Phase 4 measured the residue and judged it
    out of scope; the user confirmed on 2026-07-25 that it stays closed. Reason: ~89% is
    `-Wunused-function`/`-Wunused-variable` inherent to emitting the whole runtime and
    the whole per-type family into every program. Silencing it means changing HOW
    emission works, not tidying warnings — a design change to remove cosmetic noise from
    a lane that is not even on by default (`src/tychoc.c`'s emitted cc line carries no
    `-Wall`; see Phase 4's amended done-when). The default-on surface IS clean: Phase 4
    took it 24 → 4 and Phase 12 took it 4 → 1, with the last one justified in writing.
  - Reopen only if emission is being redesigned for another reason and this comes free.
  - Phase 4's Done-when asked for zero warnings over the suite under `-Wall
    -Wextra` and **could not deliver it** — see its evidence block for the full
    table. This is that residue, filed whole rather than absorbed or silenced.
  - Measured 2026-07-25 over 227 emitted programs (`tests/`, `examples/`,
    `corelib/`, `compiler/`), `cc -O2 -fwrapv -Wall -Wextra -std=c11 -c`, 0 errors:
    `-Wunused-function` 8507, `-Wunused-variable` 3363,
    `-Wmisleading-indentation` 1286, `-Wunused-parameter` 149,
    `-Wmissing-field-initializers` 23, `-Wunused-but-set-variable` 13.
  - **Root cause is emission strategy, not a bug.** Every program pastes the
    entire `runtime/tycho_rt.c` prelude plus the entire per-type family
    (`tycho_arr_C12_pop`, `tycho_arr_C13_eq`, `_sing_Tok_3`, … one full set per
    array/map/enum type reached) and then calls a small fraction of it. The
    `-Wmisleading-indentation` block is a separate, purely cosmetic cause: the
    emitter writes multi-statement one-liners like
    `for (…) if (…) { … } w++;`.
  - Why it is not free: fixing it means demand-driven emission (emit a family
    member only when reached) or blanket `__attribute__((unused))` on the family
    emitters. The first is a real design change with a fixpoint risk; the second
    is a suppression that would hide a genuinely dead emitter forever. That
    choice needs a ruling before code.
  - Note: this is about **emitted** C only. `-Wall -Wextra` is *not* on the path
    `tychoc` uses to compile emitted C (`src/tychoc.c:11532` — no `-Wall`, no
    `-Wextra`), so none of these reach a user today; they appear only if someone
    compiles the emitted `.c` themselves with those flags.
  - Done when: a ruling is recorded on demand-driven-emission vs. attribute, then
    the chosen route lands with `make fixpoint` green (emission changes are
    exactly what fixpoint guards).

- [x] **Phase 14 — `tychoc`'s own build has 3 `-Wmissing-field-initializers` (observed by Phase 1, re-confirmed by Phase 4)**
  - `src/tychoc.c:6092`, `:6093`, `:6095` — `missing initializer for field
    'is_sink' of 'Param'`. Count was 3 before Phase 1 and is still 3 after
    Phase 4; nothing in this plan changed it.
  - **Explicitly ruled out of "emitted C is warning-clean" (Phase 4).** This is
    the *compiler's own* `-Wall -Wextra` build (`Makefile:11`), not emitted
    output. Different surface, different fix, so it gets its own phase rather
    than riding along on a codegen change.
  - Done when: `make` compiles `src/tychoc.c` with zero warnings; full gate set
    green.

  **CORRECTED CITATIONS (2026-07-25).** The prompt's `:6092/:6093/:6095` had
  drifted 20 lines across 17 phases. The real sites, from the compiler's own
  output, were `src/tychoc.c:6112`, `:6113`, `:6115`, all inside
  `resolve_parfor`. `Param` is declared at `src/tychoc.c:1394`:
  ```c
  typedef struct { char *name; Type type; int is_inout; int is_sink; int is_variadic; const char *ffi_ct; } Param;
  ```
  Six fields; the three initializers supplied three (`{ "__plo", T_INT, 0 }`),
  so `is_sink`, `is_variadic` and `ffi_ct` were all left to implicit zero. GCC
  names only the first missing field, which is why the warning reads `is_sink`.

  **DIAGNOSIS: cosmetic — zero is the REQUIRED value, not an accident. Not a
  latent bug.** The trace, site by site:
  - `:6112` / `:6113` — `__plo` / `__phi` are the synthesized chunk bounds,
    `T_INT`. `sink` is an ownership annotation on a heap-bearing value; on an
    `int` it has no meaning, and `int` is mutable regardless
    (`mutable = (!is_array && !is_map && !IS_SOA) || is_inout || is_sink`,
    `:7106`). Both are 0 by construction.
  - `:6115` — the captures. This is the one that could have been a latent bug,
    so it was traced to its consumers. The lifted proc `pr` is pushed to
    `g_parprocs` (`:6186`) and emitted by `gen_proc` (`:11205`), which does
    `g_param_sink[i] = pr->params[i].is_sink` (`:9978`). `g_param_sink` is read
    only by `is_sink_param` (`:7271`), whose single caller is `:7858`:
    ```c
    if (arg->kind == E_IDENT && (!is_param(arg->sval) || is_sink_param(arg->sval)))
    ```
    i.e. `is_sink = 1` is what PERMITS moving out of (consuming) a parameter's
    buffer. Every chunk task of a `parallel for` is handed the SAME capture
    values — they are borrows of the enclosing scope, shared across `tycho_ncpu()`
    chunks — so consuming one in any chunk would hand off a buffer the other
    chunks still read. `is_sink = 0` is therefore mandatory, and the omission was
    correct. Confirmed against the twin path: the lambda-lift capture builder at
    `:4533` sets `caps[ncap].is_sink = 0` **explicitly** for exactly the same
    reason. Also note `resolve_parfor`'s own body-scope push at `:6172` computes
    mutability as `!is_array(pt) && !is_map(pt) && !IS_SOA(pt)` and never reads
    `is_sink` at all, and the parfor `Sig` is `memset` to 0 at `:6165`.
    Non-zero at any of the three sites would have been the bug; zero is right.
    No behaviour probe is possible because there is no observable difference —
    the fix is byte-identical in behaviour, which the gates confirm.
  - Fix: spell all six fields at each site and add the reasoning as a comment, so
    a future `Param` field re-raises the warning here and forces a decision
    rather than silently defaulting. No pragma, no `-Wno-`.

  **TOTAL warning count for the compiler's own build (not just this class).**
  The prompt asked whether "3" was the whole-file total or one class. VERIFIED:
  it is the whole-file total — `grep -c 'warning:'` over the complete
  `-Wall -Wextra` compile is 3, and all 3 lines carry
  `[-Wmissing-field-initializers]`. There are no survivors needing justification.
  ```
  $ cc -O2 -fwrapv -Wall -Wextra -std=c11 -Ibuild -c src/tychoc.c -o /tmp/ph14/tychoc_before.o
  BEFORE  exit=0  warnings=3   (all 3 -Wmissing-field-initializers)
    src/tychoc.c:6112:5: warning: missing initializer for field ‘is_sink’ of ‘Param’
     6112 |     pr->params[0] = (Param){ "__plo", T_INT, 0 };
    src/tychoc.c:6113:5: warning: missing initializer for field ‘is_sink’ of ‘Param’
     6113 |     pr->params[1] = (Param){ "__phi", T_INT, 0 };
    src/tychoc.c:6115:9: warning: missing initializer for field ‘is_sink’ of ‘Param’
     6115 |         pr->params[2 + i] = (Param){ pf->caps[i]->sval, pf->caps[i]->type, 0 };
    src/tychoc.c:1394:59: note: ‘is_sink’ declared here

  AFTER   exit=0  warnings=0   (empty stderr)
  $ env -u LD_PRELOAD make
  cc -O2 -fwrapv -Wall -Wextra -std=c11 -Ibuild src/tychoc.c -o tychoc
  (no diagnostics)
  ```
  **3 → 0.**

  **`compiler/tychoc0.ty` has no analogous gap — checked, and it is structurally
  immune.** tychoc0 has no `Param` record at all: sink-ness is a `~` prefix on
  the parameter's *type string* (`compiler/tychoc0.ty:3743`,
  `fn is_sink(ty: string) -> bool: return len(ty) > 0 and ty[0] == 126`), so a
  parameter's sink flag travels in the same value as its type and cannot be
  "omitted" from an initializer. Its parfor twin builds the chunk proc's params
  as two parallel `[]string`s (`:13418-13428`, `push(cps,"__plo")` /
  `push(cpt,"int")` then the captures), and the capture types are filled at
  `:13398` with `push(capt, base_ty(var_type(names, types, raw[j])))` — `base_ty`
  strips the `~`, so a capture of an enclosing `sink` parameter is de-sinked
  exactly as tychoc's `is_sink = 0` does. The two compilers agree on the
  semantics by different mechanisms. Nothing to fix.

  **Gates — all seven green, one per command, foreground, `env -u LD_PRELOAD`:**
  ```
  make test         passed: 478   failed: 0    / all green
  make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
  make conc         conc: passed 37   failed 0
  make fixpoint     ok  B == C : tychoc0 reproduces itself byte-identically (35141 lines C)
                    fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
  make ilp32        passed: 478   failed: 0    / all green
  make spec-check   spec-examples: 7 runnable example(s), all pass
  make check-links  link check: ok (121 markdown files, no dead relative links)
  git status --short   M src/tychoc.c        (no build spill)
  ```
  `make fixpoint` staying green is the meaningful signal here: the change is in
  the resolver's synthesized-`Param` construction, and tychoc0 still reproduces
  itself byte-identically, so nothing reached emitted text.

- [x] **Phase 15 — tychoc0 does not check that an `if` condition is a bool (found by Phase 7, NOT fixed there)**
  - *(Renumbered from 14 → 15 by the main agent: Phase 4 had already filed a
    different Phase 14 for `tychoc`'s own `-Wmissing-field-initializers`. Two
    phases independently claimed the same number; this is the later one.)*
  - Found by Phase 7's `void`-as-a-value sweep. `if nop():` escapes tychoc0's
    front end with no diagnostic; tychoc rejects it cleanly with
    `if condition must be bool`.
  - **It is NOT a void defect, which is why Phase 7 did not absorb it.** Probed
    on 2026-07-25 with two more conditions:
    ```
    fn main():          fn main():
        if 1:               if "s":
            println("y")        println("y")
    ```
    tychoc0 accepts BOTH (`tychoc0 <f> --emit-c`, exit 0). So the missing check
    is general — any non-bool condition passes — and Phase 7's `ty_has_void`
    predicate is the wrong tool for it. Root cause is a different one: tychoc0
    has no condition typecheck at all, not a hole in one.
  - Class: front-end fail-open, DECISION divergence between the compilers (the
    same family as Phase 9). Emitted C for `if 1:` is legal C and *runs*, with C's
    truthiness rather than Tycho's — so unlike Phase 7 this one is not merely an
    ugly error message: tychoc0 silently accepts a program tychoc rejects.
  - Scope when taken: the `SIf` / `SWhile` (and `elif`) condition paths in
    `compiler/tychoc0.ty` only — tychoc is already right, so this is a one-sided
    fix and `make fixpoint` is not at risk (tychoc0's own diagnostics are not
    emitted C). Check `for <cond>:` and the value-`if` condition too.
  - Watch out: tychoc0.ty itself must keep compiling — confirm no condition in
    `compiler/`, `corelib/`, `tests/` or `examples/` relies on the fail-open
    before tightening, or the self-host breaks. FAIL CLOSED only after that sweep.
  - Done when: `if 1:` / `if "s":` / `if nop():` are rejected by tychoc0 with a
    located diagnostic; a `tests/reject/` fixture locks it (one file per distinct
    rejection); legal bool conditions still compile and run on both; full gate
    set green.
  - **DONE 2026-07-25.**

    **Spec citation — tychoc is the correct side, quoted verbatim:**
    - `docs/spec/10-statements.md:26` (§14.2 `if`/`elif`/`else`): "Each
      condition MUST be `bool`." The MUST covers `elif` explicitly ("Each"),
      and `elif` is stated on `:28` to be "exactly sugar for an `else` block
      containing a single nested `if`".
    - `docs/spec/10-statements.md:52` (§14.4 Loops): "**Condition (`while`)
      form** — `for C:` runs its body while the `bool` condition `C` holds."
    - `docs/spec/09-expressions.md:48`: "**Logical** (`and`, `or`, `not`)
      operate on `bool` and yield `bool`."
    - Nothing in either file defines truthiness for any non-`bool` type.
      tychoc's stricter behaviour is what the spec mandates; tychoc0's
      acceptance was the divergence. **Verified the correct side BEFORE fixing.**

    **Corrected line citations (all re-verified after the edit):**
    | what | where |
    |---|---|
    | tychoc `if` check | `src/tychoc.c:6464` `die_at(s->line, "if condition must be bool")` |
    | tychoc `for` check | `src/tychoc.c:6562-6563` `die_at(s->line, "for condition must be bool")` |
    | tychoc `not` check | `src/tychoc.c:5571-5572` |
    | tychoc `and`/`or` check | `src/tychoc.c:5589-5591` |
    | tychoc0 NEW `if` check | `compiler/tychoc0.ty:8781` (in `gen_stmt`'s `SIf`) |
    | tychoc0 NEW `for` check | `compiler/tychoc0.ty:8795` (in `gen_stmt`'s `SWhile`) |
    | tychoc0 pre-existing `not` check | `compiler/tychoc0.ty:6316` |
    | tychoc0 pre-existing `and`/`or` check | `compiler/tychoc0.ty:6404` |
    | `elif` → nested `SIf` at parse | `compiler/tychoc0.ty:1185` |
    | value-`if` re-enters `gen_stmt` | `compiler/tychoc0.ty:8519` |

    **Every conditional position — enumerated from the `enum Stmt` declaration
    (`compiler/tychoc0.ty:478-502`), which has exactly two condition-bearing
    variants (`SIf` :485, `SWhile` :486), cross-checked against the only two
    call sites of `cond_unwrap` (`:8331` def; `:8786`, `:8804` uses).**
    Probes run as `tychoc <f>` vs `tychoc0 <f> --emit-c`:

    | # | position | probe | tychoc0 BEFORE | tychoc0 AFTER | tychoc (oracle) |
    |---|---|---|---|---|---|
    | 1 | `if` int | `if 1:` | exit 0, ran on C truthiness | `line 2: if condition must be bool` | `p1.ty:2: error: if condition must be bool` |
    | 2 | `if` str | `if "s":` | exit 0 | `line 2: if condition must be bool` | `p2.ty:2: error: if condition must be bool` |
    | 3 | `if` void call | `if nop():` | exit 0 | `line 5: if condition must be bool` | `p3.ty:5: error: if condition must be bool` |
    | 4 | `for` while-form | `for 1:` | exit 0 | `line 2: for condition must be bool` | `p4.ty:2: error: for condition must be bool` |
    | 5 | `elif` | `elif "s":` | exit 0 | `line 5: if condition must be bool` | `p5.ty:5: error: if condition must be bool` |
    | 6 | value-`if` (SValDecl) | `v := if 1:` | exit 0 | `line 2: if condition must be bool` | `p6.ty:2: error: if condition must be bool` |
    | 7 | `not` operand | `if not 1:` | **already rejected** | unchanged | already rejected |
    | 8 | `and` operand | `if b and 1:` | **already rejected** | unchanged | already rejected |
    | 9 | `or` operand | `if 1 or b:` | **already rejected** | unchanged | already rejected |

    **6 of 9 conditional positions were unchecked; all 6 fixed. 3 (`and`/`or`/
    `not`) already had checks (`:6316`, `:6404`) — the plan's "no condition
    typecheck at all" was true of statement conditions only, not of the logical
    operators. Those three were left alone (their wording differs from tychoc's
    and belongs to the Phase 2/3 divergence set, not to a new entry).**
    Positions 5 and 6 need no separate code: `elif` is parsed into a nested
    `SIf` (`:1185`) and `SValDecl` calls `gen_stmt` on its rewritten control
    statement (`:8519`), so both land on the single `SIf` guard. Verified by
    probe, not by assumption. Line numbers and wording match tychoc exactly, so
    this adds **zero** new entries to the Phase 2 divergence set.

    **Legal-forms control — every legal condition still compiles AND runs, with
    byte-identical output from both compilers** (`/tmp/ph15/legal.ty`, built
    `tychoc legal.ty -o legal.tychoc` and `tychoc0 legal.ty --emit-c | cc`;
    `diff <(./legal.tychoc) <(./legal.tychoc0)` → empty, "IDENTICAL"):
    ```
    bool var            <- if b:
    comparison          <- if x < 5:
    and/or/not          <- if x == 3 and not b or positive(x):
    bool call           <- if positive(x):
    in test             <- if "a" in m:
    array elem via local<- ae := arr[0]; if ae:
    struct field        <- if p.flag:
    bool literal        <- if true:
    string compare      <- if s == "hi":
    not paren           <- if not (x > 9):
    while ran 3         <- for i < 3:
    while call 3        <- for positive(3 - j):
    value-if yes        <- v := if b: "yes" else: "no"
    elif taken          <- elif x > 1:
    ```
    No over-tightening: 14 legal forms, 14 identical lines on both sides.

    **Did tychoc0.ty's own source trip the new check? NO.** This was the single
    biggest risk (16423 lines, the largest real-world condition corpus in the
    repo). Verified directly, not inferred:
    `./tychoc compiler/tychoc0.ty -o /tmp/ph15/tychoc0` → `built`, then
    `/tmp/ph15/tychoc0 compiler/tychoc0.ty --emit-c` → `rc=0`, 34902 lines of C,
    empty stderr. So the new compiler compiles ITSELF clean: every condition in
    tychoc0.ty was already a genuine `bool`. `make fixpoint` then confirms the
    self-reproduction is byte-identical. Neither a latent bug nor an
    over-tightening — the check found nothing to complain about in the compiler's
    own source, which is the strongest available evidence it is not over-tight.

    **Gate set — each run as its own foreground `env -u LD_PRELOAD make <t>`:**
    ```
    make test        passed: 446   failed: 0 / all green      (440 + 6 new fixtures)
    make corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc        conc: passed 36   failed 0
    make fixpoint    ok   B == C : tychoc0 reproduces itself byte-identically (34902 lines C)
                     fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32       passed: 446   failed: 0 / all green
    make spec-check  spec-examples: 7 runnable example(s), all pass
    make check-links link check: ok (121 markdown files, no dead relative links)
    ```
    `git status --short`: only the intended files, no build spill.

    **Fixtures (one per distinct rejection — the compiler halts at the first
    error, so one file cannot lock two):** `tests/reject/cond_if_int.ty`,
    `cond_if_str.ty`, `cond_if_void.ty`, `cond_elif_str.ty`, `cond_for_int.ty`,
    `cond_value_if_int.ty`. The reject lane is differential
    (`tests/run.sh:142-163`), so each asserts BOTH compilers reject.

- [x] **Phase 16 — `if <bool array element>:` makes tychoc0 emit invalid C (found by Phase 15, out of its scope)**
  - **PRE-EXISTING, NOT introduced by Phase 15** — reproduced on the *baseline*
    tychoc0 built from `HEAD` before the Phase 15 edit, byte-for-byte the same
    broken line. Phase 15 only found it, while building its legal-forms control.
  - Repro (`/tmp/ph15/bug16.ty`):
    ```
    fn main():
        arr := [true, false]
        if arr[0]:
            println("hit")
    ```
    tychoc: `built` → runs → prints `hit`. tychoc0: exits 0 and emits
    ```
    if ({ Arr_bool  _a = h_arr; _a.data[hi_bchk(0LL, _a.len)]; }) {
    ```
    which `cc` rejects: `error: expected expression before '{' token`.
  - Root cause, read not guessed: `gen_expr` lowers a checked array read to a
    GCC statement-expression `({ … })`, and `cond_unwrap`
    (`compiler/tychoc0.ty:8331`, applied at `:8786` and `:8804`) strips "only a
    single fully-parenthesised group (first byte `(`)" — which here removes the
    `(` that makes `({ … })` a statement-expression, leaving a bare `{ … }`.
  - Class: **fail-open into broken output**, not a decision divergence — tychoc0
    accepts the program (exit 0) and hands the user C that will not compile.
    Different failure mode from Phase 15, hence a separate phase.
  - Scope when taken: `cond_unwrap` must not strip when the group is a
    statement-expression (i.e. when the byte after `(` is `{`). Check the same
    hazard for any other `cond_unwrap`-style paren stripping, and add a
    `tests/` fixture that COMPILES AND RUNS `if arr[0]:` on both compilers
    (a `tests/reject/` fixture is wrong here — the program is legal).
  - Not attempted in Phase 15: out of scope (Phase 15 is condition
    *typechecking*), and it is an emitter defect, not a front-end one.
  - **DONE 2026-07-25.**

    **Citation corrections (this phase's own text was wrong once, the prior
    phases' pattern held).** `cond_unwrap` is at `compiler/tychoc0.ty:8331`
    (correct as written) but its second call site is **`:8809`**, not `:8804`
    as the phase text claimed. Post-edit the anchors are: tychoc0
    `cond_unwrap` `:8353`, call sites `:8810` and `:8833`; tychoc
    `cond_unwrap` `src/tychoc.c:8007` (was `:7986`), call sites `:9555` and
    `:9708`. (Four of these five I first wrote from the pre-edit offsets and
    had to correct against `grep` before committing — the same failure mode
    this plan has now hit in five phases. Every anchor below is grep-verified
    post-edit.) Exactly **two** call sites per compiler, both confirmed by
    reading the switch: `case S_IF` and `case S_WHILE`.

    **The value-`if` form DOES reach `cond_unwrap` — I asserted otherwise and
    was wrong.** My first draft of this evidence claimed "there is no value-`if`
    route, ternary is a permanent non-goal". The non-goal is real but the
    inference from it was false: Tycho has a block-form value-`if`
    (`x := if c:` / `else:`, ROADMAP 2.1, `SValDecl` at
    `compiler/tychoc0.ty:502`), and `:8802` states in the source that "`elif` is
    parsed into a nested SIf (`:1185`) and a value-`if` re-enters here via
    SValDecl (`:8519`), so this one site covers every `if`-shaped condition."
    So value-`if` is not a *third* call site, but it is a distinct *user-visible
    form* that was equally broken, and the phase explicitly asked for it.
    Verified by probe on the baseline binary:
    ```
    x := if arr[0]:      baseline tychoc0 -> if ({ Arr_bool  _a = h_arr; … }) {   BROKEN
        10               fixed    tychoc0 -> if (({ Arr_bool  _a = h_arr; … })) { ok, prints 10
    ```
    It is now covered in the fixture. Caught only because a truncated `grep`
    hid `:12220` and re-running it surfaced the `SValDecl` "value if/match
    decl" comment — the absence claim had not actually been searched for.

    **The phase premise "tychoc is unaffected" is FALSE — probed, not assumed.**
    tychoc survives the *array* repro only because it lowers a checked array
    read to a helper call (`tycho_arr_C0_get(h_arr, 0LL)`) instead of a
    statement-expression. But tychoc's `wait()` lowering **is** one, so
    `if wait(t):` over a bool-returning task broke tychoc identically on
    baseline:
    ```
    if ({ HTask *_tk = h_t; tycho_task_join(_tk); int _w = (*(int *)_tk->ret); arena_free(&_tk->root); _w; }) {
        ^  cc: error: expected expression before '{' token
    tychoc: C compilation failed (cc -O3 -fwrapv -pthread -o /tmp/ph16/pA …)
    ```
    So this was **two live instances of one root cause, one per compiler**, and
    both compilers are fixed. `src/tychoc.c` was in scope under the phase's own
    "or `src/tychoc.c` if it has an equivalent".

    **Input-shape enumeration — the required evidence.** The real
    `cond_unwrap` from each compiler was extracted verbatim and driven against
    every shape; `valid(in)`/`valid(out)` are `cc -fsyntax-only -std=gnu11`
    verdicts on `if (<expr>) {}`, not a reading of the code. Harness:
    `/tmp/ph16/shapes_t0.ty` (tychoc0, spliced verbatim) and
    `/tmp/ph16/shapes_c.py` (tychoc, extracted + linked).
    ```
    shape                       | valid(in) | BEFORE    | AFTER     | verdict
    ----------------------------|-----------|-----------|-----------|------------------------------------
    S1  plain-paren-binop       | yes       | STRIP/ok  | STRIP/ok  | ok, unchanged
    S2  stmt-expr               | yes       | STRIP/BAD | KEEP /ok  | WAS BROKEN -> FIXED
    S3  nested-parens           | yes       | STRIP/ok  | STRIP/ok  | ok, unchanged
    S4  call-not-paren-lead     | yes       | KEEP /ok  | KEEP /ok  | ok, unchanged
    S5  two-groups              | yes       | KEEP /ok  | KEEP /ok  | ok, unchanged
    S6  cast-like               | yes       | STRIP/ok  | STRIP/ok  | ok, unchanged
    S7  empty                   | no        | KEEP /BAD | KEEP /BAD | degenerate (invalid in, invalid out)
    S8  unbalanced-open         | no        | KEEP /BAD | KEEP /BAD | degenerate (invalid in, invalid out)
    S9  paren-in-string-lit     | yes       | STRIP/ok  | STRIP/ok  | ok, unchanged
    S10 stmt-expr-inside-group  | yes       | STRIP/ok  | STRIP/ok  | ok, unchanged
    S11 stmt-expr-operand       | yes       | STRIP/ok  | STRIP/ok  | ok, unchanged
    S12 unterminated-literal    | no        | KEEP /BAD | KEEP /BAD | degenerate (invalid in, invalid out)
    S13 stmt-expr-wait          | yes       | STRIP/BAD | KEEP /ok  | WAS BROKEN -> FIXED
    S14 bare-ident              | yes       | KEEP /ok  | KEEP /ok  | ok, unchanged
    S15 lone-open-paren         | no        | KEEP /BAD | KEEP /BAD | degenerate (invalid in, invalid out)
    S16 empty-group `()`        | no        | STRIP/BAD | KEEP /BAD | degenerate; now refused anyway
    S17 group-then-trailer      | yes       | KEEP /ok  | KEEP /ok  | ok, unchanged
    S18 double-stmt-expr        | yes       | STRIP/ok  | STRIP/ok  | ok, unchanged
    ----------------------------|-----------|-----------|-----------|------------------------------------
    valid input turned INVALID  --  BEFORE: 2    AFTER: 0
    ```
    A real example per shape, in order: `(h_a < 3LL)` ·
    `({ Arr_bool _a = h_arr; _a.data[hi_bchk(0LL, _a.len)]; })` ·
    `((h_a && h_b))` · `h_f(h_x)` · `(h_a) && (h_b)` · `((tycho_int)h_x)` ·
    `` (empty) · `(h_a` · `(hi_streq(h_s, ")") == 1LL)` ·
    `(({ Arr_int _a = h_v; _a.data[0]; }) == 1LL)` ·
    `(hi_streq(({ static char* _l = 0; … _l; }), h_s))` ·
    `(hi_streq(h_s, "abc)` ·
    `({ HTask* _tk = h_t; tycho_task_join(_tk); tycho_int _w = 1; _w; })` ·
    `h_flag` · `(` · `()` · `(h_a) == 1LL` ·
    `(({ int _x = 1; _x; }) && ({ int _y = 2; _y; }))`.

    Readings that matter:
    - **S5 `(a) && (b)` is NOT mis-stripped** — the classic off-by-one for this
      kind of scanner is genuinely absent. The `i != n - 1` / `p[1] != '\0'`
      test sees the `)` at index 2 close before the end and refuses. Checked
      because the phase asked; it is a correct KEEP, not a second instance.
    - **S10/S11/S18 still STRIP and that is right** — a statement-expression
      that is not the *whole* group keeps its own parens when the redundant
      outer layer comes off, so the result is valid C.
    - **S7/S8/S12/S15 were already invalid C on the way in** — `cond_unwrap`
      returns them unchanged. It never *makes* them worse. The load-bearing
      property is the bottom line: valid input turned INVALID went 2 → 0.
    - **S16 `()` was a third mis-strip**, producing an empty condition
      (`if () {`). Not reachable from `gen_expr` today, so it never fired in
      the wild; refused anyway under fail-closed.

    **The fix — fail-closed, both compilers.** Two guards ahead of the scan:
    refuse anything shorter than 3 bytes (covers ``, `(`, `()`), and refuse
    when the byte after `(` is `{`, because `({ … })` is a GCC
    statement-expression whose `(` is *syntax*, not a redundant layer.
    ```
    tychoc0 (compiler/tychoc0.ty:8355-8358)
        if n < 3 or s[0] != 40:      # 40 = '(' ; n < 3 covers "", "(", "()"
            return s
        if s[1] == 123:              # 123 = '{' : `({ ... })` is a GCC
            return s                 # statement-expression, not a paren layer

    tychoc (src/tychoc.c:8009-8010)
        if (!s || s[0] != '(' || !s[1] || !s[2]) return s;
        if (s[1] == '{') return s;   /* `({ ... })` statement-expression */
    ```
    `s[1] == '{'` is exact, not approximate: **no emitter in either compiler
    produces `( {`** — `grep -n '"( ' compiler/tychoc0.ty src/tychoc.c` returns
    nothing, and every statement-expression emission site spells it `({ `
    (`grep -c '"({ '` = 21 in tychoc0, `grep -c '({ '` = 35 in tychoc). The
    full enumeration is now a comment block above each function, per the
    parser/reconstructor rule.

    **Sweep for the same hazard elsewhere — nothing else found.**
    `cond_unwrap` is the **only** paren-stripping / string-surgery site on
    emitted C in either compiler. In `src/tychoc.c` the sole paren surgery is
    inside `cond_unwrap` itself (`:8009`, `:8022`, `:8023`). In
    `compiler/tychoc0.ty` every other paren/brace scan operates on **source or
    type names**, never on emitted C: `:278`, `:4027`, `:4269`, `:4397`,
    `:4417` are lexer paren-depth counters over *source*; `:4633` splits a
    generic *struct name* (`sname`); `:12220` (`fnty_split`) splits a *function
    type* string (`"fn(a,b->r)"`); `:13508` splits a *type* string (`ty`);
    `:13597`/`:13602` match *type patterns* (`pat`/`concrete`); `:9153` is an
    `Expr` array slice, not text. **No new phase filed** — nothing
    structurally different turned up.

    **Repro, before → after, both compilers.**
    ```
    BEFORE (tychoc0, baseline HEAD 46aa05f)
        if ({ Arr_bool  _a = h_arr; _a.data[hi_bchk(0LL, _a.len)]; })  {
        cc: error: expected expression before '{' token   (tychoc0 exit 0 = fail-open)
    AFTER  (tychoc0)
        if (({ Arr_bool  _a = h_arr; _a.data[hi_bchk(0LL, _a.len)]; }))  {
        cc exit=0
    ```
    ```
    $ ./tychoc     /tmp/ph16/bug16.ty -o b_tychoc && ./b_tychoc      ->  hit
    $ ./tychoc0    /tmp/ph16/bug16.ty | cc -o b_t0 - && ./b_t0       ->  hit
    $ ./tychoc     /tmp/ph16/probeA.ty -o pA && ./pA                 ->  A-hit   (if wait(t):)
    $ ./tychoc0    /tmp/ph16/probeA.ty | cc -o pA_t0 - && ./pA_t0    ->  A-hit
    ```
    Output identical on both compilers for both repros.

    **No emitted-text drift.** A baseline tychoc + tychoc0 were built from
    `HEAD` in a throwaway worktree and every pre-existing fixture emitted
    through old and new: **226 fixtures compared (`tests/*.ty` +
    `examples/*.ty`), tychoc differing 0, tychoc0 differing 0** — byte-identical,
    so no golden needed re-recording and the shape table's "unchanged" column
    is confirmed end-to-end rather than inferred.

    **Fixtures added** (compiling + running, NOT `tests/reject/` — the programs
    are legal):
    - `tests/cond_stmt_expr.ty` / `.out` — the tychoc0 side. Covers `if arr[0]:`,
      the while-form `for arr[0]:` (so **both** `cond_unwrap` call sites),
      nested `n[1][1]`, struct field `s.bits[1]`, map value `m["k"][0]`,
      statement-expression as only part of a condition (`arr[1] == false`,
      `n[0][0] and s.bits[1]`), the **value-`if`** form (`v := if arr[1]:` and
      `w := if n[1][1]:`), plus plain conditions that must still unwrap
      (`a < b`, `(a + 1) == b`, `t == "x"`) and `t == ")"` for the literal skip.
      Strength of the lock, measured: emitted through the **baseline** tychoc0
      this one fixture yields **7** malformed conditions and exactly **7**
      `error: expected expression before '{' token` from cc; through the fixed
      tychoc0, 0 — and the 8 statement-expression conditions it now emits are
      all correctly doubly-parenthesised (`if (({ … })) {`).
    - `tests/conc/cond_wait.ty` / `.out` — the tychoc side, `if wait(t):` over a
      bool-returning task, plus `wait(v) == true` and `wait(w) and not wait(x)`
      for the still-must-strip shapes.

    **Gates — all seven green, one per command, foreground.**
    ```
    make test         passed: 447   failed: 0   /  all green      (446 + cond_stmt_expr)
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 37   failed 0                  (36 + cond_wait)
    make fixpoint     ok  B == C : tychoc0 reproduces itself byte-identically (34907 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages;
                                           tychoc0 self-split dogfood)
    make ilp32        passed: 447   failed: 0   /  all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (121 markdown files, no dead relative links)
    ```
    `make fixpoint` is the load-bearing one here: `tychoc0.ty`'s own source is
    full of conditions, so an over- or under-strip in the emitter would show up
    as a self-hosting failure. `B == C` holds.
    `git status --short` shows only the four intended paths; no build spill.

- [x] **Phase 17 — CLOSED, WILL NOT DO (user decision 2026-07-25): `SMatch` carries no per-arm source locations in tychoc0 (found by Phase 8, deliberately NOT forced there)**
  - **Closed without doing it, deliberately.** Phase 8 ruled it preferable-but-
    disproportionate and declined to force it; the user confirmed closure on 2026-07-25.
    Reason: supplying per-arm locations means widening the `SMatch` variant across **43
    sites** in the self-hosting compiler, and the entire observable benefit is moving a
    diagnostic caret two lines (`match_dup_arm` 10 vs 8, `match_wildcard_not_last` 12 vs
    10). Both compilers already reject both programs with defensible messages. Phase 8
    also explicitly rejected the cheap `bodies[j][0]` proxy as wrong for block-form arms,
    so there is no shortcut.
  - Reopen only if `SMatch` is being widened for a functional reason anyway.
  - Phase 8 ruled that `match_dup_arm` should point at the duplicate arm (line 10
    of the fixture) and `match_wildcard_not_last` at the misplaced `_` arm (line
    12) — the lines the user must edit. tychoc already does
    (`src/tychoc.c:6477`, `:6540`, both `s->arms[i].line`). tychoc0 cannot: it
    reports the `match` head for both (8 and 10), and the information to do
    better does not exist in its AST.
  - Root cause, verified: `check_match` (`compiler/tychoc0.ty:10946`, one caller
    at `:11207`) is handed arm *names* as `[string]` plus a single construct-level
    line. That is all its caller has, because the variant itself is declared
    `SMatch(Expr, [string], [[string]], [[Stmt]], int)` (`:493`) — one loc for the
    whole `match`, none per arm.
  - Why it was not done in Phase 8: `grep -n "SMatch("` reports **43 sites** in
    `compiler/tychoc0.ty` that destructure or construct the variant, every one of
    which would need updating, in the self-hosting compiler, with `make fixpoint`
    (`B == C`) as the acceptance bar. Phase 8's method explicitly forbids forcing
    plumbing that large to move a caret two lines. Filed here instead.
  - Rejected shortcut, recorded so it is not re-proposed: using
    `bodies[j][0]`'s line as a proxy for the arm's line. It is right only when the
    arm body sits on the arm's own line (`Red: println("b")`) and reports the
    *following* line for the block form, plus it needs a fallback for an empty
    body — wrong in cases where today's behaviour is merely coarse.
  - Scope when taken: widen `SMatch` with an arm-locs `[int]` (or fold the arm
    name and loc into one struct), thread it from `parse_match` through all 43
    sites, and use it at `tychoc0.ty:10955` / `:10959`. Both diagnostics then
    agree with tychoc.
  - Also worth folding in when taken: the other `die_at(dc, el, …)` calls inside
    `check_match` (`:10967`, `:10970`, and the `is_result` arm below them) name a
    specific offending arm in their text and would benefit from the same locs.
  - Done when: `tests/reject/match_dup_arm.ty` reports line 10 and
    `tests/reject/match_wildcard_not_last.ty` line 12 from **both** compilers;
    full gate set green including `make fixpoint`.

- [x] **Phase 18 — two `bounded[N]T` accept/reject divergences between the compilers (found by Phase 11, out of its docs-only scope)**
  - Same normative class as Phases 9 and 15: `00-conventions.md` §1.3 makes the
    accept/reject **decision** normative, and here the two compilers decide
    differently. Phase 11 was docs-only and could not touch either compiler, so
    both are filed whole. Both were measured on the frontend column
    (`tychoc --emit-c` vs `tychoc0`'s stdout emit), the same surface
    `tests/run.sh:148-212` compares.
  - **(a) `bounded[CONST]T` — tychoc ACCEPTS, tychoc0 REJECTS.** With
    `const CAP = 4`, `bounded[CAP]int` compiles and runs on tychoc (`n 3`) and
    dies on tychoc0 with `parse: line N: bounded needs an integer capacity:
    bounded[N]T`. tychoc takes the const path deliberately —
    `src/tychoc.c:1730-1737` looks the name up via `consts_find` and its comment
    reads "an int literal or an int `const` name, **same as a fixed [N]T**".
    tychoc0's branch (`compiler/tychoc0.ty:1721-1725`) tests `!= TInt` and stops.
    This is an oversight, not a design split: tychoc0 **already implements** the
    same const-capacity form for fixed arrays at `compiler/tychoc0.ty:1679-1690`
    (the `"[#" + cnm + "]"` encoding), so the machinery is present and simply was
    not wired into the `bounded` branch. The spec's fixed-array §5.3.2 and the
    `ArrayOrMap` production's `IDENT "]" Type /* [C]T */` alternative both already
    bless the const form for `[C]T`.
  - **Decide the direction before coding**, since either is defensible: teach
    tychoc0 the const capacity (making `bounded` consistent with `[C]T`, and then
    §5.3.10's "not portable" paragraph and the `INT`-only production both get
    widened to `INT | IDENT`), or drop it from tychoc (narrowing, and gratuitously
    inconsistent with `[C]T`). The first is strongly indicated.
  - **(b) `bounded[N]Channel(T)` — tychoc REJECTS, tychoc0 FAIL-OPENS.** tychoc
    dies with `a channel handle cannot be stored in a container or aggregate --
    pass it as an argument instead`; tychoc0 accepts it and emits C that does not
    compile (`expected identifier or '(' before 'int'`). This one needs no ruling:
    the spec **already requires the reject** — `03-types.md` §5.3.9 says a handle
    "cannot be copied, stored in any aggregate, …", and `Task(T)`/`Channel(T)` are
    named there as similarly affine and non-storable. tychoc0 is straightforwardly
    non-conforming and the fix direction is fixed. Note this is a *fail-open*, the
    class `tests/run.sh:143-160` exists specifically to catch.
  - Done when: both programs get the same verdict from both compilers; a reject
    fixture is added for (b) and an accept-or-reject fixture for (a) per whichever
    direction is chosen; §5.3.10 and the `Type` production are updated to match if
    (a) resolves toward accept; full gate set green.

  **DONE 2026-07-25. Both divergences closed toward the direction the sources and
  the spec already indicated. Method: FRONT/CC/RUN per compiler, `--emit-c` on
  BOTH sides (`/tmp/ph18/probe.py`), never an `rc` comparison.**

  **1. Every citation in this phase's own text was opened and VERIFIED.**

  | Claim | Verdict |
  |---|---|
  | `src/tychoc.c:1730-1737` is tychoc's const-capacity path, comment "same as a fixed `[N]T`" | **correct** — `:1730` is the comment, `:1733` `consts_find(pkg_mangle(...))`, `:1734-1735` the non-`E_INT` reject |
  | `compiler/tychoc0.ty:1721-1725` tests `!= TInt` and stops | **correct** (pre-edit) |
  | `compiler/tychoc0.ty:1679-1690` already implements const capacity for `[C]T` | **correct**, range is `:1679-1692`; the `"[#" + cnm + "]"` encoding is at `:1692`, resolved in `mangle_type` `:2829-2839` |
  | `03-types.md` §5.3.9 already requires the affine-handle reject | **correct** — `:238-241`: a handle "cannot be copied, stored in any aggregate…", and "`Task(T)` and `Channel(T)` are similarly affine and non-storable" |
  | `tests/run.sh:148-212` compares the FRONTEND status | **correct** — `:155` `"$TYCHOC" "$hi" --emit-c` and `:159` `"$TMP/h0" "$hi" --emit-c`; no `cc` on either side of the reject loop |

  **2. (a) `bounded[CONST]T` — judgement INDEPENDENTLY CONFIRMED: an oversight,
  not a design split. Resolved toward ACCEPT (teach tychoc0).** Four pieces of
  evidence, not one:
  - tychoc's intent is written in the source (`src/tychoc.c:1730`).
  - The spec already blesses the const spelling for the sibling form:
    `03-types.md:168` (§5.3.2) — "`N` is a positive integer literal or an `int`
    `const`" — and the grammar carries `IDENT "]" Type /* [C]T */`
    (`appendix-a-grammar.md:94`, `02-grammar.md:146`).
  - tychoc0 owns the whole machinery already, and it is *measured* to agree with
    tychoc: an 11-probe `[C]T` baseline sweep (local / param / return / field ×
    const, no-const, wrong-name, string const, negative const) came back
    **11 probes, FRONTEND-DIVERGENT 0**, including all six failure modes.
  - No spec text forbade the const capacity for `bounded`. The only text that
    did — §5.3.10's "not portable" paragraph — was written by Phase 11
    *because* of this divergence, and Phase 11 said so in its own note.

    Narrowing tychoc instead would have deleted a working feature and left
    `bounded` gratuitously inconsistent with `[C]T`. Rejected.

  **3. (b) `bounded[N]Channel(T)` — citation verified, tychoc0 was
  non-conforming, fixed. No ruling needed.**

  **4. FRONT/CC/RUN, BEFORE (`/tmp/ph18/p18.py`, 15 probes).** `println` takes
  one argument, so the first probe round was thrown away and rewritten with
  `str(...)` concatenation — the recorded run is the corrected one.

  | probe | tychoc FRONT/CC/RUN | tychoc0 FRONT/CC/RUN | verdict |
  |---|---|---|---|
  | cap_const `const CAP = 4` → `bounded[CAP]int` local | ACCEPT/ok/`n 3` | REJECT `parse: line 4: bounded needs an integer capacity` | **DIVERGE** |
  | use_const_cap (param + push) | ACCEPT/ok/`n 4` | REJECT (same text) | **DIVERGE** |
  | cap_const_ret (return position) | ACCEPT/ok/`n 1` | REJECT (same text) | **DIVERGE** |
  | cap_const_field (struct field) | ACCEPT/ok/`n 2` | REJECT (same text) | **DIVERGE** |
  | cap_const_zero `const CAP = 0` | REJECT `a bounded capacity must be positive` | REJECT (parse, wrong reason) | agree |
  | cap_const_neg `const CAP = -1` | REJECT (same) | REJECT (parse, wrong reason) | agree |
  | cap_const_str `const CAP = "x"` | REJECT `must be an integer literal or an int const` | REJECT (parse, wrong reason) | agree |
  | cap_unknown_ident `bounded[NOPE]int` | REJECT (same) | REJECT (parse, wrong reason) | agree |
  | cap_const_overfill `const CAP = 2` = `[1,2,3]` | REJECT `a bounded[2] holds at most 2 elements, got 3` | REJECT (parse, wrong reason) | agree |
  | el_chan local `bounded[4]Channel(int)` | REJECT `a channel handle cannot be stored in a container or aggregate` | **ACCEPT/CCFAIL** | **DIVERGE** |
  | el_chan_param | REJECT (same) | **ACCEPT/CCFAIL** | **DIVERGE** |
  | el_chan_field | REJECT (same) | **ACCEPT/CCFAIL** | **DIVERGE** |
  | el_chan_ret | REJECT (same) | REJECT (return-type mismatch, unrelated reason) | agree |
  | el_task `bounded[4]Task(int)` local | REJECT `unknown type 'Task'` | **ACCEPT/CCFAIL** | **DIVERGE** |
  | el_task_param | REJECT (same) | **ACCEPT/CCFAIL** | **DIVERGE** |

  ```
  BEFORE  TOTAL 15 probes   FRONTEND-DIVERGENT 8
  ```
  Two more than Phase 11 recorded, because Phase 11 probed `el_chan` and
  `cap_const` in one position each; the field/param/return positions and the
  `Task(T)` twin are the same two defects seen from more angles.

  **5. FRONT/CC/RUN, AFTER — same 15 probes, same harness.**

  | probe | tychoc FRONT/CC/RUN | tychoc0 FRONT/CC/RUN | verdict |
  |---|---|---|---|
  | cap_const | ACCEPT/ok/`n 3` | ACCEPT/ok/`n 3` | agree |
  | use_const_cap | ACCEPT/ok/`n 4` | ACCEPT/ok/`n 4` | agree |
  | cap_const_ret | ACCEPT/ok/`n 1` | ACCEPT/ok/`n 1` | agree |
  | cap_const_field | ACCEPT/ok/`n 2` | ACCEPT/ok/`n 2` | agree |
  | cap_const_zero | REJECT `a bounded capacity must be positive` | REJECT `resolve: a bounded capacity must be positive` | agree |
  | cap_const_neg | REJECT (same) | REJECT (same) | agree |
  | cap_const_str | REJECT | REJECT `resolve: a fixed-size array length must be an integer literal or an int const` (shared `const_int` helper, `:2810-2814`) | agree |
  | cap_unknown_ident | REJECT | REJECT `line 2: a bounded capacity must be an integer literal or an int const -- 'NOPE' is not` | agree |
  | cap_const_overfill | REJECT `a bounded[2] holds at most 2 elements, got 3` | REJECT **same text** | agree |
  | el_chan / el_chan_param / el_chan_field / el_chan_ret | REJECT | REJECT `a channel handle cannot be stored in a container or aggregate -- pass it as an argument instead` (tychoc's exact wording) | agree |
  | el_task / el_task_param | REJECT `unknown type 'Task'` | REJECT `a task handle cannot be stored in a container or aggregate -- wait(t) first` | agree |

  ```
  AFTER   TOTAL 15 probes   FRONTEND-DIVERGENT 0
  ```

  **6. What changed — all compiler work is in `compiler/tychoc0.ty`; `src/tychoc.c`
  was NOT touched** (tychoc was already right on both counts).
  - **`:1718-1750`, the `bounded` branch of `parse_type_d`.** The capacity now
    accepts `TInt` (unchanged) *or* `TIdent`. The parser has no const table — it
    is a pure function of the token stream — so the const form is **deferred**,
    encoded `"[b#W]T"`, exactly mirroring the fixed array's `"[#W]T"` (`:1692`).
  - **`:2859-2869`, a new `has_prefix(ty, "[b#")` branch in `mangle_type`,**
    immediately after the existing `"[#"` branch it mirrors. Resolves `W`
    through `const_idx`/`const_int` and rejects a non-const name or a
    non-positive value. This pass always runs when a const is present
    (`dofold`, `:3451`), which is exactly when the form can be legal.
  - **`:11211-11212`, a `"[b#"` guard in the `STypedDecl` arm,** beside the
    `"[#"` guard Phase 3 added for the same reason: a *surviving* `[b#` means
    the name is not a const in this program, so say what tychoc says instead of
    leaking the internal spelling into a type-mismatch message.
  - **`:1746-1749`, the affine-element check.** tychoc fails closed at a
    type-intern choke point (`arrc_sized_b`, `src/tychoc.c:669-671`, one of six
    such points); tychoc0 has no intern step, so a `Channel(...)` or `Task(...)`
    element is rejected in the parser, with tychoc's own two messages
    (`src/tychoc.c:607` and `:567`).
  - Three in-source line cross-references were corrected after the edits shifted
    the file (`:1724`, `:2859`, `:11207`).

  **7. Spec updated to match, in all four places** (`(a)` resolved toward accept,
  so the plan's "widen the grammar" branch applies):
  - `docs/spec/appendix-a-grammar.md:83` **and** `docs/spec/02-grammar.md:135`,
    kept byte-identical (`spec-check`'s "Appendix A grammar matches §3/§4"
    asserts it):
    ```ebnf
                | "bounded" "[" ( INT | IDENT ) "]" Type            /* bounded[N]T / bounded[C]T, C an int const */
    ```
  - `docs/spec/02-grammar.md:164-169` — the §4.2 note no longer says the `const`
    form is unportable; it now states both spellings are admitted, as for `[C]T`.
  - `docs/spec/03-types.md` §5.3.10 — the "**not** portable / implementations
    disagree" paragraph is **replaced** by a positive rule: literal or positive
    `int const`, with the six rejection cases enumerated. Provenance extended to
    the new `tychoc0` sites and the six fixtures.
  - `docs/spec/appendix-e-conformance.md:56` — the §5.3.10 row now cites all six
    fixtures. `spec-check`'s "all Appendix E fixture citations resolve" confirms
    they exist.
  - The §5.3.10 `> Note:` about aggregate elements was left alone — that is
    Phase 19's, and `Channel`/`Task` were never in the set it names.

  **8. Fixtures — 5 new, one file per distinct rejection** (the compiler stops at
  the first error, so they cannot be merged). Test count **447 → 452**.
  ```
  tests/bounded_const_cap.ty (+ .out)      ACCEPT: bounded[CAP]T in field, param,
                                           return and local, two distinct consts,
                                           and bounded[4]int proving the literal
                                           and const spellings are the same type
  tests/reject/bounded_chan_elem.ty        (b) Channel(T) element
  tests/reject/bounded_task_elem.ty        (b) Task(T) element
  tests/reject/bounded_nonconst_cap.ty     the guard that keeps (a)'s widening
                                           from being a fail-open: bounded[NOPE]int
  tests/reject/bounded_const_cap_zero.ty   const CAP = 0 — positivity on the
                                           deferred const path
  ```
  Each reject fixture was checked by hand against **both** compilers before the
  gate run; all four give rc=1 on both.

  **9. NOT over-tightened — Phase 11's own harness re-run, unchanged binaries
  aside.** `/tmp/ph11/probe3.py` and `probe4.py` were re-run against the pre- and
  post-change tychoc0 and diffed. The **only** differences in the whole output
  are the rows this phase set out to fix, plus three improved diagnostics:
  ```
  probe3  BEFORE  FRONTEND DIVERGENCES (2): ['cap_const', 'use_const_cap']
          AFTER   FRONTEND DIVERGENCES (0): []
  probe4  BEFORE  FRONTEND DIVERGENCES: 1 ['el_chan_paren']
          AFTER   FRONTEND DIVERGENCES: 0 []
  ```
  Every other one of the 45 rows Phase 11 recorded is byte-identical. Phase 10's
  45 probes were also re-run post-change: 35 + 10, DIVERGENT 0 (see the
  re-verification appended to Phase 10 above).

  **10. Gates — all seven green, each its own foreground `env -u LD_PRELOAD make …`;
  tychoc0 built to `/tmp/ph18/`, outside the tree:**
  ```
  test        passed: 452   failed: 0  /  all green          (was 447; +5 fixtures)
  corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
  conc        conc: passed 37   failed 0
  fixpoint    ok  B == C : tychoc0 reproduces itself byte-identically (34980 lines C)
              ok  split tychoc0 (2 packages) self-hosts E==F and matches the single-file compiler
              fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
  ilp32       passed: 452   failed: 0  /  all green
  spec-check  spec-check: Appendix A grammar matches §3/§4 (ok)
              spec-check: all Appendix E fixture citations resolve (ok)
              spec-examples: 7 runnable example(s), all pass
  check-links link check: ok (121 markdown files, no dead relative links)
  ```
  `git status --short`: the 5 modified files + the 6 new fixture files only. No
  build spill.

  **Residual uncertainty:** `bounded[NOPE]int` in a *param*, *field* or *return*
  position, in a program containing **no** `const` at all, is rejected by tychoc0
  with `type: unknown type 'b#NOPE]in'` rather than the capacity message — the
  `STypedDecl` guard only covers locals. This is not a new defect and not a
  decision divergence (both compilers reject): it is the **exact** pre-existing
  behaviour of the `[C]T` form it mirrors, measured in the baseline sweep
  (`fix_nocst_param` → `type: unknown type '#NOPE]in'`, agree). Fixing it means
  widening the guard to the param/field/return arms for *both* encodings, which
  is a diagnostic-quality change to a form this phase did not introduce. Filed as
  Phase 21.

- [x] **Phase 19 — `bounded[N]T` of an aggregate element parses but emits uncompilable C (found by Phase 11, out of its docs-only scope)**
  - Not an accept/reject divergence — **both frontends accept these** — so it did
    not block Phase 11's grammar work, but it makes the compilers' own diagnostic
    false. Both compilers reject a bad element with the text `bounded elements
    must be int, float, string, **a struct**, or an array`
    (`src/tychoc.c:1741-1742`, `compiler/tychoc0.ty:1729-1730`) — yet
    `bounded[4]Pt` for a struct `Pt` fails to compile on **both**:
    `error: array type has incomplete element type 'S_Pt'`. The message promises
    something neither implementation delivers.
  - Measured 2026-07-25 (`/tmp/ph11/probe3.py`; `cc -O1 -fwrapv -pthread`):
    - **Both** emit uncompilable C for a `struct`, a tuple, or a `soa` element
      (all "incomplete element type" / "unknown type name" — a **declaration-order**
      bug: the inline `T v[N]` array is emitted before the element struct is
      completed).
    - **tychoc0 only**: `bytes`, `[N]T`, `[K:V]`, a nested `bounded`, a `bounded`
      map key, `[2]bounded[4]int`, `bounded[2]bounded[4]int` — all fine on tychoc.
      tychoc0's failures cluster on the type appearing in a **parameter** position
      (`bounded[4][2]int` as a local builds and runs, `n 7`).
    - **tychoc only**: `Option(T)` and `Result(T,E)` elements — fine on tychoc0.
  - Why it went unnoticed: `tests/bounded.ty` only ever uses `bounded[N]int`, so
    the whole aggregate-element surface is unfixtured.
  - `docs/spec/03-types.md` §5.3.10 currently carries a `> Note:` naming the
    portable subset (`int`, `float`, fixed-width numerics, `ptr`, `string`, a
    fieldless enum, `[E]`, a function type). **That note must be deleted when this
    phase lands**, and the element rule stated positively instead.
  - Done when: every element type the frontend admits produces C that compiles and
    runs identically on both compilers, with fixtures covering struct, tuple, map,
    nested-`bounded` and `bytes` elements in local, param, field and return
    positions; §5.3.10's caveat note removed; full gate set green.

  - **DONE 2026-07-25.** Direction taken: **(A) MAKE IT WORK for every element
    type.** No element type was narrowed. The one frontend narrowing that landed
    is a *new* rejection of a genuinely infinite type (below), not a retreat.
  - **Plan citations verified before patching.** `src/tychoc.c:1741-1742` and
    `compiler/tychoc0.ty:1729-1730` (now `:1794-1795` after Phase 18/20 grew the
    file) were both the `belem == void || belem == bool` guard carrying the text
    `bounded elements must be int, float, string, a struct, or an array`.
  - **ROOT CAUSE (single, shared by both emitters, and NOT bounded-specific).**
    `bounded[N]T` lowers to `struct { T v[N]; tycho_int len; }` — the element is
    stored **inline**, so `T` must be a *complete* C type at that point, not just
    forward-declared. Both emitters wrote every composite-array body with the
    pointer-shaped arrays, i.e. **before** the struct / tuple / Option / Result /
    map / soa bodies, which is only sound for a dynamic `[T]` (`T *data`).
    tychoc: `src/tychoc.c` step (2b) of the emission staging; tychoc0: driver
    step 2 (`gen_arr_type` over every `ets[i]`). The probe proved the bug is
    **not about `bounded` at all** — plain `[2]Pt`, a fixed array of a struct,
    failed identically on both compilers. Fixing one necessarily fixes the other;
    they are the same `size > 0` inline family.
  - **FIX (tychoc, `src/tychoc.c`).** Inline-storage arrays joined the by-value
    containment DFS: `inline_arrc`/`needs_body_first` `:10039-10042`, the new
    `IS_ARRC` branch of `emit_aggregate` `:10044-10070`, the four member-recursion
    sites now testing `needs_body_first`, step (2b) narrowed to dynamic arrays
    only, and the inline bodies driven from step (3) next to the struct/Option/
    Result/tuple bodies (plus a sweep loop for inline arrays no aggregate reaches).
    `check_finite_types` reserves and zeroes `g_arrc_color` and seeds the DFS from
    every array type too.
    *Crash found and fixed during this work*: the first version let a **dynamic**
    arrc fall through `emit_aggregate`'s trailing `else`, which assumes `IS_TUP`,
    so `TUP_ID(1026)` went negative and tychoc SIGSEGV'd on `compiler/tychoc0.ty`
    (gdb: `has_typaram(t=6924)` under `emit_aggregate(t=1026)`,
    `g_arrtypes[2] = {elem = 8194, size = 0}`). Guarded by the early
    `IS_ARRC(t) && !inline_arrc(t)` return at `:10045`.
  - **FIX (tychoc0, `compiler/tychoc0.ty`).** Three defects, all real:
    1. *Same declaration-order bug.* `is_byval_comp` `:9731`, `comp_dep_types`
       `:9748-9783` (a struct field / tuple element / array element that is an
       inline array is now a dependency), `emit_comp_body` `:9785-9808` (emits
       `gen_arr_type` for the inline families), driver step 2 narrowed to
       non-inline arrays, and a third topological pass for inline arrays not
       reached from a struct/tuple. Because the inline bodies now land after the
       map and soa bodies, `bounded[4][string:int]` and `bounded[4]soa[Pt]` also
       resolve.
    2. *`[2][K:V]` / `bounded[N][K:V]` function bodies.* `Arr_f2_map_str_int_set`
       calls `map_str_int_copy`, whose forward decl is emitted with the map
       families — the dynamic `[[K:V]]` case was already deferred to the
       array-of-map pass; the inline families now defer with it
       (`inline_arr_of_map` `:9739-9746`, used at the two driver call sites).
    3. *`bytes` element.* `elem_deepcopy` `:7041` treats `bytes` as heap, but
       `elem_copy_expr` `:7075-7078` matched only `"str"` and fell through to the
       struct arm, emitting an undeclared `S_bytes_copy`. Added the `bytes` arm
       (`scopy`, the same length-headered buffer — `cp_field:7100` already did
       this). This made `bounded[N]bytes` work in **all** positions.
  - **NEW REJECTION, landed in BOTH compilers together (no divergence).** With
    inline arrays in the DFS, `struct Node: kids: [2]Node` is a back-edge — a
    genuinely infinite type (`sizeof(Node)` depends on itself). tychoc reaches
    its existing struct back-edge `die_at`; tychoc0 had no cycle guard at all, so
    `emit_comp_body` gained an on-stack path list (passed **by value**, since a
    tycho parameter is borrowed read-only — the first attempt used
    `push(stack, …)` on the parameter and was rejected by the compiler) and dies
    the same way. Before: both ACCEPTed and emitted C that cc rejected. Locked by
    `tests/reject/inline_arr_self_elem.ty`.
  - **DIAGNOSTIC — the old text was false, the new text is exactly true.** The
    guard fires iff the element is `void` or `bool`; every other type now works.
    Replaced in both compilers (`src/tychoc.c:1742`, `compiler/tychoc0.ty:1795`):

    ```
    -  bounded elements must be int, float, string, a struct, or an array
    +  a bounded element cannot be bool or void
    ```

    Locked by `tests/reject/bounded_elem_bool.ty` (both compilers must reject).
  - **BEFORE -> AFTER, FRONT/CC/RUN, 121 probes x 2 compilers** (`/tmp/ph19/probe.py`,
    `cc -O1 -fwrapv -pthread -std=c11`; cells are `tychoc/tychoc0`, `ok` = CC and
    RUN both clean and the two RUN outputs identical). Divergences **53 -> 16**:

    | element | local | param | field | return | mapval | fixarrof | boundedof | arrelem `[bounded[4]T]` |
    |---|---|---|---|---|---|---|---|---|
    | int / float / string / enum / dynarr / fnty | ok/ok | ok/ok | ok/ok | ok/ok | ok/ok | ok/ok | ok/ok | ok/CCFAIL -> **unchanged** |
    | **struct** | CCFAIL/CCFAIL -> ok/ok | same | same | same | same | same | same | CCFAIL/CCFAIL -> ok/CCFAIL |
    | **tuple** | CCFAIL/CCFAIL -> ok/ok | same | same | same | same | same | same | CCFAIL/CCFAIL -> ok/CCFAIL |
    | **soa** | CCFAIL/CCFAIL -> ok/ok | same | same | same | same | same | same | CCFAIL/CCFAIL -> ok/CCFAIL |
    | **Option / Result** | CCFAIL/ok -> ok/ok | same | same | same | same | same | same | CCFAIL/CCFAIL -> ok/CCFAIL |
    | **bytes** | ok/CCFAIL -> ok/ok | same | same | same | same | same | same | ok/CCFAIL -> **unchanged** |
    | **`[N]T` fixarr** | ok/CCFAIL -> ok/ok | same | same | same | same | same | same | ok/CCFAIL -> **unchanged** |
    | **`[K:V]` map** | ok/CCFAIL -> ok/ok | same | same | same | same | same | same | ok/CCFAIL -> **unchanged** |
    | **nested `bounded`** | ok/CCFAIL -> ok/ok | same | same | same | same | same | same | ok/CCFAIL -> **unchanged** |

    Non-`bounded` twins fixed by the same change: `[2]Pt`, `[2](int,int)`,
    `[2][string:int]`, `[2][2]int`, `[2]Option(int)`, `struct Board: cells: [2]Pt`
    — all `CCFAIL -> ok` on both compilers.
  - **PER-TYPE DECISION — (A) for every element type; (B) for none.**
    | element type | decision | why |
    |---|---|---|
    | struct, tuple, soa | **(A)** fixed | one declaration-order bug, shared by both emitters; the diagnostic already promised "a struct" |
    | Option, Result | **(A)** fixed | tychoc-only, same DFS ordering; tychoc0 boxes them as `HOption`/`HResult` so it never had the bug |
    | bytes | **(A)** fixed | tychoc0-only, a missing `bytes` arm in `elem_copy_expr` — one line |
    | `[N]T`, `[K:V]`, nested `bounded` | **(A)** fixed | tychoc0-only, same declaration-order bug plus the map-fn deferral |
    | int, float, string, fixed-width, ptr, enum, `[T]`, fn type | already worked | no change |
    | bool, void | **(B)** stay rejected | no inline codegen for either (mirrors `[N]bool`); the message now says exactly this and nothing more |
    | `Channel(T)`, `Task(T)`, handles | stay rejected | affine, §5.3.9 — Phases 18/20, untouched here |
  - **§5.3.10 rewrite (`docs/spec/03-types.md`).** The temporary `> Note:` naming
    a portable subset is **deleted**. The element rule is now stated positively:
    `T` MUST NOT be `bool`, `void`, or an affine handle, and **every other type is
    a valid element**, aggregates included, "in every stored position — a local, a
    parameter, a struct field and a return type"; plus a new paragraph making the
    inline-storage consequence normative (a type reaching back into the `bounded`
    that contains it is an infinite type and MUST be rejected, as for `[N]T`). The
    Provenance block gained the new fixtures and the DFS citations.
  - **Fixtures added (474 -> 478).**
    - `tests/bounded_elems.ty` + `.out` — struct, tuple, map, nested `bounded`,
      `bytes`, `[N]E`, `Option`, `Result` elements across local / param / field /
      return, plus a value-copy independence check and `==`.
    - `tests/fixarr_aggregate.ty` + `.out` — the `[N]T` twin (struct, tuple and
      map elements; local, param, field, return), because `tests/` only ever used
      `[N]<scalar>` and that gap is what hid the bug.
    - `tests/reject/bounded_elem_bool.ty` — the corrected diagnostic.
    - `tests/reject/inline_arr_self_elem.ty` — the new infinite-type rejection.
  - **STILL DIVERGENT, deliberately NOT fixed here — filed as Phase 23.** The 16
    remaining divergences are all one tychoc0 **name collision**, and they are a
    *container-of-bounded* position, not an element type: `afam` (`:3801-3804`)
    names the family for a dynamic `[X]` `Arr_<mangle(X)>`, while `gen_arr_type`
    names the inline family for `X` itself `Arr_<mangle(X)>` — so `[bounded[4]int]`
    and `bounded[4]int` both want `Arr_b4_int`. It predates this phase (it already
    hit `[bounded[4]int]` with an `int` element), also hits `[[2]int]`, and fixing
    it means re-deriving ~22 `"Arr_" + mangle(...)` sites. `[bounded[4]int:string]`
    (`Arr_int_hash` undeclared) is the same root cause.
  - **Gates, each its own foreground command, `env -u LD_PRELOAD`:**
    - `make test` -> `passed: 478   failed: 0` / `all green`
    - `make corelib` -> `corelib: all green (tychoc and tychoc0 agree, match goldens)`
    - `make conc` -> `conc: passed 37   failed 0`
    - `make fixpoint` -> `ok B == C : tychoc0 reproduces itself byte-identically (35131 lines C)` / `fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)`
    - `make ilp32` -> `passed: 478   failed: 0` / `all green`
    - `make spec-check` -> `spec-examples: 7 runnable example(s), all pass`
    - `make check-links` -> `link check: ok (121 markdown files, no dead relative links)`
    - `git status --short` -> only the 3 edited sources + the 6 new fixture files; no build spill.

- [x] **Phase 20 — tychoc0 FAIL-OPENS on an affine handle in *every* container and aggregate, not only `bounded` (measured by Phase 18, out of its scope)**
  - Phase 18 fixed `bounded[N]Channel(T)` because that is the divergence Phase 11
    filed. While verifying it, an 11-probe sweep found the same fail-open across
    **the whole container family** — tychoc rejects at six type-intern choke
    points (`src/tychoc.c:611-613`, `:669-671`, `:752-754`, `:775-777`,
    `:839-841`, `:1009-1011`), tychoc0 has no intern step and no equivalent
    check. Spec `03-types.md` §5.3.9 (`:238-241`) already requires the reject, so
    no ruling is needed; the direction is fixed.
  - **Measured 2026-07-25** (`/tmp/ph18/p18b.py`, FRONT/CC/RUN, `--emit-c` on
    both sides; `bounded` rows excluded — those are Phase 18's and are now fixed):

    | probe | form | tychoc FRONT | tychoc0 FRONT |
    |---|---|---|---|
    | dynarr_chan | `[Channel(int)]` local | REJECT | **ACCEPT** (CCFAIL) |
    | fixarr_chan | `[2]Channel(int)` local | REJECT | **ACCEPT** (CCFAIL) |
    | dynarr_chan_param | `[Channel(int)]` param | REJECT | **ACCEPT** (CCFAIL) |
    | fixarr_chan_param | `[2]Channel(int)` param | REJECT | **ACCEPT** (CCFAIL) |
    | struct_chan | a `Channel(int)` struct field | REJECT `a struct field cannot be a channel` | **ACCEPT, C COMPILES AND RUNS** |
    | map_chan | `[string: Channel(int)]` | REJECT | **ACCEPT** (CCFAIL) |
    | opt_chan | `Option(Channel(int))` | REJECT | **ACCEPT, C COMPILES AND RUNS** |
    | tuple_chan | `(int, Channel(int))` | REJECT | **ACCEPT** (CCFAIL) |
    | chan_of_chan | `Channel(Channel(int))` | REJECT | **ACCEPT, C COMPILES AND RUNS** |
    | plain_task_ty | `Task(int)` as a written param type | REJECT `unknown type 'Task'` | **ACCEPT, C COMPILES AND RUNS** |
    | dynarr_task | `[Task(int)]` param | REJECT | **ACCEPT** (CCFAIL) |

    ```
    11 probes   FRONTEND-DIVERGENT 11
    ```
  - **The three that COMPILE AND RUN are the dangerous ones.** A CCFAIL is loud;
    `struct_chan`, `opt_chan`, `chan_of_chan` and `plain_task_ty` produce a
    working binary that stores a handle the compiler promised could not be
    stored. That is the aliasing hazard §5.3.9 exists to prevent, reaching
    codegen silently.
  - `plain_task_ty` is a second, distinct defect wearing the same coat: `Task`
    is not a *written* type name in tychoc at all (`unknown type 'Task'`), but
    tychoc0's `parse_type_d` accepts it. That is the Phase 9 bare-NAME shape,
    which Phase 9's 23-name sweep did not cover because `Task(T)` is a
    constructor, not a bare identifier.
  - Scope when taken: decide first whether `Task(T)` is a legal written type
    spelling at all (the spec names it in §5.3.9 and §20 — check whether it is
    *writable* or only *inferred*), then add the handle-in-container check to
    tychoc0 at every container form, mirroring tychoc's six choke points. Fail
    closed: reject the whole family, do not special-case.
  - Fixtures: one `tests/reject/` file per container form (dynamic array, fixed
    array, map value, tuple, `Option`, `Result`, struct field, `Channel` of
    `Channel`), since the compiler stops at the first error.
  - Done when: every row above agrees on the frontend decision, each is
    fixture-locked, `tests/conc/` still passes (the legal uses — a `Channel(T)`
    *parameter*, `spawn`, `wait` — must NOT be tightened), full gate set green.

  **DONE 2026-07-25. 75 probes, FRONTEND-DIVERGENT 40 → 0. Method: FRONT/CC/RUN
  per compiler, `--emit-c` on BOTH sides (`/tmp/ph20/probe.py`, Phase 11/18's
  harness), never an `rc` comparison. `src/tychoc.c` was NOT touched — tychoc was
  right throughout.**

  **1. Every citation this phase relied on was opened and VERIFIED.**

  | Claim | Verdict |
  |---|---|
  | `docs/spec/03-types.md` §5.3.9 already requires the reject | **correct** — §5.3.9 heading at `:234`; `:236-241` "A handle value cannot be copied, stored in any aggregate, captured by a closure or `parallel for`, or returned from a Tycho function (§25). The concurrency handle types `Task(T)` and `Channel(T)` are similarly affine and non-storable" |
  | tychoc rejects at six type-intern choke points `:611-613`, `:669-671`, `:752-754`, `:775-777`, `:839-841`, `:1009-1011` | **correct**, and there is a **seventh** the plan missed: `func_of` `:1030-1032` (a function VALUE may not take or return a handle) |
  | `tests/run.sh:148-212` compares the FRONTEND status | **correct** — `:155` `"$TYCHOC" "$hi" --emit-c`, `:159` `"$TMP/h0" "$hi" --emit-c`; no `cc` on either side |

  **2. tychoc's enforcement pattern, from its source.** Every composite type in
  tychoc is built by a *find-or-create interner*, and the affine check is the
  first thing each one does — before the dedupe scan, so no spelling can reach
  the table:
  ```
  chan_of      :611-613   arrc_sized_b :669-671   opt_of :752-754   res_of :775-777
  tup_of       :839-841   mapc_of      :1009-1011 func_of :1030-1032
      if (IS_TASK(x)) task_container_err();     /* :566-568 */
      if (IS_HANDLE(x)) handle_container_err(); /* :587-589 */
      if (IS_CHAN(x)) chan_container_err();     /* :606-608 */
  ```
  Its own comment states the argument (`:563-565`): "every aggregate containing a
  task would have to intern a type through one of these". Three positions are
  **not** composite constructions — a bare `Channel(T)` field, enum payload or
  newtype underlying — so `resolve_program` scans the declarations for those
  (`:7011-7014`, `:7019-7023`, `:7020-7022`). Seven interners + one decl scan is
  the whole enforcement.

  **3. tychoc0's equivalent choke points, and why they cover the FAMILY and not
  the sample.** tychoc0 has no intern step: **a type IS the string that spells
  it**. So the structural analogue of "interning" is "constructing the type
  string", and there are exactly **two** functions in the compiler that do it:
  - **`parse_type_d`** (`compiler/tychoc0.ty:1680`) — every *written* composite
    spelling. All 13 construction sites now call one shared guard,
    `ck_affine_part` (`:1653`): tuple `:1703`, `[N]T` `:1725`, `[#W]T` `:1740`,
    `[$N]T` `:1752`, map key+value `:1759-1760`, dynamic `[T]` `:1763`, `soa`
    `:1770`, `bounded` `:1802`, `Channel(T)` `:1862`, `Option(T)` `:1868`,
    `Result(T,E)` `:1876-1877`, generic type argument `:1914`; the `fn(P)->R`
    type is `:1820`/`:1825` with tychoc's own two `func_of` messages.
  - **`type_of`** (`:5165`) — every *inferred* composite, from a literal that
    spells no type at all. Shared guard `ck_affine_inferred` (`:5096`) at
    `EArrLit` `:5179`/`:5182`, `EArrEmpty` `:5189`, `EMapEmpty` `:5192-5193`,
    `EMapLit` `:5198-5199`, `ETuple` `:5210`, `Some(...)` `:5344`.

    Plus the same three declaration positions tychoc scans, which are not
    composite constructions: struct field `:2432`, enum payload `:2771`, newtype
    underlying `:2799` — each with tychoc's exact wording.

    **Why this is the family, not the sample.** The check is on each *immediate*
    component at construction, which closes nesting by induction exactly as
    tychoc's seven interners do: `[Option(Channel(int))]` dies at the inner
    `Option` site before the array site is ever reached; `[string: [Task(int)]]`
    dies at the inner array. A twelfth spelling cannot fail open unless it is a
    composite type built somewhere other than these two functions — and there is
    nowhere else, because a composite type in tychoc0 has no representation other
    than a string one of them produced. Phase 18's inline `bounded` check was
    **folded into** the shared guard so the rule is one rule, not eleven.
  - **The `Task(T)` twin, resolved by measurement not assumption.** `Task(T)` is
    tychoc0's INTERNAL spelling of the spawn handle (`type_of`'s `ESpawn` arm,
    `:5253`), so a *written* `Task(int)` fell through the generic-instance branch
    and was handed straight back as the real handle type — the identical fail-open
    shape Phase 9 closed for `str`/`void`/`char` one level down. tychoc reports
    `unknown type 'Task'` (`src/tychoc.c:1951`). But a blanket reject would have
    **over-tightened**: `Task` is an ordinary name in tychoc, and
    `struct Task($T)` + `[Task(int)]` is *accepted and runs on both compilers*
    (measured — probes `uTask_gen_in_arr`, `uTask_gen_opt`, `uTask_plain_in_arr`).
    So the guard is decl-aware: `declares_type_name(toks, "Task")` (`:1671`), a
    token scan reached only when a type literally spells `Task(`. `Channel` needs
    no such test — `parse_type_d` has a dedicated `Channel` branch that shadows any
    user declaration, and tychoc does the same (measured: `struct Channel($T)` +
    `[Channel(int)]` is REJECTED by tychoc, `uChannel_gen_in_arr`).

  **4. BEFORE — the plan's 11 probes plus 43 more (`/tmp/ph20/p20.py`, `x.py`,
  `y.py`).** The plan's table is reproduced exactly; the four marked **RAN** are
  the ones that produced a working binary storing a handle the compiler promised
  could not be stored.

  | probe | form | tychoc FRONT | tychoc0 FRONT |
  |---|---|---|---|
  | dynarr_chan | `[Channel(int)]` local | REJECT | **ACCEPT** (CCFAIL) |
  | fixarr_chan | `[2]Channel(int)` local | REJECT | **ACCEPT** (CCFAIL) |
  | dynarr_chan_param | `[Channel(int)]` param | REJECT | **ACCEPT** (CCFAIL) |
  | fixarr_chan_param | `[2]Channel(int)` param | REJECT | **ACCEPT** (CCFAIL) |
  | struct_chan | `Channel(int)` struct field | REJECT `a struct field cannot be a channel` | **ACCEPT, COMPILED AND RAN** |
  | map_chan | `[string: Channel(int)]` | REJECT | **ACCEPT** (CCFAIL) |
  | opt_chan | `Option(Channel(int))` | REJECT | **ACCEPT, COMPILED AND RAN** |
  | tuple_chan | `(int, Channel(int))` | REJECT | **ACCEPT** (CCFAIL) |
  | chan_of_chan | `Channel(Channel(int))` | REJECT | **ACCEPT, COMPILED AND RAN** |
  | plain_task_ty | `Task(int)` written param type | REJECT `unknown type 'Task'` | **ACCEPT, COMPILED AND RAN** |
  | dynarr_task | `[Task(int)]` param | REJECT | **ACCEPT** (CCFAIL) |

  The extended sweep found the same fail-open in **29 further shapes**, eleven of
  which also COMPILED AND RAN: `res_ok_chan`, `res_err_chan`, `res_ok_task`,
  `opt_task`, `struct_task`, `enum_chan`, `enum_task`, `chan_of_task`,
  `task_of_chan`, `soa_chan`, `soa_task`, `generic_arg_chan`, `generic_arg_task`,
  `ret_task`, `newtype_chan`, `newtype_task`, `inf_opt_chan`, `inf_some_task`.
  CCFAIL in the rest: `fixarr_task`, `map_task`, `tuple_task`, `arr_opt_chan`,
  `map_arr_task`, `arr_tuple_chan`, `opt_arr_chan`, `arr_arr_chan`,
  `ret_arr_chan`, `fnty_param_chan`, `fnty_ret_chan`, `inf_arr_chan`,
  `inf_arr_task`, `inf_map_chan`, `inf_tuple_chan`, `inf_push_chan`,
  `inf_fixarr_chan`, `inf_arr_empty_chan`, `inf_tuple_task`, `inf_map_task`.

  ```
  BEFORE  p20.py 54 probes  DIVERGENT 40      x.py 17 probes  DIVERGENT 9
          y.py    4 probes  DIVERGENT  1      TOTAL 75 / 50
  ```
  (`p20.py` and `x.py` overlap on two probes whose first spelling was invalid —
  `newtype_chan` used `newtype C = ...` where the syntax is `type C = ...`, and
  `inf_struct_chan` omitted the channel; `x.py` carries the corrected forms.)

  **5. AFTER — same three harnesses, same probes.**
  ```
  AFTER   p20.py 54 probes  FRONTEND-DIVERGENT 0
          x.py   17 probes  FRONTEND-DIVERGENT 0
          y.py    4 probes  FRONTEND-DIVERGENT 0
          TOTAL  75 probes  FRONTEND-DIVERGENT 0
  ```
  Representative rows, tychoc → tychoc0:
  ```
  struct_chan       a struct field cannot be a channel        -> same text
  enum_chan         an enum payload cannot be a channel       -> same text
  opt_chan          a channel handle cannot be stored in a container or aggregate
                    -- pass it as an argument instead         -> same text
  fnty_param_chan   a function value cannot take a task or channel handle -> same text
  fnty_ret_chan     a function value cannot return a task or channel handle -> same text
  newtype_chan      (tychoc: newtype underlying restriction)  -> a newtype cannot wrap a channel
  plain_task_ty     unknown type 'Task'                       -> unknown type 'Task' -- a task
                                                                 handle is produced by `spawn f(...)`
  inf_arr_chan      a channel handle cannot be stored ...     -> same text (die_at, with caret)
  inf_some_task     a task handle cannot be stored ... wait(t) first -> same text
  bounded_chan / bounded_task (Phase 18) — still REJECT on both, unchanged
  ```

  **6. Legal-use control — NOT over-tightened.** Eight positive probes carried in
  the same sweep, all ACCEPT/cc-ok/correct output on **both** compilers, before
  and after, byte-identical:
  ```
  ok_chan_local    ch := channel(int,4); send; recv; close      -> "7"
  ok_chan_param    Channel(int) param + spawn + wait + recv     -> "3 3"
  ok_spawn_wait    t := spawn f(21); wait(t)                    -> "42"
  ok_arr_int / ok_opt_int / ok_struct_arr / ok_bounded_int / ok_generic_box
  uTask_gen_in_arr    struct Task($T) + [Task(int)]             -> "2"   (would have regressed
  uTask_gen_opt       struct Task($T) + Option(Task(int))       -> "1"    under a blanket
  uTask_plain_in_arr  struct Task + [Task]                      -> "1"    Task( reject)
  ```
  `make conc` (37 fixtures, the real channel/spawn/parallel-for corpus) and
  `make fixpoint` (tychoc0 compiling **itself** — its own source uses channels)
  are the load-bearing tripwires and both stayed green.

  **7. Fixtures — 22 new, one file per distinct rejection** (the compiler halts at
  the first error, so they cannot be merged). Test count **452 → 474**. Each was
  hand-checked against **both** compilers before the gate run: 22/22 reject on
  both, with a non-empty diagnostic from tychoc.
  ```
  tests/reject/affine_chan_arr_elem.ty        [Channel(int)]
  tests/reject/affine_chan_fixarr_elem.ty     [2]Channel(int)
  tests/reject/affine_chan_map_val.ty         [string: Channel(int)]
  tests/reject/affine_chan_tuple_elem.ty      (int, Channel(int))
  tests/reject/affine_chan_opt_inner.ty       Option(Channel(int))          <- RAN before
  tests/reject/affine_chan_result_inner.ty    Result(Channel(int), string)  <- RAN before
  tests/reject/affine_chan_struct_field.ty    struct field                  <- RAN before
  tests/reject/affine_chan_enum_payload.ty    enum payload                  <- RAN before
  tests/reject/affine_chan_of_chan.ty         Channel(Channel(int))         <- RAN before
  tests/reject/affine_chan_generic_arg.ty     Box(Channel(int))             <- RAN before
  tests/reject/affine_chan_newtype.ty         type C = Channel(int)         <- RAN before
  tests/reject/affine_chan_soa_field.ty       soa[S], S has a chan field    <- RAN before
  tests/reject/affine_chan_fnty_param.ty      fn(Channel(int)) -> int
  tests/reject/affine_chan_fnty_ret.ty        fn(int) -> Channel(int)
  tests/reject/affine_task_written_type.ty    Task(int) as a written type   <- RAN before
  tests/reject/affine_task_arr_elem.ty        [Task(int)]
  tests/reject/affine_chan_infer_arr.ty       cs := [ch]            (inferred)
  tests/reject/affine_task_infer_arr.ty       ts := [spawn f(1)]    (inferred)
  tests/reject/affine_chan_infer_map.ty       m := ["a": ch]        (inferred)
  tests/reject/affine_chan_infer_tuple.ty     p := (1, ch)          (inferred)
  tests/reject/affine_chan_infer_some.ty      o := Some(ch)         (inferred) <- RAN before
  tests/reject/affine_chan_infer_push.ty      cs := []; push(cs,ch) (inferred, no type anywhere)
  ```

  **8. Gates — all seven green, each its own foreground `env -u LD_PRELOAD make …`;
  tychoc0 built to `/tmp/ph20/`, outside the tree:**
  ```
  test        passed: 474   failed: 0  /  all green          (was 452; +22 fixtures)
  corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
  conc        conc: passed 37   failed 0
  fixpoint    ok  B == C : tychoc0 reproduces itself byte-identically (35081 lines C)
              ok  split tychoc0 (2 packages) self-hosts E==F and matches the single-file compiler
              fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
  ilp32       passed: 474   failed: 0  /  all green
  spec-check  spec-examples: 7 runnable example(s), all pass
  check-links link check: ok (121 markdown files, no dead relative links)
  ```
  `git status --short`: `compiler/tychoc0.ty` modified + the 22 new fixtures only.
  No build spill.

  **Residual uncertainty.** `declares_type_name` matches `struct|enum|type Task`
  anywhere in the token stream, so a program that declares `Task` in *one*
  package of a multi-package bundle re-opens the written-`Task` hole for the
  others. That is strictly narrower than the pre-phase behaviour (which was open
  unconditionally), and it needs a decl table the parser does not have; a
  program that declares a type named `Task` at all is the exotic case the guard
  exists to protect. Not fixture-locked.

- [x] **Phase 21 — CLOSED, WILL NOT DO (user decision 2026-07-25): the deferred const-size encodings leak into diagnostics outside a local declaration (observed by Phase 18)**
  - **Closed without doing it, deliberately.** User confirmed closure on 2026-07-25.
    Reason: cosmetic. The internal `[#W]`/`[b#W]` spellings appear in *diagnostic text* in
    some positions; the accept/reject **decision agrees** on both compilers, which is the
    only property the spec makes normative (`00-conventions.md` §1.3). It is in the same
    non-normative class as the 75 wording divergences Phase 2 measured and Phase 3
    deliberately declined to gate — closing it is consistent with that pre-registered
    decision rather than an exception to it.
  - Reopen if the encodings start leaking into a position where they mislead rather than
    merely look internal.
  - Both `[#W]T` (fixed array, `compiler/tychoc0.ty:1692`) and `[b#W]T`
    (`bounded`, `:1734`) are parser-internal encodings resolved by `mangle_type`.
    When the name is not a `const` anywhere in the program the mangle pass never
    runs (`dofold`, `:3451`), the encoding survives, and only the `STypedDecl`
    arm (`:11205-11212`) translates it into the message tychoc gives.
  - In a **param**, **field** or **return** position the raw spelling leaks:
    `type: unknown type '#NOPE]in'` / `'b#NOPE]in'`, and in return position
    `returning [2]int but this function returns [#NOPE]int`. Measured
    2026-07-25 (`/tmp/ph18/p18c.py`, 11 probes): the DECISION agrees with tychoc
    in every case — this is a diagnostic-quality defect, not a divergence, which
    is why Phase 18 did not absorb it.
  - tychoc says the right thing in all positions because it dies at parse
    (`src/tychoc.c:1734-1735`, `:1818-1820`).
  - Scope when taken: hoist the two guards out of the `STypedDecl` arm to
    wherever a written type is first checked, so param/field/return get the same
    message. Add `tests/diag/` fixtures (`.err` + `.h0err`) for one position each,
    the mechanism Phase 3 established for locking message text.
  - Done when: all four positions give tychoc's wording on both compilers, the
    diag fixtures lock it, full gate set green.

- [x] **Phase 22 — tychoc0 does not restrict a newtype's UNDERLYING type the way tychoc does (found by Phase 20, out of its scope)** — **CLOSED BY REFERENCE, 2026-07-25: measured in full by Phase 25 (row H2), fix re-filed as Phase 27.** The "not known" list below is now known: `type C = (int,int)` / `Option(int)` / `Result(int,string)` / `fn(int)->int` / an enum / `soa[P]` / another newtype / `ptr` / `bytes` / `u8` / `f32` are ALL rejected by tychoc and ALL accepted by tychoc0, and eight of the eleven compile and run. The legal set (`int`, `float`, `string`, `bool`, `[int]`, `[2]int`, `[string:int]`, a struct, `bounded[4]int`) agrees on both. Evidence: `docs/internals/frontend-restriction-audit-2026-07-25.md` §5 row H2. The diagnostic-quality observation about `soa[Channel(int)]` carries forward into Phase 27's scope. This box is ticked because the MEASUREMENT this phase asked for is done; the FIX is Phase 27 (Phase 25 was measurement-only by ruling).
  - Phase 20 needed one newtype probe (`type C = Channel(int)`) and found that
    tychoc rejects it with a *general* rule — `a newtype's underlying type must
    be int, float, string…` (measured 2026-07-25, `/tmp/ph20/x.py`) — while
    tychoc0 had no such rule at all and accepted it, compiled it and ran it.
  - Phase 20 closed **only the affine-handle case** (`compiler/tychoc0.ty:2799`,
    `a newtype cannot wrap a channel`, tychoc's `resolve_program` wording from
    `src/tychoc.c:7020-7022`), because that is what its scope named. The rest of
    tychoc's underlying-type restriction is unmeasured: whether `type C = [int]`,
    `type C = (int, int)`, `type C = Option(int)`, `type C = SomeStruct`,
    `type C = fn(int)->int` and friends agree is **not known**, and every one of
    them is a potential fail-open of the same class.
  - Scope when taken: read tychoc's newtype underlying-type check, sweep the
    whole underlying-type space with the FRONT/CC/RUN harness, fix whichever side
    the spec supports, and fixture-lock each distinct rejection.
  - Also fold in one diagnostic-quality observation from the same probe:
    `soa[Channel(int)]` is rejected by both, but tychoc says `soa requires a
    struct element type` where tychoc0 now says the affine-container message
    (before Phase 20 it said `codegen: soa of unknown struct`). Decision agrees;
    text does not. Phase 2's divergence set, not a fail-open.
  - Done when: the underlying-type decision agrees on both compilers across the
    swept space, each distinct rejection is fixture-locked, full gate set green.

- [x] **Phase 23 — CLOSED, WILL NOT DO (user decision 2026-07-25): tychoc0's array-family name collision: `[bounded[N]T]` and `bounded[N]T` mangle to the same `Arr_*` (measured by Phase 19, out of its element-type scope)**
  - **Closed without doing it, deliberately.** Phase 19 measured it, judged it
    disproportionate (~22 mangling sites) and filed rather than forced it; the user
    confirmed closure on 2026-07-25. Reason: it needs a coordinated rename across ~22
    mangling sites in the self-hosting compiler, and the reachable symptom is a
    container-of-`bounded` position that Phase 19 already reduced to 16 rows from 53 —
    pre-existing, and it affects plain `[[2]int]` too, so it is not `bounded`-specific.
    Reopen if the `Arr_*` mangling is being reworked anyway (Phase 33 touches the same
    family; check whether its fix collapses this for free before doing it standalone).
  - `compiler/tychoc0.ty:3801-3804` — `afam(ty)` returns `"Arr_" + mangle(ty)` for
    an inline array (`bounded[N]T` / `[N]T`) but `"Arr_" + mangle(elem_ty(ty))`
    for a dynamic `[X]`. `mangle(bounded[4]int)` is `b4_int`, so the family for
    the type `bounded[4]int` and the family for the *dynamic array of it*,
    `[bounded[4]int]`, are both `Arr_b4_int`. tychoc0 then emits
    `Arr_b4_int_push(&_scope, &h_xs, Arr_b4_int_copy(&_scope, h_a))` where the
    third argument is a whole `Arr_b4_int` but the parameter is `tycho_int`:
    `error: incompatible type for argument 3 of 'Arr_b4_int_push'`.
  - Measured 2026-07-25 (`/tmp/ph19/probe.py`, FRONT/CC/RUN): 15 of the 16
    remaining divergences are this — `[bounded[4]T]` with **every** element type
    T (int, float, string, enum, struct, tuple, bytes, `[2]int`, `[int]`,
    `[K:V]`, nested bounded, Option, Result, soa, fn). tychoc is `ok` for all 15.
    The 16th, `[bounded[4]int: string]` (a `bounded` map key), is the same root
    cause surfacing as an undeclared `Arr_int_hash`.
  - **Not a `bounded` element-type bug** — it is a *container-of-inline-array*
    bug and predates Phase 19 (it already hit `[bounded[4]int]` with a plain `int`
    element). It also hits `[[2]int]`, a dynamic array of a fixed array.
  - Scope when taken: give the dynamic family a name derived from the ARRAY type
    when its element is an inline array (`Arr_arr_b4_int`), consistently across
    the ~22 `"Arr_" + mangle(...)` sites (`gen_arr_type`, `gen_arr_fns`, the
    driver's forward typedefs / copy protos / str protos / hash protos, `afam`,
    `cty`). Programs that use `[bounded[N]T]` or `[[N]T]` do not compile today, so
    the rename cannot regress a working program — but it is emitted-text churn and
    `make fixpoint` is load-bearing.
  - Done when: `[bounded[N]T]`, `[[N]T]` and a `bounded` map key compile and run
    identically on both compilers, with fixtures; full gate set green.

- [x] **Phase 24 — CLOSED, WILL NOT DO (user decision 2026-07-25): `runtime/tycho_rt.c` has 4 real `-Wmisleading-indentation` warnings under the project's own flags (found by Phase 14, out of its scope)**
  - **Closed without doing it, deliberately.** User confirmed closure on 2026-07-25.
    Reason: 4 `-Wmisleading-indentation` warnings are cosmetic, and the surface they sit
    on is ungated by design — the runtime is never compiled by `make`, only embedded as a
    string. **The real finding here is the gate gap, not the four warnings**, and that is
    Phase 38 (no gate builds `tychoc` itself under a sanitizer either). If Phase 38 is
    taken, decide the runtime's lane alongside it and these four fall out for free;
    fixing them standalone leaves the blind spot untouched.
  - Phase 14 owned the *compiler's* own build (`src/tychoc.c`, `Makefile:31-32`),
    which is now 0 warnings. The runtime is a different surface: `Makefile:23-26`
    never compiles `runtime/tycho_rt.c` — it `awk`s it into a C string literal
    (`build/tycho_rt_embed.h`), and the bytes are only ever compiled as part of an
    *emitted* program, on the codegen `cc` line inside `src/tychoc.c`. So the
    runtime's warning-cleanliness under `-Wall -Wextra` is currently unmeasured by
    any gate, in either direction.
  - Measured on 2026-07-25 (probe, not a change):
    ```
    $ cc -O2 -fwrapv -Wall -Wextra -std=c11 -c runtime/tycho_rt.c -o /tmp/ph14/rt.o
    exit=0  warnings=37  errors=0
      33  [-Wunused-function]
       4  [-Wmisleading-indentation]   runtime/tycho_rt.c:2380, :2390, :2400, :2410
    ```
  - **Only the 4 are real.** The 33 `-Wunused-function` are an artifact of the
    probe: every runtime helper is `static`, and compiling the file standalone
    means nothing calls them. In real use the runtime is inlined into a program
    that uses a subset, so that class is expected there too and is not evidence of
    dead code. The 4 `-Wmisleading-indentation` are genuine source hygiene in the
    runtime itself and are independent of how it is compiled.
  - NOT absorbed into Phase 14 (scope lock: Phase 14's scope is the compiler's own
    build). Also distinct from Phase 4, which scored the *emitted* C of specific
    fixtures, not the embedded runtime prelude.
  - Scope when taken: read the four sites, fix the indentation (or the control
    flow, if the indentation is telling the truth and the braces are not — check
    that first, `-Wmisleading-indentation` occasionally finds a real bug). Decide
    separately whether a gate should compile the runtime standalone with
    `-Wall -Wextra -Wno-unused-function`; that is a new gate and new failure
    surface, so it is a judgement call, not automatic.
  - Done when: the 4 `-Wmisleading-indentation` sites are diagnosed as cosmetic or
    real (with the reasoning written down) and fixed; full gate set green.

- [x] **Phase 25 — SYSTEMATIC AUDIT: every frontend restriction tychoc enforces, checked against tychoc0 (user-ordered, 2026-07-25)**
  - **Why this exists.** Six consecutive phases each found another instance of the
    same shape: tychoc enforces a restriction, tychoc0 does not, and tychoc0
    fail-opens — Phase 9 (`str`/`void` as type names), Phase 15 (non-bool
    conditions), Phase 18 (`bounded` capacity/element), Phase 20 (affine handles
    in containers — 50 divergences from an 11-shape sample), Phase 22 (newtype
    underlying types). Discovering these one at a time is not converging. The
    ruling is to stop chasing instances and enumerate the population.
  - **Root asymmetry, established by Phase 20 (`187b9d3`):** tychoc *interns*
    types — every composite is built through a handful of find-or-create
    functions, so one check per constructor covers every spelling of that
    restriction. tychoc0 has no intern step: a type IS its string spelling, built
    ad hoc at many sites. So tychoc's restrictions are structurally centralized
    and tychoc0's are structurally scattered — which is exactly why tychoc0 keeps
    missing whole categories rather than individual cases.
  - Scope: **enumerate, then measure, then trip.** Not a fixing phase.
    1. Enumerate every frontend restriction tychoc enforces. Work from tychoc's
       structure, not from memory: its type interners (Phase 20 found seven —
       `chan_of`, `arrc_sized_b`, `opt_of`, `res_of`, `tup_of`, `mapc_of`,
       `func_of`), its `resolve_program` declaration scan, its `die_at` call sites,
       and its reject-fixture corpus. Each becomes a row: what is forbidden, where
       tychoc enforces it, the spec clause requiring it (or "unspec'd").
    2. For each row, construct a probe and run it through BOTH compilers using the
       FRONT/CC/RUN method (`--emit-c` both sides + separate `cc`; **never** raw
       `rc` — see Phase 11's method note). Record the frontend verdict per side.
    3. Classify: AGREE / tychoc0-FAIL-OPEN / tychoc-FAIL-OPEN / needs-a-ruling.
       Flag separately any fail-open whose emitted C **compiles and runs** — those
       are the dangerous ones (Phase 20 found four).
  - **Deliverable is the measurement**, written to
    `docs/internals/frontend-restriction-audit-<date>.md`: the full table, the
    method as code, and totals. A table showing zero new divergences is a
    legitimate and valuable outcome.
  - Non-scope: **fixing**. Fixes are separate phases appended from the findings,
    prioritised fail-open-that-runs first. Resist fixing as you go — Phase 20
    showed that fixing per-instance produces scattered ad-hoc checks, and the
    right fix may be one shared choke point covering many rows at once.
  - Subsumes Phase 22 (newtype underlying types); close 22 by reference if the
    audit covers it.
  - Done when: the audit doc lists every enumerated restriction with a probe and a
    both-compiler verdict; the enumeration method is stated so a reader can judge
    its completeness; every divergence is filed as a new phase, ranked by whether
    it runs; full gate set green (this phase changes no compiler source).

  **DONE 2026-07-25. Measurement only — NO compiler source touched.** Deliverable:
  `docs/internals/frontend-restriction-audit-2026-07-25.md`.

  **1. Enumeration method (four passes, from tychoc's structure, not from memory).**
  1. A script walked `src/tychoc.c` (11 689 lines) attributing every `die_at(` /
     `die(` / `*_err(` call to its enclosing function: **501 sites**
     (`/tmp/ph25/dieat.txt`).
  2. The population was restricted to the layer where the intern/no-intern asymmetry
     lives — the seven type interners (`chan_of :610`, `arrc_sized_b :668`,
     `opt_of :751`, `res_of :774`, `tup_of :837`, `mapc_of :1004`, and the seventh,
     which is **`funcc_of :1027`**, not `func_of` as Phase 20's evidence names it —
     its guards at `:1030`/`:1032` are exactly where Phase 20 put them),
     `parse_type_inner` (26 sites), the declaration parsers (`parse_fn` 9,
     `parse_extern_fn` 3, `parse_handle` 3, `parse_struct` 6, `parse_enum` 7,
     `parse_typedecl` 3) and `resolve_program`'s declaration scan (14). The 188
     `resolve_expr_inner` + 69 `resolve_stmt` sites are the Phase 2/3 diagnostic-parity
     domain and are excluded by name, not by omission. **60 distinct rules after
     grouping.**
  3. Every row was looked up in `docs/spec/`; a row with no clause is marked
     `unspec'd` and is itself a finding.
  4. **Completeness cross-check:** all 182 single-file `tests/reject/` fixtures were run
     through tychoc and their diagnostics collected (`/tmp/ph25/rjmsgs.json`) — 182
     fixtures, **135 distinct messages**, every one mapped back to an enumerated rule.
     **No fixture exercised a rule the enumeration had missed.**

  **The structural filter that makes 60 a complete rather than arbitrary boundary:**
  `tests/run.sh:150-164` runs BOTH compilers over every `tests/reject/*.ty` and fails
  with `"tychoc0 ACCEPTED an invalid program (fail-open)"`. So a rule with a reject
  fixture **cannot** be a live fail-open — the gate would already be red. The candidate
  population is exactly the rules tychoc enforces that have no fixture.

  **2. Measurement.** FRONT/CC/RUN, `--emit-c` on BOTH sides plus a separate `cc`;
  never a raw `rc` comparison. Harness `/tmp/ph25/probe.py` (Phase 11/18/20's, retargeted),
  probe sets `/tmp/ph25/rows.py` (76 probes) and `/tmp/ph25/rows2.py` (37 probes, the
  re-probes for rules whose first probe tripped an earlier error, plus `…_used` variants
  that actually exercise the accepted construct). tychoc0 built to `/tmp/ph25/`.

  **3. TOTALS.**
  ```
  rules enumerated                                  60
    AGREE                                           40
    tychoc0-FAIL-OPEN (spec requires the reject)    15
    NEEDS-A-RULING    (spec silent, both differ)     4
    tychoc-FAIL-OPEN                                 0
    not probed (E2, >256 handle types)               1
  probes executed                                  113   (76 + 37)
  DIVERGENT ROWS WHOSE EMITTED C COMPILES AND RUNS  18 of 19
  ```

  **4. The dangerous (running) fail-opens, by name.** Only `E1` (duplicate `handle`
  name) stops at CCFAIL. Every other divergence produces a working binary:
  - **I10 `main` signature** — `fn main(x: int):` compiles and runs on tychoc0.
    `15-program.md:27-32` makes this a MUST-reject and even names reference sites.
  - **H2 newtype underlying type** — eleven forbidden underlying types accepted
    (`(int,int)`, `Option`, `Result`, `fn(…)`, an enum, `soa[P]`, another newtype,
    `ptr`, `bytes`, `u8`, `f32`); eight run. `03-types.md:317-321` is an explicit
    MUST-NOT list. **This is plan Phase 22, now measured in full.**
  - **I6 `inout Channel(T)` parameter** — accepted, runs, and prints `Some(7)` where
    the program is supposed to be rejected: an aliasing rule and a semantic divergence
    in one.
  - **D2 `extern fn` struct parameter** — `14-ffi.md:21-22,:38-39` rejects a struct at
    the C boundary; tychoc0 emits the call and it runs.
  - **B20 / B21 partial generic type arguments** (`05-generics.md:76-83`).
  - **The arity family, all RUN** — B6 `fn(…)` > 8 params, B8 tuple > 8, B9 tuple < 2,
    B11 > 16 type params, B12 > 16 size params, C5 > 8 `where` constraints, C7 > 16
    types in a `where` type set, F2 > 8 struct type params, G2 > 8 enum type params,
    G5 > 8 enum payload fields. Every one of these is a fixed-size array bound in
    tychoc (`Type params[8]`, `g_cur_typarams[16]`, …); tychoc0 has no equivalent.
  - **I7 > 16 function parameters** (runs, prints `18`) and **I8 `inout` function
    value** — both unspec'd, both run.

  **No memory-unsafety was found.** Every divergence is accepted-when-it-should-be-
  rejected; none produced an out-of-bounds read or write. The arity rows are the
  closest call — they are bounds-keeping limits — but tychoc's arrays are never
  reached, because tychoc is the side that rejects.

  **5. A second class the sweep surfaced (both frontends AGREE, tychoc0's C does not
  build):** `type C = [2]int` / `[string:int]` / `bounded[4]int` used as a parameter
  type — tychoc0 emits `unknown type name 'Arr_f2_int'` / `'Map_str_int'` /
  `'Arr_b4_int'`, the mangled aggregate name without its declaration. Phase 19/23
  family. Filed as Phase 33.

  **6. Incidental doc drift:** `docs/spec/15-program.md:31-32` cites
  `src/tychoc.c:6354-6355` and `:6379-6380` for the `main`-signature rules; those lines
  are now `s->decl_type = t; vars_push(…)` and the `declared type %s but value is %s`
  diagnostic. The live sites are `:7097-7098` and `:7123-7124`. Filed as Phase 34.

  **7. Gates — all seven green, each its own foreground `env -u LD_PRELOAD make …`:**
  ```
  test        passed: 478   failed: 0  /  all green
  corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
  conc        conc: passed 37   failed 0
  fixpoint    ok  B == C : tychoc0 reproduces itself byte-identically (35141 lines C)
              ok  split tychoc0 (2 packages) self-hosts E==F and matches the single-file compiler
              fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
  ilp32       passed: 478   failed: 0  /  all green
  spec-check  spec-examples: 7 runnable example(s), all pass
  check-links link check: ok (122 markdown files, no dead relative links)   (121 -> 122, this doc;
              scripts/check_links.sh:51 counts `git ls-files '*.md'`, so it only rises once staged)
  ```
  `git status --short`: the new audit doc + `plan.md` only. No build spill. All probe
  artefacts and the tychoc0 build stayed in `/tmp/ph25/`.

  **Residual uncertainty.** (a) The audit does not cover the 257 expression/statement
  abort sites, the lexer, runtime traps, multi-package programs, or output equivalence
  beyond "did the binary print something" — §9 of the doc states this explicitly.
  (b) `E2` (> 256 handle types) was not probed. (c) The `tychoc-FAIL-OPEN = 0` result is
  measured — the harness flags both directions — but only over these 60 rows.

- [x] **Phase 26 — `char_at(s, i) -> char`, the ergonomics helper (Phase 6's ruling (c))**
  - **UNBLOCKED 2026-07-25.** The open question recorded under Phase 6 was put to
    the user, who confirmed: **`char_at(s, i) -> char`** — the string-index helper
    returning a `char`, so `char_at(s, 1) == 'e'` is expressible. NOT a char→int
    conversion (that already exists as `to_int(char)` / `to_u32(char)`,
    `docs/spec/03-types.md:78-79`, and adding a duplicate was explicitly rejected).
  - **`s[i]` semantics are UNCHANGED and that is the point of ruling (c)**:
    `s[1]` still yields `int` (`101` for `"hello"`), so no existing program
    changes meaning. The helper sits alongside it.
  - Bounds behaviour MUST match `s[i]`: `docs/spec/03-types.md:86` says indexing
    "aborts if `i` is out of bounds". `char_at` aborts identically — do not invent
    a different failure mode (no `Option`, no clamp, no sentinel) without a
    separate ruling.
  - Scope when unblocked: add the helper to both compilers' builtin tables plus
    the runtime if it needs one, spec it in `docs/spec/16-builtins.md` and
    `appendix-d-builtins.md`, note the `s[i] -> int` rationale in
    `docs/spec/03-types.md` §5.2.4 so the wart is explained rather than silent,
    and fixture it on both compilers.
  - Done when: the helper compiles and runs identically on both compilers, the
    spec documents it and the `s[i]` rationale, `s[i]` semantics are UNCHANGED
    (that is the whole point of ruling (c)), full gate set green.
  - **DONE 2026-07-25. Evidence below.**

  **Template builtin traced end to end before writing anything.** The task asked
  for a `(string, int) -> scalar` builtin; none exists, so two were traced and
  merged: `find` (`(string,string) -> int`, a `Sig` builtin with a `hi_`-prefixed
  tychoc0 twin) for the *dispatch* shape, and the `s[i]` E_INDEX path for the
  *bounds* shape. Every line below was opened and quoted, not recalled:

  | layer | tychoc | tychoc0 |
  |---|---|---|
  | builtin table | `src/tychoc.c:4103-4133` `register_builtins`; `find` at `:4119` | `compiler/tychoc0.ty:2830` `is_builtin_call` |
  | UFCS eligibility | `src/tychoc.c:4464-4472` `is_ufcs_builtin` | `compiler/tychoc0.ty:2838` `is_ufcs_builtin` ("kept byte-identical", `:2836`) |
  | purity | `src/tychoc.c:6289-6296` `is_pure_builtin` | `compiler/tychoc0.ty:6077` `is_pure_bi` |
  | return type | from the `Sig` row (`.ret`), checked at `src/tychoc.c:5476-5522` | `compiler/tychoc0.ty:4808+` `sig_ret` (a string-returning `if` ladder) |
  | arg checking | generic, from `.params[]`: `src/tychoc.c:5488-5522` | **none for most builtins** — `check_call_args` (`:11040`) only special-cases the numeric conversions |
  | codegen | `gen_call`, `src/tychoc.c:8174-8178` (`find` → `tycho_str_find`) | `gen_expr`'s `ECall` ladder, `compiler/tychoc0.ty:6790-6791` (`find` → `hi_find`) |
  | runtime helper | `runtime/tycho_rt.c` — `s[i]` uses `tycho_str_get` `:1037-1044` (bounds check + `exit(1)`) | none: tychoc0 **emits** its own `hi_sidx` as C text, `compiler/tychoc0.ty:10237` — byte-identical predicate and message to `tycho_str_get` |
  | `s[i]` emit | `src/tychoc.c:8653` `tycho_str_get(a, ix)` | `compiler/tychoc0.ty:6337` `hi_sidx(base, idx)` |
  | spec | `docs/spec/16-builtins.md:125-131` (§29.5 table) + its `> Provenance:` line | — |

  **The key finding from that trace, and the design it dictated.** `char` is
  carried as `tycho_int` in C on both sides (`src/tychoc.c:1210`
  `case T_CHAR: return "tycho_int ";`; `compiler/tychoc0.ty:4662`). So
  `char_at(s, i)` needs **no new runtime helper and no new emitted C**: it emits
  *literally the same call* `s[i]` emits, and only the STATIC type differs. That
  makes "aborts identically to `s[i]`" true by construction rather than by a
  parallel bounds check that could drift.

  **Changes.**
  - `src/tychoc.c:4120` — `Sig` row `.name="char_at", .ret=T_CHAR, .params={ T_STRING, T_INT }, .nparams=2`. Arity + arg types are then checked by the generic `Sig` path for free.
  - `src/tychoc.c:4466` — added to `is_ufcs_builtin` (receiver-first, like `substr`/`find`), so `s.char_at(i)` works.
  - `src/tychoc.c:6291` — added to `is_pure_builtin` (discarding it in statement position is a no-op).
  - `src/tychoc.c:8179-8187` — codegen: `tycho_str_get(s, ix)`, the same call `:8653` emits for `s[i]`.
  - `compiler/tychoc0.ty:2830`,`:2838`,`:6078` — the three name lists (`is_builtin_call`, `is_ufcs_builtin`, `is_pure_bi`).
  - `compiler/tychoc0.ty:4827-4828` — `sig_ret` returns `"char"`.
  - `compiler/tychoc0.ty:6792-6797` — codegen: `hi_sidx(s, ix)`, the same helper `:6337` emits for `s[i]`.
  - `compiler/tychoc0.ty:11064-11075` — a targeted arg-type check in `check_call_args`. **Needed**: tychoc0 has no `Sig` table, so without it `char_at(5, 0)` fail-opened where tychoc rejects, and a `tests/reject/` fixture (which requires BOTH compilers to reject, `tests/run.sh:159-160`) could not pass.
  - NOT changed: `s[i]`'s type, its codegen, or the runtime. `runtime/tycho_rt.c` is untouched.

  **Probe — `char_at` works identically on both compilers** (`/tmp/ph26/probe.ty`;
  tychoc0 built as instructed with `./tychoc compiler/tychoc0.ty -o /tmp/ph26/tychoc0`,
  its C compiled with `cc -O2 -fwrapv -std=c11 … -lm`). Byte-identical output:
  ```
  $ ./tychoc /tmp/ph26/probe.ty -o /tmp/ph26/probe && /tmp/ph26/probe
  char_at(s,1) == 'e' : yes
  s[1] = 101
  char_at first=h last=o
  ufcs=o
  $ /tmp/ph26/tychoc0 /tmp/ph26/probe.ty > probe0.c && cc … && ./probe0
  char_at(s,1) == 'e' : yes
  s[1] = 101
  char_at first=h last=o
  ufcs=o
  ```
  Both halves of the ruling are in that output: `char_at("hello", 1) == 'e'` is
  **true**, and `s[1]` still yields **`101`**.

  **`s[i]` UNCHANGED — proof, not assertion.** Three independent checks:
  1. the probe above prints `s[1] = 101` on both compilers, unchanged;
  2. `tests/char_at.ty` locks `str(s[1])` → `101` and `str(to_int(char_at(s,1)))`
     → `101` **in the same golden**, so the two can never silently converge;
  3. no edit touches the `s[i]` path — `src/tychoc.c:8653` and
     `compiler/tychoc0.ty:6337` are byte-for-byte as before (`git diff` over both
     files shows only the additive hunks listed above), and `make test` /
     `make ilp32` pass all 478 pre-existing fixtures unchanged.

  **Bounds abort — identical to `s[i]`, demonstrated.** `char_at(s, 9)` vs
  `s[9]` on `"hello"`, all four binaries:
  ```
  --- tychoc  char_at(s,9) ---  tycho: string index 9 out of bounds (len 5)   exit=1
  --- tychoc0 char_at(s,9) ---  tycho: string index 9 out of bounds (len 5)   exit=1
  --- tychoc  s[9]         ---  tycho: string index 9 out of bounds (len 5)   exit=1
  --- tychoc0 s[9]         ---  tycho: string index 9 out of bounds (len 5)   exit=1
  ```
  Same message, same exit status, all four. This is now locked by
  `tests/abort/char_at_oob.ty`, and the abort harness (`tests/run.sh:191-218`)
  asserts the two compilers' stderr match **byte-for-byte**, not merely that both
  die. No `Option`, no clamp, no sentinel — the existing abort path, reused.

  **Wrong-typed argument fails closed on both** (the two `tests/reject/` fixtures):
  ```
  tychoc : char_at(5, 0)     -> argument 1 of 'char_at' is int, expected string -- wrap it with str(...), e.g. char_at(str(x))
  tychoc0: char_at(5, 0)     -> argument 1 of 'char_at' is int, expected string
  tychoc : char_at(s, "x")   -> argument 2 of 'char_at' is string, expected int
  tychoc0: char_at(s, "x")   -> argument 2 of 'char_at' is str, expected int
  ```
  (The `is int, expected string -- wrap it with str(...)` tail is tychoc's
  pre-existing F6 hint for any `Sig` builtin's string parameter,
  `src/tychoc.c:5518-5520`; `char_at` inherits it rather than adding a special
  case. The `string`/`str` spelling difference is the known, pre-existing
  cross-compiler diagnostic divergence measured by Phase 2, not new here.)

  **Spec sections added.**
  - `docs/spec/16-builtins.md` §29.5 — the `char_at(s, i)` table row, a paragraph
    stating it is the same byte read as `s[i]` and that
    `to_int(char_at(s, i)) == s[i]` for every in-range `i`, and the
    `> Provenance:` citation extended (house style — the neighbours carry one).
    Every line number in it was opened and verified after the final edit.
  - `docs/spec/03-types.md` §5.2.4 (`char`) — indexing does not produce a `char`;
    `char_at` is the `char`-typed reader; `s[1] == 'e'` is a type error under §13.2.
  - `docs/spec/03-types.md` §5.2.5 (`string`) — **the wart, explained rather than
    silent**, as the ruling intended: why `s[i]` yields `int` and MUST keep
    yielding it (`s[i] ± n` would wrap to `0..255` under a `char` result — a
    silent change of meaning for existing code), what that costs, and how
    `char_at` closes the gap at zero performance difference.
  - `docs/spec/17-runtime.md` §30.2 — the abort set's "String index out of
    bounds" entry now names `char_at(s, i)` alongside `s[i]`.
  - `docs/spec/appendix-d-builtins.md` — alphabetical locator row.
  - `docs/spec/appendix-e-conformance.md` — **builtins DO carry conformance rows**
    (checked: §29.3/§29.5/§29.6/§29.12 all have one, keyed by clause not by
    builtin), so a §29.5 row was added naming all four new fixtures.

  **Fixtures (4 new; test count 478 → 482).**
  - `tests/char_at.ty` + `.out` — normal index, index 0, last index, comparison
    against a char literal, `str(char)` vs `str(int)` side by side, `to_int`
    round-trip back to `s[i]`, char arithmetic, ordering, the UFCS form, and an
    accumulate-into-string loop. Runs on both compilers, goldens matched.
  - `tests/abort/char_at_oob.ty` — the runtime bounds abort. This is a **runtime
    abort, not a reject**, and the harness does have a mechanism for it
    (`tests/abort/`, `tests/run.sh:191-218`), which also asserts byte-identical
    stderr between the two compilers.
  - `tests/reject/char_at_arg_index_type.ty`, `tests/reject/char_at_arg_recv_type.ty`.

  **Gate set — each run as its own foreground `env -u LD_PRELOAD make …`:**
  ```
  make test        passed: 482   failed: 0        all green          (478 + 4 new)
  make corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
  make conc        conc: passed 37   failed 0
  make fixpoint    ok  B == C : tychoc0 reproduces itself byte-identically (35167 lines C)
                   fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
  make ilp32       passed: 482   failed: 0        all green
  make spec-check  spec-examples: 7 runnable example(s), all pass
  make check-links link check: ok (122 markdown files, no dead relative links)
  ```
  `git status --short` clean of build spill (7 modified sources/specs + the 5 new
  fixture files, nothing else).

  **Note on `make fixpoint`, per the preamble's CORRECTION.** It is green, and
  that is meaningful but narrower than "the two compilers agree": it proves
  tychoc0 reproduces *itself* byte-identically after the change. Cross-compiler
  agreement on `char_at` is proven separately, by the probe above, by
  `tests/char_at`'s shared golden, and by the abort fixture's byte-identical
  stderr assertion.

### Filed by Phase 25's audit (2026-07-25), ranked fail-open-that-RUNS first

Ranking rule: a fail-open whose emitted C **compiles and runs** outranks one that
CCFAILs, and a row the spec already decides outranks one that needs a ruling. All of
27–31 are spec-backed and all of them RUN. Every row id below indexes
`docs/internals/frontend-restriction-audit-2026-07-25.md` §5.

**Consider doing 27–31 as ONE structural change rather than five.** Phase 20's lesson
was that fixing per-instance produces scattered ad-hoc checks; its own fix collapsed 50
divergences into two shared guards (`ck_affine_part`, `ck_affine_inferred`) because
tychoc0 has exactly two places a composite type string is built. Rows 27–31 are all
*declaration-shape* rules rather than type-construction rules, so they may collapse the
same way onto tychoc0's declaration parsers. Check that before writing five checks.

- [x] **Phase 27 — tychoc0 has no newtype UNDERLYING-type restriction (audit row H2; supersedes Phase 22)**
  - tychoc: `src/tychoc.c:3719-3721` — `U` must be `int`/`float`/`string`/`bool`/an
    array/a map/a struct. Spec: `docs/spec/03-types.md:317-321`, an explicit MUST-NOT
    list. tychoc0 has only the affine-handle case Phase 20 added
    (`compiler/tychoc0.ty:2799`).
  - Eleven forbidden underlying types accepted by tychoc0; **eight compile and run**:
    `Option(int)`, `Result(int,string)`, an enum, `soa[P]`, another newtype, `ptr`,
    `bytes`, `u8`, `f32` run; `(int,int)` and `fn(int)->int` CCFAIL.
  - Note `soa` is in neither the spec's permitted nor its forbidden list — it is
    excluded by "MUST be one of". Say so in the fix, or add it to the forbidden list.
  - Fold in Phase 22's diagnostic-quality observation: `soa[Channel(int)]` is rejected
    by both, but tychoc says `soa requires a struct element type` where tychoc0 says the
    affine-container message.
  - Done when: the underlying-type decision agrees across the full swept space, each
    distinct rejection is fixture-locked (the compiler halts at the first error, so one
    file per rejection), full gate set green.
  - **DONE 2026-07-25**, as one phase group with 28, 30, 31 and 34.

    ### THE CONSOLIDATION FINDING (referenced by Phases 28, 30, 31)

    **Answer: they do NOT collapse onto one or two shared choke points. Four rules,
    four call sites — but they DO share one missing primitive, and that primitive was
    written once.** Evidence, site by site:

    | row | tychoc site(s) | tychoc0 site (post-fix) | the predicate it asks |
    |---|---|---|---|
    | 27 | `src/tychoc.c:3719-3721` (1) | `compiler/tychoc0.ty:2906-2907` in `parse_newtype` | is this type in the newtype-underlying whitelist? |
    | 28 | `src/tychoc.c:7124-7125` (1) | `compiler/tychoc0.ty:3637-3648` in `parse_program` | *(not a type predicate at all)* is this declaration named `main`, and does it have parameters or a return? |
    | 30 | `src/tychoc.c:1905` + `:1935` (2) | `compiler/tychoc0.ty:1981` in `parse_type_d` | does this type argument mention a type parameter *partially*? |
    | 31 | `src/tychoc.c:3530` + `:3556` (2) | `compiler/tychoc0.ty:2345` `extern_ok_ty` | is this type in the FFI-crossable whitelist? |

    **Why this is unlike Phase 20.** Phase 20's 50 divergences were 50 *positions*
    asking ONE predicate ("is this type affine?"), so one predicate function
    (`ck_affine_part`) plus two call sites covered all 50. Rows 27/30/31 are three
    *different* predicates asked at three different positions — no position sees more
    than one of them, and no predicate answers more than one of them — and row 28 is
    not a type rule at all. Forcing them through one guard would mean a guard that
    takes a mode flag and switches on it, i.e. four checks with extra plumbing.

    **What DID consolidate, and was exploited.** All four rows failed open for the
    *same underlying reason*: tychoc resolves a type to a `Type` id and can ask its
    decl tables what kind of declaration a name has (`struct_find`/`enum_find`/
    `newtype_find`/`handle_find`), while tychoc0 checks these at parse time and has no
    decl table. Phase 20 already built the substitute — a token-stream scan
    (`declares_type_name`, `compiler/tychoc0.ty:1671-1687`). This phase generalised it
    ONCE into `declares_kind_name(toks, kw, nm)` (`:1690-1706`) plus
    `type_head_name(ty)` (`:1729-1737`), and **three of the four rows call it**:
    27 (struct vs. enum vs. newtype vs. handle), 30 (struct vs. enum, for the message
    word), 31 (handle vs. struct/enum/newtype). So the consolidation is in the shared
    *primitive*, not a shared call site. Row 28 needs no type knowledge and shares
    nothing.

    **One genuine site collapse, in row 30.** tychoc needs two checks
    (`:1905` generic struct, `:1935` generic enum) because it has two branches;
    tychoc0's `parse_type_d` has a single `Name(args)` branch serving both, so
    `ck_generic_targ` is called from **one** site and reads the `struct`/`enum` word
    for the diagnostic off the declaration.

    ### CITATION AUDIT (every citation in these five phase blocks, verified)

    | claim in the plan | verdict |
    |---|---|
    | 27: tychoc `:3719-3721` | **correct** |
    | 27: spec `03-types.md:317-321` | **WRONG** — `:317-321` is the `bounded` provenance block. The MUST-NOT list is `03-types.md:334-338` (§5.4) |
    | 27: tychoc0 affine case `:2799` | **correct** (pre-edit) |
    | 28: tychoc `:7123-7124` | **off by one** — the `if` is `:7124`, the `die_at` `:7125` |
    | 30: tychoc `:1905` / `:1935` | **both correct** |
    | 31: tychoc `:3530` | **correct**; its return-side sibling is `:3556`, not `:3546` |
    | 31: spec `14-ffi.md:21-22`, `:38-39` | **correct** |
    | 34: `15-program.md:31-32` cites `:6354-6355`, `:6379-6380` | **confirmed wrong** — `:6354-6355` is `a value if/match cannot produce a task handle` + `s->decl_type = t`; `:6379-6380` is `if (s->typed_decl) { if (t != s->annot)`. Neither is about `main` |
    | 34: "the live sites are `:7097-7098` and `:7123-7124`" | **both off by one** — `sig_find("main")` is `:7098`, `no 'main' procedure` `:7099`; the signature rule is `:7124-7125` |

    Two further stale citations found in the file Phase 27 had to edit, corrected in
    the same breath because they name the very rule this phase implements:
    `03-types.md:350` said the underlying restriction lives at `src/tychoc.c:3439-3441`
    (actually `subst_place`, unrelated) and the chapter header `:17` said "newtype decl
    `:3430-3446`" (same wrong region). Both now point at `parse_typedecl` `:3710-3726`
    / the check `:3719-3721`.

    ### `soa` — stated explicitly, not left ambiguous

    `soa` was in **neither** of `03-types.md` §5.4's two lists. It is excluded by the
    permitted list being *closed* ("`U` MUST be one of: …"), and tychoc agrees: a soa
    type is not `is_array`, not `is_map` and not `IS_STRUCT`, so `:3719-3721` refuses
    it. Resolution: **refused in tychoc0, and added to the spec's MUST-NOT list** so no
    future reader has to re-derive it (`docs/spec/03-types.md:336-339`). The same edit
    also corrected the sized-numeric wording from `u32/u64/f32` to the whole
    `u8`…`u64`/`i8`…`i64`/`f32` family, which is what both compilers actually refuse
    (measured: `nt_u8`, `nt_i64`, `nt_u32`, `nt_f32` all reject on both).

    ### `soa[Channel(int)]` diagnostic, aligned to tychoc (Phase 22's observation)

    Same accept/reject decision on both before and after; only tychoc0's *wording*
    changed, to tychoc's (`src/tychoc.c:1724`), so no third phrasing was invented:
    ```
    BEFORE  tychoc  : soa requires a struct element type, e.g. soa [Point]
            tychoc0 : a channel handle cannot be stored in a container or aggregate -- pass it as an argument instead
    AFTER   tychoc  : soa requires a struct element type, e.g. soa [Point]
            tychoc0 : soa requires a struct element type, e.g. soa [Point]
    ```

    ### Phase 27 before / after — FRONT/CC/RUN, both compilers

    Method (Phase 11's, as used by 18/20/25): `--emit-c` on BOTH sides, then a separate
    `cc` step, then run. Raw `rc` is never compared between compilers — tychoc0 never
    invokes `cc`, so its status is a frontend verdict only. Harness `/tmp/ph27/probe.sh`.

    | probe (`type A = U`) | tychoc | tychoc0 BEFORE | tychoc0 AFTER |
    |---|---|---|---|
    | `Option(int)` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `Result(int,string)` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | an enum `E` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `soa[P]` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | another newtype `B` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `ptr` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `bytes` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `u8` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `f32` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `u32` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `i64` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `(int,int)` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `fn(int)->int` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | a handle `H` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |

    **Correction to the phase text's own measurement.** It said eleven forbidden types
    were accepted and *eight* run, with `(int,int)` and `fn(int)->int` CCFAILing. On a
    probe whose newtype is merely *declared* (not used), **fourteen** were accepted and
    **all fourteen compiled and ran** — the tuple and function-type cases CCFAIL only
    when the newtype is also used. The fail-open was wider than the row recorded. Two
    types beyond the eleven (`u32`, `i64`) were swept in and are equally closed.

    tychoc0's message is tychoc's, verbatim (`compiler/tychoc0.ty:2907`):
    ```
    tychoc  /tmp/ph27/p/nt_option.ty:1: error: a newtype's underlying type must be int, float, string, bool, an array, a map, or a struct (got Option(int))
    tychoc0 parse: line 1: a newtype's underlying type must be int, float, string, bool, an array, a map, or a struct (got Option(int))
    ```

    ### Legal-program controls (must still ACCEPT/CCOK/RUN on both — all do)

    `type A = int` · `= float` · `= string` · `= bool` · `= [int]` · `= [string: int]`
    · `= P` (a struct) — 7/7 `ACCEPT/CCOK/RUN` on both compilers, after the fix. The
    repo corpus's own newtypes (`grep -h '^type X = '` over every `.ty`: 16 `int`,
    5 `string`, 4 `float`, 2 `[int]`, 1 `[string: int]`, 1 struct, 1 `bool`, 1
    `Channel(int)` in a reject fixture) are entirely inside the permitted set, so the
    whitelist could not regress one — confirmed by `make test` 502/502 and `make
    fixpoint` B==C.

    ### Files changed by this phase group

    - `compiler/tychoc0.ty` — `declares_kind_name` `:1690`, `ck_generic_targ` `:1717`,
      `type_head_name` `:1729`, soa wording `:1835`, `ck_generic_targ` call `:1981`,
      `extern_ok_ty` `:2345`, `extern_param_ok` `:2350`, `newtype_under_ok` `:2880`,
      `parse_newtype` check `:2906`, `main` check `:3637-3648`.
    - `docs/spec/03-types.md` — `soa` + the full sized-integer family added to §5.4's
      MUST-NOT list; §5.4 provenance and the chapter header re-pointed at live code.
    - `docs/spec/15-program.md` — Phase 34 (below).
    - `tests/reject/` — 20 new fixtures.

    ### Gate set (each its own foreground command, `env -u LD_PRELOAD`)

    ```
    make test        passed: 502   failed: 0   /  all green      (482 -> 502: +20 fixtures)
    make corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc        conc: passed 37   failed 0
    make fixpoint    ok   B == C : tychoc0 reproduces itself byte-identically (35277 lines C)
                     fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32       passed: 502   failed: 0   /  all green
    make spec-check  spec-examples: 7 runnable example(s), all pass
    make check-links link check: ok (122 markdown files, no dead relative links)
    ```
    `git status --short` showed only the intended files — no build spill.

- [x] **Phase 28 — tychoc0 accepts a `main` with parameters or a non-void return (audit row I10)**
  - tychoc: `src/tychoc.c:7123-7124`. Spec: `docs/spec/15-program.md:27-32` — "MUST
    reject a `main` that declares any parameter or a non-`void` return type".
  - `fn main(x: int):` is accepted by tychoc0, **compiles and runs**. `fn main() -> int:`
    is accepted and CCFAILs.
  - This is the single most load-bearing row in the audit: it is the program entry
    point, and the spec states the rule as a MUST with named reference sites.
  - Done when: both forms reject on both compilers, both fixture-locked, gates green.
  - **DONE 2026-07-25** — see the consolidation finding and the gate set under Phase 27.
  - **Citation corrected:** the live rule is `src/tychoc.c:7124-7125`, not `:7123-7124`
    (`:7124` is the `if`, `:7125` the `die_at`). Verified by `grep -n "must be 'fn main():'"`.
  - **Where it landed, and why there.** `compiler/tychoc0.ty:3637-3648`, in
    `parse_program`'s `fn` arm — NOT in `parse_func`. tychoc tests the **post-mangle**
    name (`!strcmp(pr->name, "main")` at `:7124`), so only the *entry* package's `main`
    is the entry point; an imported package's `fn main` is `<pkg>__main` and is an
    ordinary procedure. tychoc0 mangles in pass 2, so the faithful test one pass earlier
    is `curpfx == "" and pf.name == "main"` — `package main` and a plain single-file
    program both keep the empty prefix, an imported package gets `<pkg>__`. Testing the
    raw name inside `parse_func` would have rejected a legal `fn main(x: int)` in a
    non-entry package: an over-tightening avoided by reading tychoc's site first.
  - The diagnostic line number is the `fn` token's (`fnline`, captured before the body
    is parsed), because tychoc reports `pr->line`. Taking `toks[pos].line` after
    `parse_func` returned pointed at the line *after* the body — caught and fixed.

    **Before / after — FRONT/CC/RUN, both compilers**

    | probe | tychoc | tychoc0 BEFORE | tychoc0 AFTER |
    |---|---|---|---|
    | `fn main(x: int):` | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `fn main() -> int:` | REJECT | ACCEPT/CCFAIL/- | REJECT |
    | `fn main():` (control) | ACCEPT/CCOK/RUN | ACCEPT/CCOK/RUN | ACCEPT/CCOK/RUN |

    Message and line agree exactly:
    ```
    tychoc  /tmp/ph27/p/main_param.ty:1: error: 'main' must be 'fn main():' with no return
    tychoc0 parse: line 1: 'main' must be 'fn main():' with no return
    ```
  - Fixtures: `tests/reject/main_with_param.ty`, `tests/reject/main_with_ret.ty`.

- [x] **Phase 29 — tychoc0 has no counterpart to ANY of tychoc's arity limits (audit rows B6, B8, B9, B11, B12, C5, C7, F2, G2, G5)**
  - Ten rows, **all ten RUN**. Each limit is a fixed-size array bound in tychoc:

    | row | limit | tychoc site | spec |
    |---|---|---|---|
    | B6 | function type ≤ 8 parameters | `:1752` | `02-grammar.md:170` |
    | B8 | tuple ≤ 8 elements | `:1768` | `02-grammar.md:137,:170`, `03-types.md:175` |
    | B9 | tuple ≥ 2 elements | `:1772` | same |
    | B11 | ≤ 16 type parameters per generic | `:1714` | `05-generics.md:20` |
    | B12 | ≤ 16 size parameters per generic | `:1801` | `05-generics.md:20` |
    | C5 | ≤ 8 `where` constraints | `:3312` | `02-grammar.md:78`, `05-generics.md:63` |
    | C7 | ≤ 16 types in a `where` type set | `:3324` | `02-grammar.md:78`, `05-generics.md:61` |
    | F2 | ≤ 8 struct type parameters | `:3605` | **conflict, see below** |
    | G2 | ≤ 8 enum type parameters | `:3661` | **conflict, see below** |
    | G5 | ≤ 8 variant payload fields | `:3695` | `02-grammar.md:97`, `12-aggregates.md:484` |

  - **A spec conflict must be resolved before F2/G2 are fixed:** `05-generics.md:20`
    says "At most 16 type parameters and 16 size parameters may be introduced per
    generic", but tychoc enforces **16 for functions** (`:1714`) and **8 for structs
    and enums** (`:3605`, `:3661`). Decide which number is right and make the spec and
    both compilers agree — do not encode 8 in tychoc0 while the spec says 16.
  - **USER RULING 2026-07-25: RAISE structs and enums to 16.** The spec text at
    `05-generics.md:20` is correct as written and does NOT change; tychoc's `:3605`
    and `:3661` limits go 8 → 16, and tychoc0 gains the 16 limit it currently lacks
    entirely. Rationale: one number everywhere, no spec asymmetry for a reader to
    remember, and — decisively — **no program that compiles today is rejected**.
    Lowering the spec to 8 would have made any existing 9..16-parameter struct
    illegal, which is a breaking change adopted to document an implementation
    accident. Widening two fixed-size arrays in tychoc is the smaller cost.
  - Implementation note for F2/G2: check whether the 8 is a bare array bound or is
    coupled to other fixed-size state (a parallel array, a bitmask, a mangling
    buffer). Widen every place the bound is assumed, not only the check — a
    half-widened limit is a buffer overrun, not a rejected program. The other eight
    rows are spec-backed and need no ruling.
  - Done when: every row's decision agrees, each is fixture-locked, the spec states one
    number per limit, gates green.
  - **DONE 2026-07-25.** All ten rows now agree, message-for-message and line-for-line.

    **THE F2/G2 COUPLING TRACE, written before any edit.** Every fixed-size array in
    `src/tychoc.c` indexed by a struct/enum *type-parameter number* — the complete set,
    found by reading every one of the 60 `typarams`/`from_args`/`ntyparams` hits in the
    file (pre-edit line numbers):

    | # | site (pre-edit) | array | why it is coupled to the 8 |
    |---|---|---|---|
    | 1 | `:531` | `StructDef.typarams[8]` | written `for i < _ntp` at `:3627` |
    | 2 | `:532` | `StructDef.from_args[8]` | written `for i < ntyparams` at `:1626`, read at `:1592` |
    | 3 | `:796` | `EnumDef.typarams[8]` | written `for i < _ntp` at `:3679` |
    | 4 | `:797` | `EnumDef.from_args[8]` | written `for i < ntyparams` at `:1663`, read at `:1599` |
    | 5 | `:3600` | `parse_struct` local `_tp[8]` | staged, then copied into #1 |
    | 6 | `:3656` | `parse_enum` local `_tp[8]` | staged, then copied into #3 |
    | 7 | `:1891` | `parse_type_inner` local `args[8]` (generic **struct** application) | filled `for i < np` where `np = ntyparams` — **this is the overrun**: a 9-parameter `Box(...)` in type position writes `args[8]` |
    | 8 | `:1921` | `parse_type_inner` local `args[8]` (generic **enum** application) | same, for `Bag(...)` |

    **What is NOT coupled, and why — checked, not assumed.**
    - Mangled instance names: `nm = sfmt("%s__%s", …)` (`:1617`, `:1654`) — heap, grows
      per parameter, no buffer.
    - `Type binds[256]` (`:1908`, `:1938`, `:5095`, `:5130`, `:6791`) and `Type b[256]`
      (`:4087`) are indexed by the **global** typaram id (`t - T_TYPARAM_BASE`,
      `:637`), i.e. by how many distinct `$Name`s the whole program uses — not by the
      per-generic count. Untouched by this widening. (They have their own pre-existing
      defect; filed as Phase 37 below.)
    - `Variant.payload[8]` (`:794`) and the `pl[8]` staging buffer (`:1673`) are sized by
      the payload-field count (row G5), which stays **8**. Deliberately not widened.
    - No bitmask over type parameters exists — grep over all 60 `typarams` hits shows
      only array indexing and `strcmp`, never a shift.
    - `Type tas[16]` (`:2240`, explicit call-site type args) is already 16 and admits
      exactly 16, so a 16-parameter generic's `V0$(t1,…,t16)` fits with no change. The
      positive fixture exercises that spelling.

    **The widening.** A named bound replaces the eight literal `8`s so it cannot be
    half-landed again: `#define TYCHO_MAX_TYPARAMS 16` (`src/tychoc.c:538`, with the
    trace above recorded as its comment), then `:542`, `:543`, `:807`, `:808`, `:1902`,
    `:1932`, `:3611`, `:3667`, and the two limit checks `:3616` / `:3672` (`8` → `16`,
    messages `(max 8)` → `(max 16)`). The spec is unchanged: `05-generics.md:20` already
    said 16 for every generic, which is why the RULING chose to raise rather than lower.

    **Proof the widening is safe (not "should be").** An ASan+UBSan build of tychoc
    (`cc -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1`) over the
    boundary set, `ASAN_OPTIONS=detect_leaks=0` (tychoc is arena-style and frees
    nothing, so LeakSanitizer fires on every input, before and after):
    ```
    f2_8       rc=0 sanitizer_errors=0
    f2_9       rc=0 sanitizer_errors=0
    f2_15      rc=0 sanitizer_errors=0
    f2_16      rc=0 sanitizer_errors=0
    f2_17      rc=1 sanitizer_errors=0   p/f2_17.ty:1: error: too many type parameters (max 16)
    f2_dup17   rc=1 sanitizer_errors=0   p/f2_dup17.ty:1: error: too many struct type parameters (max 16)
    g2_8       rc=0 sanitizer_errors=0
    g2_16      rc=0 sanitizer_errors=0
    g2_17      rc=1 sanitizer_errors=0   p/g2_17.ty:1: error: too many type parameters (max 16)
    g2_dup17   rc=1 sanitizer_errors=0   p/g2_dup17.ty:1: error: too many enum type parameters (max 16)
    b11_16     rc=0 sanitizer_errors=0
    b12_16     rc=0 sanitizer_errors=0
    ```
    A 16-type-parameter struct and a 16-type-parameter enum both **compile and RUN**
    (`tests/arity_limits_max.ty`, printing `0 15` and `99`); 17 rejects cleanly, never
    crashes.

    **Two distinct rejections at 17, both reproduced.** With 17 *distinct* names the
    shared per-generic cap fires first, inside `parse_type` (`:1725`), so the message is
    `too many type parameters (max 16)`. Only 17 *repeats of one name* reach the
    struct/enum slot cap (`:3616`/`:3672`), because the `seen` test at `:1723` stops the
    repeat from re-registering. Both orders are reproduced in tychoc0 and both are
    fixture-locked (`generic_typaram_max.ty` vs `struct_typaram_max.ty` /
    `enum_typaram_max.ty`).

    **CITATION AUDIT — 9 of the 10 rows' spec citations were right; one was wrong.**
    | row | plan said | verdict |
    |---|---|---|
    | B6 | `02-grammar.md:170` | **correct** — "A function type has up to 8 parameters." Also `03-types.md:246`. |
    | B8/B9 | `02-grammar.md:137,:170` | **correct** — `:137` is the grammar comment `/* tuple, 2..8 elements */`, `:170` the prose. |
    | B8/B9 | `03-types.md:175` | **WRONG.** `:175` is about **bracket-array** element types (`void`/`bool` not permitted). The tuple sentence — "A tuple `(T1, …, Tn)` is an **anonymous product** of 2 to 8 elements" — is `03-types.md:193`. |
    | B11 | `05-generics.md:20` | **correct** (verbatim: "At most 16 type parameters and 16 size parameters may be introduced per generic"). |
    | B12 | `05-generics.md:20` | **correct** (same sentence). |
    | C5 | `02-grammar.md:78`, `05-generics.md:63` | **both correct.** |
    | C7 | `02-grammar.md:78`, `05-generics.md:61` | **both correct.** |
    | F2/G2 | see RULING | RULING applied; `05-generics.md:20` unchanged. |
    | G5 | `02-grammar.md:97`, `12-aggregates.md:484` | **both correct.** Also `03-types.md:232`. |
    All ten tychoc site line numbers (`:1752`, `:1768`, `:1772`, `:1714`, `:1801`,
    `:3312`, `:3324`, `:3605`, `:3661`, `:3695`) were **correct** pre-edit.

    **The spec states exactly one number per limit, and it matches both compilers.**
    Verified by grepping every "at most / up to / 2–8" statement in `docs/`: 8 for
    function-type parameters, tuple elements (2 floor), `where` constraints and variant
    payload fields; 16 for type parameters, size parameters and `where` type-set members.
    No struct/enum-specific type-parameter number exists anywhere in the spec, so
    nothing had to be edited to land the 8 → 16 raise.

    **TEN-ROW BEFORE / AFTER — FRONT/CC/RUN on BOTH compilers with `--emit-c` plus a
    separate `cc` step** (`tychoc0` emits its C to *stdout*, tychoc to `BASE.c`; raw `rc`
    is never compared). ✔ = both compilers agree, same message text, same line number.

    | row | limit | tychoc BEFORE | tychoc AFTER | tychoc0 BEFORE (N+1) | tychoc0 AFTER (N+1) |
    |---|---|---|---|---|---|
    | B6 | fn type ≤ 8 params | 8 ok / 9 REJECT | unchanged | **ACCEPT/CCOK/RUN** | REJECT ✔ |
    | B8 | tuple ≤ 8 elems | 8 ok / 9 REJECT | unchanged | **ACCEPT/CCOK/RUN** | REJECT ✔ |
    | B9 | tuple ≥ 2 elems | 2 ok / 1 REJECT | unchanged | rejected, but for the WRONG reason (`returning int but this function returns (int)` — it parsed the 1-tuple) | REJECT with tychoc's own wording ✔ |
    | B11 | ≤ 16 type params | 16 ok / 17 REJECT | unchanged | **ACCEPT/CCOK/RUN** | REJECT ✔ |
    | B12 | ≤ 16 size params | 16 ok / 17 REJECT | unchanged | **ACCEPT/CCOK/RUN** | REJECT ✔ |
    | C5 | ≤ 8 `where` constraints | 8 ok / 9 REJECT | unchanged | **ACCEPT/CCOK/RUN** | REJECT ✔ |
    | C7 | ≤ 16 types in a type set | 16 ok / 17 REJECT | unchanged | **ACCEPT/CCOK/RUN** | REJECT ✔ |
    | F2 | struct type params | **8** ok / 9 REJECT | **16** ok / 17 REJECT | **ACCEPT/CCOK/RUN** at any count | 16 ok / 17 REJECT ✔ |
    | G2 | enum type params | **8** ok / 9 REJECT | **16** ok / 17 REJECT | **ACCEPT/CCOK/RUN** at any count | 16 ok / 17 REJECT ✔ |
    | G5 | ≤ 8 payload fields | 8 ok / 9 REJECT | unchanged | **ACCEPT/CCOK/RUN** | REJECT ✔ |

    **BOUNDARY PROBES — N-1 / N / N+1 for every limit, both compilers, after the fix.**
    35 probes, zero disagreements. `RUN` means the emitted C compiled with
    `cc -O2 -fwrapv -std=c11` and the binary exited 0 with identical output on both sides.
    ```
    B6  N=7   tychoc RUN(21)   tychoc0 RUN(21)
    B6  N=8   tychoc RUN(28)   tychoc0 RUN(28)
    B6  N=9   both REJECT  "a function type has at most 8 parameters"        (line 5 / line 5)
    B9  N=1   both REJECT  "a tuple type needs at least two elements"        (line 1 / line 1)
    B8  N=2   tychoc RUN    tychoc0 RUN
    B8  N=7   tychoc RUN    tychoc0 RUN
    B8  N=8   tychoc RUN    tychoc0 RUN
    B8  N=9   both REJECT  "a tuple has at most 8 elements"                  (line 1 / line 1)
    B11 N=15  tychoc RUN    tychoc0 RUN
    B11 N=16  tychoc RUN    tychoc0 RUN
    B11 N=17  both REJECT  "too many type parameters (max 16)"              (line 1 / line 1)
    B12 N=15  tychoc RUN    tychoc0 RUN
    B12 N=16  tychoc RUN    tychoc0 RUN
    B12 N=17  both REJECT  "too many size parameters (max 16)"              (line 1 / line 1)
    C5  N=7   tychoc RUN    tychoc0 RUN
    C5  N=8   tychoc RUN    tychoc0 RUN
    C5  N=9   both REJECT  "at most 8 `where` constraints per function"     (line 1 / line 1)
    C7  N=15  tychoc RUN    tychoc0 RUN
    C7  N=16  tychoc RUN    tychoc0 RUN
    C7  N=17  both REJECT  "at most 16 types in a `where` type set"         (line 1 / line 1)
    F2  N=8   tychoc RUN    tychoc0 RUN          (was 8 = the old max; still legal)
    F2  N=9   tychoc RUN    tychoc0 RUN          (NEWLY legal, per the RULING)
    F2  N=15  tychoc RUN    tychoc0 RUN
    F2  N=16  tychoc RUN    tychoc0 RUN
    F2  N=17  both REJECT  "too many type parameters (max 16)"              (line 1 / line 1)
    F2  17 repeats of one name  both REJECT "too many struct type parameters (max 16)"
    G2  N=8   tychoc RUN    tychoc0 RUN
    G2  N=9   tychoc RUN    tychoc0 RUN          (NEWLY legal, per the RULING)
    G2  N=15  tychoc RUN    tychoc0 RUN
    G2  N=16  tychoc RUN    tychoc0 RUN
    G2  N=17  both REJECT  "too many type parameters (max 16)"              (line 1 / line 1)
    G2  17 repeats of one name  both REJECT "too many enum type parameters (max 16)"
    G5  N=7   tychoc RUN    tychoc0 RUN
    G5  N=8   tychoc RUN    tychoc0 RUN
    G5  N=9   both REJECT  "too many payload fields (max 8)"                (line 2 / line 2)
    ```

    **The tychoc0 side, and the one place it needed a new primitive.** Eight of the ten
    rows are a length test on a list the parser already builds: `parse_type_d`'s tuple
    branch (`compiler/tychoc0.ty:1764`, `:1766`) and `fn`-type branch (`:1892`), the
    `where` loop (`:2170`, `:2194`), `parse_struct` (`:2555`), `parse_enum` (`:2899`) and
    the variant payload (`:2932`). Every message is tychoc's, verbatim, and every line
    number is the token tychoc reports at (the `(`, the `fn`, the struct/enum name, the
    variant name) — so the Phase 2 divergence set gains **no** new entries.

    B11/B12 could not be done that way. tychoc counts type and size parameters in
    signature-wide scopes (`g_cur_typarams` / `g_cur_sizeparams`, `src/tychoc.c:1725`,
    `:1812`) that `parse_type_d` has no equivalent of, and the *encoded type string* is
    ambiguous: a dynamic array of a type parameter and a size-parameter array are both
    spelled `[$X]`, told apart only by whether a type follows the `]`. Rather than guess
    from the string, `ck_generic_param_counts` (`compiler/tychoc0.ty:2093`) takes both
    counts over the signature's **token range** using the identical disambiguation
    `parse_type_d` itself applies at `:1805` — `[` `$` NAME `]` followed by a type that is
    not the contextual keyword `where`. Distinct names, first-appearance order, exactly
    like the C. Called once per function over `sigstart..pos` (`:2223`, covering
    parameters, return type and `where`) and per struct/enum header parameter (`:2553`,
    `:2897`). It can only ever count **fewer** `$`-introductions than tychoc (it does not
    scan struct field types), so it cannot over-tighten.

    **No over-tightening, checked three ways.** (1) Every N-1 and N probe above still
    compiles and RUNs on both compilers. (2) `tests/arity_limits_max.ty` is a single
    program sitting exactly ON all ten maxima at once and it runs, so any future
    narrowing of any one of them turns the suite red. (3) `make fixpoint` — tychoc0's own
    16 833 lines, which lean on generics, tuples and `where` throughout — is green, and
    tychoc0 rebuilt itself through the new checks without tripping one.

    **Fixtures.** Ten `tests/reject/` files, one per distinct diagnostic (the compiler
    halts at the first error, so they cannot be merged): `fnty_arity.ty`,
    `tuple_arity_max.ty`, `tuple_arity_min.ty`, `generic_typaram_max.ty`,
    `generic_sizeparam_max.ty`, `where_constraint_max.ty`, `where_typeset_max.ty`,
    `struct_typaram_max.ty`, `enum_typaram_max.ty`, `variant_payload_max.ty` — each
    verified REJECT with identical text and line on both compilers. Plus the positive
    boundary lock `tests/arity_limits_max.ty` + `.out`. Test count 502 → **513**.

    **Gate set — one per command, foreground, `env -u LD_PRELOAD`.**
    ```
    make test         passed: 513   failed: 0   /  all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 37   failed 0
    make fixpoint     ok   B == C : tychoc0 reproduces itself byte-identically (35376 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32        passed: 513   failed: 0   /  all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (122 markdown files, no dead relative links)
    ```
    `git status --short` shows only the two edited compilers, the eleven new fixtures and
    `plan.md`. No build spill. All scratch output stayed in `/tmp/ph29`.

- [x] **Phase 30 — tychoc0 accepts a generic type argument that partially mentions a type parameter (audit rows B20, B21)**
  - tychoc: `src/tychoc.c:1905` (generic struct), `:1935` (generic enum). Spec:
    `docs/spec/05-generics.md:76-83` — "A type argument MUST be either fully concrete or
    a whole own-parameter reference; it may not *partially* mention a type parameter."
  - `Box([$U])` and `Opt([$U])` are accepted by tychoc0 and **compile and run**.
  - Done when: both forms reject on both compilers, fixture-locked, gates green.
  - **DONE 2026-07-25** — see the consolidation finding and the gate set under Phase 27.
  - **Citations verified: `:1905` and `:1935` are both correct**, and the spec text at
    `05-generics.md:80-82` is the sentence quoted.
  - **The one genuine site collapse in this group.** tychoc needs two checks because it
    has a generic-struct branch and a generic-enum branch; tychoc0's `parse_type_d` has
    a single `Name(args)` branch (`compiler/tychoc0.ty:1969-1987`), so ONE call to
    `ck_generic_targ` (`:1981`) covers both. The `struct`/`enum` word in the message is
    read off the declaration via `declares_kind_name`, so tychoc's wording is reproduced
    verbatim for both kinds rather than a generic third phrasing being invented.
  - **The rule, and why it cannot over-tighten.** A bare `$T` is the only spelling
    `parse_type_d` returns that starts with `'$'` (`:1685-1691`); every composite that
    embeds one starts with `'['`, `'{'`, `'('`, `fn(` or a nominal head. So
    "contains `$` **and** does not start with `$`" is exactly "partially mentions a type
    parameter", with no decl table needed and no legal spelling caught.

    **Before / after — FRONT/CC/RUN, both compilers**

    | probe | tychoc | tychoc0 BEFORE | tychoc0 AFTER |
    |---|---|---|---|
    | `Box([$T])` (generic struct, B20) | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `Opt([$T])` (generic enum, B21) | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `Box(int)` (control) | ACCEPT/CCOK/RUN | ACCEPT/CCOK/RUN | ACCEPT/CCOK/RUN |
    | `Box($T)` whole own-parameter ref (control) | ACCEPT/CCOK/RUN | ACCEPT/CCOK/RUN | ACCEPT/CCOK/RUN |
    | `Opt($T)` whole own-parameter ref (control) | ACCEPT/CCOK/RUN | ACCEPT/CCOK/RUN | ACCEPT/CCOK/RUN |

    ```
    tychoc  ...:3: error: generic struct 'Box': a type argument may not partially mention a type parameter; use the generic applied to its own parameters (a recursive reference) or to concrete types
    tychoc0 parse: line 3: generic struct 'Box': a type argument may not partially mention a type parameter; use the generic applied to its own parameters (a recursive reference) or to concrete types
    ```
  - Fixtures: `tests/reject/generic_partial_struct.ty`,
    `tests/reject/generic_partial_enum.ty`.
  - **Out of scope, filed as Phase 36:** tychoc *also* refuses a WHOLE reference to a
    type parameter that is not the generic's own (`Box($U)` where `struct Box($T)`);
    tychoc0 still accepts it and it runs. That is a distinct row from B20/B21 (which are
    the *partial* case) and closing it needs the generic's own parameter list.

- [x] **Phase 31 — tychoc0 accepts a struct as an `extern fn` parameter (audit row D2)**
  - tychoc: `src/tychoc.c:3530`. Spec: `docs/spec/14-ffi.md:21-22` ("a **map, struct, or
    non-scalar array** is rejected at the boundary") and `:38-39`.
  - tychoc0 enforces the rule **partially**: it rejects `[string]` but accepts a struct
    parameter, and the emitted C **compiles and runs** — a struct crossing the C
    boundary with no flat, self-describing ABI.
  - Scope check while there: D1 (`inout` out-parameter) and D3 (return type) both agree
    today; verify the fix does not over-tighten them.
  - Done when: the FFI boundary type set agrees on both compilers, fixture-locked,
    `make test`'s FFI lane (`tests/ffi/`) still green, gates green.
  - **DONE 2026-07-25** — see the consolidation finding and the gate set under Phase 27.
  - **Citations verified:** tychoc `:3530` (parameter) correct; spec `14-ffi.md:21-22`
    and `:38-39` correct. The return-side sibling is `:3556`, not `:3546` (`:3546` is the
    sized-int return path).
  - **Root cause, read off the source.** `extern_ok_ty`'s bare-name arm accepted ANY
    non-composite identifier, on the stated theory that only a `handle` can be spelled
    that way. It cannot: a struct, an enum and a newtype are spelled identically, and the
    old comment said as much ("can't consult dc here at parse time"). The fix replaces
    the guess with an answer — `declares_type_name(toks, ty)` (the Phase 20 scan) is true
    for a `struct`/`enum`/`type` declaration and false for a `handle`, which is a
    different keyword. Fails **closed**: only a name the program does not declare as one
    of those three still crosses.
  - **D1/D3 scope check — and D3 was NOT actually agreeing.** The phase text said "D1
    (`inout` out-parameter) and D3 (return type) both agree today; verify the fix does
    not over-tighten them." D1 did agree and still does. **D3 did not**: the same
    bare-name arm let a struct RETURN cross, and it compiled and ran. The fix closes it
    as a side effect (`extern_ok_ty` serves both positions), which is why
    `tests/reject/extern_ret_struct.ty` is in the fixture set.

    **Before / after — FRONT/CC/RUN, both compilers**

    | probe | tychoc | tychoc0 BEFORE | tychoc0 AFTER |
    |---|---|---|---|
    | `extern fn f(s: P)`, `struct P` (D2) | REJECT | ACCEPT/CCFAIL/- | REJECT |
    | `extern fn f(s: E)`, `enum E` | REJECT | ACCEPT/CCFAIL/- | REJECT |
    | `extern fn f(s: NT)`, `type NT = int` | REJECT | ACCEPT/CCFAIL/- | REJECT |
    | `extern fn f(a: int) -> P` (D3) | REJECT | ACCEPT/CCOK/**RUN** | REJECT |
    | `extern fn f(s: [string])` (already agreed) | REJECT | REJECT | REJECT |
    | `extern fn f(s: [string: int])` (already agreed) | REJECT | REJECT | REJECT |
    | `extern fn f(s: inout string)` (D1, must stay) | REJECT | REJECT | REJECT |

    **Legal FFI controls — every one still ACCEPT on both, and the ones with a real C
    symbol still CCOK/RUN:** `string` param · `bytes` param · `[int]` param ·
    `int` + `inout int` params · `u8` param with `-> u32` · a typed `handle` param and
    return (`handle FILEH:` / `free: hclose`). 6/6 unchanged. The whole `tests/ffi/`
    lane (including `tests/ffi/pkgext`) is green inside `make test` 502/502.
  - Fixtures: `tests/reject/extern_param_struct.ty`, `extern_param_enum.ty`,
    `extern_param_newtype.ty`, `extern_ret_struct.ty`.

- [x] **Phase 32 — NEEDS A USER RULING: four restrictions tychoc enforces that the spec does not state (audit rows E1, I6, I7, I8)**
  - The spec is silent on all four, so the direction of the fix is a decision, not a
    lookup. `docs/spec/` was grepped for each phrasing before asserting the absence;
    §9 of the audit doc records the searches.

    | row | tychoc enforces | site | tychoc0 | runs? |
    |---|---|---|---|---|
    | I6 | a `Channel(T)` parameter may not be `inout` | `:7068` | accepts | **RUNS**, and prints `Some(7)` where a `recv` should yield `7` — a semantic divergence, not only a decision one |
    | I7 | at most 16 function parameters | `:7075` | accepts | **RUNS** (prints `18`) |
    | I8 | an `inout` parameter may not be a function value | `:7090` | accepts | **RUNS** |
    | E1 | a `handle` name may not collide with a struct/enum/newtype/handle | `:3579` | accepts | CCFAIL — the only divergence in the whole audit that does not run |

  - The ruling needed per row: does the restriction become normative (spec it, then add
    it to tychoc0), or is it an unspec'd implementation limit tychoc should drop?
  - **USER RULING 2026-07-25: ALL FOUR BECOME NORMATIVE.** Spec each of I6, I7, I8
    and E1, then add the check to tychoc0. tychoc keeps every restriction it
    currently enforces; nothing is dropped, so **no program tychoc accepts today
    starts being rejected, and no program it rejects today starts compiling**. The
    alternative — dropping I7/I8/E1 as unspec'd implementation limits — would have
    LOOSENED tychoc, and loosening a compiler to resolve a documentation gap is the
    wrong direction when the restrictions are defensible on their own terms.
  - I6 is independent of the ruling and must be fixed regardless: tychoc0 accepts
    `inout Channel(T)`, runs it, and prints `Some(7)` where a `recv` should yield
    `7`. That is a WRONG-OUTPUT bug, the only one the whole audit found — every
    other divergence is accepted-when-it-should-be-rejected. Treat I6 as the
    priority row and verify the corrected behaviour by running, not only by
    rejecting.
  - Spec placement: I6 and I8 belong with the `inout` rules (`07-memory-model.md`
    §11 / `11-functions.md`), I7 with the other arity limits (`02-grammar.md` /
    `11-functions.md` — put it beside whatever Phase 29 does for its ten limits so
    the two are consistent), E1 with the `handle` declaration rules
    (`12-aggregates.md`). Match each neighbourhood's house style and MUST/MAY
    conventions rather than inventing a new section shape.
  - Done when: each row has a ruling recorded here, the spec says what was ruled, both
    compilers agree, fixture-locked, gates green.
  - **DONE 2026-07-25.** All four rows agree on the frontend decision, with tychoc's
    wording verbatim and the same reported line. The spec now states all four.

    **I6 IS NOT A WRONG-OUTPUT BUG. The audit misread its own probe.** This phase was
    told to treat I6's `Some(7)` as a semantic divergence and to trace the lowering
    before rejecting. The trace says the lowering is correct and the *expectation* was
    wrong, so the finding is retracted here rather than repaired.

    The audit's probe (recovered from `/tmp/ph25/rows2.py:48`) is:
    ```
    fn g(c: inout Channel(int)):
        send(c, 7)

    fn main():
        ch := channel(int, 2)
        g(&ch)
        println(str(recv(ch)))
    ```
    `recv(ch)` has type **`Option(T)`**, not `T` — `docs/spec/16-builtins.md:227`
    ("`recv(ch)` | `Channel(T) -> Option(T)`") and `13-concurrency.md:109` ("yields
    `Option(T)`: `Some(v)` for a …"). So `str(recv(ch))` renders `Some(7)`, and
    `Some(7)` is the **correct** output for that program. The audit compared it against
    a bare `7`, which no spelling of that line produces.

    **Proved by running the legal spellings, both compilers** (`--emit-c` + a separate
    `cc`, never a raw `rc` comparison):
    ```
    audit_chan_plain_used   fn g(c: Channel(int))  tychoc  RUN -> Some(7)
                                                   tychoc0 RUN -> Some(7)
    audit_chan_nofn_used    send/recv inline, no fn tychoc RUN -> Some(7)
                                                   tychoc0 RUN -> Some(7)
    ```
    Neither goes anywhere near an `inout` channel, and both print `Some(7)`.

    **The emitted-C trace, `inout` vs by-value (tychoc0, the accepting side):**
    ```
    inout    void h_g(Arena* _parent, HChan** h_c) { ... HChan* _ch = (*h_c); ... }
             call site:  h_g(&_t, &h_ch);
    by-value void h_g(Arena* _parent, HChan* h_c)  { ... HChan* _ch = h_c;    ... }
             call site:  h_g(&_t, h_ch);
    ```
    One extra level of indirection, correctly dereferenced on entry, then the identical
    `tycho_chan_send_cell` / `_commit` sequence. There is no mis-lowering to hide.

    **"Is there a legal route to the same lowering?" — YES, and that is the point, not
    a problem.** The `HChan*`-by-value form is what every plain `Channel(T)` parameter
    compiles to; the whole `tests/conc/` lane (37 fixtures) exercises it, and it is
    correct. The `inout` form only wrapped it in one pointer. So rejecting
    `inout Channel(T)` hides nothing: I6 is an ordinary missing-gate row, exactly like
    I7 and I8, and the audit's `Some(7)` note should be read as retracted. Locked
    against silent return by the positive fixture `tests/chan_param_recv.ty`, whose
    golden holds **both** renderings side by side (`Some(7)` from `str(recv(ch))` and
    `7` from the `match` binding), so the same misreading cannot be made from the
    fixture alone.

    **CITATION AUDIT — all four tychoc sites in the prompt and the table above were
    WRONG** (drift from Phase 37's edits to `src/tychoc.c`, plus one that was never
    right). Corrected, each verified by reading the line:
    | row | prompt said | actual | the line that is actually there |
    |---|---|---|---|
    | I6 | `:7068` | **`:7101-7102`** | `:7068` is `"an enum payload cannot be a channel"` |
    | I7 | `:7075` | **`:7109`** | `:7075` is the `[$N]T` struct-field loop head |
    | I8 | `:7090` | **`:7123-7126`** | `:7090` is `diag_use_proc(pr)` |
    | E1 | `:3579` | **`:3616-3617`** | `:3579` is a `}` inside `parse_extern_fn`'s param loop |
    Exact strings, verbatim (they are what tychoc0 now emits):
    `"a channel parameter cannot be inout (the handle is already shared)"`,
    `"too many parameters (max 16)"`,
    `"inout parameter '%s': a function value can't be inout (a callee could write a closure back into the caller and it would dangle)"`,
    `"'%s' is already defined"`.

    **CORRECTION to this phase's own spec-placement instructions.** Two were wrong.
    (1) *"Phase 29 just specced ten arity limits — put I7 in the same place."* Phase 29
    (`f0f7a2e`) touched **no** file under `docs/` — `git show --stat f0f7a2e` lists only
    `compiler/tychoc0.ty`, `src/tychoc.c`, `plan.md` and twelve fixtures. Its own
    evidence says so: *"nothing had to be edited to land the 8 → 16 raise"*, because
    every one of its ten limits was **already** stated. So there is no Phase 29 section
    to sit beside; what I matched is its *form* — one sentence in the prose of the
    chapter that owns the construct, like `02-grammar.md:170` and `05-generics.md:20`.
    (2) *"E1 → `12-aggregates.md`."* That file contains no `handle` declaration content
    at all (`grep -n handle 12-aggregates.md` returns one hit, the word "handles" in a
    sentence about `Result`). The handle declaration rules are `14-ffi.md` **§25 Typed
    handles**, which is also where the audit's sibling row E3 (handle body shape) is
    already spec'd (`14-ffi.md:68`). E1 went there.

    **Spec added — four files:**
    - `docs/spec/07-memory-model.md` — new **§11.5 "Types that cannot be `inout`"** at
      the end of §11, covering I6 and I8 with the reason each is unsound (a channel
      copy-out could retarget the caller's queue; a function-value copy-out could store
      a callee-local closure past its re-homed captures, citing §10.2). MUST NOT, both
      implementations named, both fixtures named — the shape of the existing §11.2.
    - `docs/spec/11-functions.md` **§15.1** — I7, appended to the existing MUST-NOT
      sentence: *"and MUST NOT declare more than **16** parameters (the same cap for an
      `extern fn`; a function type, which is a distinct construct, allows up to 8 —
      §5.3.8)"*. The parenthetical exists because `05-generics.md:20`'s 16 is about
      *type* parameters and `03-types.md:246`'s 8 is about *function types* — the two
      numbers a reader would otherwise conflate.
    - `docs/spec/14-ffi.md` **§25** — E1 as the first bullet, *"A handle name is a type
      name"*, MUST NOT collide with a struct/enum/newtype/handle **declared earlier in
      the file** (declaration order is what tychoc actually enforces — see the Phase 39
      finding below), with the C-typedef-collision rationale and the fixture.
    - `docs/spec/appendix-e-conformance.md` — three coverage rows: §11.5 in the
      §9–11 table, a second §15.1 row for the parameter cap, and a §25 row in the
      §23–24 table. Phase 11 and Phase 26 both established that this appendix carries
      such rows, and it is the file the conformance oracle is read from.

    **tychoc0 — four edits, `compiler/tychoc0.ty`** (all parse-time, the house style
    Phase 29 established: `die("parse: line " + str(N) + ": <tychoc's wording>")` with
    a comment citing both the spec clause and the `src/tychoc.c` site):
    1. `parse_func` — `fnln` captured at the `fn` keyword (tychoc reports `pr->line`).
    2. `parse_func`, after the typaram rewrite — the I6 sweep, then the I7 count, then
       the I8 sweep, **in that order**, because tychoc checks them in that order
       (`:7100-7102`, `:7109`, `:7123`) and a declaration tripping two must report the
       same one the C compiler reports.
    3. `parse_extern_func` — the I7 count (`exln` at the `extern` keyword).
    4. New `ck_handle_dup()` + its call from the top-level `handle` arm of
       `parse_program`, which is where the incremental decl lists still reflect
       declaration order. tychoc0's `parse_handle` has no view of those tables, so the
       check sits one call earlier, guarded on `TIdent` so `parse_handle`'s own
       *"expected a handle name"* still wins for `handle 1:`.

    **I6 and I8 are deliberately guarded on non-genericity; I7 deliberately is not.**
    Not a shortcut — measured. tychoc's `resolve_program` `continue`s past a `$T`
    template at `:7091-7096`, *before* any of the three checks, so:
    ```
    gen_chan_inout   fn f(c: inout Channel(int), p: $T)   tychoc RUN -> 1   (ACCEPTS!)
    ```
    An unguarded I6/I8 in tychoc0 would therefore reject a program tychoc compiles,
    violating the ruling's "nothing tychoc accepts starts being rejected". I7 is left
    unguarded because tychoc rejects a 17-parameter generic anyway (at the call site:
    `'f__int__…' takes 0 argument(s), got 17`), so no accepted program is lost and the
    cap then holds for every declaration. The genericity gap in tychoc's own I6/I8 is
    filed as Phase 39.

    **FOUR-ROW BEFORE / AFTER — FRONT/CC/RUN on BOTH compilers, `--emit-c` plus a
    separate `cc` step** (tychoc0 emits to stdout, tychoc to `BASE.c`; raw `rc` never
    compared). ✔ = same decision, same wording, same line.

    | row | probe | tychoc | tychoc0 BEFORE | tychoc0 AFTER |
    |---|---|---|---|---|
    | I6 | `chan_inout_param` | REJECT `:1` | **ACCEPT/CCOK/RUN** | REJECT `line 1`, same text ✔ |
    | I6 | `chan_inout_used` (send+recv) | REJECT `:1` | **RUN → `7`** | REJECT `line 1` ✔ |
    | I6 | `audit_chan_inout_used` (the audit's own probe) | REJECT `:1` | **RUN → `Some(7)`** | REJECT `line 1` ✔ |
    | I7 | `params_17` | REJECT `:1` | **RUN → `153`** | REJECT `line 1`, same text ✔ |
    | I7 | `extern_params_17` | REJECT `:1` | **RUN → `hi`** | REJECT `line 1` ✔ |
    | I8 | `inout_fnvalue` | REJECT `:4` | **RUN → `2`** | REJECT `line 4`, same text ✔ |
    | E1 | `handle_dup_name` (struct then handle) | REJECT `:4` | rejected, but for the WRONG reason (`line 7: extern fn 'hclose': a parameter must be …` — tychoc0 had drifted since the audit measured `ACCEPT/CCFAIL`) | REJECT `line 4`, tychoc's text ✔ |
    | E1 | `e1_struct` (no extern touching `H`) | REJECT `:4` | **ACCEPT/CCFAIL** (`incompatible type for argument 1 of ‘S_H_eq’`) | REJECT `line 4` ✔ |
    | E1 | `e1_enum` | REJECT `:5` | **RUN → `hi`** | REJECT `line 5` ✔ |
    | E1 | `e1_newtype` | REJECT `:3` | **RUN → `hi`** | REJECT `line 3` ✔ |
    | E1 | `e1_handle` (handle twice) | REJECT `:4` | **RUN → `hi`** | REJECT `line 4` ✔ |

    Note the two extra E1 collision kinds (`enum`, `newtype`) and the extern-arity
    variant: the audit probed only one shape per row, and three of these were live
    fail-opens it never saw.

    **I7's 15 / 16 / 17 BOUNDARY, both compilers, both directions:**
    ```
    params_15   tychoc RUN -> 120    tychoc0 RUN -> 120     (legal, unchanged)
    params_16   tychoc RUN -> 136    tychoc0 RUN -> 136     (ON the maximum, unchanged)
    params_17   tychoc REJECT        tychoc0 REJECT         (was RUN -> 153)
    ```
    16 compiles and runs on both; 17 rejects on both. `tests/params_16_max.ty` pins the
    16 permanently, so a future narrowing turns the suite red.

    **LEGAL-PROGRAM CONTROLS — nothing over-tightened.** Every one still compiles and
    runs identically on both compilers after the change:
    ```
    chan_plain_ok      fn pump(ch: Channel(int))        RUN -> 7    / RUN -> 7
    fnvalue_ok         fn f(h: fn(int) -> int)          RUN -> 42   / RUN -> 42
    handle_ok          struct S + handle H (no clash)   RUN -> 1    / RUN -> 1
    params_15/16       15 and 16 parameters             RUN         / RUN
    gen_chan_inout     fn f(c: inout Channel(int), p: $T)  RUN -> 1 / RUN -> 1
    make conc          the whole channel lane           passed 37   failed 0
    make fixpoint      tychoc0 rebuilds itself          B == C, byte-identical
    ```

    **Fixtures — seven new files. Test count 514 → 521.** Five `tests/reject/` (one per
    distinct rejection; the compiler halts at the first error so they cannot be merged):
    `chan_inout_param.ty`, `inout_fnvalue.ty`, `params_17.ty`, `extern_params_17.ty`,
    `handle_dup_name.ty` — each verified REJECT on both compilers with identical text
    and line. Plus two positives with goldens: `tests/chan_param_recv.ty`/`.out` (the
    I6 wrong-output tripwire described above; tychoc and tychoc0 outputs byte-identical)
    and `tests/params_16_max.ty`/`.out` (the 16-parameter boundary lock).

    **Gate set — one per command, foreground, `env -u LD_PRELOAD`.**
    ```
    make test         passed: 521   failed: 0   /  all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 37   failed 0
    make fixpoint     ok   B == C : tychoc0 reproduces itself byte-identically (35474 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32        passed: 521   failed: 0   /  all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (122 markdown files, no dead relative links)
    ```
    `git status --short` shows only the five edited files and the seven new fixtures.
    No build spill. All scratch output stayed in `/tmp/ph32`.

- [x] **Phase 33 — a newtype over a fixed array / map / `bounded` emits uncompilable C on tychoc0 (audit §7)**
  - NOT a fail-open: both frontends ACCEPT. tychoc builds and runs; tychoc0's C does not
    compile — `type C = [2]int` / `[string:int]` / `bounded[4]int` used as a parameter
    type emits `unknown type name 'Arr_f2_int'` / `'Map_str_int'` / `'Arr_b4_int'`, the
    mangled aggregate name with no declaration ever emitted.
  - Same family as Phase 19 (bounded aggregate elements) and Phase 23 (the `Arr_*`
    mangling collision) — check whether one emitter fix closes all three before writing
    a third separate patch.
  - Done when: the three probes build and run on both compilers with identical output,
    fixture-locked in the positive lane (they are legal programs), gates green.

  - **DONE 2026-07-25**, as one phase group with 35.

    ### THE SHARED-FIX QUESTION, ANSWERED

    **No — Phase 19's machinery covers ONE of the four defects here, and Phase 23 did
    NOT collapse for free.** Phase 19's `is_byval_comp` / `comp_dep_types` /
    `emit_comp_body` DFS was the right place for exactly one of them (the tychoc-side
    declaration-order failure), and there it needed no new code at all — only that the
    DFS *resolve the newtype*. The other three are different mechanisms in different
    passes. Site by site:

    | defect | symptom | pass | fix |
    |---|---|---|---|
    | 1. the underlying family is never REGISTERED | tychoc0 `unknown type name 'Arr_f2_int'` | `collect_elem_types` (type collection) | seed `note_arr_types` from `prog.ntunder` |
    | 2. element equality dispatches on the newtype NAME | tychoc0 `invalid operands to binary ==` | `eq_field` (codegen helper) | resolve the newtype first, as `str_field` already did |
    | 3. array-of-newtype-of-map emitted before the map's fns | tychoc0 `implicit declaration of 'map_str_int_copy'` | driver emission order | resolve in `inline_arr_of_map` + the two driver tests |
    | 4. the newtype is invisible to the by-value containment DFS | **tychoc** `field 'f_c' has incomplete type` | `needs_body_first` / `emit_aggregate` (Phase 19's DFS) | `t = base_of(t)` at both entries |

    Only row 4 is Phase 19's. Rows 1-3 are tychoc0-only and sit in three passes Phase 19
    never touched. What they DO share is one root cause statement, and it is worth
    recording because it predicts the Phase 40 findings too: **tychoc0 resolves a
    newtype at some choke points and not at others, and every one of these four is a
    site that forgot to.** `str_field` (`compiler/tychoc0.ty:10175`) opens with
    `resolve_nt`; `eq_field`, whose own comment claims to mirror it, did not.

    **Phase 23 (the `Arr_*` mangling collision) did NOT collapse for free.** Nothing
    here renames a mangling site, and the collision is independent of newtypes. Measured
    after the fix (`/tmp/ph33/probe2.py`):
    ```
    p23_arr_of_bounded   tychoc ok "n 1 [1, 2]"   tychoc0 CCFAIL
        error: implicit declaration of function 'Arr_b4_int_from'; did you mean 'Arr_int_from'?
    p23_arr_of_fixarr    tychoc ok "n 1 [1, 2]"   tychoc0 CCFAIL
        error: implicit declaration of function 'Arr_f2_int_from'; did you mean 'Arr_int_from'?
    ```
    Phase 23 stays closed on its own terms (~22 mangling sites, user-confirmed
    will-not-do). Its reopen condition — "check whether Phase 33's fix collapses this
    for free" — is now answered: it does not.

    **PLAN CITATIONS RE-DERIVED (the prompt's were stale, as warned).**

    | cited | actual | what is there |
    |---|---|---|
    | Phase 19's DFS at `src/tychoc.c:10039-10070` | **`:10124-10130`** | `inline_arrc` / `needs_body_first` |
    | `register_builtins` `src/tychoc.c:4103-4133` | **`:4140-4171`** | (Phase 35 also needed this) |

    **The audit's §7 symptom did NOT reproduce as written, and the reason matters.** A
    `type C = [2]int` parameter is fine *if anything in the program grounds the
    underlying shape* — and every probe that constructs a `C(...)` does. The defect
    needs the family to be referenced with **no** literal of that shape anywhere:
    ```
    type C = [2]int            # or [string: int] / bounded[4]int

    fn f(c: C) -> int:         # never called
        return 1

    fn main():
        println("hi")
    ```
    ```
    BEFORE  tychoc0 FRONT ok, then cc:
              error: unknown type name 'Arr_f2_int'; did you mean 'Arr_int'?
              error: unknown type name 'Map_str_int'
              error: unknown type name 'Arr_b4_int'; did you mean 'Arr_int'?
    AFTER   all three: FRONT ok, cc clean, run clean
    ```
    The audit's own row is therefore accurate but under-specified; `tests/newtype_over_aggregate.ty`
    carries an `unreached(a: FA, m: MP, b: BD)` function precisely to keep that condition
    fixtured, because a fixture that constructs every value would pass even unfixed.

    **POSITION MATRIX — FRONT/CC/RUN on both compilers, 3 forms x 10 positions**
    (`/tmp/ph33/probe.py`; `--emit-c` on both sides plus a separate
    `cc -O1 -fwrapv -pthread -std=c11`; tychoc0's rc is a frontend verdict only, it
    never invokes cc). Cells are `tychoc/tychoc0`; `ok` = FRONT + CC + RUN all clean.
    Divergent-or-broken rows **21 -> 3**:

    | position | fixarr `[2]int` | map `[string:int]` | bounded `bounded[4]int` |
    |---|---|---|---|
    | local, bare literal + annotation | FRONT/FRONT | FRONT/FRONT | FRONT/FRONT |
    | local, `C(...)` | ok/ok | ok/ok | ok/ok |
    | param | ok/ok | ok/ok | ok/ok |
    | return | ok/ok | ok/ok | ok/ok |
    | struct field | ok/ok | ok/ok | ok/ok |
    | `[C]` element | ok/ok | ok/ok | ok/ok |
    | `[string: C]` value | ok/ok | ok/ok | ok/ok |
    | `[1]C` element | ok/ok | ok/ok | ok/ok |
    | `bounded[2]C` element | ok/ok | ok/ok | ok/ok |
    | tuple element | ok/ok | ok/ok | ok/ok |

    The three remaining `FRONT/FRONT` rows are the ONLY non-`ok` cells and they are
    **agreement, not divergence**: `c: C = [1, 2]` assigns a bare `[2]int` literal to a
    `C`-annotated local, which both compilers reject on newtype identity. Only the
    message TEXT differs, which is expressly not normative (`00-conventions.md` §1.3,
    `appendix-f-impl-defined.md:63-64`):
    ```
    tychoc   /tmp/.../a_fixarr_local.ty:4: error: declared type C but value is [int]
    tychoc0  line 4: 'c' expects C, got a plain [2]int value (newtype identity differs)
    ```
    Before the fix that same matrix had 21 non-identical cells: 12 tychoc0 CCFAILs on
    `==`, 3 on map emission order, 4 tychoc CCFAILs on incomplete types, plus the 3
    shared rejections above. Every one of the 27 legal cells now builds, runs, and
    prints the same bytes on both compilers.

    **THE FOUR FIXES.**
    1. **tychoc0 `collect_elem_types` (`compiler/tychoc0.ty:11797-11801`).** The walk
       seeded array families from every function signature, struct field and enum
       payload, but never from a **newtype declaration**. `note_arr_types(&acc,
       prog.ntunder[i])` for every newtype. This is the audit's §7 row, and nothing
       else fixes it — the family is referenced by `cty` at a position no literal
       reaches.
    2. **tychoc0 `eq_field` (`:10135-10139`).** Fell through every aggregate predicate
       for a newtype name and emitted the scalar `(a == b)` on a whole C struct. Now
       recurses on `resolve_nt` first. `str_field:10175` had done this since it was
       written, and its comment says it "Mirrors eq_field's dispatch" — the mirror was
       one-way.
    3. **tychoc0 emission order (`inline_arr_of_map:10060-10066` + the driver at
       `:16849` and `:16904`).** `[C]` / `[2]C` / `bounded[4]C` over a map newtype were
       emitted with the ordinary array families, i.e. before `gen_map_fns`, so
       `Arr_map_str_int_copy` called an undeclared `map_str_int_copy`. `inline_arr_of_map`
       gained `dc`/`ctx` and resolves at both the top and the element; the two driver
       tests resolve too. Exactly the deferral Phase 19 built for a bare
       `[2][K:V]`, extended through the newtype.
    4. **tychoc `needs_body_first` + `emit_aggregate` (`src/tychoc.c:10125-10130`).**
       `t = base_of(t)` at both entries. `c_type(newtype)` already returned the
       underlying's C type (`:1235`), so a `type C = [2]int` struct field emitted
       `Arr_f2_int f_c;` while the inline array's body was never pulled ahead of the
       struct — `field 'f_c' has incomplete type`. This is Phase 19's DFS, and the fix
       is that the DFS resolve the newtype; no new traversal.

    **A FIFTH, and why the fixture is written the way it is.** With row 2 fixed, the
    general `==` path (`:6835`) began *reaching* the structural branch for aggregate
    newtypes and then over-rejecting: its identity check compared two raw `type_of`
    strings, and `type_of` resolves a newtype at a VAR read but not at an element read
    or a sig return, so `xs[0] == a` came out `FA` vs `[2]int`. Now compares
    `resolve_nt` on both sides. A first attempt ALSO compared `nt_skin_of` to keep
    newtype identity — that closed a real pre-existing fail-open (`A == B` over two
    distinct newtypes with the same underlying) **but broke `tests/newtype_agg.ty`**,
    because `nt_skin_of` returns `""` for an INFERRED local (`dup := ids`):
    ```
    env -u LD_PRELOAD make fixpoint
      FAIL newtype_agg.ty (B differs from the C compiler)
      tychoc0: line 33: cannot compare [int] with [int]   <-- `if dup == ids:`, legal
    ```
    Caught by `make fixpoint`, not `make test` — `tests/run.sh` runs only tychoc on the
    positive lane, so tychoc0's behaviour on `tests/*.ty` is gated by fixpoint and
    corelib. The skin condition was removed: `A == B` fails open exactly as it did
    before this phase (verified against a HEAD-built tychoc0: `rc=0`), and closing it
    requires completing `nt_skin_of` first. Filed as Phase 40, not smuggled in.

    **`[2]int` / `bounded[4]int` inside a struct still cannot be INFINITE.** The DFS
    change makes the newtype transparent, so a genuine cycle had to stay closed. Both
    declaration orders, both compilers:
    ```
    type C = [2]Node / struct Node: k: C     tychoc  unknown type 'C' (fwd ref)
                                             tychoc0 infinite type: Node contains itself by value
    struct Node: k: C / type C = [2]Node     same verdicts, both nonzero
    ```

    **LEGAL-PROGRAM CONTROLS — the whole permitted underlying set (Phases 20/27) still
    works**, param position, FRONT/CC/RUN, both compilers, identical output:
    `int` `ok/ok`, `float` `ok/ok`, `string` `ok/ok`, `bool` `ok/ok`, `struct` `ok/ok`,
    `[int]` `ok/ok`. Plus value semantics: `copy_map` and `copy_fixarr` (a stored
    element is independent of later mutation of the source; `==` between two stored
    elements is the underlying's deep equality) `ok/ok`, output identical.

    **FIXTURES.** `tests/newtype_over_aggregate.ty` + `.out` — one positive fixture
    covering all three forms in all nine legal positions, the never-called
    newtype-parameter function that is the only shape exposing defect 1, `==` both
    directly and through a container element, and the value-semantics check. Output is
    byte-identical on both compilers. Two shapes are deliberately spelled around
    pre-existing over-rejections that are NOT this phase's subject and are filed as
    Phase 40, each with an in-file comment saying so: a map literal whose value is an
    already-newtyped variable, and `local == call_returning_the_newtype`.

- [x] **Phase 34 — `15-program.md`'s `main`-signature provenance cites the wrong lines (audit §8)**
  - `docs/spec/15-program.md:31-32` cites `src/tychoc.c:6354-6355` and `:6379-6380`.
    Those lines are `s->decl_type = t; vars_push(…)` and the `declared type %s but value
    is %s` diagnostic — unrelated code. The live sites are `:7097-7098` (`no 'main'
    procedure`) and `:7123-7124` (the signature rule).
  - Docs only. Worth pairing with Phase 28, which touches the same rule.
  - Done when: the citation resolves to the code it claims, `make spec-check` and
    `make check-links` green.
  - **DONE 2026-07-25**, paired with Phase 28 as the plan suggested.
  - **All four line ranges verified by reading them, not by trusting the plan.**

    | cited range | what is actually there |
    |---|---|
    | `src/tychoc.c:6354-6355` | `die_at(… "a value if/match cannot produce a task handle")` and `s->decl_type = t;` — unrelated |
    | `:6379-6380` | `if (s->typed_decl) {` / `if (t != s->annot)` — the `declared type %s but value is %s` path, unrelated |
    | `:7097-7098` (the plan's proposed replacement) | `}` and `Sig *m = sig_find("main");` — **off by one**; the diagnostic `no 'main' procedure` is at `:7099` |
    | `:7123-7124` (the plan's proposed replacement) | `vars_push(…)` / the `if` — **off by one**; the `die_at` is at `:7125` |

    So the plan's own replacement lines were wrong too. The citation now written is
    **`src/tychoc.c:7098-7099`** (the `sig_find("main")` lookup and the `no 'main'
    procedure` diagnostic) and **`:7124-7125`** (the `if` and its `die_at`), verified
    against the file after Phase 28 landed — Phase 28 changed `compiler/tychoc0.ty`
    only, so no `src/tychoc.c` line moved.
  - Both sites in `docs/spec/15-program.md` were stale, not one: the chapter provenance
    block at `:19` and the normative sentence at `:31-32`. Both corrected, and `:32` now
    also names the twin site `compiler/tychoc0.ty` `parse_program` `:3637-3648` that
    Phase 28 added, so the rule has a citation on both compilers.
  - `make spec-check`: `spec-examples: 7 runnable example(s), all pass` (including
    `docs/spec/15-program.md:36`, the `fn main():` example in this very section).
    `make check-links`: `link check: ok (122 markdown files, no dead relative links)`.

### Filed by Phase 26 (2026-07-25)

- [x] **Phase 35 — tychoc0 arity-checks NO builtin; a wrong-arity builtin call crashes the compiler instead of diagnosing it (found by Phase 26, out of its one-builtin scope)**
  - **Pre-existing, not introduced by Phase 26** — `substr` shows the identical
    shape, and it predates this plan. Measured on both, 2026-07-25:
    ```
    $ tychoc  bad3.ty          # fn main(): print(str(char_at(s)))
    bad3.ty:3: error: 'char_at' takes 2 argument(s), got 1
    $ tychoc0 bad3.ty
    tycho: index 1 out of bounds (len 1)      # tychoc0's OWN runtime abort
    $ tychoc0 bad4.ty          # fn main(): print(substr("hi"))
    tycho: index 2 out of bounds (len 1)      # same shape, pre-existing
    ```
  - Mechanism: tychoc gets arity checking free from the `Sig` table
    (`src/tychoc.c:5486-5488`, `'%s' takes %d argument(s), got %d`). tychoc0 has
    no such table — its builtin codegen ladder indexes `args[1]`/`args[2]`
    directly (`compiler/tychoc0.ty:6788-6797`), so a short arg list walks off the
    end of the array and hits tychoc0's own bounds abort. The exit status is
    nonzero and a message is printed, so it fails *closed*, but the message names
    an internal array index rather than the user's mistake, and it carries no
    source location. Phase 26 deliberately did **not** widen to fix this: the
    fix is a per-builtin arity table, which is exactly the "refactor the builtin
    table" the phase was told not to do.
  - Scope: an arity check for the whole builtin set in tychoc0, message matching
    tychoc's. Ideally one table consulted by both `check_call_args` and the
    codegen ladder. Blast radius: every builtin call site in every fixture.
  - Also here (docs, cheap, same file): `docs/spec/16-builtins.md`'s
    `> Provenance:` lines cite a `register_builtins` at `src/tychoc.c:3818-3849`
    / `:3833-3835`; the function actually lives at `:4103-4133` today (`:3833` is
    inside a path-joining helper). Verified 2026-07-25 while adding `char_at`'s
    own — correct — citation. At least four provenance lines in that one file
    carry the stale range; the rest of the spec was not swept.
  - Done when: a wrong-arity builtin call produces a located diagnostic on
    tychoc0 naming the builtin and both arities, reject fixtures lock it for a
    representative sample, and the gate set is green.

  - **DONE 2026-07-25**, as one phase group with 33.

    **PLAN CITATIONS RE-DERIVED. Three of the four were stale.**

    | cited in this phase block | actual | what is there |
    |---|---|---|
    | `Sig` arity check `src/tychoc.c:5486-5488` | **`:5522-5524`** | `die_at(..., "'%s' takes %d argument(s), got %d", e->sval, s->nparams, e->nargs)` |
    | `register_builtins` `src/tychoc.c:4103-4133` | **`:4140-4171`** | 27 `Sig` appends, each with `.nparams` |
    | Phase 26's `char_at` one-off `compiler/tychoc0.ty:11064-11075` | **`:11372-11383`** | the two argument-TYPE checks in `check_call_args` |
    | codegen ladder `compiler/tychoc0.ty:6788-6797` | *not the only crash site* | see below — `type_of` crashes strictly earlier |

    **THE SWEEP — 56 builtins, 174 probes, every arity from 0 to correct+1**
    (`/tmp/ph33/sweep35.py`; names taken from tychoc0's own `is_builtin_call`
    (`compiler/tychoc0.ty:3099-3100`) plus `print`/`println`/`to_under`/`wait`/`send`/
    `recv`/`close`; each probe run on BOTH compilers, and on tychoc0 in both the
    `_r := call` and bare-statement forms with the worse outcome taken). Classification:
    `CRASH` = tychoc0's own internal abort, `unloc` = a diagnostic with no source
    location, `DIAG` = a located diagnostic, `ok` = accepted.

    | wrong-arity probes (118) | BEFORE | AFTER |
    |---|---|---|
    | **CRASH** (tychoc0's bounds abort, no location) | **49** | **0** |
    | **ok** (silently ACCEPTED — a fail-OPEN tychoc rejects) | **44** | **0** |
    | unlocated diagnostic | 14 | 12 |
    | located diagnostic | 11 | 106 |

    | correct-arity probes (56) | BEFORE | AFTER |
    |---|---|---|
    | accepted | 49 | **49** (no regression) |
    | not accepted (probe artefacts, unchanged) | 7 | 7 |

    So: **49 builtins crashed the compiler, 44 wrong-arity calls were accepted outright,
    and 11 diagnosed. Now 106 diagnose and none crash.** The 44 fail-opens were the half
    this phase block did not know about — it described only the under-supply crash.

    The 12 remaining unlocated rows are all four `map_get`/`map_has`/`map_set`/`map_del`
    names at every arity. Both compilers **reject** them (they were removed from the
    language: `map_get was removed; use m.get(k, default)`), so the verdict agrees and no
    arity question arises; only tychoc0's removal message lacks a location, which is a
    pre-existing message-location gap in a different diagnostic. Filed as Phase 40.
    The 7 correct-arity non-accepts are the same four `map_*` names plus `Ok`/`Err`/`None`
    needing a type annotation to infer — probe artefacts, identical before and after,
    and tychoc rejects those 7 too.

    **THE CRASH, and why one check was not enough.** tychoc gets arity free: every `Sig`
    carries `.nparams` (`src/tychoc.c:4140-4171`) and one test rejects a mismatch
    (`:5522-5524`). tychoc0 had none, so a short arg list ran off the end of `args`:
    ```
    $ tychoc0 bad.ty        # fn main(): println(substr("hi"))
    tycho: index 2 out of bounds (len 1)      # internal, no source location
    ```
    The first attempt put the check at the top of `check_call_args`
    (`compiler/tychoc0.ty:11348`), the one place every call in every body passes through.
    That took 49 crashes to **6** — `pop()`, `keys()`, `Some()`, `Ok()`, `Err()`,
    `recv()` still aborted, because `type_of`'s `ECall` arm indexes `args[0]` unguarded
    (`:5641` `pop`, `:5646` `keys`, `:5652` `recv`, `:5661-5668` `Some`/`Ok`/`Err`) and
    `type_of` runs **strictly earlier** than the collect walk that calls
    `check_call_args`. The plan's cited codegen ladder is a third such site, later still.
    So the gate is one function called from the two earliest choke points.

    **THE FIX — one table, one gate, two call sites.**
    - `builtin_arity(name) -> int` (`compiler/tychoc0.ty:4811-4838`): the single source
      of arity truth. Numbers are `16-builtins.md`'s signature column, cross-checked
      against tychoc by the sweep. `-1` = deliberately not checked here, each case
      justified in the comment (`print`/`println` are parsed specially and already give a
      located parse error; `null` is not a callable builtin on either compiler; the
      `map_*` names are rejected as removed before this runs).
    - `ck_builtin_arity(dc, ctx, nm, nargs, el)` (`:4840-4855`): the gate. Returns
      silently when the program declares the name itself — `is_variant` / `is_struct` /
      `dc.sigmap` — so a user enum with a `Some` variant, a struct constructor, or a user
      fn shadowing a builtin name is untouched.
    - Called from `type_of`'s `ECall` arm (`:5637`) and from `check_call_args` (`:11349`).

    **WORDING.** tychoc's `Sig` diagnostic verbatim,
    `'%s' takes %d argument(s), got %d`, for the whole set — one shared message, as the
    phase asked. For the ~27 `Sig` builtins that is byte-identical to tychoc modulo the
    two compilers' known prefix formats (`file:LINE: error:` vs `line LINE:`, this plan's
    Pre-flight). For tychoc's *magic* builtins tychoc uses bespoke text
    (`len(...) takes one argument`, `pop(arr) takes one argument`) and only the
    accept/reject verdict is shared — which is the normative part
    (`00-conventions.md` §1.3, `appendix-f-impl-defined.md:63-64`). Mirroring ~15
    bespoke literals was judged churn against a non-normative property; the divergence
    is visible in the fixture evidence below rather than hidden.

    **PHASE 26's `char_at` ONE-OFF: its ARITY half is folded in, its TYPE half retained
    and that retention is load-bearing.** Phase 26's check was never an arity check — it
    validates the two argument *types* (`compiler/tychoc0.ty:11372-11383`), which the
    general arity gate says nothing about. What it *did* carry was a defensive
    `and len(args) == 2` guard, added only so the check could not itself read out of
    bounds. That guard is now redundant — `ck_builtin_arity` guarantees the arity earlier
    in the same function — and has been removed, so the condition is a plain
    `if nm == "char_at":`. Its two reject fixtures
    (`tests/reject/char_at_arg_index_type.ty`, `char_at_arg_recv_type.ty`) still pass.

    **REJECT FIXTURES — 5, one per distinct rejection (the compiler halts at the first
    error), each verified to be rejected by BOTH compilers:**
    ```
    builtin_arity_too_few     substr("hi")   tychoc  :8: error: 'substr' takes 3 argument(s), got 1
                                             tychoc0 line 8: 'substr' takes 3 argument(s), got 1
    builtin_arity_char_at     char_at(s)     tychoc  :6: error: 'char_at' takes 2 argument(s), got 1
                                             tychoc0 line 6: 'char_at' takes 2 argument(s), got 1
    builtin_arity_niladic     clock(1)       tychoc  :4: error: 'clock' takes 0 argument(s), got 1
                                             tychoc0 line 4: 'clock' takes 0 argument(s), got 1
    builtin_arity_too_many    len(xs, 1)     tychoc  :6: error: len(...) takes one argument
                                             tychoc0 line 6: 'len' takes 1 argument(s), got 2
    builtin_arity_zero_args   pop()          tychoc  :7: error: pop(arr) takes one argument
                                             tychoc0 line 7: 'pop' takes 1 argument(s), got 0
    ```
    The first three are text-identical to tychoc; the last two are the magic-builtin
    wording divergence, verdict and line number identical. `builtin_arity_too_many` locks
    the fail-open half and `builtin_arity_zero_args` locks the `type_of`-path crash that
    one check placement would have missed.

    **SELF-HOSTING, checked directly before the gates** (tychoc0 uses builtins heavily,
    so an over-tight arity table would break it first):
    ```
    tychoc0 compiler/tychoc0.ty --emit-c   rc=0, 2415027 bytes
    cc ... -> tychoc0_b ; tychoc0_b compiler/tychoc0.ty --emit-c
    cmp: B == C  (self-reproduction holds)
    ```

    **DOCS — every stale `register_builtins` provenance range in
    `docs/spec/16-builtins.md` corrected, and each new citation verified by reading the
    line.** The old ranges were pre-`char_at` and pre-Phase-37, so the offset is not even
    constant (+322 before `char_at`, +323 after it):

    | line | was | now | verified content |
    |---|---|---|---|
    | 17 | `:3818-3849` | `:4140-4171` | `static void register_builtins(void) {` … `}` |
    | 33 | `:3818-3849` | `:4140-4171` | same |
    | 84 | `:3822-3840` | `:4144-4163` | `print` … `getenv` |
    | 140 | `:3833-3835` | `:4155-4156` + `:4158` | `substr`, `find`; `split` (`:4157` is now `char_at`) |
    | 141 | `char_at :4120` | `:4157` | `.name="char_at",.ret=T_CHAR,.params={ T_STRING, T_INT }` |
    | 254 | `:3827-3828`,`:3836-3838` | `:4149-4150`, `:4159-4161` | `clock`/`now`; `read_file`/`write_file`/`list_dir` |
    | 272 | `:3844-3848` | `:4167-4170` | `sqrt`/`pow`/`floor`/`fabs` |
    | 291 | `die :3831` | `:4153` | `.name="die"` |
    | 292 | `:3818-3849` | `:4140-4171` | same |

    `grep 'tychoc.c:3[0-9]' docs/spec/16-builtins.md` is now empty. **The file's
    NON-`register_builtins` citations were spot-checked and are ALSO stale** (`:7342`
    "eprint codegen" is a blank line; `:7421-7423` "die codegen" is `body_pushcount`;
    `:8179-8187` "char_at codegen" is the `map_get`/`map_has` block; `:4789-4794` "len
    magic" is slice checking; `:4845-4851` "keys" is `zero$`). That is a separate docs
    sweep beyond this phase's named scope — filed as Phase 40 with the evidence rather
    than absorbed.

### Filed by Phases 27/28/30/31/34 (2026-07-25)

- [ ] **Phase 36 — tychoc0 accepts a generic type argument that WHOLLY names a foreign type parameter (found by Phase 30, out of its B20/B21 scope)**
  - **MAIN-AGENT RULING 2026-07-25, made on delegation ("close the 2 remaining phases")
    after the question was put to the user three times and left open. Direction: SPEC THE
    LIMITATION, add the check to tychoc0, and file the expressiveness gap as named future
    work. Stated loudly because it blesses a real hole.**
  - **Why not simply leave it open.** The two compilers disagree on the *accept/reject
    decision* for `fn wrap(x: $U) -> Box($U)`. That is the one property
    `docs/spec/00-conventions.md` §1.3 and `appendix-f-impl-defined.md:63-64` make
    normative — the two-implementation conformance oracle is the project's central
    claim. Leaving it open means shipping a known oracle violation, which costs more than
    any generics limitation does.
  - **Why not implement it in tychoc instead.** Phase 39 established this is structural,
    not an oversight: `parse_type` defers only *positional self-reference*, and that works
    only because `struct_instantiate` indexes the caller's binds by the template's own
    typaram ids. Deferring `Box($U)` reads an unset slot. There is no node for an
    unapplied generic application — supporting it needs a **new type form**. That is a
    language feature, and building one unilaterally to close a conformance gap is far
    outside what this plan should decide.
  - **What this costs, stated honestly rather than buried.** Tycho will not be able to
    express these, and no renaming workaround reaches them:
    ```
    fn dup(x: $A) -> Pair($A, $A)
    fn swap(p: Pair($A,$B)) -> Pair($B,$A)
    ```
    A language that cannot write `swap` over its own pair type has a real expressiveness
    hole. tychoc0 implements both correctly today (probed across swap, duplication,
    struct fields and type identity, ASan+UBSan clean) and **this ruling removes that
    working behaviour**. The justification is the same one the user accepted for Phase 39:
    such programs compile on exactly one of the two compilers, so they were never
    portable Tycho — but unlike Phase 39 this is a capability loss, not just a tightened
    check, and it should be recorded as one.
  - **Reversible by design.** Reversing it is: delete the spec sentence, delete tychoc0's
    check, delete the reject fixtures — then build the type form. Nothing here forecloses
    that, and the ROADMAP entry exists to keep it visible.
  - **MAIN-AGENT DIRECTION 2026-07-25 (user delegated: "do 36 and 39 too"): DO NOT RULE
    ON PREFERENCE — SETTLE A FACT FIRST.** Unlike Phase 39 this is not an oversight with
    an obvious direction. The disputed program is ordinary generic code:
    ```
    fn wrap(x: $U) -> Box($U):
        return Box(x)
    ```
    tychoc REJECTS it; tychoc0 ACCEPTS, compiles and RUNS it, printing the right answer.
    If tychoc is right, the language cannot express a generic function returning a
    generic struct over its own type parameter — a severe hole. If tychoc0 is right,
    tychoc is over-tight. `05-generics.md:80-82` ("fully concrete or a whole
    own-parameter reference") genuinely reads both ways depending on whose *own* is
    meant, so the spec cannot settle it.
  - **The deciding question is capability, not taste: CAN tychoc's monomorphizer
    instantiate this, and it merely refuses — or can it genuinely not?** Evidence to
    gather before touching either compiler:
    - Does the pattern appear anywhere in `corelib/` (36 packages), `examples/`, or
      `tests/`? If it is absent, check whether the code visibly WORKS AROUND it
      (e.g. concrete wrappers, `$T`-named parameters chosen to match the struct's) —
      a workaround is evidence the restriction bites in practice.
    - Can `ginst` (`src/tychoc.c`, the instantiation path) bind a foreign parameter, or
      does its design assume self-reference? Read it; do not infer from the error text.
    - Does tychoc0's acceptance produce CORRECT code in non-trivial cases — nested
      generics, two foreign parameters, a foreign parameter inside a container — or does
      it only happen to work for the one-level case measured? Probe it. tychoc0 being
      permissive is not evidence of correctness.
  - **Then rule, and say which:** if tychoc CAN support it, the pattern becomes legal,
    tychoc relaxes, and the spec gains a sentence saying any in-scope parameter counts.
    If it genuinely CANNOT, the spec says so plainly, tychoc0 gains the check, and the
    limitation is documented rather than silent. **If the answer is "cannot, and this is
    a real expressiveness hole", STOP and report rather than speccing the limitation** —
    that is a language-design decision for the user, not a conformance fix.
  - Phase 30 closed the *partial* mention (`Box([$T])`). tychoc refuses more than that:
    at `src/tychoc.c:1900-1907` it defers only the **self-reference** — the generic
    applied to *exactly its own* type parameters — and then dies on any remaining
    argument for which `has_typaram()` holds. So a WHOLE reference to a *different*
    parameter is refused too. Measured 2026-07-25, both compilers, FRONT/CC/RUN:
    ```
    struct Box($T):
        v: $T
    fn wrap(x: $U) -> Box($U):
        return Box(x)
    fn main():
        b := wrap(5)
        print(str(b.v))

    tychoc  :3: error: generic struct 'Box': a type argument may not partially mention a
                       type parameter; use the generic applied to its own parameters
                       (a recursive reference) or to concrete types
    tychoc0 ACCEPT / CCOK / RUNS -> prints `5`
    ```
    The control `Box($T)` (the SAME name as the struct's own parameter) is
    `ACCEPT/CCOK/RUN` on **both** — tychoc interns type parameters globally by name, so
    the self-reference test matches on the name.
  - **Not folded into Phase 30 deliberately.** Rows B20/B21 name the *partial* case, and
    that is what the audit measured. Closing this one needs the generic's own parameter
    list at parse time — obtainable from the token stream (`struct Box($T)` is right
    there) but a bigger scan than `declares_kind_name`, and getting it wrong
    over-tightens every generic aggregate in the corpus. It wanted its own phase with its
    own verification rather than being absorbed silently.
  - **Which way it should go is not obvious and should be settled first.**
    `05-generics.md:80-82` says a type argument "MUST be either fully concrete or a whole
    own-parameter reference". `Box($U)` inside `fn wrap(x: $U)` *is* a whole reference to
    a parameter, just not to `Box`'s own — so the spec sentence can be read either way
    depending on whose "own" is meant. Decide what the sentence means BEFORE changing
    either compiler; if it means the generic's own, tychoc is right and tychoc0 gains a
    check; if it means any in-scope parameter, tychoc is over-tight and the spec needs a
    sentence saying so.
  - Done when: the spec sentence is unambiguous, both compilers agree on the decision,
    the `Box($T)` self-reference control still runs on both, fixture-locked, gates green.

  - **STILL OPEN — DELIBERATELY. Investigated in full 2026-07-25 alongside Phase 39; the
    answer is the DIRECTION block's third branch ("cannot, and this is a real
    expressiveness hole"), which says STOP AND REPORT rather than spec the limitation.
    No compiler and no spec line was changed for this phase. The box stays unticked
    because this is a language-design decision for the user.**

    **The capability question, answered from the source (not from the error text).**
    tychoc genuinely cannot instantiate it today, and the reason is structural:
    - `parse_type` defers **only** the positional self-reference. `src/tychoc.c:1938-1941`
      computes `self_ref` as `args[i] == g_structs[sid].typarams[i]` for every `i`, and on
      a hit returns the bare template type `STRUCT_TYPE(sid)`; anything else with a
      typaram in it dies at `:1944-1948`.
    - That deferral works **only** by name coincidence. `subst_type` (`:1537`) re-
      instantiates the deferred template with the **caller's** `binds`, and
      `struct_instantiate` (`:1656-1657`) indexes that array by the **template's own**
      typaram ids: `binds[(int)(g_structs[tmpl].typarams[i] - T_TYPARAM_BASE)]`. Given
      call-site binds `{$U → int}`, instantiating `Box` (whose parameter is `$T`) reads an
      **unset** slot: the instance would be named `Box__void` and its field would keep a
      live `$T`. Deferring `Box($U)` the same way does not under-approximate — it
      miscompiles.
    - So there is nowhere to put the renaming `Box.$T := $U`. `Type` is a flat scalar tag
      and there is no node for an *unapplied generic application*. Supporting the pattern
      needs a new type form (a `T_GAPP`-style side table alongside `g_arrtypes` /
      `g_restypes`), a `subst_type` case that composes bindings, and interning care so
      `Box($U)` at `$U=int` is the SAME type as a direct `Box(int)`. That is a type-system
      feature, not a conformance fix.

    **tychoc0's acceptance is not luck — it is correct everywhere it was probed.** Every
    row FRONT/CC/RUN, and the emitted C re-run under ASan+UBSan (`-fno-sanitize-recover=all`,
    clean, rc=0, same output):

    | probe | tychoc | tychoc0 |
    |---|---|---|
    | `fn wrap(x: $U) -> Box($U)`, instantiated at `int` **and** `string` | REJECT | ACCEPT → `5`, `hi` |
    | `struct Pair($A,$B)` + `fn mk(x: $U, y: $V) -> Pair($U,$V)` | REJECT | ACCEPT → `5`, `hi` |
    | names in scope but **swapped**: `fn flip(x: $B, y: $A) -> Pair($B,$A)` | REJECT | ACCEPT → `5`, `hi` (positional, not name-matched) |
    | foreign parameter in a **struct field**: `struct Wrap($A): b: Box($A)` | REJECT | ACCEPT → `5` |
    | **type identity**: `a := wrap(5); b := Box(7); a = b` | REJECT | ACCEPT → `7` (both intern to one type) |
    | nested `Box(Box($U))` | REJECT | **REJECT**, same message — Phase 30's partial-mention rule, agreed |
    | control `fn wrap(x: $T) -> Box($T)` | ACCEPT → `5`, `hi` | ACCEPT → `5`, `hi` |

    The swap and identity rows were chosen specifically to expose a name-matching
    shortcut; tychoc0 passes both. `mono_instantiate` substitutes positionally via
    `subst_field_type` and interns by concrete name (`inst_name`), which is why renaming,
    reordering and duplication all come out right.

    **The workaround is NOT easy and NOT complete — this is the finding that forces the
    third branch.** The workaround is "name the function's type parameter the same as the
    struct's". Both corpus occurrences already do exactly that
    (`tests/generic_enum_param.ty:15` `fn wrap(x: $T) -> Opt($T)` over `enum Opt($T)`;
    `tests/generic_struct_instance_types.ty:16` `fn wrap(x: $T) -> Box($T)` over
    `struct Box($T)`), and `$T` accounts for 913 of the corpus's typaram mentions, so the
    restriction is invisible in practice — but only because the corpus never needed
    anything else. **Two shapes no renaming can express**, both rejected by tychoc and
    both correct on tychoc0:
    ```
    fn dup(x: $A) -> Pair($A, $A)              tychoc REJECT   tychoc0 -> 5, 5
    fn swap(p: Pair($A,$B)) -> Pair($B,$A)     tychoc REJECT   tychoc0 -> hi, 5
    ```
    `dup` needs one function parameter to occupy two of the struct's slots — a name can
    only be one thing. `swap` needs a permutation, and `self_ref` is positional-identity,
    so every permutation fails. A language that cannot write `swap` on its own pair type
    has an expressiveness hole, not a stylistic restriction.

    **Consequences of each direction, for the user to weigh:**
    - *Relax tychoc* (make the pattern legal): the right answer semantically, and safe in
      blast radius — the new path is reachable only where tychoc dies today, so nothing
      currently green changes route. But it is a type-system addition (the `T_GAPP` node
      above), not a small patch, and it deserves its own phase and its own ruling.
    - *Spec the limitation and add the check to tychoc0*: cheap, and it would close the
      divergence — but it would bless `swap` and `dup` as permanently inexpressible, and
      it would make tychoc0 **lose** working, correct, ASan-clean behaviour. Shipping that
      sentence is the call this phase was told not to make on the user's behalf.
    - `05-generics.md:80-82` is left exactly as it was. It is ambiguous, and resolving the
      ambiguity IS the decision.

- [x] **Phase 37 — tychoc's `binds[256]` arrays are indexed by the GLOBAL type-parameter id, which is unbounded: a program with >256 distinct `$Name`s overruns a stack array (found by Phase 29's coupling trace, out of its F2/G2 scope)**
  - **This is NOT the 8 → 16 widening Phase 29 did.** Those were *per-generic* arrays and
    are now all sized by `TYCHO_MAX_TYPARAMS` (`src/tychoc.c:538`). These are a different
    family: six fixed locals sized **256** that are indexed by `t - T_TYPARAM_BASE`
    (`:637`), the id of a type parameter in the **program-wide** `g_typarams` table —
    and that table grows without a bound (`typaram_of`, `:708-716`, plain `TBL_ENSURE`,
    no cap, no `die_at`).
  - Sites: `Type binds[256]` at `src/tychoc.c:1919`, `:1949`, `:5106`, `:5141`, `:6802`;
    `Type b[256]` at `:4098`; and the sibling `int64_t sizebinds[256]` at `:6804`, which
    is indexed by size-parameter id and has the same shape.
  - **Reproduced, not theorised.** 40 generic functions × 8 distinct type parameters =
    320 distinct `$Name`s, then one call. An ASan+UBSan build of tychoc
    (`ASAN_OPTIONS=detect_leaks=0`) says:
    ```
    src/tychoc.c:6803:48: runtime error: index 256 out of bounds for type 'Type [256]'
    ```
    That is `for (int i = 0; i < g_ntyparams; i++) binds[i] = T_VOID;` walking off the
    end of the local at `:6802`. The release build has no such check: it writes past the
    array into the frame. So this is a memory-safety hole reachable from a *valid*
    program (each individual generic is well under the 16-parameter cap), not a rejected
    one.
  - Two candidate fixes, both need the spec consulted first: (a) heap-allocate the bind
    vectors at `g_ntyparams` (`ginst`'s `gi.binds` at `:6870` already does exactly this,
    so the pattern exists in-file), or (b) cap the global table and diagnose, which needs
    a spec sentence because no implementation limit on *total distinct type-parameter
    names* is stated anywhere in `docs/spec/`.
  - Check tychoc0 for the same defect: it stores bindings in `[string]` name/type lists
    (`match_typaram_str`, `compiler/tychoc0.ty:13902`), which grow, so it is probably
    unaffected — but "probably" is not a verdict; probe it.
  - Done when: the 320-distinct-name program either compiles clean under ASan+UBSan or is
    diagnosed by a spec-backed limit; both compilers agree; fixture-locked; gates green.
  - **DONE 2026-07-25. Fix (a) — heap-allocate at the table's current length.** Two
    citations in the prompt were WRONG and are corrected here: `T_TYPARAM_BASE` is
    defined at **`src/tychoc.c:648`** (not `:637`), and `typaram_of` is at
    **`:722-728`** (not `:708-716`). `TYCHO_MAX_TYPARAMS 16` at `:538` was correct.

    **My own grep of every `[256]` in `src/tychoc.c` (pre-fix), classified.** Seven are
    the defect family; three are NOT, each verified capped by reading the guard:
    ```
    214:    int indent_stack[256];          NOT the family -- capped at :249
                                           `if (sp + 1 >= 256) die_at(line, "indentation too deep");`
    592:    static HandleType g_handles[256];  NOT the family -- capped at :3591
                                           `if (g_nhandles >= 256) die_at(..., "too many handle types (max 256)");`
    1919:   Type binds[256];               FAMILY -- parse_type_inner, generic struct application
    1949:   Type binds[256];               FAMILY -- parse_type_inner, generic enum application
    4098:   Type b[256];                   FAMILY -- ufcs_generic
    5106:   Type binds[256];               FAMILY -- generic struct literal, field-driven inference
    5141:   Type binds[256];               FAMILY -- generic enum variant constructor
    6802:   Type binds[256];               FAMILY -- instantiate_generic
    6804:   int64_t sizebinds[256];        FAMILY -- instantiate_generic, `$N` side
    11398:  char *imp_paths[256]; int n_imp = 0;   NOT the family -- scan_imports takes `max`
    11485:  char *imp_paths[256]; int n_imp = 0;   and guards at :11320 `if (*n < max) paths[(*n)++] = ...`
    ```
    So the prompt's list of seven was complete and correct; the three non-family
    `[256]`s are all fail-closed with a real bound check. Also checked the sibling
    growing tables (`g_structs`, `g_enums`, `g_arrtypes`, `g_opttypes`, `g_restypes`,
    `g_tuptypes`, `g_soatypes`, `g_maptypes`, `g_functypes`, `g_chantypes`,
    `g_tasktypes`, `g_sigs`, `g_ginsts`, `g_newtypes`, `g_consts`, `g_vars`,
    `g_laminfo`, `g_esc`, ...): every one of those is reached through a **grown**
    `TBL_ENSURE` table or a bounded per-generic count, never through a fixed local
    indexed by its id. `g_typarams` and `g_sizeparams` were the only two growing
    tables whose ids indexed a fixed-size stack local. **Nothing structurally
    different was found, so no Phase 38 was filed.**

    **The two families are genuinely independent, and BOTH were live.** The `$T` side
    overran the `binds[i] = T_VOID` init loop. The `$N` side is worse than the plan
    said: `:6805`'s init loop *was* clamped (`i < g_nsizeparams && i < 256`), which
    hid it, but `match_type`'s write at `:1585` `g_sizebinds[sid] = cs_` had no clamp,
    so >256 distinct `$N` names wrote past the array with no diagnostic at all.

    **Fix choice — (a), heap-allocate; justified, not defaulted.** Candidate (b) (cap
    the global table + diagnose) was rejected because the limit it would impose does
    not exist in the language. Verified by search, not assumed: `docs/spec/05-generics.md:20`
    says "At most 16 type parameters and 16 size parameters" — **per generic**, and
    that bound is already enforced and already honoured by this program (every
    generic here has 8). Grepping all of `docs/spec/` for `type parameters` / `typaram`
    and `appendix-f-impl-defined.md` for `distinct` / `program-wide` / `per program`
    returns **no** statement of any limit on the *total distinct `$Name`s in a
    program*. So (b) would invent a new user-visible restriction to paper over an
    implementation array size; (a) makes the implementation match what the spec
    already permits, needs no spec sentence, and removes the cliff instead of moving
    it. Raising 256 to a bigger constant was explicitly not done for the same reason.

    Two allocators added next to the tables they size — `new_binds()` after
    `typaram_name` (`:729`) and `new_sizebinds()` after `sizeparam_id` — each
    `xmalloc`ing `g_n{typarams,sizeparams}` entries (min 1) and filling the unbound
    sentinel (`T_VOID` / `0`). All seven sites became `Type *binds = new_binds();` /
    `int64_t *sizebinds = new_sizebinds();`, deleting the init loops.

    **Lifetime / allocation-discipline reasoning (the part that makes (a) safe).**
    Not a new discipline: `gi.binds` at `:6870` already `xmalloc`s a bind vector at
    `g_ntyparams` and never frees it, so this matches the file. Nothing frees a bind
    vector, so nothing can free one early; the leak is bounded and one-shot (tychoc
    is a single-pass compiler that leaks by design throughout — `sfmt`, every
    `xmalloc` of an `Expr`/`Stmt`/`Proc`). Sizing at *allocation time* is sufficient
    rather than merely convenient: `g_typarams` only ever **grows** (`typaram_of` is
    find-or-append, no removal), and every id ever written into a vector belongs to a
    `$Name` interned **before** that vector was allocated — the writers index by
    `gt->typarams[i] - T_TYPARAM_BASE` / `g_{structs,enums}[id].typarams[i] - ...`,
    all recorded when the template was parsed. Confirmed the growth cannot race a
    live vector: every caller of `typaram_of` is parse-time (`:1728`, `:1886`,
    `:3313`, `:3331`, `:3349` — `parse_type`, `parse_type_inner`, `parse_fn`), so
    `g_ntyparams` is final before `resolve_*` runs, and the two parse-time vectors
    (`:1919`/`:1949`) are consumed synchronously by `struct_instantiate`/
    `enum_instantiate` before parsing resumes. `ufcs_generic` is the only hot path
    that now allocates on a negative answer; at `8 * g_ntyparams` bytes per call in a
    process that already leaks every AST node, that is not a regression worth
    branching for. ASan on tychoc itself is not part of any gate (`tests/run.sh:74`
    applies `-fsanitize=address,undefined` to the *emitted* C, and `:50` sets
    `ASAN_OPTIONS=detect_leaks=$TYCHO_LSAN` for those binaries), so no gate can be
    tripped by the added allocations.

    **BEFORE — ASan+UBSan tychoc, 320 distinct `$Name`s, 8 per generic (`/tmp/ph37/p320.ty`,
    generated by `/tmp/ph37/gen.py`).** Built
    `gcc -fsanitize=address,undefined -g -O1 -fwrapv -std=c11 -Ibuild src/tychoc.c`,
    run with `env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0`:
    ```
    src/tychoc.c:1920:60: runtime error: index 256 out of bounds for type 'Type [256]'
    src/tychoc.c:1920:64: runtime error: store to address 0x7b85c49685a0 with insufficient space for an object of type 'Type'
    =================================================================
    ==310664==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x7b85c49685a0 at pc 0x55ad9be9fcc9 bp 0x7fffbf0b7140 sp 0x7fffbf0b7138
    WRITE of size 4 at 0x7b85c49685a0 thread T0
        #0 0x55ad9be9fcc8 in parse_type_inner src/tychoc.c:1920
        #1 0x55ad9bea1c44 in parse_type src/tychoc.c:1713
        #2 0x55ad9beb22aa in parse_stmt src/tychoc.c:3126
        #3 0x55ad9beb39fc in parse_block src/tychoc.c:3221
        #4 0x55ad9beb7dc3 in parse_fn src/tychoc.c:3359
        #5 0x55ad9beb98e4 in parse_program src/tychoc.c:3999
        #6 0x55ad9bf0f8e3 in main src/tychoc.c:11662
    Address 0x7b85c49685a0 is located in stack of thread T0 at offset 1440 in frame
        #0 0x55ad9be9bd71 in parse_type_inner src/tychoc.c:1717
      This frame has 8 object(s):
        [416, 1440) 'binds' (line 1919) <== Memory access at offset 1440 overflows this variable
        [1568, 2592) 'binds' (line 1949)
    SUMMARY: AddressSanitizer: stack-buffer-overflow src/tychoc.c:1920 in parse_type_inner
    ```
    Note this is a hard **stack-buffer-overflow WRITE**, not only the UBSan note the
    plan quoted — the release build writes 4 bytes past a 1024-byte frame object.

    **BEFORE — the `$N` side, proved separately** (`/tmp/ph37/sz320.ty`: 320 distinct
    `[$N]int` size-parameter names, same pre-fix binary). Different site, different
    access, same root cause:
    ```
    ==311153==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x7bddbf7f71a0 ...
    READ of size 8 at 0x7bddbf7f71a0 thread T0
        #0 0x559d25b51325 in match_type src/tychoc.c:1584
        #1 0x559d25bf3f3b in instantiate_generic src/tychoc.c:6818
        #2 0x559d25bed94d in resolve_expr_inner src/tychoc.c:5486
    ```

    **AFTER — same ASan+UBSan build, post-fix.** Clean compile *and* correct run:
    ```
    $ env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 ./tychoc-san2 /tmp/ph37/p320.ty -o /tmp/ph37/p320
    built /tmp/ph37/p320
    $ /tmp/ph37/p320
    41 10 7 8 9 5 ok          (one per line; = the 8-name control's output, byte for byte)
    $ env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 ./tychoc-san2 /tmp/ph37/sz320.ty -o /tmp/ph37/sz_new
    built /tmp/ph37/sz_new
    $ /tmp/ph37/sz_new
    9
    ```
    The 8-distinct-name control (`/tmp/ph37/p8.ty`) was clean before AND after, which
    is what makes the 320 result attributable to the id, not to the program shape.

    **SCALED UP — the cliff is gone, not moved.** Same ASan+UBSan binary, no diagnostic,
    correct output at every size:
    ```
    320  distinct $Names -> built, ran: 41 10 7 8 9 5 ok
    1200 distinct $Names -> built, ran: 41 10 7 8 9 5 ok
    4000 distinct $Names -> built, ran: 41 10 7 8 9 5 ok
    ```
    A raised constant would have failed at some size; nothing here does.

    **tychoc0's verdict — PROBED, genuinely unaffected (not "probably").** tychoc0 has
    no `-o`; it emits C on stdout, so each probe was `tychoc0 X.ty > X.c0.c`, then
    `gcc -O1 -fwrapv -std=c11 X.c0.c -o X.c0.bin -lm -lpthread`, then run:
    ```
    p8      OK  out=41 10 7 8 9 5 ok
    p320    OK  out=41 10 7 8 9 5 ok
    p1200   OK  out=41 10 7 8 9 5 ok
    p4000   OK  out=41 10 7 8 9 5 ok
    sz320   OK  out=9
    ```
    Identical output at every size, no rejection, no crash — consistent with its
    binding representation being growing `[string]` name/type lists
    (`match_typaram_str`, `compiler/tychoc0.ty:13902`) rather than an id-indexed
    array. So the defect was tychoc-only, and after the fix **both compilers agree**
    on all five probes. tychoc0 was built with the fixed tychoc via
    `./tychoc compiler/tychoc0.ty -o /tmp/ph37/tychoc0` (there is no `make tychoc0`).

    **FIXTURE — carried, and it is not impractical: 191 lines.** New
    `tests/generic_many_typaram_names.ty` + `.out`. 272 distinct `$T` names (34 fns x
    8) and 272 distinct `$N` names (34 fns x 8), every generic at 8 parameters so the
    program stays comfortably inside the spec's per-generic 16, then a tail that
    exercises **all seven** sites in one program: `ident(41)` and `ident("ok")`
    (`:6802` + `:6804`, two instances), `xs.firstof()` (`:4098`), `Box(7)` (`:5106`),
    `bb: Box(int)` (`:1919`), `Yep(9)` (`:5141`), `mm: Maybe(int)` (`:1949`), and
    `lastof(fx)` for `[$N]T` size inference. Golden: `41 ok 10 7 8 9 5 6`.
    The fixture is a real lock, confirmed by running it against the **pre-fix** ASan
    binary:
    ```
    $ ASAN_OPTIONS=detect_leaks=0 ./tychoc-san tests/generic_many_typaram_names.ty -o /tmp/ph37/fx_pre
    src/tychoc.c:1920:60: runtime error: index 256 out of bounds for type 'Type [256]'
    ==311553==ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 4
    ```
    and both compilers match the golden post-fix (`TYCHOC-FIXTURE-MATCH`,
    `TYCHOC0-FIXTURE-MATCH`). **Test count 513 -> 514**; no count is hard-coded in
    `tests/run.sh`, so `make test` and `make ilp32` both picked it up automatically.
    `gcc -O2 -fwrapv -Wall -Wextra -std=c11` on the edited `src/tychoc.c` is warning-clean.

    **Gate set — one per command, foreground, `env -u LD_PRELOAD`:**
    ```
    make test         passed: 514   failed: 0 / all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 37   failed 0
    make fixpoint     ok B == C : tychoc0 reproduces itself byte-identically (35376 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32        passed: 514   failed: 0 / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (122 markdown files, no dead relative links)
    ```
    `git status --short` before the commit: `M src/tychoc.c` plus the two new fixture
    files only — no build spill.

- [x] **Phase 38 — NO GATE EVER BUILDS `tychoc` ITSELF UNDER A SANITIZER; that is how Phase 37's overrun survived (filed by the main agent, 2026-07-25)**
  - **Verified, not inferred.** `tests/run.sh:73-76` builds the **emitted C** of each
    fixture with `-fsanitize=address,undefined -fno-sanitize-recover=all`. Every
    sanitizer mention in `Makefile` + `scripts/ci.sh` is about that lane or about
    disabling it for `-m32` (`Makefile:85`, `:202`, `:214` — 32-bit ASan is absent under
    multilib). **Nothing builds `src/tychoc.c` with `-fsanitize`.** So the compiler's own
    memory safety is unmeasured by every gate, in either direction.
  - **This is the coverage gap that let Phase 37 happen.** A stack-buffer-overflow WRITE
    (`src/tychoc.c:1920`, 4 bytes past a 1024-byte frame) sat in the reference compiler
    reachable from a valid program, through 16 phases of this plan and a full 1.0 freeze,
    because the harness sanitizes what tychoc *produces* and never tychoc *itself*.
    Phase 37 found it only because Phase 29's ruling forced a coupling trace, and the
    agent happened to build an ASan tychoc by hand to check its own widening.
  - Scope when taken: add a lane (suggest `make asan-self`) that builds `src/tychoc.c`
    with `-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1` and runs THAT
    binary over the fixture corpus — compiling each fixture, not running it, since the
    target is the compiler's own execution. Wire into `scripts/ci.sh`.
    - Expect it to be SLOW (a sanitized compiler over ~500 fixtures). Measure the cost
      before wiring it in; if it is prohibitive for every-run CI, gate it on a subset
      chosen for type-system coverage (generics, deep nesting, large aggregates — the
      shapes that index fixed arrays) and say explicitly what is not covered.
    - Prove it works the way Phase 3 was required to: show it GREEN, then show it RED by
      reverting Phase 37's fix (or stubbing an overrun), then restore. A gate never seen
      to fail is not a gate.
  - **Expect it to find more than Phase 37 did.** Phase 37 swept `[256]` literals and
    every growing `TBL_ENSURE` table, but that was a targeted grep for one shape. A
    sanitized compiler over the whole corpus tests every path the fixtures reach. Budget
    for the lane's first run producing a list of findings, each of which becomes its own
    phase — do NOT fix them inside this phase.
  - Same-family note: Phase 24 records that `runtime/tycho_rt.c` is never compiled by
    `make` either (it is `awk`ed into a string literal), so its warning-cleanliness is
    also ungated. Two different surfaces, same blind spot — the gates cover emitted
    programs thoroughly and the toolchain's own sources barely at all. Worth deciding
    both together.
  - Done when: a sanitized-tychoc lane exists, is demonstrated green AND demonstrated
    red, its cost is measured and its coverage stated honestly, findings are filed as
    separate phases, and the full gate set stays green.
  - **DONE 2026-07-25. `make asan-self` (`scripts/asan_self.sh`), wired into
    `scripts/ci.sh` as step `[2c/19]`. NO new findings — the corpus is clean under a
    sanitized compiler beyond what Phase 37 already fixed.**

    **THE GAP, RE-VERIFIED BY READING, NOT INHERITED.** Every `-fsanitize` /
    `sanitiz` / `ASAN` occurrence in the build system, grepped and classified:
    ```
    tests/run.sh:42     NO_ASAN="${TYCHO_NO_ASAN:-0}"          -- emitted-C lane switch
    tests/run.sh:49-50  ASAN_OPTIONS=detect_leaks=$TYCHO_LSAN  -- for the EMITTED binaries
    tests/run.sh:74-75  $CC -fsanitize=address,undefined -fno-sanitize-recover=all
                        ... -o "$san" "$c"                      -- "$c" is the EMITTED C
    tests/run.sh:80-88  runs/greps those emitted binaries
    Makefile:85-86      comment describing that same lane
    Makefile:202,:214   TYCHO_NO_ASAN=1 for -m32 (32-bit ASan absent under multilib)
    scripts/ci.sh:4     the word "sanitizer" in the file header
    ```
    `Makefile:31-32` is the only rule that compiles `src/tychoc.c`, and it uses
    `CFLAGS ?= -O2 -fwrapv -Wall -Wextra -std=c11` (`:11`). **No `-fsanitize`
    anywhere near the compiler's own translation unit.** Confirmed: the gap is real,
    the citations in the phase text are correct.

    **THE LANE.** `scripts/asan_self.sh` builds
    `cc -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv -std=c11
    -Ibuild src/tychoc.c -o build/tychoc-asan` (gitignored via `.gitignore:50 /build/`,
    so no sanitized binary is ever committed), then compiles **527 fixtures** with it
    using `--emit-c`. The emitted programs are never built or run here — `tests/run.sh`
    already sanitizes those, and duplicating it would double the cost for no new
    coverage. The subject is the compiler's own execution.

    **The verdict is deliberately NOT the accept/reject decision.** `tests/reject/`
    and `tests/diag/` fixtures are *supposed* to exit nonzero; `make test` scores that.
    A fixture fails this lane only if the sanitizer speaks
    (`AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:|ERROR: `)
    or the compiler dies on a signal (`rc >= 128`). Checked for false positives before
    relying on that grep: tychoc's diagnostics are `file:LINE: error: MSG` (lowercase,
    cannot match `ERROR: `), and the only `runtime error` string in `src/tychoc.c` is a
    comment at `:8320`, never printed.

    **`ASAN_OPTIONS=detect_leaks=0` — justified, not silently disabled.** tychoc never
    frees, by design: it is one-shot and leaks every `xmalloc`'d `Expr`/`Stmt`/`Proc`
    and every `sfmt` string; `gi.binds` at `src/tychoc.c:6870` xmalloc's a bind vector
    and never frees it, and Phase 37's `new_binds()` (`:742-747`) deliberately followed
    that same pattern. With `detect_leaks=1` every fixture would report hundreds of
    intended "leaks" and a real overrun would be one line in the flood. This is the
    *opposite* of `tests/run.sh:49-50`, which keeps `detect_leaks=1` — correct there,
    because emitted programs run under the implicit-arena model where every scope frees
    its arena, so a leak is a real missing-arena-free bug. The compiler has no arenas.
    ASan and UBSan themselves are fully on, `-fno-sanitize-recover=all`, first fault
    fatal. `UBSAN_OPTIONS=print_stacktrace=1` is set so a finding arrives with a trace.

    **LD_PRELOAD.** The lane unsets a non-empty `LD_PRELOAD` for its own children and
    prints a NOTE saying it did. Reason: a foreign preload in this dev shell
    (`/home/igzo/phonic/tools/block-nnp.so`) loads before `libasan.so` and aborts any
    ASan binary at startup — a property of that unrelated preload, not of tycho. It
    does **not** set `verify_asan_link_order=0`; that check stays live for real
    link-order bugs, per the plan preamble's standing instruction.

    **COST — measured, not estimated. 13.9 s wall, whole lane, no subsetting.**
    ```
    $ time env -u LD_PRELOAD make asan-self
    asan-self: building build/tychoc-asan  (ASan+UBSan, -fno-sanitize-recover=all)
    -----------------------------------------
    asan-self: compiled: 527   failed: 0
    asan-self: all green (tychoc's own execution is ASan+UBSan clean over the corpus)
    env -u LD_PRELOAD make asan-self  10.48s user 3.37s system 99% cpu 13.862 total
    ```
    Split: ~7.1 s to build the sanitized compiler, ~6.8 s for all 527 compiles. The
    phase text budgeted for this being prohibitive; it is not, because the lane only
    *compiles* — the expensive half of `make test` is the two `cc` invocations and the
    two runs per fixture, which this lane skips. Single worst input, the 14k-line
    self-host source `compiler/tychoc0.ty`, is 0.31 s sanitized. **So no subset was
    taken and nothing is excluded for cost reasons.**

    **COVERAGE — what is in and what is NOT.** IN (527): `examples/*.ty` (23),
    `tests/*.ty` (213), `tests/pkg/*/main.ty` (14), `tests/reject/*.ty` (224),
    `tests/reject/pkg/*/main.ty` (1), `tests/abort/*.ty` (16), `tests/diag/*.ty` (15),
    `tests/warn/*.ty` (6), `tests/conc/*.ty` (11), plus the four largest real Tycho
    programs in the tree — `compiler/tychoc0.ty`, `tools/tycho.ty`, `tools/tychofmt.ty`,
    `tools/lsp.ty` — which reach deeper generic/aggregate shapes than any single
    fixture. NOT covered, stated so it is not mistaken for coverage: `corelib/` and
    `examples/corelib/` (their harnesses carry per-module dependency skips this lane
    does not replicate); the fuzz corpora (generated, not committed); `-m32` (no 32-bit
    ASan runtime under multilib, the same reason `Makefile:214` skips it); and the
    emitted programs' own runtime behaviour, which `tests/run.sh` owns.

    **DEMONSTRATED RED — by restoring the `[256]` bound Phase 37 removed.** Temporary
    edit at the `parse_type_inner` generic-struct site, reverting `Type *binds =
    new_binds();` to the pre-Phase-37 `Type binds[256]; for (int i = 0; i <
    g_ntyparams; i++) binds[i] = T_VOID;`. Rebuilt, re-ran, unmodified lane:
    ```
    $ env -u LD_PRELOAD make asan-self
    asan-self: building build/tychoc-asan  (ASan+UBSan, -fno-sanitize-recover=all)
    FAIL  tests/generic_many_typaram_names.ty  (sanitizer report)
          src/tychoc.c:1949:60: runtime error: index 256 out of bounds for type 'Type [256]'
              #0 0x55e2efe1ba5b in parse_type_inner src/tychoc.c:1949
              #1 0x55e2efe1d3d8 in parse_type src/tychoc.c:1742
              #2 0x55e2efe2850f in parse_stmt src/tychoc.c:3154
              #3 0x55e2efe29521 in parse_block src/tychoc.c:3249
              #4 0x55e2efe2c564 in parse_fn src/tychoc.c:3387
              #5 0x55e2efe2db0e in parse_program src/tychoc.c:4027
              #6 0x55e2efe69efb in main src/tychoc.c:11689
    -----------------------------------------
    asan-self: compiled: 526   failed: 1
    failed: tests/generic_many_typaram_names.ty
    make: *** [Makefile:100: asan-self] Error 1
    ```
    Then reverted the temporary edit; `git diff src/tychoc.c` is **empty** (verified
    before committing — `src/tychoc.c` is byte-identical to `8616aad`), and the lane
    returns to `compiled: 527   failed: 0` (green output pasted above, that run was
    after the restore).

    **The lane is non-vacuous with the corpus it already has.** This was the real risk
    and it was checked rather than assumed: Phase 37's overrun needs >256 distinct
    `$Name`s in one program, which no ordinary fixture has — had Phase 37 not landed
    `tests/generic_many_typaram_names.ty`, reverting the fix would have left this lane
    green and the "red proof" would have been theatre. It is that fixture, already in
    `tests/*.ty`, that reddens the lane. Recorded here because it is the lane's load-
    bearing dependency: **deleting that fixture silently defangs this gate.**

    **FINDINGS: NONE.** 527 fixtures compiled by an ASan+UBSan tychoc produced zero
    sanitizer reports and zero signal deaths. No phase is filed from 41, and none was
    manufactured to look productive. The phase text expected more than Phase 37 found;
    that expectation was reasonable and turned out to be wrong, which is itself the
    result: Phase 37's `[256]`-literal sweep plus its growing-table audit really did
    exhaust the reachable family, and the compiler's remaining fixed-size structures
    (`indent_stack[256]` capped at `:249`, `g_handles[256]` capped at `:3591`,
    `imp_paths[256]` bounded by `scan_imports`' `max` param) are all fail-closed. The
    value delivered here is the gate, not a defect list — the next such defect now
    reddens CI on the commit that introduces it instead of surviving a release.

    **PHASE 24 DECISION: STAYS CLOSED (will-not-do). Different job — reasoned, not
    dodged.** Re-measured today, unchanged from Phase 14's probe:
    ```
    $ cc -O2 -fwrapv -Wall -Wextra -Wno-unused-function -std=c11 -c runtime/tycho_rt.c -o /tmp/ph38/rt.o
    exit=0   warnings=4
      runtime/tycho_rt.c:2380:9: warning: this 'if' clause does not guard... [-Wmisleading-indentation]
      runtime/tycho_rt.c:2390:9: warning: this 'if' clause does not guard... [-Wmisleading-indentation]
      runtime/tycho_rt.c:2400:9: warning: this 'if' clause does not guard... [-Wmisleading-indentation]
      runtime/tycho_rt.c:2410:9: warning: this 'if' clause does not guard... [-Wmisleading-indentation]
    ```
    `-Wno-unused-function` does suppress the 33 artifact warnings exactly as Phase 24
    predicted, so the compile is cheap and clean apart from those four. It was **not**
    added to this lane, for one decisive reason: **a gate whose baseline is 4 warnings
    is red on the day it lands**, and turning it green requires editing
    `runtime/tycho_rt.c` — which is a source fix, is Phase 24's scope, is closed by
    user decision, and is explicitly forbidden inside this phase ("do NOT fix them
    inside this phase"). The alternatives were both worse: shipping the check
    non-fatal makes it a report, not a gate; shipping it with a suppressed baseline
    installs exactly the kind of silent allowance this plan exists to remove. The two
    surfaces also differ in kind — this lane sanitizes a binary it *builds and runs*,
    whereas the runtime is never built standalone in production (it is `awk`ed into a
    string literal at `Makefile:23-26` and compiled only inside emitted programs, where
    `make test`'s ASan lane already exercises its runtime behaviour across 236
    programs). So Phase 24's four warnings are **not** covered by `asan-self`; the
    measurement is preserved above so the number is not lost if it is ever reopened.

    **Gate set, one per command, foreground, all green after the change:**
    ```
    make test        passed: 527   failed: 0   /  all green
    make corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc        conc: passed 37   failed 0
    make fixpoint    ok  B == C : tychoc0 reproduces itself byte-identically (35537 lines C)
                     fixpoint: all green (self-hosting; B==C; single files + packages; ...)
    make ilp32       passed: 527   failed: 0   /  all green
    make spec-check  spec-examples: 7 runnable example(s), all pass
    make check-links link check: ok (122 markdown files, no dead relative links)
    make asan-self   asan-self: compiled: 527   failed: 0   /  all green
    ```
    `git status --short` before the commit: `M Makefile`, `M scripts/ci.sh`,
    `?? scripts/asan_self.sh` — no build spill, no sanitized binary.

### Filed by Phase 32 (2026-07-25)

- [x] **Phase 39 — the type-name collision check is ONE-DIRECTIONAL: `handle H` after `struct H` is rejected, `struct H` after `handle H` is not (found by Phase 32, out of its E1 scope)**
  - **MAIN-AGENT RULING 2026-07-25 (user delegated: "do 36 and 39 too"): FIX BOTH HALVES.**
    This *does* reject something tychoc compiles today, which the Phase 32 ruling said to
    avoid — the reasons it is not a contradiction, both checkable:
    1. **The rule already exists; only its application is asymmetric.** `parse_handle`
       checks all four namespaces (`src/tychoc.c:3616`). `parse_struct` (`:3652`),
       `parse_enum` (`:3705`) and `parse_typedecl` (`:3751`) omit `handle_find`.
       `parse_const` (`:3996-3997`) *includes* it — so the omission is an oversight, not
       a design decision. Enforcing it symmetrically is completing an existing rule, not
       adopting a new limit.
    2. **Nothing that works today stops working.** The accepted program is already broken
       on tychoc0 (CCFAIL: `incompatible type for argument 1 of 'S_H_eq'`). It compiles
       on exactly one of the two compilers, so it was never portable Tycho.
    The second half — `resolve_program` `continue`ing past a generic template before the
    I6/I7/I8 checks (`:7091-7096`) — is likewise not a new restriction: Phase 32 ratified
    those three as MUST NOTs in the spec, and this closes a technicality that lets a
    program bypass a rule the spec now states. Phase 32 mirrored the gap in tychoc0
    deliberately so the two agreed; now close it in both.
  - Verify the ruling's premises before acting on them (both are line citations, and this
    plan's citations have been wrong repeatedly). If `parse_const` does NOT include
    `handle_find`, the "oversight" argument collapses and you should stop and report.
  - **NOT the E1 row Phase 32 closed.** E1 is the `handle` declaration checking the
    names already taken. This is the reverse: the three *other* declaration forms do
    **not** check the handle table, so the same collision is legal in one order and
    illegal in the other.
  - **Read, not inferred.** `parse_handle` checks all four namespaces
    (`src/tychoc.c:3616`: `struct_find(nm) >= 0 || enum_find(nm) >= 0 || newtype_find(nm) >= 0 || handle_find(nm) >= 0`).
    The siblings check only three — `handle_find` is absent from every one:
    `parse_struct` `:3652`, `parse_enum` `:3705`, `parse_typedecl` `:3751`. (For
    contrast, `parse_const` `:3996-3997` *does* include `handle_find`, so the omission
    reads as an oversight, not a design decision.)
  - **Reproduced on both compilers.** Probe `e1_struct_rev` — `handle H: free: hclose`
    first, then `struct H: x: int`:
    ```
    tychoc  ACCEPT / CCOK / RUN -> hi
    tychoc0 ACCEPT / CCFAIL -> error: incompatible type for argument 1 of 'S_H_eq'
    ```
    So it is **two** defects in one program: a tychoc fail-open (it emits a binary for a
    program its own mirrored rule forbids) and a tychoc0 uncompilable-C row of the
    Phase 33 family (frontends agree, tychoc0's C does not build). Phase 32 could not
    absorb it: the ruling forbids making tychoc reject anything it accepts today, and
    this fix does exactly that.
  - **A second, independent gap in the same neighbourhood: `resolve_program` skips a
    generic template before three of its per-declaration checks.** `src/tychoc.c:7091-7096`
    `continue`s past a `$T` proc, so the I6 (`:7101`), I7 (`:7109`) and I8 (`:7123`)
    checks never see one. Measured:
    ```
    fn f(c: inout Channel(int), p: $T) -> int   tychoc ACCEPT / CCOK / RUN -> 1
    ```
    That is the rule 07-memory-model.md §11.5 now states as a MUST NOT, passing on a
    technicality. Phase 32 deliberately mirrored the gap in tychoc0 rather than close
    it (same reason: it would reject a program tychoc compiles), so the two agree and
    the divergence set did not grow — but the spec and the implementations disagree
    until this is fixed.
  - Scope when taken: decide whether the collision rule is symmetric (it is stated
    symmetrically in `14-ffi.md` §25 as "declared earlier in the file", which is
    honest about today's behaviour but weaker than the rule deserves), then add
    `handle_find` to the three sibling sites and move the I6/I7/I8 checks ahead of the
    generic `continue` — or into the instantiation path. Both changes REJECT programs
    that compile today, so both need a ruling before code.
  - Done when: both orders of every collision pair agree on both compilers, a generic
    declaration is held to §11.5 and §15.1, each is fixture-locked, and the full gate
    set is green.

  - **DONE 2026-07-25. Both halves landed in both compilers.**

    **The ruling's two premises were re-verified before any code was written** (the
    prompt made this a hard gate, and offsets had shifted in Phases 37/38/40):
    - (a) HOLDS. `src/tychoc.c:3616` is `if (struct_find(nm) >= 0 || enum_find(nm) >= 0
      || newtype_find(nm) >= 0 || handle_find(nm) >= 0)` — all four. The siblings at
      `:3652` / `:3705` / `:3751` were the identical expression **minus** `handle_find`,
      byte-for-byte (`grep -n "struct_find(pkg_mangle"` returned exactly those three
      lines, all three the three-way form).
    - (b) HOLDS. `:3996-3997` is `struct_find(nm) >= 0 || enum_find(nm) >= 0 ||
      newtype_find(nm) >= 0 || handle_find(nm) >= 0 || variant_find(nm, &vi) >= 0 ||
      consts_find(nm)`. `parse_const` does include `handle_find`, so the omission in the
      three sibling *type* declarations is an oversight, not a design decision, and the
      ruling's reasoning stands. Nothing was stopped.

    **HALF 1 — the collision matrix, all 16 ordered pairs of {handle, struct, enum,
    newtype}, FRONT/CC/RUN on both compilers.** Exactly three cells were broken, and
    they were the three `handle`-first cells:

    | before | tychoc | tychoc0 |
    |---|---|---|
    | H→S | ACCEPT / CCOK / RUN | ACCEPT / **CCFAIL** `incompatible type for argument 1 of 'S_X_eq'` |
    | H→E | ACCEPT / CCOK / RUN | ACCEPT / CCOK / RUN |
    | H→N | ACCEPT / CCOK / RUN | ACCEPT / CCOK / RUN |
    | the other 13 | REJECT | REJECT |

    So H→E and H→N were a fail-open on **both** compilers, not only tychoc — the plan
    had only measured H→S. After the fix all 16 cells are REJECT/REJECT, tychoc reporting
    `'X' is already defined` at the second declaration in every one.
    - tychoc: `handle_find(pkg_mangle(...))` added to `parse_struct` (`:3658`),
      `parse_enum` (`:3714`), `parse_typedecl` (`:3763`).
    - tychoc0: new `ck_type_dup_handle` beside the existing `ck_handle_dup`, called from
      the top-level parse loop before `parse_struct` / `parse_enum` / `parse_newtype`, so
      the test runs in DECLARATION order against the handles seen so far — the same shape
      `ck_handle_dup` already used, with tychoc's wording verbatim.

    **HALF 2 — why the `continue` exists, answered from the source before moving
    anything.** Its own comment says it: *"a `$T` template: not a callable Sig — stash it;
    instances are made per call"*. It exists to skip **Sig registration**, not to defer
    validation — and the three checks stranded behind it are rules about the
    *declaration*, not about the Sig. Measured bypasses:

    | probe | tychoc before | tychoc0 before |
    |---|---|---|
    | `fn f(c: inout Channel(int), p: $T)` (I6) | ACCEPT / CCOK / RUN → `1` | ACCEPT / CCOK / RUN → `1` (Phase 32 mirrored the gap) |
    | `fn f(g: inout fn(int)->int, p: $T)` (I8) | ACCEPT / **CCFAIL** `'h_g' is a pointer` | rejected, but incidentally (`unknown function 'g'`) |
    | 17 params + `q: $T` (I7) | **STACK OVERRUN**, then a nonsense diagnostic | correctly rejected at parse |
    | `fn f(c: Channel(int), p: $T) -> Channel(int)` (CC-4) | ACCEPT / CCOK / RUN | **REJECT** — a live accept/reject divergence |

    **I7's bypass was a memory-safety bug, not only a missing diagnostic.**
    `instantiate_generic` builds `Type cparams[16]` (`:6866` pre-fix) and loops to
    `gt->nparams`, so a 17-parameter template wrote past a stack array. Reproduced on an
    ASan+UBSan build of the pre-fix compiler:
    ```
    $ cc -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -Ibuild src/tychoc.c -o /tmp/ph39/tychoc-asan
    $ /tmp/ph39/tychoc-asan /tmp/ph39/g/g_params17.ty --emit-c -o /tmp/ph39/w/p17
    src/tychoc.c:6868:16: runtime error: index 16 out of bounds for type 'Type [16]'
    ```
    The corrupted `s.nparams` is what produced the pre-fix garbage message
    `'f__int__…__int' takes 0 argument(s), got 18`. This is the same family as Phase 37
    and is exactly what Phase 38's new `asan-self` gate exists to catch — it did not,
    because no fixture had a 17-parameter generic. `tests/reject/generic_params_17.ty`
    now supplies one, so `make asan-self` covers it from here.

    **What was NOT forced across the `continue`, and why.** A check that genuinely cannot
    run on an uninstantiated template was moved to instantiation instead of being forced
    early. `inout $T` is legal to *write* — the rule is about what `T` becomes — so §11.5
    is applied twice: at the declaration on the WRITTEN types, and in
    `instantiate_generic` / `mono_instantiate` on the SUBSTITUTED ones, reported at the
    call that chose the binding. Both were live fail-opens before this:
    ```
    fn f(c: inout $T) -> int   called with a channel  : both ACCEPT/CCOK/RUN -> 1   (before)
    fn f(g: inout $T) -> int   called with add1       : both ACCEPT/CCOK/RUN -> 1   (before)
    ```
    Both now reject on both compilers, at the CALL line, with identical wording. No guard
    on `has_typaram` was needed anywhere: a bare `$T` trips neither `IS_CHAN` nor
    `IS_FUNC`, so a genuinely deferred parameter passes untouched, while
    `inout Channel($T)` is rejected on purpose — the rule is about the channel, not
    about `T`.

    **Shape of the tychoc change.** The three declaration rules were hoisted above the
    generic stash (`:7142-7146`), and the two `inout` type rules were factored into one
    `check_inout_param_type` (`:6844-6852`) with three callers — the concrete
    declaration, the template declaration, and the instantiation — so the rule has one
    source of truth rather than three copies. The duplicated inline copies in the
    concrete path were deleted. tychoc0 mirrors it: the two `if not isgen:` guards Phase
    32 added to `parse_func` are **gone** (that guard is now the divergence, not the
    fix), and `mono_instantiate` gained the substituted-signature check with the tloc
    decoded (`ln / 100000`, per `tloc` at `:123`) so the reported line is a line.

    **Legal code still legal (over-tightening check).** All 13 non-`handle`-first
    collision cells unchanged; `struct Box($T)` + `fn wrap(x: $T) -> Box($T)`
    ACCEPT/CCOK/RUN on both, unchanged; 540/540 fixtures, whole corelib, and
    `tests/conc/` all green; tychoc0 still self-hosts.

    **Fixtures — 9 new, one per distinct rejection** (`make test` 531 → 540):
    `handle_then_struct.ty`, `handle_then_enum.ty`, `handle_then_newtype.ty`,
    `generic_chan_inout_param.ty`, `generic_inout_fnvalue.ty`, `generic_params_17.ty`,
    `generic_ret_chan.ty`, `generic_inst_chan_inout.ty`,
    `generic_inst_inout_fnvalue.ty`. Every one verified REJECT on both compilers
    individually before the gates were run.

    **Spec.** `14-ffi.md` §25 now states the collision rule symmetrically (it had said
    "declared earlier in the file", honest about the old behaviour but weaker than the
    rule deserves) and cites both directions' fixtures. `07-memory-model.md` §11.5 gained
    the generic paragraph (declaration vs instantiation) and its provenance was
    re-pointed at `check_inout_param_type`. `11-functions.md` §15.1 now says the
    16-parameter cap applies to a generic template.

    **Citations kept in sync** (every one my edit moved): `07-memory-model.md:221`,
    `tests/reject/params_17.ty`, `extern_params_17.ty`, `inout_fnvalue.ty`,
    `chan_inout_param.ty`, `handle_dup_name.ty`.

    **Gates — one per command, foreground, all green:**
    ```
    make test        passed: 540   failed: 0        / all green
    make corelib     corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc        conc: passed 37   failed 0
    make fixpoint    ok  B == C : tychoc0 reproduces itself byte-identically (35691 lines C)
                     fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)
    make ilp32       passed: 540   failed: 0        / all green
    make asan-self   asan-self: compiled: 540   failed: 0 / all green
    make spec-check  spec-examples: 7 runnable example(s), all pass
    make check-links link check: ok (122 markdown files, no dead relative links)
    ```
    `git status --short` clean of build spill (only the 10 edited + 9 new files).
    `make fixpoint` is load-bearing here: `compiler/tychoc0.ty` changed in three places,
    including inside `parse_func`, and B==C proves tychoc0 still reproduces itself
    through them.

### Filed by Phases 33/35 (2026-07-25)

- [x] **Phase 40 — `nt_skin_of` is incomplete for an INFERRED local, which fails open on newtype identity in three places; plus two message-location gaps and a `16-builtins.md` citation sweep (found by Phases 33/35, out of both scopes)**
  - **One root cause, three symptoms.** `nt_skin_of`
    (`compiler/tychoc0.ty:11174-11219`) recovers the newtype an expression's value
    carries. Its own doc comment states the requirement: *"Must be complete over the
    forms that can yield a newtype, else a valid newtype value reads as `""` and a
    correct program is wrongly rejected."* It is not complete. For an **inferred** local
    it reads the recorded var type, and `dup := ids` records the RESOLVED `[int]`, so
    `nt_skin_of(dup)` is `""` while `nt_skin_of(ids)` is `"Ids"`. Measured against a
    HEAD-built tychoc0 (`/tmp/ph33/tychoc0_head`), so all three predate Phase 33:

    | probe | tychoc | tychoc0 |
    |---|---|---|
    | `type A = [int]` / `type B = [int]` / `a == b` | `error: cannot compare A with B` | **ACCEPT (rc=0)** |
    | `type A = [int]` / `a == [1]` | `error: cannot compare A with [int]` | **ACCEPT (rc=0)** |
    | `type C = int` / `mv: [string: C] = ["one": a]` where `a := C(7)` | ACCEPT | **REJECT** `declared type {str:C} but value is {str:int}` |
    | `type C = [int]` / `a == mk()` where `mk() -> C` | ACCEPT, prints | **REJECT** `cannot compare [int] with C` |

    Two fail-OPENs and two over-REJECTIONs from the same gap. Note the third and fourth
    rows reproduce for `type C = int` and for the long-standing `type C = [int]`, so this
    is **not** aggregate-specific and not new.
  - **Phase 33 tried the obvious fix and backed it out, deliberately.** Adding
    `nt_skin_of` to the structural-`==` identity check (`compiler/tychoc0.ty:6847`)
    closes rows 1 and 2 — and breaks `tests/newtype_agg.ty`, whose `if dup == ids:` is
    legal:
    ```
    env -u LD_PRELOAD make fixpoint
      FAIL newtype_agg.ty (B differs from the C compiler)
      tychoc0: line 33: cannot compare [int] with [int]
    ```
    So the ORDER is forced: complete `nt_skin_of` for inferred locals FIRST, then the
    identity checks can be tightened. Doing it the other way round rejects working code.
    Worth noting `make test` does **not** catch this — `tests/run.sh` runs only tychoc on
    the positive lane, so tychoc0's behaviour on `tests/*.ty` is gated by `make fixpoint`
    and `make corelib` alone.
  - **Also here — two message-LOCATION gaps (verdicts already agree, so these are
    diagnostic quality, not divergence):**
    - The four removed `map_*` names produce an unlocated message on tychoc0
      (`parse: map_get was removed; use m.get(k, default)` — no line number) where
      tychoc gives `file:LINE: error: ...`. 12 rows of Phase 35's sweep.
    - `print()` / `print(a, b)` give `parse: line N: expected an atom` /
      `unexpected token` on tychoc0 rather than an arity message; they are parsed
      specially and so sit outside Phase 35's builtin arity table by design. Located,
      just unhelpful.
  - **Also here — `docs/spec/16-builtins.md`'s NON-`register_builtins` citations are all
    stale.** Phase 35 corrected every `register_builtins` range (its named scope) and
    spot-checked the rest, which are wrong by Phase 37's shift:

    | citation | claims | actually at that line |
    |---|---|---|
    | `:7342` | `eprint` codegen | a blank line |
    | `:7421-7423` | `die` codegen | `body_pushcount` loops |
    | `:8179-8187` | `char_at` codegen | the `map_get`/`map_has` codegen block |
    | `:4789-4794` | `len` magic | slice-bound checking |
    | `:4845-4851` | `keys` | the `zero$` type-arg case |
    | `:8653` | `s[i]` codegen | `case E_CALL: return gen_call(...)` |

    Others in the file (`:4716-4787` conversion magic, `:4852-4885` push,
    `:4397-4408` `m.get` sugar, `:4355-4371` `zero$`, `:6191` defaultable,
    `:4671-4689` wait/channel/send, `:4355-4930` the magic block) were not checked
    individually and should be assumed stale until they are.
  - **A cheaper structural fix worth considering while in here:** these provenance
    ranges have now gone stale twice in one plan (Phase 34 for `15-program.md`, Phase 35
    for `16-builtins.md`). A gate that resolves each `src/tychoc.c:N` citation to an
    expected symbol or substring would turn the whole class from "audit again next time"
    into a green/red check. Scope it against `make check-links`, which already walks
    every markdown file.
  - Scope when taken: complete `nt_skin_of` over inferred locals (and audit the rest of
    its arms against `type_of`'s newtype-producing leaves), THEN tighten the
    structural-`==` and map-literal identity checks; locate the `map_*` removal
    message; re-derive `16-builtins.md`'s remaining citations. Each rejection change
    needs its own reject fixture, and `tests/newtype_agg.ty` plus
    `tests/newtype_over_aggregate.ty` are the regression canaries — the latter has two
    shapes written around these very over-rejections, with in-file comments pointing
    here, which should be restored to the natural spelling once this lands.
  - Done when: `nt_skin_of` returns the carried newtype for every form `type_of` can
    produce one from; the four probe rows above agree with tychoc; `make fixpoint` stays
    green (it is the gate that catches an over-tightening here); each new rejection is
    fixture-locked; `16-builtins.md`'s citations all resolve.

  - **DONE 2026-07-25.**

    **PLAN CITATIONS RE-DERIVED (stale, as warned).** `nt_skin_of` was cited at
    `compiler/tychoc0.ty:11174-11219`; at the start of this phase it was at
    **`:11232-11277`**, and the structural-`==` check cited at `:6847` was at
    **`:6906`**. Everything below quotes post-phase line numbers.

    ### THE ORDER TAKEN, AND WHY IT WAS FORCED

    Step A (complete the identity recovery) landed and was verified BEFORE step B
    (tighten `==`) was written. The intermediate measurement is the proof that the
    reverse order would have failed: after step A alone the two over-rejections were
    gone and the two fail-opens were **still open**, and `make fixpoint` — the gate that
    caught Phase 33's break — was green with `tests/newtype_agg.ty` passing:
    ```
    after step A only, tychoc0 rebuilt from source:
      row1  A == B          tychoc FRONT  | tychoc0 ok      DIVERGE   <-- still fails open
      row2  A == [1]        tychoc FRONT  | tychoc0 ok      DIVERGE   <-- still fails open
      row3  ["one": a]      tychoc ok     | tychoc0 ok      AGREE     <-- fixed
      row4  a == mk()       tychoc ok     | tychoc0 ok      AGREE
    env -u LD_PRELOAD make fixpoint
      ok   B == C : tychoc0 reproduces itself byte-identically (35607 lines C)
      fixpoint: all green
    ```
    Only then was the skin compare added to the structural `==`. Had it gone first it
    would have hit exactly Phase 33's `newtype_agg.ty` failure, because `dup := ids`
    still read as "carries no newtype" until step A recorded it as `Ids`.

    ### THE FOUR ROWS, BEFORE AND AFTER

    Method: FRONT/CC/RUN on BOTH compilers via `--emit-c` plus a separate
    `cc -O1 -fwrapv -pthread -std=c11` (`/tmp/ph40/probe.py`); tychoc0's rc is a
    frontend verdict only, it never invokes `cc`, so raw rc is never compared. Rows 3
    and 4 were each run for `type C = int`, `[int]` and `[2]int`.

    | row | probe | tychoc | tychoc0 BEFORE | tychoc0 AFTER |
    |---|---|---|---|---|
    | 1 | `type A = [int]` / `type B = [int]` / `a == b` | `cannot compare A with B` | **ACCEPT** (fail-open) | `line 7: cannot compare A with B` |
    | 2 | `type A = [int]` / `a == [1]` | `cannot compare A with [int]` | **ACCEPT** (fail-open) | `line 5: cannot compare A with [int]` |
    | 3 | `mv: [string: C] = ["one": a]`, `a := C(...)` | ACCEPT, prints `1` | **REJECT** `declared type {str:C} but value is {str:int}` | ACCEPT, prints `1` |
    | 4 | `a == mk()` where `mk() -> C` | ACCEPT, prints | ACCEPT (see below) | ACCEPT |

    All four now AGREE. Rows 1 and 2's text is byte-identical to tychoc modulo the two
    compilers' known prefix formats (`file:LINE: error:` vs `line LINE:`).

    **Row 4 did NOT reproduce at HEAD, and the reason is on the record.** The plan
    measured it against `/tmp/ph33/tychoc0_head`, a **pre**-Phase-33 binary. Phase 33's
    own fifth fix (the structural `==` comparing `resolve_nt` on both sides,
    now `:6935`) already closed it. Verified two ways at HEAD before any edit: the standalone
    probe (`a := C(...)`; `a == mk()`) is `ok/ok` for `int` / `[int]` / `[2]int` /
    `[string:int]`, and `tests/newtype_over_aggregate.ty` with only line 92 restored to
    `a == mk_fa()` compiles clean on tychoc0 (`rc0=0`). So the over-rejection Phase 33
    wrote its fixture around was already gone; only the map-literal one was live.

    ### THE FIXES

    1. **`decl_ty_of` (`compiler/tychoc0.ty:11334-11349`) — the root cause.** `type_of`
       resolves a newtype at a var read, so an INFERRED local recorded the RESOLVED type
       and `nt_skin_of` had nothing left to recover. `decl_ty_of` records the skin when
       there is one. This is **not** a new spelling: an annotated local already pushes
       its annotation (`STypedDecl`), and `type_of` itself already returns the
       unresolved newtype for an array element (`:5529-5530`), a map value
       (`:5524-5526`) and `m.get` (`:5585`). Applied at every walk that records an `SDecl` type, so the six
       envs agree: branch-tail typing `:8976`, `gen_stmt` `:9058`, `:9752`,
       `collect_stmt` `:11787`, the lift pass `:13690`, `mono_stmt` `:15309`, `gfix_stmt`
       `:16407`. Consistency matters because `nt_skin_of` is consulted from BOTH the
       check walk and codegen (`gen_expr`'s `==`), and a disagreement between the two
       envs is what re-creates Phase 33's break.
    2. **`nt_skin_of`'s missing arms (`:11267-11332`).** Audited against every
       newtype-producing leaf of `type_of`. Added: `zero$<C>()` (its type IS the type
       argument), `pop(xs)` over a `[C]` (`type_of` returns `elem_ty` unresolved, so the
       skin must too), and a whole `ECallV` arm — `m.get(k[, d])` on a `[K: C]` map, and
       a UFCS method whose declared return is a newtype. `ECallV` previously fell to
       `_: return ""`, i.e. every method-spelled call read as "no newtype".
    3. **`EMapLit` keeps the key's and the value's skin (`:5553-5562`).** This is row 3.
       `EArrLit` (`:5535`) and `ETuple` (`:5569`) had done this since they were written;
       the map literal did not, so `["one": a]` typed to `{str:int}` and a
       `[string: C]` annotation rejected its own value. It over-rejected the ANNOTATED
       spelling too, so it is a second gap in the same family, not a consequence of the
       first — both had to be fixed for row 3.
    4. **The structural `==` compares skins (`:6933-6942`) — step B.** Mirrors what the
       scalar arm below it (`:6979`) has always done with `sl`/`sr`. The message names
       the skin when there is one, so the text matches tychoc's `cannot compare A with B`
       rather than `cannot compare [int] with [int]`.

    ### LEGAL-SET DIFF (the over-tightening check)

    19 programs: a newtype over `int` / `float` / `string` / `bool` / `[int]` / `[2]int` /
    `[string:int]` / `bounded[4]int` / a struct, each exercised in **eleven** positions in
    one program (inferred local, annotated local, inferred-from-a-call, inferred-from-
    another-newtype-local, param, return, uncalled-param, struct field, array element,
    map value, tuple element) plus same-newtype `==` four ways
    (`a == b`, `a == dup`, `a == mk()`, `xs[0] == a`), a different-value `==`,
    `to_under`; plus newtype arithmetic, newtype string ordering, a newtype-KEYED map
    literal, `zero$`, `pop`, `m.get`; plus `tests/newtype_agg.ty`,
    `tests/newtype_over_aggregate.ty`, and that fixture in its natural spelling.
    FRONT/CC/RUN on both compilers, stdout compared (`/tmp/ph40/legal.py`).

    | | BEFORE (HEAD) | AFTER |
    |---|---|---|
    | DIVERGE | **11** | **0** |
    | AGREE | 8 | 19 |

    Not one legal shape regressed; eleven that tychoc accepted and tychoc0 refused now
    pass. The eleven were all the same root cause and the BEFORE run found three the
    phase block had not listed:
    ```
    - legal_{int,float,string,bool,arr,fixarr,map,bnd,struct}
        line 23: argument 1 of 'take' expects C, got a plain <base> value (newtype identity differs)
        (`dup := a` — an inferred local of an inferred newtype local, ALL nine underlying types)
    - legal_nt_key       line 5: declared type {K:int} but value is {str:int}   (a newtype-KEYED map literal)
    - fixture_over_agg_natural  line 65: declared type {str:FA} but value is {str:[2]int}
    + all nineteen: tychoc=ok tychoc0=ok, byte-identical stdout
    ```

    ### FIXTURES

    - `tests/newtype_over_aggregate.ty`: **both** shapes restored to their natural
      spelling, as this phase block asked. `mv: [string: FA] = ["one": a]` (was
      `["one": FA([1, 2])]`) and `a == mk_fa()` (was `a == a3` with a throwaway
      `a3 := mk_fa()`, now deleted). Same values, so `.out` is unchanged; the in-file
      comments now describe the rule instead of pointing here.
    - `tests/reject/newtype_eq_two_newtypes.ty` — row 1. Both operands INFERRED locals,
      which is the shape that used to fail open.
    - `tests/reject/newtype_eq_raw_underlying.ty` — row 2.
    - `tests/reject/print_arity_zero.ty`, `tests/reject/println_arity_many.ty` — the
      message-location work below. All four rejected by BOTH compilers (`make test`'s
      reject lane asserts exactly that).

    ### THE TWO MESSAGE-LOCATION GAPS — both reproduced, both FIXED

    Both were small; neither was filed.

    | probe | tychoc | tychoc0 BEFORE | tychoc0 AFTER |
    |---|---|---|---|
    | `map_get(m, "a")` | `mg.ty:3: error: map_get was removed; use \`m.get(k, default)\`` | `parse: map_get was removed; ...` **(no line)** | `parse: line 3: map_get was removed; ...` |
    | `print()` | `pr0.ty:2: error: 'print' takes 1 argument(s), got 0` | `parse: line 2: expected an atom` | `parse: line 2: 'print' takes 1 argument(s), got 0` |
    | `print("a", "b")` | `:2: error: 'print' takes 1 argument(s), got 2` | `parse: line 2: unexpected token` | `parse: line 2: 'print' takes 1 argument(s), got 2` |
    | `println()` / `println("a","b")` | same, `'println'` | same grammar noise | matching arity text |

    - The `map_*` removal message: all **eight** sites (four names x the expression form
      `:879-885` and the statement form `:1625-1631`) now carry
      `"parse: line " + str(toks[pos].line) + ": "`, the located spelling `expect` and
      `parse_atom` already use (`:620-622`, `:972`).
    - `print`/`println` are parsed specially (`:1203-1223`, `:1268-1283`), so Phase 35's
      `ck_builtin_arity` never sees them and a wrong arity fell out of the *grammar*.
      Both now check arity at the parse site and quote tychoc's `Sig` wording verbatim.
      Text is byte-identical to tychoc modulo the prefix format.

    ### `16-builtins.md` CITATION SWEEP — every citation verified by READING the line

    Phase 35's nine `register_builtins` ranges re-checked and all still correct. Every
    NON-`register_builtins` citation was stale and is re-derived below; the offset is not
    constant (it runs from +0 through +941), so each was found by its content, never by a
    delta. Verified by `/tmp/ph40/verify_cites.sh`, which re-reads all 43 citations now in
    the file and prints what is at each.

    | claim | was | now | what is actually there |
    |---|---|---|---|
    | conversion magic | `:4716-4787` | `:5248-5321` | `/* str is polymorphic ... */` through `to_under` |
    | `len` magic | `:4789-4794` | `:5323-5329` | `if (!strcmp(e->sval, "len"))` |
    | `keys`/`push`/`pop`/`reserve` | `:4845-4930` | `:5378-5468` | the four magic blocks |
    | `keys` | `:4845-4851` | `:5378-5385` | `keys(m) -> [string] or [int]` |
    | `push` | `:4852-4885` | `:5386-5420` | `if (!strcmp(e->sval, "push"))` |
    | `pop` | `:4887-4904` | `:5421-5441` | `if (!strcmp(e->sval, "pop"))` |
    | `reserve` | `:4906-4929` | `:5442-5468` | `if (!strcmp(e->sval, "reserve"))` |
    | `m.get` sugar | `:4397-4408`,`:4477-4488` | `:4876-4889`,`:4963-4975` | the two `!strcmp(..., "get")` rewrites |
    | `zero$` | `:4355-4371` | `:4834-4850` | the `zero$(T)` lowering |
    | `defaultable` predicate | `:6191` | `:6819` | `if (!strcmp(pred, "defaultable"))` |
    | concurrency magic | `:4671-4714` | `:5203-5247` | `wait` `:5203-5210`, `channel` `:5211-5221`, `send` `:5222-5230`, `recv` `:5231-5236`, `close` `:5237-5247` |
    | task/channel method sugar | `:4372-4386` | `:4851-4865` | `t.wait() / ch.send(v) ... sugar` |
    | `map_*` removal (parse) | `:2100-2103` | `:2308-2311` | the four `die_at(t->line, "... was removed ...")` |
    | `eprint` codegen | `:7342` (blank line) | `:8283` | `tycho_eprint(...)` |
    | `die` codegen | `:7421-7423` | `:8362-8364` | `tycho_die(...)` |
    | `char_at` codegen | `:8179-8187` | `:8212-8220` | the `tycho_str_get` emit |
    | `s[i]` codegen | `:8653` | `:8686` | `sfmt("tycho_str_get(%s, %s)", a, ix)` |
    | `ncpu` `Sig` | `:3829` | `:4151` | `.name="ncpu"` |
    | `chr` `Sig` | `:3830` | `:4152` | `.name="chr"` |
    | `resolve_expr` magic block | `:4355-4930` | `case E_CALL:` `:4833` … `reserve` `:5468` | |
    | tychoc0 `char_at` arity | `:4827-4828` | `:4853-4854` | `builtin_arity`'s 2-arg row |
    | tychoc0 `char_at` codegen | `:6792-6797` | `:7180-7185` | the `hi_sidx` emit |
    | tychoc0 `s[i]` emit | `:6337` | `:6698` | `EIndex` on `str` -> `hi_sidx` (the in-source comment at `:7181` cited this too and was corrected with it) |

    One citation was not merely stale but **wrong about the mechanism**: `to_i32` was
    cited as a `Sig` (`:3841-3843`). It is not in `register_builtins` at all — it is
    `is_sized_conv` (`:948-952`) / `sized_conv_target` (`:937-947`), resolved inline at
    `:5281-5286`. Corrected in place, with the distinction stated. `is_null`/`to_ptr`
    genuinely are `Sig`s (`:4164-4165`).

    ### GATES (each its own foreground command, `env -u LD_PRELOAD`)

    Test count rose 527 -> 531: the four new reject fixtures.
    ```
    make test         passed: 531   failed: 0        / all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 37   failed 0
    make fixpoint     ok   B == C : tychoc0 reproduces itself byte-identically (35663 lines C)
                      fixpoint: all green (self-hosting; B==C; single files + packages; self-split dogfood)
    make ilp32        passed: 531   failed: 0        / all green
    make asan-self    asan-self: compiled: 531   failed: 0   / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (122 markdown files, no dead relative links)
    git status --short  only the 3 modified + 4 new fixture files; no build spill
    ```

### Filed by Phase 40 (2026-07-25)

- [x] **Phase 41 — the positive lane never runs tychoc0, which is why a tychoc0-only over-rejection is invisible to `make test` (found by Phase 40; the same blind spot let Phase 33's five over-rejections ship)**
  - `tests/run.sh:70` compiles each `tests/*.ty` with **tychoc only**; tychoc0 appears on
    the positive lane nowhere. `tests/run.sh:159` runs it on the *reject* lane only. So a
    program tychoc accepts and tychoc0 refuses scores green, and the only gates that
    would notice are `make fixpoint` (goldens over `tests/` + `examples/`, via a
    tychoc0-built B) and `make corelib`.
  - Measured cost of the gap: Phase 40 found **eleven** legal newtype shapes tychoc0
    over-rejected, nine of them the plain `dup := a` binding over every legal underlying
    type. `make test` was green for all eleven, before and after.
  - Not free: fixpoint already covers `tests/*.ty` behaviourally, so a naive second lane
    duplicates ~530 compiles. The question to answer first is whether the right shape is
    a lane at all or a `--front-only` sweep (tychoc0 frontend verdict vs tychoc's, no cc,
    no run) — cheap, and it scores exactly the property fixpoint's golden comparison
    cannot: *which* compiler refused.
  - Done when: a green/red gate exists that fails when tychoc0 refuses a program tychoc
    accepts in `tests/` + `examples/`, its cost is measured and stated, and it is wired
    into `ci.sh` next to `asan-self`.
  - **DONE 2026-07-25.** New: `scripts/frontparity.sh`, `make frontparity`
    (`Makefile:102-113`), wired at `scripts/ci.sh:53-62` as step `[2d/19]`, between
    `asan-self` and `fixpoint`.

    ### CORRECTION TO THIS PHASE'S OWN TEXT — two citations were wrong

    `tests/run.sh:70` is inside `run_one()`, not the loop; the positive loop is
    `tests/run.sh:113` (`for hi in examples/*.ty tests/*.ty`). And tychoc0 is used in
    `run.sh` on **four** lanes, not one: reject `:159`, reject-pkg `:178`, abort `:199`,
    diag goldens `:262`. What they have in common is the point: every one of them scores
    tychoc0 **refusing**. Nothing there scores tychoc0 **accepting**. The warn lane
    (`tests/warn/`) is the positive lane that runs `$TYCHOC` alone.

    ### THE SHAPE QUESTION, ANSWERED FIRST — and the answer is not the flattering one

    **What `make fixpoint` already covers.** `compiler/fixpoint.sh:24-30` walks
    `tests/*.ty examples/*.ty`, `continue`s past whatever tychoc cannot build (`:26`),
    then requires **B** — a tychoc0-derived binary — to emit C, cc it, and match tychoc's
    golden output (`:28-29`). A tychoc0 refusal makes `"$T/B" < "$f"` exit nonzero, so
    fixpoint **does** redden on the missing property over that glob. `tests/pkg/*/` is
    covered the same way (`:41-52`). Measured, not assumed: over `tests/*.ty` +
    `examples/*.ty`, the number of files whose `--emit-c` frontend succeeds but whose
    `-o` (cc-inclusive) ref build fails — i.e. files fixpoint's `:26 || continue`
    silently drops — is **0**. So fixpoint's positive glob is not being thinned.

    Elsewhere: `tests/conc/*.ty` is tychoc0-parity'd at `tests/conc/run.sh:63-67`,
    `tests/abort/*.ty` at `tests/run.sh:199`, `corelib/` by `make corelib`, and all 15
    `tests/diag/*.ty` are rejected by tychoc so they are not in the property's domain.

    **Why fixpoint caught Phase 33's break and not Phase 40's eleven.** Not a gate gap —
    a **fixture** gap. Phase 33's over-tightening hit `tests/newtype_agg.ty:33`
    (`if dup == ids:`), a committed program that exercised the shape, so fixpoint printed
    `FAIL newtype_agg.ty (B differs from the C compiler)` (quoted at plan.md:5510-5514).
    Phase 40's eleven were found by a generated legal-set probe (`/tmp/ph40/legal.py`);
    nine were `dup := a` over nine underlying types, a shape **no committed program
    contained**. The two that did correspond to a fixture had been deliberately written
    AROUND — `tests/newtype_over_aggregate.ty` carried `["one": FA([1,2])]` and a
    throwaway `a3 := mk_fa()`, with in-file comments saying tychoc0 rejects the natural
    spelling (see the `git show bfae65d -- tests/newtype_over_aggregate.ty` diff).

    **Consequence, stated plainly: a second full positive lane over the same glob would
    have missed all eleven too.** So would this front-only sweep. Neither shape buys
    coverage of an absent fixture, and no gate can. That is the honest limit of this
    phase, and it is why the lane below is deliberately small rather than a second
    `run.sh`.

    **What was built, and the three things it does buy.**
    1. **It names the verdict.** fixpoint discards tychoc0's stderr
       (`compiler/fixpoint.sh:28`, `2>/dev/null`) and reports a frontend refusal, a cc
       failure of the emitted C, and a genuine output divergence as one string,
       `B differs from the C compiler`. Phase 40 had to re-run tychoc0 by hand to learn
       which. The lane's failure line *is* the refusal and its diagnostic.
    2. **It widens the glob by 9 programs no gate front-checks against tychoc0**:
       `tests/warn/*.ty` (6 — `tests/run.sh`'s warn lane runs `$TYCHOC` only) and
       `tools/*.ty` (3 — `tycho.ty`, `tychofmt.ty`, `lsp.ty`, built at `Makefile:38,:44,
       :49` with tychoc alone, and among the largest real Tycho programs in the tree).
       Verified by grep over every `*.sh` plus the `Makefile`; the only other hits are
       `scripts/asan_self.sh`, which runs the *sanitized tychoc*, not tychoc0.
    3. **It is a fast tripwire** — no cc, no run, no 3-stage bootstrap — so it reddens
       ahead of fixpoint.

    **Why the alternative was not built.** A second full positive lane (tychoc0 →
    `cc` → run → compare goldens over `tests/*.ty`) is what `compiler/fixpoint.sh:24-30`
    already is. It would duplicate ~250 compiles plus a cc and a run each for zero new
    property, and the one property it adds over the front-only sweep — output equality —
    is fixpoint's job and is already green.

    ### THE LANE

    Both compilers frontend-only, which is the method requirement: `./tychoc F --emit-c
    -o X` stops after emitting `X.c` (`tests/run.sh:70` cc's it as a separate step) and
    `tychoc0 F --emit-c` writes C to stdout. A bare `./tychoc F -o X` would conflate
    tychoc's `cc` step with tychoc0's frontend exit; it is not used. One direction only:
    tychoc accepts and tychoc0 refuses. tychoc refusing → skip (owned by
    `tests/reject/`, `tests/diag/`); tychoc0 fail-OPEN → owned by `tests/run.sh:159`,
    `:178`. Nothing is grepped for — the verdict is the two exit statuses — so Phase 38's
    marker false-positive class does not arise here. `H0=<path>` reuses a prebuilt
    tychoc0; unset in every gate run. `mktemp -d` + `trap rm`, so no working-tree spill.

    Glob: `examples/*.ty tests/*.ty tests/conc/*.ty tests/warn/*.ty tests/abort/*.ty
    tests/diag/*.ty tools/*.ty compiler/tychoc0.ty` + `tests/pkg/*/main.ty` (the
    standalone `tychoc0 <entry>` driver form of `compiler/fixpoint.sh:48`).
    NOT: `tests/reject/**` (other direction, already gated); `corelib/` and
    `examples/corelib/` (`make corelib` runs both compilers there with per-module
    dependency skips this lane does not replicate — the same boundary
    `scripts/asan_self.sh:69-70` draws); output equality (fixpoint's).
    `tests/diag/*.ty` is in the glob although all 15 skip today, so the day one starts
    being accepted it is covered with no script edit.

    ### GREEN

    ```
    $ env -u LD_PRELOAD make frontparity
    -----------------------------------------
    frontparity: agreed: 287   diverged: 0   (skipped, tychoc refused: 15)
    frontparity: all green (tychoc0's frontend accepts every program tychoc accepts)
    rc=0
    ```
    287 = 23 examples + 213 tests + 11 conc + 6 warn + 16 abort + 3 tools + tychoc0.ty
    + 14 pkg. The 15 skips are exactly `tests/diag/*.ty`, all 15 rejected by tychoc.
    **No current divergence — so nothing to file.** The lane found the tree already
    correct on this property, which is the expected result one phase after Phase 40
    closed the last eleven.

    ### RED — a real Phase 40 fix reverted, plus the control that proves the blind spot

    Reverted Phase 40's `EMapLit` arm of `type_of` (`compiler/tychoc0.ty:5576-5581`) to
    its pre-Phase-40 form (`mkt := type_of(keys[0], …)` / `mvt := type_of(vals[0], …)`,
    the `-` side of `bfae65d`'s `@@ -5531,8 +5551,12 @@` hunk), rebuilt, ran both gates
    on the SAME tree:

    ```
    ########## RED: make frontparity with Phase 40's EMapLit fix reverted ##########
    FAIL  tests/newtype_over_aggregate.ty  (tychoc ACCEPTED it, tychoc0 REFUSED it)
          line 66: declared type {str:FA} but value is {str:[2]int}
                  mv: [string: FA] = ["one": a]
                  ^
    -----------------------------------------
    frontparity: agreed: 286   diverged: 1   (skipped, tychoc refused: 15)
    failed: tests/newtype_over_aggregate.ty
    make frontparity rc=2

    ########## control: make test with the SAME regression in place ##########
    ok    warn_result_discarded
    -----------------------------------------
    passed: 540   failed: 0
    all green
    ```
    That control is the whole phase in four lines: a live tychoc0 over-rejection of a
    committed fixture, and `make test` says `all green`. Restored with
    `git checkout -- compiler/tychoc0.ty`; `git status --short` clean of it; the lane
    back to `agreed: 287  diverged: 0  rc=0`.

    ### COST — 23.5s, of which 22.3s is the tychoc0 build every such gate already pays

    | | wall |
    |---|---|
    | `env -u LD_PRELOAD make frontparity` (2 runs) | **23.76s**, **23.52s** |
    | of that: `./tychoc compiler/tychoc0.ty -o …` | **22.25s** |
    | of that: the 287-program sweep itself | **~1.5s** |

    Wired **unconditionally** into `scripts/ci.sh:53-62`, no subsetting: comparable to
    `asan-self`'s 13.9s (Phase 38), and its *marginal* cost is the 1.5s sweep — the 22.3s
    is the same tychoc0 build `tests/run.sh:148`, `tests/conc/run.sh:34` and
    `scripts/tools_check.sh:121` each already pay. Placed before `fixpoint` so the cheap
    frontend verdict reddens first.

    ### GATE SET — all green, `git status --short` shows no build spill

    ```
    make test         passed: 540   failed: 0 / all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 37   failed 0
    make fixpoint     ok B == C (35691 lines C) / fixpoint: all green
    make ilp32        passed: 540   failed: 0 / all green
    make asan-self    asan-self: compiled: 540   failed: 0 / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (122 markdown files, no dead relative links)
    make frontparity  frontparity: agreed: 287   diverged: 0   (skipped: 15) / all green
    ```

- [x] **Phase 42 — `src/tychoc.c:N` / `compiler/tychoc0.ty:N` provenance citations across `docs/` have now gone stale three times in one plan, and nothing checks them (Phase 34, Phase 35, Phase 40)**
  - Phase 34 fixed `15-program.md`, Phase 35 fixed `16-builtins.md`'s
    `register_builtins` ranges, Phase 40 fixed the rest of `16-builtins.md`. Each time
    the finding was "the plan's own replacement lines were wrong too". There are **96**
    `tychoc0.ty:N` citations under `docs/` alone, plus the `src/tychoc.c:N` ones, and
    every phase that edits either file shifts a share of them.
  - Already measured stale, and NOT fixed by Phase 40 (out of its `16-builtins.md`
    scope): `docs/spec/03-types.md:305` cites `compiler/tychoc0.ty:1718-1750` for the
    `bounded` branch of `parse_type_d` — that branch is now at `:1866-1897`; `:309`
    cites `:1746-1749` for the affine-element check, now `:1890-1896`; `:306` cites
    `mangle_type` at `:2859`, now `:3237`; `:307` cites the unresolved-name guard at
    `:11211`, now `:11840`. All four were already wrong by >100 lines before Phase 40
    touched the file. In-source comments carry the same rot (`tychoc0.ty:1870-1874`
    cites `:1669`, `:1686`, `:2859`, `:3451`).
  - The fix the Phase 40 block suggested: a gate that resolves each `path:N` citation to
    an expected symbol or substring, so the class becomes green/red instead of
    "audit again next time". `make check-links` already walks every markdown file and is
    the natural host. The hard part is the *expected content*, which the citation does
    not carry today — so the gate probably needs the citation syntax to name an anchor
    (e.g. `` `src/tychoc.c:5323 len` ``), which is a docs-wide edit.
  - Done when: every `path:N` citation under `docs/` resolves to the code it claims,
    verified by reading, and a gate fails when one stops resolving.
  - **DONE 2026-07-25, but the premise was wrong by an order of magnitude and the phase
    is closed on a *scoped* deliverable, not on "every citation repaired". Read this.**
  - **Magnitude, measured (`/tmp/ph42/scan.py`, `/tmp/ph42/classify.py`).** Not 96
    citations — **1451** `path:N` citations across `docs/`, of which **850** point into
    implementation trees. `docs/spec/**` alone holds **295**. The plan's "96
    `tychoc0.ty:N` citations" counted one target in one spelling.
  - **Staleness, measured.** A structural symbol indexer (C: column-0 definition to the
    first column-0 `}`; Tycho: `fn`/`struct`/… to the next one) was used to ask, for
    each `docs/spec` citation that sits next to a backticked symbol name, whether the
    cited line lies inside that symbol. Of the **119** citations answerable that way:
    **114 point outside the function the prose names; 5 are correct.** The other 176 name
    no symbol and could not be judged mechanically. Spot-reads confirm the machine:
    `compile_package` cited `:10355-10360` (actually `clone_expr`; the function is at
    `:11522-11527`), `parse_extern_fn` cited `:3212-3282` (actually compound-assign
    parsing; the function is at `:3530-3600`). **The `docs/spec` provenance corpus is
    substantially stale, not marginally so.** Repairing all of it means re-deriving each
    sentence's intent — a project, not a phase. Filed as **Phase 44**.
  - **What this phase DID repair (21 citations, each verified by reading the target):**
    `15-program.md`'s whole entry-point provenance block (7 citations — Phase 43's four
    plus `compile_package`, the driver, `parse_extern_fn`, the `cc` invocation);
    `03-types.md`'s `bounded` provenance block (6, including the four the phase named —
    and **two of those four replacements given in the phase text were themselves wrong**:
    `mangle_type` at `:2859` is not `:3237` (that is the *variadic* branch) but
    `:3301@[b#`, and the unresolved-name guard is not `:11840` (a `j := 0` in a switch)
    but `:11858-11862` in `collect_stmt`); 8 in-source comment citations in
    `compiler/tychoc0.ty` (`:1669`→`:1803`, `:1686`→`:1821`, `:2859`→`:3251`,
    `:3451`→`:11858-11862`, `:1734`→`:1882` ×2, `src/tychoc.c:669-671`→`:679-691` ×5 —
    `arrc_sized_b` had moved); and 9 unattributable bare continuations in
    `plan-int64-DONE.md` / `plan.md` given their explicit path.
  - **THE GATE — `scripts/check_citations.py`, hosted in the `check-links` lane**
    (`Makefile:65-71`; `scripts/ci.sh`'s last step already calls `make check-links`, so it is
    wired into CI with no new step). Extending the existing lane beat a new one: every
    caller that already runs `check-links` picks the new check up without remembering to.
  - **Design decision, and the two shapes that were tried and rejected.**
    1. *Symbol-extent anchors* (the form `src/tychoc.c:7206-7207` suffixed `@resolve_program` — assert the cited range
       lies inside that function) were implemented first and **demonstrated useless for
       this failure mode**: pointing the citation back at the original bad `src/tychoc.c:7098-7099`
       left the gate GREEN, because `resolve_program` spans `src/tychoc.c:7096-7219` and the wrong
       lines are inside it too. Function granularity is too coarse. Rejected on evidence.
    2. *A generated lockfile of cited-line hashes* was considered and rejected: it
       freezes whatever is on disk at generation time, so it would have blessed all ~290
       stale citations as correct, and it puts the expected content in a second file that
       drifts from the prose.
    3. **Adopted: an opt-in content anchor.** `` `src/tychoc.c:7478-7479@'main' must be` ``
       — the gate asserts the cited range literally contains that token. The token is
       chosen by whoever verifies the citation, which is exactly the expected content a
       bare `path:N` cannot carry. Not applied docs-wide (1300+ sites, and anchoring an
       unverified citation to its own wrong line is worse than leaving it bare) — the
       rule is **anchor a citation when you verify it**. 18 anchored so far.
  - **What the gate does NOT catch — stated in its header and repeated here.**
    A **bare** `path:N` that drifts onto a different-but-existing line is invisible: that
    is precisely the Phase 43 failure mode, and only anchoring catches it. Coverage is
    therefore **22 anchored / 1320 bare** at commit (the 18/1304 in the transcripts
    below was the state at proof time, before this evidence block added its own
    anchored quotes), and a green run honestly means "no
    anchored citation has drifted and no citation of either kind points outside its
    file" — nothing more. It also cannot catch a drift that keeps the token inside the
    new range, a citation anchored to its own wrong line on the day it was written, or a
    docs claim that is wrong about *behaviour* rather than about a line number.
    Fail-open by design: a bare `:N` whose paragraph names no path is skipped, not
    guessed at.
  - **Two bugs the gate found in its own author's work, before any of this was committed.**
    (a) `docs/spec/03-types.md` and `15-program.md` each had a continuation `:N` that
    silently inherited the *wrong* file, because naming `` `compiler/tychoc0.ty` ``
    without a line number does not rebind the path. (b) The anchor regex banned spaces,
    so every multi-word anchor (`@'main' must be`) matched nothing and was scored as
    unchecked — 18 anchored citations silently became 6. Both fixed; (b) is the reason
    the header now documents the shapes explicitly (RULE 7).
  - **DEMONSTRATED RED — reproducing the real failure mode, not an out-of-range number.**
    `docs/spec/15-program.md:20`'s anchor re-pointed at `:7098-7099`, lines that exist
    and hold plausible C (`/* CC-4: a channel handle must not outlive its creating
    scope. ... */`) — exactly the drift that survived Phase 34 and shipped to Phase 43:
    ```
    link check: ok (122 markdown files, no dead relative links)
    STALE  docs/spec/15-program.md:20  `:7098-7099@'main' must be` -> lines 7098-7099 of src/tychoc.c do NOT contain ''main' must be'; it appears at :7207
    citation check: FAILED (1 stale citation(s) above)
    make: *** [Makefile:70: check-links] Error 1
    exit=2
    ```
    Restored → GREEN:
    ```
    link check: ok (122 markdown files, no dead relative links)
    citation check: ok (18 anchored contain the token they name, 1304 bare in bounds)
    exit=0
    ```
  - **Cost: 0.07 s** (`0.05s user 0.02s system` over 122 markdown files / 1322 citations).
    Free at the scale of the `check-links` lane; no separate `make` target needed.
  - Gate set, one per command, foreground, `env -u LD_PRELOAD`:
    ```
    make test         passed: 540   failed: 0 / all green
    make corelib      corelib: all green (tychoc and tychoc0 agree, match goldens)
    make conc         conc: passed 37   failed 0
    make fixpoint     ok B == C : tychoc0 reproduces itself byte-identically (35691 lines C) / fixpoint: all green
    make ilp32        passed: 540   failed: 0 / all green
    make asan-self    asan-self: compiled: 540   failed: 0 / all green
    make frontparity  frontparity: agreed: 287   diverged: 0   (skipped, tychoc refused: 15) / all green
    make spec-check   spec-examples: 7 runnable example(s), all pass
    make check-links  link check: ok (122 markdown files, no dead relative links)
                      citation check: ok (18 anchored contain the token they name, 1304 bare in bounds)
    ```

### Filed by Phase 39 (2026-07-25)

- [x] **Phase 43 — `15-program.md`'s `main`-signature provenance is stale AGAIN, and it now points at two unrelated rules (found by Phase 39, out of its scope)**
  - Phase 34 was the phase that fixed these exact citations. They have drifted since, and
    the drift is the bad kind: the lines still exist and still contain plausible-looking
    code, so nothing looks wrong until you read them.
  - `docs/spec/15-program.md:19` and `:31` both cite `src/tychoc.c:7098-7099` for the
    entry point / `main` signature rule, and `:31` also cites `:7124-7125`. Measured
    **before** Phase 39 touched anything (so this is pre-existing drift, not drift Phase
    39 introduced): `grep -n "no 'main' procedure" src/tychoc.c` → **`7132`**, and
    `:7098-7099` was `if (IS_CHAN(pr->ret)) die_at(... "a function cannot return a
    channel" ...)` while `:7124-7125` was the `inout` function-value die. Both citations
    named a *different rule* than the sentence they support.
  - `docs/internals/frontend-restriction-audit-2026-07-25.md:356` has the same drift one
    notch further (`:7097-7098` / `:7123-7124`).
  - Also: `src/tychoc.c` has no `die_at` at all for "`main` must take no parameters" —
    `grep -n "main.*no parameters"` returns nothing in `src/tychoc.c`, though
    `tests/reject/main_with_param.ty` passes and `compiler/tychoc0.ty` has the check
    explicitly. Find where tychoc actually enforces it before re-citing, rather than
    picking the nearest plausible line — that is how these citations went wrong twice.
  - **Not folded into Phase 42.** Phase 42 is the *gate* — something that fails when a
    citation stops resolving. This is the concrete repair of four known-wrong citations,
    which Phase 42 will need as its first passing case anyway. If Phase 42 runs first it
    may absorb this; if it does, close this by reference.
  - Note that Phase 39 DID keep every citation it moved in sync
    (`07-memory-model.md:221`, `tests/reject/params_17.ty`, `extern_params_17.ty`,
    `inout_fnvalue.ty`, `chan_inout_param.ty`, `handle_dup_name.ty`); these four were
    already wrong on arrival and are out of its scope lock.
  - Done when: each of the four citations resolves to the code its sentence claims,
    verified by reading, and the gate set is green.
  - **DONE 2026-07-25. Ran before Phase 42, as this block asked.**
  - **THE TRACED ENFORCEMENT SITE.** Started from the fixture, not from a grep of the
    spec's wording. `./tychoc tests/reject/main_with_param.ty` emits:
    ```
    tests/reject/main_with_param.ty:3: error: 'main' must be 'fn main():' with no return
    ```
    Grepping **that** message lands on `src/tychoc.c:7207`, inside `resolve_program`
    (`:7096-7219`), and the rule is **one combined test covering both halves** — there is
    no separate "no parameters" `die_at`, which is why `grep "main.*no parameters"`
    returned nothing and why the earlier repairs guessed:
    ```
    7206|         if (!strcmp(pr->name, "main") && (pr->nparams != 0 || pr->ret != T_VOID))
    7207|             die_at(pr->line, "'main' must be 'fn main():' with no return");
    ```
    The companion "no `main` at all" rule is **not** a `die_at` either — it is a bare
    `fprintf`/`exit` twelve lines earlier, `src/tychoc.c:7181`:
    ```
    7180|     Sig *m = sig_find("main");
    7181|     if (!m) { fprintf(stderr, "%s: error: no 'main' procedure\n", g_srcname); exit(1); }
    ```
    tychoc0's twin is at `compiler/tychoc0.ty:3861`, in `parse_program` (`:3741-3947`) —
    a **parse-time** check, where tychoc's is a **resolve-time** one. Both reject; the
    phase difference is real and is now stated in the spec sentence.
  - **Re-derived at HEAD, and the plan's own numbers had moved again.** This block
    recorded `no 'main' procedure` at `:7132`; at HEAD it is `:7181` (Phase 39 shifted
    `resolve_program`). The prompt's numbers were not trusted.
  - **Repaired (5 sites, each verified by reading the target line):**
    `docs/spec/15-program.md:19-23` (the provenance block) and `:31-36` (§27.1's
    sentence) — both `:7098-7099` and `:7124-7125` replaced with
    `` `src/tychoc.c:7453@no 'main' procedure` `` and
    `` `:7478-7479@'main' must be` ``, plus the tychoc0 citation corrected from
    `parse_program` `:3637-3648` to `` `compiler/tychoc0.ty:3911@'main' must be` ``;
    `docs/internals/frontend-restriction-audit-2026-07-25.md:356` — the audit's §8 is a
    dated record, so its wrong text is left standing and a **Corrected 2026-07-25** note
    is appended beneath it naming the real sites (rewriting a dated audit in place would
    destroy the evidence that the drift happened twice);
    `tests/reject/main_with_param.ty:1` — the fixture's own header cited the same dead
    `src/tychoc.c:7124-7125`, now `:7206-7207 (resolve_program)`.
  - **Each repaired citation is anchored** (`@token`), so Phase 42's gate content-checks
    all five rather than merely bounds-checking them. Verified RED against exactly this
    drift — see Phase 42's evidence.
  - Gate set: all green (single run shared with Phase 42; summary lines under Phase 42).

### Filed by Phase 42 (2026-07-25)

- [x] **Phase 44 — CLOSED, WILL NOT DO AS A SWEEP (user decision 2026-07-25): the `docs/spec` provenance corpus is substantially stale, not marginally: 114 of the 119 mechanically-checkable citations point outside the function their own sentence names (measured by Phase 42, far beyond its scope)**
  - **Closed without sweeping it, deliberately.** The user asked to close the remaining
    phases on 2026-07-25. Reasons, in order of weight:
    1. **A hand sweep of 1451 citations cannot be trusted, and this plan proved it.**
       Phases 34, 35 and 40 each repaired a batch, and each time the *replacement* lines
       were also wrong — including two of the four replacements written into Phase 42's
       own text by the main agent. A corpus that large, repaired by hand against a moving
       file, regenerates its own error rate. Doing it would produce the appearance of
       accuracy rather than accuracy.
    2. **The bleeding is stopped where it can be.** `scripts/check_citations.py` (Phase
       42) gates every *anchored* citation and costs 0.07s. New and touched citations can
       carry an `@token` anchor and are then checked forever.
    3. **The unfixed remainder is honestly labelled.** The count, the method, the
       reproduction scripts and the indexer's known false-positive mode are all recorded
       above. A reader is told the corpus is unreliable rather than being left to assume
       it is not.
  - **The standing rule that replaces the sweep:** when a phase edits `src/tychoc.c` or
    `compiler/tychoc0.ty`, it repairs and `@`-anchors the citations it touches — the
    incremental path Phase 42's gate was built for. Anchored coverage at closure: 22 of
    1342.
  - Reopen only with a *mechanical* repairer (a symbol-indexer that rewrites `path:N`
    from the named symbol), never as another hand sweep. That tool is the real Phase 44;
    the sweep is not.
  - Phase 42 was scoped to "fix the four known-wrong ones and gate the class". Measuring
    the class first is what turned up the real number, and it is not a batch — it is a
    corpus. **Do not let a future phase quietly re-scope itself into this.**
  - **The measurement.** `docs/` holds **1451** `path:N` citations, **850** into
    implementation trees, **295** in `docs/spec/**`. A structural symbol indexer answered
    "is the cited line inside the function the prose names?" for the **119** citations
    that sit beside a backticked symbol: **114 outside, 5 inside**. The remaining 176
    name no symbol and cannot be judged without reading each sentence's intent.
    Reproduce with `/tmp/ph42/scan.py` and `/tmp/ph42/classify.py` (both throwaway;
    re-derive rather than trust them, and note the indexer is a heuristic — it false-
    positives when the prose names a symbol it does not actually mean, e.g. a doc saying
    "the generated `main`" while `main` is also a function in `src/tychoc.c`).
  - **Worked examples confirming the machine, not just its count:**
    `15-program.md` cites `compile_package` at `:10355-10360` — those lines are
    `clone_expr` field copies; the function is at `:11522-11527` (Phase 42 fixed this
    one). It cites `parse_extern_fn` at `:3212-3282` — compound-assign parsing; the
    function is at `:3530-3600` (also fixed). Roughly a dozen more citations in that
    file alone (`:10162`, `:10604`, `:3728`, `:10601`, `:10600`, `:10518`, `:10534`,
    `:10541-10547`, `:10537-10540`, `:10415-10424`, `:10328-10347`, `:4319-4331`,
    `:10336-10339`, `:10197`, `:3466-3478`, `:3559-3568`, `:3572-3578`, `:3448-3452`,
    `:1343-1345`, `:3580-3587`) were read and are wrong in the same way, and were left
    alone rather than half-fixed. `03-types.md`'s `arrc_sized_b` message citations
    `:567` / `:607` are unverified and likely share the drift.
  - **Do it with the mechanism, not by hand.** Phase 42 shipped
    `scripts/check_citations.py` and the `@token` anchor. The unit of work here is: read
    the sentence, find what it actually means, re-point the citation, **anchor it**, and
    watch the gate's anchored count rise. A citation repaired without an anchor will
    silently rot again — that is the whole lesson of Phases 34/35/40/43.
  - **This is a corpus, so it needs a stopping rule, not a completion date.** A defensible
    sub-scope: `docs/spec/**` only (295), or even one chapter per phase. The archival
    trees (`docs/internals/*-DONE.md`, `docs/internals/*audit*`, `docs/rfc/`) are dated
    point-in-time records — **do not rewrite them in place**; append a correction note
    the way Phase 43 did for `frontend-restriction-audit-2026-07-25.md`.
  - Done when: a named sub-scope's citations each resolve to what their sentence claims,
    verified by reading, and each is anchored so the gate holds them.

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
