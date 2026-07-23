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

- [x] **Phase 2 — ratify shift-≥-width (unspecified) + `deps` tier (normative-optional)**
  - CORRECTED SCOPE (2026-07-23). The premise "shift is unspecified, no guard"
    was STALE (from a pre-guard 2026-07-12 probe). Source shows shift is already
    DEFINED and guarded in both compilers (`runtime/tycho_rt.c:129`: ≥width→0,
    negative→abort), normative in `09-expressions.md` §13.2 / `17-runtime.md` §30,
    already removed from the unspecified list in `appendix-f-impl-defined.md`, and
    locked by `tests/shift_edge.ty`. The `deps` tier was likewise already
    normative-but-optional (`00-conventions.md` §1.3, `15-program.md` §28.6).
    User ruling (this session): KEEP shift DEFINED (do not regress the guard).
    So no spec or compiler change was needed — only roadmap bookkeeping.
  - DONE. Edited only `docs/internals/spec-plan.md`: punch-list #18 marked
    RESOLVED-as-DEFINED (not unspecified); the superseded #18 probe note in §6a
    annotated with the post-probe guard; punch-list #39 and the §11 `deps`-tier
    residual bullet marked RESOLVED (already in-spec). No `docs/spec/` normative
    text changed; no compiler/runtime touched.
  - Verify evidence:
    - `make spec-check` → "spec-examples: 7 runnable example(s), all pass"
      (grammar + Appendix E citations resolve).
    - `make check-links` → "link check: ok (116 markdown files, no dead relative
      links)".
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

