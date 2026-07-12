# Appendix G — Glossary

Terms are defined normatively in the sections cited; this is a locator with
one-line reminders.

- **Arena** — a per-scope bump allocator reclaimed as a unit at scope exit; the
  reference implementation's realization of implicit storage management. Not
  observable beyond the storage guarantees of [§10.3](07-memory-model.md#103-observable-storage-guarantees).
- **Value semantics** — assignment, argument passing, and return copy deeply; no
  two variables share mutable storage. The language's defining contract
  ([§9](07-memory-model.md)).
- **Deep copy** — a copy that duplicates all heap storage a value owns,
  recursively, so source and copy share nothing ([§9.2](07-memory-model.md#92-copy-on-assignment-argument-and-return)).
- **Escape** — a value outliving its defining scope, by exactly two syntactic
  routes: *down* (as an argument) or *up* (returned or assigned outward)
  ([§10.2](07-memory-model.md#102-the-escape-rule)).
- **Place** — an lvalue: a variable, index, field, tuple element, or subscript
  designating assignable storage ([§13.1](09-expressions.md#131-place-expressions)).
- **`inout`** — an exclusive, call-scoped, copy-in/copy-out mutable borrow, not a
  stored reference ([§11](07-memory-model.md#11-inout)).
- **`sink`** — a parameter mode: the callee owns and consumes the argument
  ([§15.2](11-functions.md#152-parameter-passing-modes)).
- **Newtype** — a distinct type over an underlying type, erased in lowering
  ([§5.4](03-types.md#54-newtypes)).
- **Nominal / structural type** — identified by declaration vs. by structure
  ([§5.1](03-types.md#51-the-type-identity-model)).
- **Monomorphization** — one specialized instance of a generic per distinct
  concrete instantiation ([§7](05-generics.md)).
- **Observationally transparent** — of a storage optimization: never changes any
  observed value, output, or accept/reject decision
  ([§9.5](07-memory-model.md#95-observationally-transparent-optimizations)).
- **Fail closed** — reject or abort a doubtful operation rather than proceed into
  undefined behavior ([§30](17-runtime.md)).
- **Core tier / extended tier** — the language plus libc-only corelib, versus the
  additionally-required `pkg-config` corelib packages
  ([§1.3](00-conventions.md#13-conformance)).
- **Fixpoint** — the byte-identical self-hosting property that anchors the
  two-implementation conformance oracle ([§1.2](00-conventions.md#12-the-two-implementation-contract)).
