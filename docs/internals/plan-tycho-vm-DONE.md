# What `tycho-vm` said the language needs

**Status: DONE. All 5 phases complete 2026-08-03. Program: bytecode VM (`tools/tycho-vm/`, commits 1bb0668, 89909d2, 4782ff8) — the first program here to need a hot dispatch loop and fixed-capacity memory. Outcome: scalar `match` shipped, `&` restricted to `inout` arguments, `inout` docs corrected.**

Previous plan: the four-of-seven plan (archived, then pruned 2026-08-03;
still at `git show docs-archive:docs/internals/plan-four-of-seven-DONE.md`).

**Five findings came out of it; two became phases.** Rule: *does anything that is not `tycho-vm` need this?* A finding becomes a phase only with a second caller.

Recorded, not phases:

- **`bounded[N]T` has no `push`/`pop`/`len`**, so it cannot model a stack.
  `grep -rl 'bounded\[' --include='*.ty' corelib/ examples/ tools/ server/`
  returns **one file, `tools/tycho-vm/main.ty`** — the other 51 hits are all
  under `tests/`. One customer. Returns if a second caller appears.
- **A `[N]T` table cannot be a top-level `const`** (`expected '=' after the
  constant name`; §12.2 wants the RHS to fold to one literal). The only
  fixed-array table returned by a function in the tree is
  `tools/tycho-vm/main.ty@optable`. Same reasoning.
- **`defer` does not exist** (absent from `docs/spec/appendix-b-keywords.md`;
  `defer f()` is rejected as a bare expression statement). Memory is arena-freed
  at scope exit, `core:io` is path-based with no handles, and the only manual
  cleanup in the tree is 15 `close` calls in `server/main.ty` plus a handful in
  `examples/`. Nothing needs it.

Measured, on this tree:

- A struct-with-a-string copy costs **815 ns** against **110 ns** for a bare
  `JMP` — 7x — isolated by adding 20 jumps per iteration and differencing. VM
  throughput 1.39 M instructions/sec.
- `match` on an `int` or a `string` is a parse error:
  `expected a match arm \`Variant(bindings):\` or \`Variant:\``. Confirmed by
  probe, not inherited.
- Six files carry an `if`/`elif` ladder of 4+ arms over a scalar — `tools/lsp.ty`
  (54), `tools/tycho-vm/main.ty` (38), `tools/tychofmt.ty` (18),
  `corelib/json/json.ty` (18), `corelib/markdown/markdown.ty` (16),
  `tools/tycho-q/main.ty` (9). Five of the six predate `tycho-vm`, which is why
  scalar `match` is a phase and the two constructs above are not.

## Phases

- [x] **Phase 1 — DESIGN: `match` on scalars**
  - Deliverable was a written design at `docs/internals/design-scalar-match.md`,
    **not code**.
  - `match` was enums-only, so every dispatch table in the tree was an `if`/`elif`
    ladder: `tools/tycho-vm/main.ty` averages 12 comparisons per instruction
    over 24 opcodes, and `corelib/json/json.ty@parse_value` ladders on byte
    values. Lexers, protocol decoders and state machines all hit this.
  - The design answers, each with a reason: which scalar types (`int`,
    `string`, `char`, `bytes`?); whether `_` is required; whether duplicate arms are an error; fall-through; ranges/sets (`1..9`, `1 | 3 | 5`); codegen shape (jump table, binary search, chain) with the deciding threshold.
  - Verify: the document exists, `sh scripts/check_links.sh` and
    `python3 scripts/check_citations.py` green. No build gate.
  - **DONE 2026-08-03.** The design is
    `docs/internals/design-scalar-match.md`; both doc gates green.

- [x] **Phase 2 — IMPLEMENT scalar `match`**
  - Scope: `src/tychoc.c`, `docs/spec/` (§4.3.2 statements, §5.5 or wherever
    `match` is specified, Appendix A grammar), and fixtures under `tests/`.
  - Done when: the VM's dispatch and `corelib/json/json.ty@parse_value` are both
    written as a scalar `match`, and both still pass their gates.
  - Verify: `make test`, `make corelib`, `make vm-check`. Measured instructions/sec before and after.
  - **DONE 2026-08-03.** Both rewrites landed and pass: the VM dispatch is a scalar `match` (`OP_ADD..OP_GE` grouped arithmetic arm, `OP_JZ | OP_JNZ` branch pair) and `corelib/json/json.ty@parse_value`'s byte dispatch is `45 | 48..57`. Codegen emits a C `switch`-of-gotos for 4+ arms (the goto keeps a user `break` targeting the loop, not the switch; verified by `tests/match_scalar_break.ty`) and a chain below; the resolver folds const-name arms, checks dup/overlap by interval merge, and requires `_` on int/char. `make test` 580/0, `make corelib` green, `make vm-check` green. **Measured:** 300 fib runs best-of-3: 2.354 s → 2.311 s (**-1.8%**); 500 sort runs best-of-5: 0.616 s → 0.601 s (**-2.4%**). Dispatch was ~1% of instruction time; the switch is marginally faster — ergonomics and fail-closed semantics were the point.