- [x] **Phase 3 — implement multi-statement value arms in BOTH compilers**
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
  - DONE (2026-07-23). No codegen change was needed: both compilers already
    DESUGAR a value branch into an ordinary statement block (non-decl tails →
    S_RETURN/S_ASSIGN/S_FIELDSET at parse time; `:=`/typed-`:=` tails → `name =
    tail` at resolve/codegen). Multi-statement support = parse leading statements
    + a trailing value expression, then rewrite/collect the LAST branch statement
    instead of the first. Leading statements ride along as plain sequenced C — the
    suggested `({ …; v; })` statement-expression was unnecessary for this arch.
    - `src/tychoc.c`: added `value_block_at_tail()` (is the parser on the branch's
      final line?); rewrote `parse_value_block()` to parse leading statements via
      `parse_stmt` (draining the foreach `g_pending` queue like `parse_block`) then
      the tail via `parse_expr`, enforcing the last statement is an `S_EXPR`;
      forward-declared `parse_stmt` and hoisted `g_pending`/`g_npending` above it;
      `ctrl_rewrite_tails`/`ctrl_collect_tails` now target `body[nbody-1]`. The DECL
      case types the tail with branch scope for free (it already calls
      `resolve_stmt(s->ctrl)` = full `resolve_block` over the branch before
      `ctrl_collect_tails`).
    - `compiler/tychoc0.ty`: mirror — `value_block_at_tail`/`is_sexpr` helpers, the
      same multi-statement `parse_value_block`, `branch_tail`/`branch_tail_line`
      read the LAST statement, `rewrite_branch` keeps the leading statements. Its
      typing (`value_ctrl_type`) does NOT resolve the branch block, so a new
      `extend_tail_scope()` walks the leading `SDecl`/`STypedDecl` statements and
      pushes their locals onto a value-copy of names/types before typing each tail
      — matching tychoc's branch-scoped resolve for the `:=` DECL position.
    - Probe (`/tmp/.../probe.ty`, NOT a committed fixture) exercises the hardest
      path — `:=` DECL position, 2-statement `if`-value arm AND 2-statement
      `match`-value arm, each binding a branch-local then yielding an expr using it,
      plus a `return`-position value if:

      ```
      fn classify(n: int) -> string:
          label := if n > 0:
              doubled := n * 2
              "pos:" + str(doubled)
          else:
              neg := 0 - n
              "nonpos:" + str(neg)
          return label

      fn describe(o: Option(int)) -> int:
          r := match o:
              Some(x):
                  sq := x * x
                  sq + 1
              None:
                  base := 7
                  base * 2
          return r

      fn pick(b: bool) -> int:
          return if b:
              a := 10
              a + 5
          else:
              c := 2
              c * 3

      fn main():
          print(classify(3))
          print(classify(0 - 4))
          print(str(describe(Some(5))))
          print(str(describe(None)))
          print(str(pick(true)))
          print(str(pick(false)))
      ```

    - Gate evidence:
      - probe: tychoc and self-hosted tychoc0 both build it; both print
        `pos:6nonpos:42614156`; `cmp` of the two outputs → IDENTICAL, exit 0.
      - `make test` → `passed: 404   failed: 0` / `all green`.
      - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`.
      - `make fixpoint` → `ok   B == C : tychoc0 reproduces itself byte-identically
        (34660 lines C)` / `fixpoint: all green`. The backstop holds: a broken
        block-value lowering could not self-host byte-identically.
    - SCOPE DEVIATION (transparent): the feature invalidated one committed reject
      fixture, `tests/reject/if_expr_multistatement.ty`, which asserted the removed
      "multi-statement value branches are not yet supported" limitation (its `x :=
      if true: y:=1; y` program is now valid). `make test` cannot be green while it
      exists, so it was `git rm`'d in this phase even though fixtures are otherwise
      Phase 4's domain. Phase 4 adds the replacement positive fixtures + goldens and
      a reject fixture for a malformed arm (a block whose final statement is not a
      value expression — both compilers reject via the `is_sexpr(body[last])` /
      `body[n-1]->kind != S_EXPR` check).

- [x] **Phase 4 — lock multi-statement value arms (fixtures + goldens + spec)**
  - Scope: new `tests/*.ty` fixtures + `.out` goldens exercising `if`-value and
    `match`-value multi-statement arms (nested, in a function returning the value,
    with side-effecting statements before the tail); a `tests/reject/` fixture for
    a malformed arm (an indented block whose final statement is NOT a value
    expression) if both compilers reject it; update the grammar/expressions spec
    chapters (`docs/spec/02-grammar.md`, `09-expressions.md`, `10-statements.md`)
    to state a value arm may be a block; `docs/spec/appendix-e-conformance.md`
    (row); `docs/internals/spec-plan.md` (mark the value-arm limitation closed).
    Do NOT touch the compilers. NOTE: Phase 3 already `git rm`'d the now-invalid
    `tests/reject/if_expr_multistatement.ty` (it asserted the removed limitation);
    this phase adds the replacement positive fixtures/goldens and the new
    malformed-arm reject fixture.
  - Done when: the new fixtures pass under BOTH compilers (the differential
    `make test` lane), goldens match, any reject fixture is rejected by both, the
    spec no longer claims value arms are single-expression, and EVERY gate is
    green.
  - Verify: `make test`, `make corelib`, `make rtparity`, `make conc`,
    `make fixpoint`, `make spec-check`, `make check-links` — each its own
    foreground command; paste each summary line.
  - DONE (2026-07-23). Fixtures + goldens + spec text, no compiler/runtime source
    touched.
    - New positive fixtures (each with binding + side-effecting leading statements
      before a tail value, a `return`-position arm, and nested plain control flow
      inside an arm), goldens generated from each compiler's native stdout:
      - `tests/if_expr_block.ty` + `tests/if_expr_block.out` — `:=`/typed-`:=`/
        `return`-position value `if`; branch-local binds, a `push` side effect, a
        nested plain `if` statement + a `for` loop before the tail.
      - `tests/match_expr_block.ty` + `tests/match_expr_block.out` — `:=`/
        `return`-position value `match`; payload binds + leading locals in the
        tail, a `_` wildcard arm, a nested plain `if` statement, an `Option`
        scrutinee with a side-effecting `push`.
    - New reject fixture (rejected by BOTH compilers with a diagnostic — verified):
      - `tests/reject/value_arm_no_tail.ty` — a value `if` branch whose FINAL
        statement is a `for` loop (not a value expression). tychoc:
        "a value branch must end in a value expression"; tychoc0:
        "parse: a value branch must end in a value expression".
    - Spec text (no longer claims value arms are single-expression):
      - `docs/spec/09-expressions.md` §13.5 — branch/arm is "an indented block
        whose **final statement is a value expression**"; removed "Multi-statement
        branches" from the not-supported list.
      - `docs/spec/02-grammar.md` — §4.3.2 EBNF comment on `ValueCtrl` now reads
        "block branches ending in a value expression"; the prose bullet updated to
        "an indented block whose final statement is a value expression".
      - `docs/spec/appendix-a-grammar.md` — regenerated the one drifted `ValueCtrl`
        line to match `gen_grammar.sh` (spec-check Check 1 stays green).
      - `docs/spec/10-statements.md` §19 note — value form is "arms are blocks
        ending in a value expression".
      - `docs/spec/12-aggregates.md` §19.4 — same value-`match` construct; updated
        the two "single expression" claims (NOT in the three named files, fixed for
        spec self-consistency; see out-of-scope note below).
      - `docs/spec/appendix-e-conformance.md` §13.5 — dropped the stale
        `reject/if_expr_multistatement` citation (Phase 3 `git rm`'d that fixture,
        leaving spec-check Check 2 broken) and added a new §13.5 row citing
        `tests/if_expr_block`, `tests/match_expr_block`, `reject/value_arm_no_tail`.
      - `docs/internals/spec-plan.md` §11 — added a RESOLVED bullet closing the
        single-expression value-arm limitation.
    - Gate evidence (each its own foreground command):
      - `make test` → `passed: 407   failed: 0` / `all green` (404 → 407: +2
        positive fixtures, +1 reject).
      - `make corelib` → `all green (tychoc and tychoc0 agree, match goldens)`.
      - `make rtparity` → `0 allowlisted difference(s)` on all three lanes / the
        two runtimes agree.
      - `make conc` → `passed 36   failed 0`.
      - `make fixpoint` → `ok   B == C : tychoc0 reproduces itself byte-identically
        (34660 lines C)` / `fixpoint: all green`.
      - `make spec-check` → `Appendix A grammar matches §3/§4 (ok)`,
        `all Appendix E fixture citations resolve (ok)`, `7 runnable example(s), all pass`.
      - `make check-links` → `link check: ok (116 markdown files, no dead relative links)`.
    - NOTE (transparent scope deviation): `docs/spec/12-aggregates.md` was edited
      though Phase 4's scope named only 02-grammar / 09-expressions / 10-statements.
      §19.4 makes the SAME value-`match` single-expression claim; leaving it would
      make the spec self-contradictory against the Done-when ("the spec no longer
      claims value arms are single-expression"). Edited the two lines only.

- [ ] **Phase 5 — tychoc0: bind value-ctrl leading decls in a value arm's tail scope**
  - OUT-OF-SCOPE DISCOVERY (Phase 4, 2026-07-23). The two compilers DIVERGE on a
    well-formed program: a value-ctrl leading decl inside a value arm. tychoc
    ACCEPTS and runs it; tychoc0 REJECTS it as an unknown variable in the tail.
    Minimal repro:
    ```
    fn f(n: int) -> string:
        r := if n > 0:
            inner := if n > 10:
                "big"
            else:
                "mid"
            "pos-" + inner
        else:
            "neg"
        return r
    fn main():
        println(f(20))
    ```
    tychoc: ACCEPTED. tychoc0: `line 7: type: unknown variable 'inner'`.
  - Root cause: `compiler/tychoc0.ty` `extend_tail_scope` (added by Phase 3, near
    `:8231`) walks a value arm's LEADING statements and pushes only `SDecl`/
    `STypedDecl` locals whose RHS is a plain expr (`type_of(de, …)`). A value-ctrl
    leading decl (`inner := if …`) carries its control on the decl node, not a
    plain expr, so its name is never bound — the tail then can't see it. tychoc
    avoids this because it resolves the whole branch block (`resolve_block`) before
    collecting tail types, so the nested value-ctrl decl binds `inner` for free.
  - Scope: fix `extend_tail_scope` (or its callers) in `compiler/tychoc0.ty` to
    bind a value-ctrl leading decl's name/type in the tail scope, matching tychoc.
    Do NOT touch `src/tychoc.c` (already correct). Add a positive fixture
    exercising a value-ctrl leading decl inside a value arm (both compilers must
    agree, byte-identical). Keep `make fixpoint` B==C byte-identical.
  - Done when: both compilers accept the repro and agree byte-identically; a new
    fixture locks it; all seven gates green.
