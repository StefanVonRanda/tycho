# 9. Value semantics · 10. Object lifetimes and storage · 11. `inout`

This part defines Tycho's memory and object model. The model has one observable
contract — **value semantics** — and one realization that makes it work without a
garbage collector — **implicit hierarchical arenas**. The contract is normative;
the arena realization is described because it is how the reference
implementation achieves the contract and why the language needs neither a GC nor
manual `free`, but an implementation MAY use any strategy with the same
observable behavior.

> Provenance: `docs/memory-model.md`, `docs/thesis.md` §2–§5; runtime
> `runtime/tycho_rt.c`; `docs/internals/value-semantics-limits.md`.

## 9. Value semantics

### 9.1 No reference type

Tycho has **no reference type**. A program cannot name, store, or return a
pointer into another value's storage. Consequently two variables never share
mutable state, and there is no aliasing to reason about. Every binding is an
independent value.

The single, tightly-scoped exception is `inout` (§11): a call-scoped,
copy-in/copy-out mutable *borrow* that is provably equivalent to `x = f(x)` and
is not a storable reference. `inout` does not reintroduce aliasing; it is
specified precisely so that it cannot.

### 9.2 Copy on assignment, argument, and return

Assignment (`b := a`, `b = a`), argument passing, and returning a value all
**copy** the value, and the copy is **deep**: copying a heap-bearing value
(a `string`'s bytes, an array's buffer, and recursively a struct's or array's
heap-bearing fields/elements) duplicates all of that storage, so the source and
the copy share nothing. After `b := a`, a mutation of `b` MUST NOT be observable
through `a`, at any depth of nesting.

This is the language's defining invariant. It holds identically at every level:
copying a `[string]` copies the element buffer *and* every element's bytes;
copying a struct copies each heap field, recursing.

Immutability is **not** a substitute for the copy. Immutability makes aliasing
safe (no one can mutate a shared buffer) but does nothing for *lifetime* — a
value whose storage is reclaimed is gone whether or not it was mutable. The deep
copy is what guarantees a value outlives every place that holds it.

### 9.3 Structural equality mirrors the copy

