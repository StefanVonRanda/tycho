# Design: a cheap reference to an aggregate

Status: the deliverable of `plan.md` Phase 3 (the tycho-vm findings). Read
before any implementation starts; this document's recommendation is that the
answer is **mostly already shipped**, that the plan's premise is stale in one
measured place, and that the residual gap is not worth a new construct.

## The premise, and the measured correction

The plan's brief names three programs, three symptoms, one cause: *"a
parameter is borrowed read-only, `y := a` copies, and `inout` is
copy-in/copy-out, so nothing can touch part of a large value without copying
it."* The three symptoms:

1. `tools/tycho-vm/main.ty` — `vpush(&st, &sp, v)` "would copy the entire
   1024-slot stack twice per instruction", so push/pop were inlined
   (`tools/tycho-vm/main.ty:475-478`).
2. `tools/tycho-ar/main.ty` — a streaming digest cannot thread state through
   calls, so `core:sha256` is one-shot and the archiver wrote its own
   (`tools/tycho-ar/main.ty:140-162`).
3. `corelib/decimal/decimal.ty` and the corelib generally — return-new
   signature shapes.

**The premise is wrong about the codegen, and the measurement is the emitted
C.** `inout` on a heap value already passes a **pointer plus the value's owning
arena** as a hidden `_ina_` parameter (`src/tychoc.c:9024-9032`), and every
allocating mutation of the parameter allocates into that arena — the caller's,
where the value lives — not the callee's scope (`src/tychoc.c@owner_arena_of`).
The codegen was already in place at the Hier→Tycho rename; nothing in the
current tree copies an `inout` aggregate.

Verified by emitting C for the exact VM shape — `bounded[1024]V` where `V` is
a struct with a `string` field, plus an `inout int` counter, in a `vpush`
helper called in a loop:

```c
h_vm(&_scr2, &_scope, &(h_st), &(h_sp), ((S_V){ 0, h_i, "v" }));
```

