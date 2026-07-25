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
  (`%s*ekeys`, `:10292`, `ks="char *"`) while tychoc0 emits `char** ekeys`
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

- [ ] **Phase 12 — the integer-literal emitter produces two default-on C warnings (found by Phase 4, out of its scope)**
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

- [ ] **Phase 13 — emitted C is not clean under opt-in `-Wall -Wextra`: 13346 warnings, ~89% unused-symbol (measured by Phase 4)**
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

- [ ] **Phase 14 — `tychoc`'s own build has 3 `-Wmissing-field-initializers` (observed by Phase 1, re-confirmed by Phase 4)**
  - `src/tychoc.c:6092`, `:6093`, `:6095` — `missing initializer for field
    'is_sink' of 'Param'`. Count was 3 before Phase 1 and is still 3 after
    Phase 4; nothing in this plan changed it.
  - **Explicitly ruled out of "emitted C is warning-clean" (Phase 4).** This is
    the *compiler's own* `-Wall -Wextra` build (`Makefile:11`), not emitted
    output. Different surface, different fix, so it gets its own phase rather
    than riding along on a codegen change.
  - Done when: `make` compiles `src/tychoc.c` with zero warnings; full gate set
    green.

- [ ] **Phase 14 — tychoc0 does not check that an `if` condition is a bool (found by Phase 7, NOT fixed there)**
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
