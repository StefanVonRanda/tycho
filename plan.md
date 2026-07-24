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

- [ ] **Phase 1 — `type_name` names every reachable type (#2)**
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

- [ ] **Phase 2 — MEASURE diagnostic-text divergence across all 143 reject fixtures (#3, part 1)**
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

- [ ] **Phase 3 — gate diagnostic parity, IF Phase 2 says it is bounded (#3, part 2)**
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