- [x] **Phase 3 — DESIGN: a cheap reference to an aggregate**
  - Deliverable was a written design at `docs/internals/design-aggregate-ref.md`,
    **not code**.
  - **Three programs, three symptoms, one cause.** A parameter is borrowed
    read-only, `y := a` copies, and `inout` reads as copy-in/copy-out, so nothing appeared able to
    touch part of a large value without copying it:
    - `tools/tycho-ar/main.ty` — a streaming digest cannot thread state through
      calls, so `core:sha256` is one-shot and the archiver wrote its own.
    - `corelib/decimal/decimal.ty` and the corelib generally — the same default
      shapes every hashing and compression interface in the tree.
    - `tools/tycho-vm/main.ty` — push/pop had to be inlined into one function,
      because a `vpush(&st, &sp, v)` helper copies a 1024-entry stack twice per
      instruction. **815 ns vs 110 ns, measured.**
  - The design answers: what the construct is; how it interacts with arenas and `inout`; what stops it outliving its target; whether it appears in a type or only at a binding; what it does to corelib signatures shaped by its absence.
  - **It includes the counter-argument.** Value semantics with no references is a deliberate position — the design states what is lost and whether it is worth it.
  - Verify: the document exists, both doc gates green. No build gate.
  - **DONE 2026-08-03.** The design is
    `docs/internals/design-aggregate-ref.md`; both doc gates green. **The
    premise was stale in a measured place:** `inout` already passes a pointer
    plus the value's owning arena (`_ina_`) — no aggregate is copied in or out
    (verified in emitted C: `h_vm(&_scr2, &_scope, &(h_st), &(h_sp), ...)`). The tycho-vm `vpush` helper is free today;
    tycho-ar's streaming state threads through in-place `inout`; the spec's
    "copy-in/copy-out" is the semantic contract (`x = f(x)`), not the
    implementation. Recommendation: build nothing new; the compatible
    construct already ships twice (in-place `inout`, yielding subscripts). Two
    real findings became the phases below: `&` outside argument position
    compiles to invalid C, and the `inout` docs read as implementation.

- [x] **Phase 4 — REJECT `&` outside an inout argument**
  - Found by phase 3's design. `&` parsed as unary `E_ADDR` everywhere
    (`src/tychoc.c:3454-3457`) and the resolver only validated it at call
    sites (`src/tychoc.c:6872-6874`), so `r := &a` compiled to invalid C
    (`TychoArrInt h_r = &(h_a);` — cc: "invalid initializer") and `&a + 1`
    emitted garbage. The one valid use is the direct argument of an inout
    parameter.
  - Scope: `src/tychoc.c` (an E_ADDR context check), reject fixtures under
    `tests/reject/`, `docs/spec/` if the grammar implies otherwise.
  - Done when: `r := &a`, `x := &a`, `&a + 1` reject with a clean diagnostic;
    `f(&x)` into an `inout` param still compiles; `make test` green.
  - Verify: `make test`.
  - **DONE 2026-08-03.** A `g_in_arg` resolve-context flag, set around the
    three argument-resolution paths (the `E_CALL` loop, `instantiate_generic`
    inference, and the two fn-value call loops) and cleared around lambda
    bodies, makes `resolve_expr`'s `E_ADDR` case reject anywhere else:
    `r := &a`, `&a + 1`, and `&` in a lambda body all die with a clean
    diagnostic; `f(&x)` into `inout`, subscripts as inout arguments, and
    generic inout calls all still compile. Reject fixtures
    `tests/reject/addr_{binding,expr}.ty`. `make test` 582/0.

- [x] **Phase 5 — CORRECT the stale `inout` documentation**
  - Found by phase 3's design. The spec and reference said `inout` is
    copy-in/copy-out; that is the semantic contract, but it read as
    the implementation. The codegen is an in-place pointer pass with the owner arena carried
    (`src/tychoc.c:10365-10373`); no aggregate is copied.
  - Scope: `docs/spec/07-memory-model.md` §11, `docs/reference/basics.md`,
    the stale "one big function" comment at `tools/tycho-vm/main.ty:475`.
  - Verify: the two doc gates, `make vm-check`. No build gate.
  - **DONE 2026-08-03.** `docs/spec/07-memory-model.md` §11 and
    `docs/reference/basics.md` now state the contract/codegen split (the
    promise is `x = f(x)`; the codegen is an in-place pointer pass plus the
    owner arena, so no aggregate is copied), and the stale comment at `tools/tycho-vm/main.ty:475` is corrected. The 41 spec
    citations shifted by phase 4's compiler edits were repointed. Doc gates,
    `make vm-check`, `make spec-check` green.

## All phases complete

## Not in this plan

The backlog as filed was stale on arrival — the three-gates audit it came from
admitted nineteen unchecked items across four rotations with colliding counts
and false claims. Re-scored against the demand test (does a program
that is not its own test need it?) on 2026-08-03, four survive:

- **incremental digest** — `core:sha256` init/update/final over in-place
  `inout` state; tycho-ar wrote its own streaming sha256 for want of it
  (`tools/tycho-ar/main.ty:140-162`).
- **JSON UTF-8 validation** — raw UTF-8 in strings is not validated.
- **`strings.parse_int` failing open** — `0` is both "zero" and "garbage";
  the corelib's own house style is a `Result` for exactly this class.
- **`io.write_bytes`** — the read side has `read_bytes`/`read_at`; the write
  side stops at strings.

Filed, not phases — the demand test fails, the customer would have to appear
first: recursive `make_dirs`, writable mtime, the libpng image shim, the
ParallelFor width slot. `eprintln` is stale (`eprint` ships). All four real
items are library work; none is a language change.
