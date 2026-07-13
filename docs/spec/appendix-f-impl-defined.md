# Appendix F — Unspecified and implementation-defined behavior

Tycho is designed so that this register is **small**: value semantics removes
the aliasing undefined behavior of pointer languages, defined integer wrap
removes overflow undefined behavior, and the fail-closed aborts of
[§30.2](17-runtime.md#302-conditions-that-abort) convert the remaining partial
operations into clean termination. What is left is collected here. A conforming
program MUST NOT depend on anything in the "unspecified" list.

## F.1 Unspecified behavior

| # | Behavior | Reference | Status |
|---|---|---|---|
| 1 | **Argument / operand / place evaluation order** within one expression (order of side effects among a call's arguments, a binary operator's operands, or a place's sub-expressions). | [§13.4](09-expressions.md#134-evaluation-order) | probed; inherited from the target, not sequenced by Tycho |
| 2 | **Floating-point reduction reassociation** in `parallel for`: the result MAY differ across thread counts. (Integer reductions are deterministic and are **not** in this list.) | [§22](13-concurrency.md#22-parallel-for) | defined boundary |
| 3 | **Using a typed handle after `close(h)`** — passes null to C; a logic bug, not memory corruption, not compile-rejected. | [§25](14-ffi.md#25-typed-handles) | defined boundary |
| 4 | **Behavior on the far side of the FFI boundary** — C-side global/`static` races and misuse. | [§26](14-ffi.md#26-ffi-and-concurrency) | outside all guarantees |

Two items were formerly in this list and are now defined: an out-of-range **shift
count** (count ≥ width → `0`, negative → abort — [§13.2](09-expressions.md#132-operators))
and an out-of-range **`to_int(float)`** (NaN / out-of-range → abort — [§8.5](06-conversions.md#85-out-of-range-conversions);
the sized conversions are total).

Item 1 (evaluation order) is **deliberately** unspecified, matching Swift and
Odin (see §13.4): Tycho emits C and defers operand/argument order to the C
compiler rather than lifting every argument into a sequenced temporary. The two
reference compilers agree by construction (both emit arguments in source order);
a conforming implementation need not.

## F.2 Implementation-defined behavior

An implementation MUST document its choice for each of these; none affects the
value semantics of a program.

| Behavior | Reference |
|---|---|
| **Textual rendering of `NaN`/`inf` by `str`** (e.g. `-nan` vs `nan`). The float *values* are IEEE-754 and fully defined; only their string form varies. | [§30.5](17-runtime.md#305-unspecified-behavior) |
| **The concurrent-task ceiling** (default 1024) and whether it is overridable at run time. | [§21](13-concurrency.md#21-spawn-task-wait) |
| **The `parallel for` worker count** (default: one per CPU) and whether it is overridable. | [§22](13-concurrency.md#22-parallel-for) |
| **Diagnostic message text** for all errors and aborts (this specification constrains the *presence* of an error/abort, never its wording). | [§1.1](00-conventions.md#11-scope) |

## F.3 Explicitly *not* implementation-defined

For the avoidance of doubt, the following are **fixed by this specification** and
an implementation MUST NOT vary them, even where its backend's native types
differ:

- the width and overflow behavior of every scalar (`int` = 64-bit two's
  complement, the fixed-width integers `u8`…`u64` / `i8`…`i64` = exactly
  8/16/32/64-bit, `f32`/`float` = IEEE-754 binary32/binary64) —
  [§5.2](03-types.md#52-scalar-types);
- the defined signed-overflow wrap and the div/mod-by-zero abort;
- the deep-copy value semantics and the no-dangling / no-leak storage guarantees
  — [§9](07-memory-model.md), [§10.3](07-memory-model.md#103-observable-storage-guarantees);
- the accept/reject decision for every program (the two-implementation
  conformance oracle, [§1.3](00-conventions.md#13-conformance)).
