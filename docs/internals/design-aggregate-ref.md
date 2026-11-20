# Design: a cheap reference to an aggregate

> Status: decided — do not build a reference construct. The compatible forms already ship: in-place `inout` codegen and yielding subscripts.

## The premise, and why it was wrong

The request came from three programs that seemed to need a reference: a VM
whose `vpush(&st, &sp, v)` "would copy the 1024-slot stack twice per
instruction" (`tools/tycho-vm`), an archiver that could not thread a streaming
digest through calls (`tools/tycho-ar`), and corelib's return-new signatures
(`corelib/decimal/decimal.ty`).

The premise confused the spec's **semantic contract** for `inout`
(copy-in/copy-out, "provably equivalent to `x = f(x)`",
`docs/spec/07-memory-model.md`) with its codegen. `inout` on a heap value
passes a **pointer plus the value's owning arena** as a hidden `_ina_`
parameter (`src/tychoc.c:10429-10437`, archived), and every allocating
mutation of the parameter allocates into that arena — the caller's
(`src/tychoc.c@owner_arena_of`). No `inout` aggregate is copied. Emitted C for
the VM shape (`bounded[1024]V` plus an `inout int` counter):

```c
h_vm(&_scr2, &_scope, &(h_st), &(h_sp), ((S_V){ 0, h_i, "v" }));
```

Against the three symptoms:

- **tycho-vm**: helpers are free; the dispatch was refactored into
  vpush/vpop/arith helpers with a byte-identical golden. CALL/RET stay inline
  for a different reason — extracting them cost the call-heavy fib program
  ~4.5% through lost cc inlining, not copies.
- **tycho-ar**: the gap was `core:sha256`'s API, not the language; the
  `init`/`update`/`final_hex` trio over `inout` state exists and tycho-ar
  calls it (`tools/tycho-ar/main.ty@final_hex`).
- **decimal**: operands pass by value (a borrow, no copy) and results are
  allocated in the caller's arena, with move-on-last-use eliding assignment
  copies. The signatures are taste, not a missing reference.

## The two compatible forms

1. **In-place `inout`** — an exclusive, call-scoped, non-escaping mutable
   alias. It cannot outlive the call by construction.
2. **Yielding subscripts** — a scoped, zero-copy place projection into part
   of the receiver ([subscripts](../reference/subscripts.md)). A subscript
   result is usable as an `inout` argument and mutates the original
   (`bump(&g.node(1))`). Subscripts are compile-time place macros with no
   runtime object.

Both were reached independently by the reference spike
(`docs/rfc/limited-references-spike.md`, "The one compatible increment"). A `ref` **binding**
(`r := &a`) is the one piece of the space that does not exist, and it must not
be added. `&` is legal only as the direct argument of an `inout` parameter;
anywhere else it is rejected ("'&' is only valid as the argument to an inout
parameter").

## Why not a reference

- **Arena interaction.** A `ref` binding would need owning-arena routing at
  every use site — bookkeeping that becomes alias analysis the moment the
  binding can be passed on.
- **Outliving the target.** A stored reference has no sound deep-copy across a
  thread boundary, so it breaks the invariant that makes tasks race-free (the
  spike's compatibility test, `docs/rfc/limited-references-spike.md`).
- **Type vs binding.** Both compatible forms live at the binding/declaration
  level. A reference *type* could be stored, composed and passed on — the
  escape question [thesis.md](../thesis.md) exists to make unaskable.
- **The counter-argument.** A general borrow is Design A of the regions study,
  "a borrow checker with a different keyword"
  (`docs/rfc/value-lifetime-regions.md` §6). The one sound increment there
  (Design B, value-owned arenas) has no paying customer, and the one compatible
  ergonomic increment (projections) has shipped. The cost of a reference is the
  whole-program alias reasoning the model exists to avoid; the measured benefit
  is zero, because the copies it would remove do not exist.