The stack and the counter arrive **by address**; the callee mutates the
caller's storage in place, and the helper runs (50 pushes, correct result).
There is no 1024-slot copy in or out. The plan's `vpush` hypothetical was
never tested against the codegen; the spec's "copy-in/copy-out" is the
**semantic contract** (`docs/spec/07-memory-model.md:23-26` — "provably
equivalent to `x = f(x)`"), not the implementation, and the
`tools/tycho-vm/main.ty:475` comment read the contract as the implementation.

Re-examining the three symptoms against the codegen:

- **tycho-vm**: the helper is free today; the machine does not have to live in
  one function. The cost the comment fears was already gone when the comment
  was written. **Validated 2026-08-03:** the dispatch was refactored —
  vpush/vpop/arith are now helpers called from every arm, the gate's golden is
  byte-identical, and the emitted C shows the pointer passes (`&(h_st),
  &(h_sp)`) with no copy. The one boundary the extraction hit was cc inlining,
  not copies: extracting CALL/RET regressed the call-heavy fib program ~4.5%
  in two sessions, so they stay inline. The stale premise was wrong in
  mechanism but the one-function shape had a real reason after all.
- **tycho-ar**: the file's own comment says `inout` "is the answer, it works on
  `[u32]` and on `bytes`, and it forwards" (`tools/tycho-ar/main.ty:154-156`).
  Streaming state threading is supported; what is missing is `core:sha256`'s
  **API** (an `init`/`update`/`final` trio), which is library work, not
  language work — and it is writable today with in-place `inout`.
- **decimal**: the operations pass operands by value (a borrow — no copy), and
  the fresh results are allocated in the caller's arena (a move, not a copy)
  with move-on-last-use eliding assignment copies. The signatures are shaped
  by taste, not by a missing reference.

## What the construct would be — and that it already ships, twice

The brief asks what the construct is: a borrow, a view, a `ref` binding, or
something else. The tree already contains the two compatible answers, both
measured above:

1. **In-place `inout`** — an exclusive, call-scoped, non-escaping mutable
   alias. It is not a stored reference; it cannot outlive the call by
   construction (the parameter dies at return); its exclusivity is the
   existing inout rule. Its codegen is the pointer + `_ina_` arena pass.
2. **Yielding subscripts** — a scoped, zero-copy place projection into one of
   the receiver's parts (`docs/reference/subscripts.md:1-8`). Verified: a
   subscript result is usable as an `inout` argument and mutates the original
   (`bump(&g.node(1))` — the array element changed in place). Subscripts are
   compile-time place macros with no runtime object, so they carry none of the
   lifetime machinery a stored reference would.

Both were independently concluded to be "the one compatible increment" by the
reference spike (`docs/rfc/limited-references-spike.md:104-131`), and the
latter half has since shipped. A `ref` **binding** (`r := &a`) is the only
piece of the space that does not exist — and it must not be added (below).

## The six questions

1. **What the construct is.** The compatible forms are a parameter annotation
   (`inout`, in-place in codegen) and a declaration form (subscripts). A
   general borrow/view/`ref` binding is the design that should not be built
   (the counter-argument).
2. **Arena interaction.** In-place `inout` is sound because the callee's
   allocating mutations route to the caller's arena via `_ina_`
   (`src/tychoc.c:9024-9032`); the callee's own arena holds only its
   transients and is freed at return. A `ref` binding would need the same
   owning-arena routing at every use site — the bookkeeping that grows into
   alias analysis the moment the binding can be passed on.
3. **What stops it outliving its target.** `inout` is call-scoped by
   construction; subscripts are compile-time. A stored reference fails the
   thread boundary: it has no sound deep-copy, so it breaks the invariant that
   makes tasks race-free — the reference spike's recorded decision
   (`docs/rfc/limited-references-spike.md:90-102`).
4. **Type or binding.** Both compatible forms live at the binding/declaration
   level; neither introduces a reference **type**. A reference type would let
   references be stored, composed, and passed on — the escape question the
   thesis exists to make unaskable.
5. **Corelib signatures.** The one-shot shapes are not copy-taxed; they are
   conventions. The streaming digest the plan wanted (`core:sha256`
   init/update/final) is writable today with in-place `inout` state, exactly
   as `tools/tycho-ar` already does it inline. That is a library phase, not a
   language feature.
6. **The counter-argument.** Value semantics with no references is the
   project's central position, not an oversight (`docs/thesis.md`). A general
   borrow is Design A of the regions study — "a borrow checker with a
   different keyword" that "contradicts the thesis at the joint"
   (`docs/rfc/value-lifetime-regions.md:405-406`). The regions study's
   recommendation applies here unchanged: the one genuinely sound increment in
   the space (Design B, value-owned arenas) has no paying customer in the tree,
   and the one compatible ergonomic increment (projections) has shipped. What
   is lost by adding a reference: the compiler must track who may hold a
   pointer and prove it dies first — the whole-program alias reasoning the
   model exists to avoid — and every exception becomes a soundness debt that
   only grows.

## The recommendation

**Do not build a new reference construct.** The measured benefit is zero —
the copy costs the plan premises do not exist in the codegen — and the cost is
the borrow checker. Three things ARE worth doing, in order:

1. **Fix the stale documentation.** `docs/spec/07-memory-model.md:23-26` and
   `docs/reference/basics.md` say `inout` is copy-in/copy-out; the spec is the
   semantic contract (fine), but it has been read as the implementation, which
   is how the plan's copy-tax premise entered. One sentence per site: the
   contract is `x = f(x)`; the codegen is an in-place pointer pass with the
   owner arena carried, so no aggregate is copied. Correct
   `tools/tycho-vm/main.ty:475-478` — the helper is free, so the "one big
   function" comment is misleading; the machine could be factored if anyone
   wants to.
2. **Reject `&` outside argument position.** `&` parses as a unary `E_ADDR`
   everywhere (`src/tychoc.c:2890-2893`) and resolves to the place's type
   without position validation (`src/tychoc.c:5809-5811` — "only valid as an
   inout argument", enforced only at call sites). `r := &a` therefore compiles
   to invalid C (`TychoArrInt h_r = &(h_a);` — cc: "invalid initializer"),
   and `&a + 1` emits garbage. The fix is one check: `E_ADDR` is legal only as
   the direct argument of an inout parameter, so `resolve_expr`'s `E_ADDR`
   case must die unless a "resolving a call argument" flag is set. This is the
   only code change this design requests.
3. **If the demand appears, the streaming API is a library phase.** A
   `core:sha256` init/update/final trio over in-place `inout` state would close
   the tycho-ar symptom properly and has a real customer (the archiver wrote
   its own). That is `corelib` work under the demand-gated rule, not language
   work, and it needs no new construct.

## Verification

- This document: both doc gates green.
- The claims above were measured, not assumed: the emitted C for the
  VM-shaped `bounded` stack and for the subscript-as-`inout`-argument program
  was inspected, and both programs ran correctly. The `&`-hole was reproduced
  (`r := &a` → invalid C from cc) before being written down.
