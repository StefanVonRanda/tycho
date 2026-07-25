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

- [ ] **Phase 17 — `SMatch` carries no per-arm source locations in tychoc0 (found by Phase 8, deliberately NOT forced there)**
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

- [ ] **Phase 21 — the deferred const-size encodings leak into diagnostics outside a local declaration (observed by Phase 18)**
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

- [ ] **Phase 22 — tychoc0 does not restrict a newtype's UNDERLYING type the way tychoc does (found by Phase 20, out of its scope)**
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

- [ ] **Phase 23 — tychoc0's array-family name collision: `[bounded[N]T]` and `bounded[N]T` mangle to the same `Arr_*` (measured by Phase 19, out of its element-type scope)**
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