Equality (`==`) is the mirror image of the deep copy: it compares by content,
recursing through the same structure ([§5.5](03-types.md#55-equality-and-ordering)).
Therefore `a == b` holds exactly when `b` is an independent deep copy of `a`.
Function values are the exception: a bare function is **not comparable** at all
(§5.5), and a function embedded in a comparable aggregate compares by **identity**,
not content — the one place the copy/equality symmetry does not apply.

### 9.4 Uniqueness

Because nothing aliases, **every mutable heap buffer has exactly one owner**
(immutable interned string literals, which are shared and immortal, aside). This
uniqueness is what lets the reference implementation mutate or recycle a value's
storage in place whenever the value is provably dead or uniquely held, with no
reference counting — the check a reference-counted language performs at run time
is discharged here statically, by the absence of aliasing.

### 9.5 Observationally-transparent optimizations

A conforming implementation MAY perform storage optimizations that eliminate
copies or reuse buffers — the reference implementation performs at least
return-slot move, in-place accumulation, move-on-last-use, and dead-buffer
recycling. Every such optimization is **observationally transparent**: it MUST
NOT change any observed value, program output, or accept/reject decision. It may
change only *when* and *whether* storage is allocated or freed. This
transparency is guaranteed by the value-semantics asymmetry (§10.4): under value
semantics a wrong storage decision can change only when memory is reclaimed,
never whether a pointer dangles.

The reference implementation verifies this by requiring the emitted C, compiled
under AddressSanitizer/UBSan, to produce output byte-identical to the native
`-O2` build across the whole suite (exit 0, clean sanitizers), with per-
optimization A/B baselines separately confirming each optimization is
output-invisible; a conforming implementation MUST hold to the same
observable-equivalence standard.

## 10. Object lifetimes and storage

### 10.1 Lifetime is lexical

Every value's lifetime is determined **lexically**, from the statement that
produces it and the signatures involved — never from a whole-program pointer
analysis (which value semantics makes unnecessary). A value lives at least as
long as the scope it is bound in, and longer exactly when it *escapes* that
scope by one of the two syntactic routes in §10.2.

The reference implementation realizes this with **implicit hierarchical arenas**:
each function activation (and, as scratch, each block and each loop iteration)
owns a bump-allocated arena that is reclaimed as a unit when the scope exits, and
each allocation is directed into the arena of the scope the value must outlive.
The arena *mechanism* (which scope resets versus frees, block-level scratch
arenas, per-statement temporaries) is an implementation realization and is not
observable beyond the guarantees in §10.3.

### 10.2 The escape rule

A value moves between scopes' storage in exactly two directions, both visible in
the syntax:

- **Down** — passed as an argument to a callee. The callee's activation is
  nested within the caller's, so the argument outlives the call; no copy is
  required to pass it down.
- **Up** — returned, or assigned to a variable that lives in an enclosing scope.
  The value is produced directly in the destination's storage (the caller's, for
  a `return`; the outer variable's, for an outer assignment), so it survives the
  inner scope's collapse.

The write forms that route a value to another scope, and their destinations,
are: **up** — `return e` (to the caller), and `outer = e` / `push(outer, v)` /
any store through a place whose root is an outer variable (to that variable's
storage); and **down** — passing an argument or `inout` (to the callee). Every
destination is decidable at the write site from the local scopes and signatures.

### 10.3 Observable storage guarantees

A conforming implementation MUST guarantee:

1. **No dangling.** No value is ever read after its storage has been reclaimed;
   there is no use-after-free. Under value semantics this is guaranteed *by
   construction* (§10.4).
2. **No leak at scope exit.** When a scope exits (including `main`), the storage
   it exclusively owns is reclaimed; no value's storage outlives its owning scope,
   and at normal program termination there are no reachable leaks. (The reference
   implementation enforces
   this under LeakSanitizer.)

The reference implementation additionally provides two properties that follow
from the arena realization and are relied on in practice, stated here as strong
expectations (performance characteristics are otherwise out of scope, §1.1): a
loop whose body only produces throwaway temporaries runs in memory bounded
independently of the iteration count (per-iteration scratch is reset), and a
self-append accumulation (`acc = acc + e` in a loop) runs in linear, not
quadratic, memory.

### 10.4 Why the model is sound

Correctness rests on a single asymmetry:

> Under value semantics, a wrong *escape* decision can change only **when** memory
> is reclaimed — never **whether** a pointer dangles.

Over-approximating "this might escape" allocates a value in a longer-lived scope
than strictly necessary: at worst mild retention, never a bug, because there are
no aliases to invalidate. There is no symmetric failure. This is why the
storage decisions and the §9.5 optimizations can be made freely by the
implementation, and why no borrow checker, lifetime annotation, or GC is needed.

## 11. `inout`

### 11.1 Copy-in / copy-out semantics

An `inout` parameter is an **exclusive, call-scoped, copy-in/copy-out mutable
borrow**. A call `f(&x)` where `f`'s parameter is `inout` is defined to be
equivalent to `x = f_body_applied_to(x)`: the argument's value is made available
to the callee, the callee may mutate it, and the final value is written back to
the caller's variable when the call returns. The `&` at the call site is
required and MUST name a mutable place in the caller. `inout` is **not** a stored
reference: the borrow exists only for the duration of the call and cannot be
captured, returned, or stored.

### 11.2 Exclusivity

The same caller variable MUST NOT be passed to two `inout` parameters of a single
call, and more generally two `inout` arguments of one call MUST NOT share a root
variable. The check is by **root variable**, conservatively (may-overlap): both
`&a[i]` and `&a[j]` — and `&a.x` with `&a.y` — are rejected because they root at
the same `a`, even when the elements or fields provably do not overlap. Such
overlap is rejected at compile time (*"overlapping mutable
access"*); it would make the copy-out order observable and reintroduce aliasing.

Both implementations enforce this (`src/tychoc.c:5019`;
`compiler/tychoc0.ty` `check_call_args`). A `tychoc0` fail-open on this rule was
found during the drafting of this specification and fixed; it is locked by
`tests/reject/inout_alias.ty` (both compilers reject, differential).

### 11.3 Heap `inout`

`inout` extends to heap-bearing values — `[int]`, `[string]`, heap-bearing
structs, and maps — including `push`/growth and element/field mutation through
the borrow. A reassignment through an `inout string` (`s = s + "."`) reaches the
caller, with the new bytes produced in the caller's storage. To make an
allocating mutation land where the borrowed value lives, the caller's owning
scope is threaded to the callee; this threading is an implementation realization
(not observable) but its *effect* — that the mutation survives the call in the
caller's value — is normative. This is what lets a memoization table, a mutable
context object, or a recursive output collector be genuinely shared across all
call frames while preserving value semantics.

### 11.4 Relationship to `sink`

`sink` is a distinct parameter mode: the callee **owns and consumes** the
argument (§15.2). Unlike `inout`, `sink` does not write a value back;
it takes ownership, and the caller MUST NOT use the argument after the call.
`sink` and `inout` are mutually exclusive on a parameter, and neither may combine
with a variadic parameter.
