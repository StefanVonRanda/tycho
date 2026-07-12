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
| 2 | **Out-of-range narrowing conversion** — `to_int(f)` for `f` outside `int` range; `to_u32`/`to_u64`/`to_f32` whose source does not fit. (In-range `to_int(float)` truncates toward zero.) | [§8.5](06-conversions.md#85-out-of-range-conversions) | probed |
| 3 | **Floating-point reduction reassociation** in `parallel for`: the result MAY differ across thread counts. (Integer reductions are deterministic and are **not** in this list.) | [§22](13-concurrency.md#22-parallel-for) | defined boundary |
| 4 | **Using a typed handle after `close(h)`** — passes null to C; a logic bug, not memory corruption, not compile-rejected. | [§25](14-ffi.md#25-typed-handles) | defined boundary |
| 5 | **Behavior on the far side of the FFI boundary** — C-side global/`static` races and misuse. | [§26](14-ffi.md#26-ffi-and-concurrency) | outside all guarantees |

An out-of-range **shift count** was formerly in this list; it is now defined
(count ≥ width → `0`, negative count → runtime abort — [§13.2](09-expressions.md#132-operators)).

Items 1–2 are candidates for future tightening (for example, pinning
left-to-right evaluation by emitting sequenced temporaries, or defining the
out-of-range narrowing conversion). Any such change would move the item from this
list into the normative body; until then it is unspecified. (The out-of-range
shift count was such a candidate and has since been defined — §13.2.)

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
  complement, `u32`/`u64` = exactly 32/64-bit, `f32`/`float` = IEEE-754
  binary32/binary64) — [§5.2](03-types.md#52-scalar-types);
- the defined signed-overflow wrap and the div/mod-by-zero abort;
- the deep-copy value semantics and the no-dangling / no-leak storage guarantees
  — [§9](07-memory-model.md), [§10.3](07-memory-model.md#103-observable-storage-guarantees);
- the accept/reject decision for every program (the two-implementation
  conformance oracle, [§1.3](00-conventions.md#13-conformance)).
