# Close the remaining gaps to a Tycho 1.0 freeze

Supersedes the completed rtparity plan (Phases 1–6 done; evidence in commits
`0513cb3`, `d725c98`, `bcf9ae9`, `d7ae8a5`, `34d30fc`, `56345a9`). That work
emptied the rtparity allowlist and made both abort lanes differential.

## Goal

Close the four gaps the language-status survey surfaced as all that stands
between the current tree and a 1.0 formal-spec freeze. Three are unratified spec
decisions (now ruled by the user); one is a real compiler limitation present in
BOTH compilers. Done = the three corners are ratified as normative spec text with
`make spec-check` green, multi-statement value arms work in both compilers and
are locked by fixtures under every gate, and `docs/internals/spec-plan.md`'s
residual-decision list (§11) and punch-list items #16/#18/#39 are marked resolved.

User rulings (this session):
- **#16 `int` width** → REQUIRE 64-bit lowering (spec mandates fixed-width
  64-bit two's-complement; note the reference codegen conforms on LP64 only —
  roadmap rec (a), `spec-plan.md:334,546`). No codegen migration in this plan.
- **#18 shift ≥ width** → RATIFY AS UNSPECIFIED (count MUST be `0..width−1`;
  otherwise unspecified — matches both compilers today, no codegen change,
  `spec-plan.md:430-434`).
- **#39 `deps` tier** → NORMATIVE-BUT-OPTIONAL (an impl MAY omit the extended
  tier and still conform at the core tier, `spec-plan.md:548`).
- **value arms** → IMPLEMENT multi-statement value arms in both compilers
  (`src/tychoc.c:2501`, `compiler/tychoc0.ty:2325` both `die` today).

## Pre-flight

- Worst case: the value-arm feature is the only implementational change. A wrong
  parser/codegen edit can miscompile VALID programs — both compilers build
  themselves, the corelib and every fixture, so a bad block-value lowering breaks
  the compiler for everyone. `make fixpoint` (B==C byte-identical) is the
  backstop: a self-hosting break cannot pass it. The three spec phases touch only
  `docs/`, cannot affect any binary, and are gated by `make spec-check`.
