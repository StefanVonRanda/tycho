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
  element-wise (`docs/spec/12-aggregates.md:146`, `:289`,
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

- [ ] **Phase 1 — array ⊕ array, both kinds, with the two mismatch behaviours**
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

- [ ] **Phase 2 — scalar broadcast, both directions**
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

- [ ] **Phase 3 — the spec, and the conformance record**
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

- [ ] **Phase 4 — the one full sweep**
  - `make ci`, once, at the end of the chain, per `CLAUDE.md`'s gate budget. This
    is the phase that runs the fuzzers, the ILP32 rebuild and the TSan lane over
    everything phases 1 and 2 added.
  - Done when: `CI GREEN`, exit 0. If it reddens, name the step and whether this
    plan caused it.

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
