# What `tycho-vm` said the language needs

Previous plan archived at
[docs/internals/plan-four-of-seven-DONE.md](docs/internals/plan-four-of-seven-DONE.md).

Five findings, from writing a bytecode VM (`tools/tycho-vm/`, commits e0906e7,
4ec02c7, 7250fb1) — the first program here to need a hot dispatch loop and
fixed-capacity memory. Ordered by land-ability, not by value. Finding 1 is the
most valuable and comes last, because it needs a design before anyone writes code.

Measured, on this tree:

- A struct-with-a-string copy costs **815 ns** against **110 ns** for a bare
  `JMP` — 7x — isolated by adding 20 jumps per iteration and differencing. VM
  throughput 1.39 M instructions/sec.
- `match` on an `int` or a `string` is a parse error:
  `expected a match arm \`Variant(bindings):\` or \`Variant:\``. Confirmed by
  probe, not inherited.
- `defer` is absent from `docs/spec/appendix-b-keywords.md`; `defer f()` parses
  as a bare expression statement and is rejected. **This is recorded and is not
  a phase.** Memory is arena-freed at scope exit, `core:io` is path-based with
  no handles, and the only manual cleanup in the tree is 15 `close` calls in
  `server/main.ty` plus a handful in `examples/`. Nothing here needs it; it was
  filed because the probe surprised me, which is not a reason.

## Phases

- [ ] **Phase 1 — `bounded[N]T` becomes a container**
  - It compiles and reserves capacity, and has no `push`, `pop` or `len`, so it
    cannot model a stack — the one thing fixed-capacity code always needs.
    `tools/tycho-vm/main.ty` uses it for the operand stack and drives the depth
    with a hand-rolled `sp`, so the type checks the capacity but not the thing
    that overflows it.
  - Add `push`/`pop`/`len`, with overflow and underflow as errors the caller can
    act on rather than aborts. Decide whether that is a `Result`, a `bool`
    return or a trap, and say why — `docs/spec/03-types.md` §5.3.10 is the
    contract to keep.
  - Done when: `tools/tycho-vm/main.ty`'s operand stack drops its `sp` and the
    VM still passes `make vm-check`.
  - Verify: `make test` (was `passed: 566 failed: 0`), then `make vm-check`.

- [ ] **Phase 2 — a `[N]T` table can be a top-level `const`**
  - `const T: [N]Meta = [...]` is rejected — `expected '=' after the constant
    name` — because §12.2 requires the RHS to fold to one literal.
    `tools/tycho-vm/main.ty@optable` is a function that rebuilds a 24-entry
    dispatch table instead, which is exactly the shape a static table wants not
    to be.
  - Decide the scope: does a const array literal of scalars fold, or does this
    need general const evaluation? The first is small and covers the case; say
    which you did.
  - Done when: the VM's opcode table is a `const` and `make vm-check` passes.
  - Verify: `make test`, then `make vm-check`.

- [ ] **Phase 3 — DESIGN: `match` on scalars**
  - Deliverable is a written design at `docs/internals/design-scalar-match.md`,
    **not code**. No phase after this starts until it is read.
  - `match` is enums-only, so every dispatch table in the tree is an `if`/`elif`
    ladder: `tools/tycho-vm/main.ty` averages 12 comparisons per instruction
    over 24 opcodes, and `corelib/json/json.ty@parse_value` ladders on byte
    values. Lexers, protocol decoders and state machines all hit this.
  - The design must answer, each with a reason: which scalar types (`int`,
    `string`, `char`, `bytes`?); is a `_` arm required, or is a non-exhaustive
    scalar `match` an error; are duplicate arms an error; does an arm fall
    through; can an arm carry a range or a set (`1..9`, `1 | 3 | 5`); and does
    codegen emit a jump table, a binary search or a chain — with the threshold
    that decides.
  - **Say what it would buy.** The copy cost above dominates dispatch at current
    numbers, so a jump table may buy less than it looks. Estimate it before
    committing to an implementation.
  - Verify: the document exists, `sh scripts/check_links.sh` and
    `python3 scripts/check_citations.py` are green. No build gate.

- [ ] **Phase 4 — IMPLEMENT scalar `match`**
  - Only after phase 3's design is written and its scope agreed.
  - Scope: `src/tychoc.c`, `docs/spec/` (§4.3.2 statements, §5.5 or wherever
    `match` is specified, Appendix A grammar), and fixtures under `tests/`.
  - Done when: the VM's dispatch and `corelib/json/json.ty@parse_value` are both
    written as a scalar `match`, and both still pass their gates.
  - Verify: `make test`, `make corelib`, `make vm-check`. Measure the VM's
    instructions/sec before and after and report both — phase 3 predicted a
    number, and this is where it is checked.

- [ ] **Phase 5 — DESIGN: a cheap reference to an aggregate**
  - Deliverable is a written design at `docs/internals/design-aggregate-ref.md`,
    **not code**. This is the most valuable item here and the one most likely to
    be got wrong by starting with an implementation.
  - **Three programs, three symptoms, one cause.** A parameter is borrowed
    read-only, `y := a` copies, and `inout` is copy-in/copy-out, so nothing can
    touch part of a large value without copying it:
    - `tools/tycho-ar/main.ty` — a streaming digest cannot thread state through
      calls, so `core:sha256` is one-shot and the archiver wrote its own
      (`docs/internals/plan-ar-DONE.md` phases 14, 15).
    - `corelib/decimal/decimal.ty` and the corelib generally — the same default
      shapes every hashing and compression interface in the tree.
    - `tools/tycho-vm/main.ty` — push/pop had to be inlined into one function,
      because a `vpush(&st, &sp, v)` helper copies a 1024-entry stack twice per
      instruction. **815 ns vs 110 ns, measured.**
  - The design must answer: what the construct is (a borrow, a view, a `ref`
    binding, something else); how it interacts with the arena model and with
    `inout`; what stops it outliving what it points at; whether it appears in a
    type or only at a binding; and what it does to the existing corelib
    signatures that were shaped by its absence.
  - **It must include the counter-argument.** Value semantics with no references
    is a deliberate position, not an oversight — say what is lost by adding this
    and why it is worth it, or conclude that it is not.
  - Verify: the document exists, both doc gates green. No build gate.

## Not in this plan

The eleven-item backlog from `docs/internals/plan-three-gates-DONE.md` (JSON
UTF-8 validation, `strings.parse_int` failing open, `io.write_bytes`,
`io.make_dirs`, writable mtime, incremental digest, `eprintln`, the `image`
shim, a document-reachability gate, the `ParallelFor` width slot), plus the six
filed by the last plan. None is a language change and none blocks these.