- Reversibility: git; each phase is one commit. No user data, no persistent state.
- Verified: value-arm reject sites at `src/tychoc.c:2501-2502` and
  `compiler/tychoc0.ty:2325` (both emit "multi-statement value branches are not
  yet supported"). `make spec-check` → `scripts/spec_check.sh` (`Makefile:61-62`).
  `docs/spec/appendix-e-conformance.md` holds the conformance rows. Shift/int
  spec sites live in `03-types.md`, `09-expressions.md`, `appendix-f-impl-defined.md`.
- Assuming: block-valued arms lower to a C statement-expression `({ …; v; })`,
  the same GCC/clang extension the rtparity plan already emitted for soa slices —
  so no new codegen capability is needed, only new parsing + reuse. Risk if
  wrong: the feature needs a deeper codegen change; the phase reports the failure
  and halts rather than guessing.

## Phases

Spec phases (1–2) edit ONLY `docs/` and must NOT touch any compiler/runtime
source. Compiler phase (3) edits ONLY `src/tychoc.c` + `compiler/tychoc0.ty` and
must NOT add fixtures or spec text. Lock phase (4) edits fixtures + `docs/`, not
the compilers. Every phase runs each verification gate in its own foreground
command; commits carry NO trailers (repo convention).

- [x] **Phase 1 — ratify `int` = 64-bit (punch-list #16)**
  - Scope: `docs/spec/03-types.md` (the `int` definition), `docs/spec/appendix-f-impl-defined.md`
    (note the reference `long` lowering conforms on LP64 targets only),
    `docs/spec/appendix-e-conformance.md` (conformance row), `docs/internals/spec-plan.md`
    (mark #16 and its §11 residual entry resolved with the ruling). Spec only —
    no compiler change; do NOT migrate codegen to `int64_t` (out of scope, note as
    a possible follow-up).
  - Done when: the spec normatively REQUIRES `int` to be a 64-bit two's-complement
    integer with a fixed-width lowering, states the reference compiler satisfies
    this on LP64 targets only (impl-defined limitation), the conformance row
    exists, and #16/§11 are struck from the open list.
  - Verify: `make spec-check` green; `make check-links` green; grep the new
    normative sentence and the appendix-F note and paste them.
  - DONE (2026-07-23). Spec-only edits, no compiler/runtime source touched:
    - `docs/spec/03-types.md` §5.2.1 — added the normative fixed-width lowering
      requirement: "a conforming implementation MUST realize `int` at exactly 64
      bits through a **fixed-width 64-bit lowering** — a C backend MUST emit a
      fixed-width 64-bit type (`int64_t` / `long long`), never a type whose width
      varies by platform such as C `long`".
    - `docs/spec/appendix-f-impl-defined.md` — added the F.3 reference-implementation
      note: the reference compilers lower `int` to C `long`, 64-bit on LP64 (conforms)
      but 32-bit on LLP64/ILP32 (does NOT conform); int64_t/long long migration is a
      tracked follow-up, not a spec relaxation.
    - `docs/spec/appendix-e-conformance.md` — added §5 row `| §5.2.1 | int = required
      64-bit two's-complement (range, defined wrap) | tests/int_overflow |`
      (`tests/int_overflow` exercises LONG_MAX=2^63−1 / LONG_MIN=−2^63 + wrap).
    - `docs/internals/spec-plan.md` — punch-list #16 marked RESOLVED (option a);
      §11 residual `int`-width bullet struck (`~~...~~`) + RESOLVED.
    - Verify evidence:
      - `make spec-check` → "Appendix A grammar matches §3/§4 (ok)",
        "all Appendix E fixture citations resolve (ok)", "7 runnable example(s), all pass".
      - `make check-links` → "link check: ok (116 markdown files, no dead relative links)".
      - grep 03-types.md:42-43 (MUST realize int at exactly 64 bits / fixed-width 64-bit
        lowering) and appendix-f-impl-defined.md:66-71 (Reference-implementation note:
        lower int to C long / 32-bit on LLP64 / does not conform) — both present.
    - No out-of-scope work discovered (the int64_t codegen migration was already the
      designated follow-up, recorded in spec-plan.md #16 per scope; not a new phase).

- [ ] **Phase 2 — ratify shift-≥-width (unspecified) + `deps` tier (normative-optional)**
  - Scope: `docs/spec/09-expressions.md` (shift operators: count MUST be
    `0..width−1`, else unspecified), `docs/spec/appendix-f-impl-defined.md` (list
    the out-of-range shift as unspecified), the conformance-tiers text (Ch 1 /
    program chapter — the agent locates it; define core tier = pure-Tycho+libc,
    extended `deps` tier = normative-but-optional), `docs/spec/appendix-e-conformance.md`
    (rows), `docs/internals/spec-plan.md` (mark #18, #39 and their §11 entries
    resolved). Spec only.
  - Done when: (a) an out-of-range or negative shift count is ratified as
    unspecified with the `0..width−1` requirement stated; (b) the `deps` extended
    tier is defined as normative-but-optional and "conforming at the core tier" is
    well-defined without the extended C libs; both #18/#39/§11 struck.
  - Verify: `make spec-check` green; `make check-links` green; grep both new
    normative rules and paste them.

- [ ] **Phase 3 — implement multi-statement value arms in BOTH compilers**
  - Scope: `src/tychoc.c` (the value-branch parser at `:2498-2506`, plus type
    check and codegen) and `compiler/tychoc0.ty` (the mirror at `:2321-2329` plus
    codegen). A value arm of an expression-`if`/`match` may now be an indented
    block of statements whose FINAL statement is a value expression; lower it to a
    C statement-expression `({ stmts; value; })`. Keep the two compilers
    symmetric so tychoc0 still self-hosts byte-identically. Do NOT add test
    fixtures or spec text in this phase (that is Phase 4). Do NOT weaken the
    existing single-expression path (a bare value arm must still work unchanged).
  - Done when: both compilers parse, type-check and codegen a multi-statement
    value arm; the "not yet supported" `die` no longer fires; a hand-run probe
    with a 2-statement `if`-value arm and a 2-statement `match`-value arm compiles
    under BOTH compilers and prints identical output; NO regression in
    `make test`/`make corelib`; `make fixpoint` B==C byte-identical.
  - Verify: build both, run the probe, `cmp` the two outputs (paste identical +
    exit 0). Then `make test`, `make corelib`, `make fixpoint` — each its own
    command, paste each summary line. If the feature needs a codegen change deeper
    than a statement-expression, HALT and report; do not guess.

- [ ] **Phase 4 — lock multi-statement value arms (fixtures + goldens + spec)**
  - Scope: new `tests/*.ty` fixtures + `.out` goldens exercising `if`-value and
    `match`-value multi-statement arms (nested, in a function returning the value,
    with side-effecting statements before the tail); a `tests/reject/` fixture for
    a malformed arm (an indented block whose final statement is NOT a value
    expression) if both compilers reject it; update the grammar/expressions spec
    chapters (`docs/spec/02-grammar.md`, `09-expressions.md`, `10-statements.md`)
    to state a value arm may be a block; `docs/spec/appendix-e-conformance.md`
    (row); `docs/internals/spec-plan.md` (mark the value-arm limitation closed).
    Do NOT touch the compilers.
  - Done when: the new fixtures pass under BOTH compilers (the differential
    `make test` lane), goldens match, any reject fixture is rejected by both, the
    spec no longer claims value arms are single-expression, and EVERY gate is
    green.
  - Verify: `make test`, `make corelib`, `make rtparity`, `make conc`,
    `make fixpoint`, `make spec-check`, `make check-links` — each its own
    foreground command; paste each summary line.
