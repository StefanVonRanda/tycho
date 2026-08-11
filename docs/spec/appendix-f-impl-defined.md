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
| 1 | **Argument / operand evaluation order** within one expression (order of side effects among a call's arguments or a binary operator's operands). *Exceptions:* a side-effecting **index in an assignment place** (`a[f()] = g()`) and the **holes of an f-string** (`f"{a()}{b()}"`) are sequenced **left-to-right** — specified, not in this list. | [§13.4](09-expressions.md#134-evaluation-order) | probed; arguments/operands inherited from the target, not sequenced by Tycho |
| 2 | **Floating-point reduction reassociation** in `parallel for`: the result MAY differ across thread counts. (Integer reductions are deterministic and are **not** in this list.) | [§22](13-concurrency.md#22-parallel-for) | defined boundary |
| 3 | **Using a typed handle after `close(h)`** — passes null to C; a logic bug, not memory corruption, not compile-rejected. | [§25](14-ffi.md#25-typed-handles) | defined boundary |
| 4 | **Behavior on the far side of the FFI boundary** — C-side global/`static` races and misuse. | [§26](14-ffi.md#26-ffi-and-concurrency) | outside all guarantees |

Two items were formerly in this list and are now defined: an out-of-range **shift
count** (count ≥ width → `0`, negative → abort — [§13.2](09-expressions.md#132-operators))
and an out-of-range **`to_int(float)`** (NaN / out-of-range → abort — [§8.5](06-conversions.md#85-out-of-range-conversions);
the sized conversions are total).

Item 1 (argument/operand evaluation order) is **deliberately** unspecified,
matching Swift and Odin (see §13.4): Tycho emits C and defers argument/operand
order to the C compiler rather than lifting every argument into a sequenced
temporary. Sequencing them soundly is not free — an argument may sit inside a
short-circuit (`f(x, cond and g())`), so a naive lift to a statement-level temp
would evaluate it *unconditionally*; a correct lift is a per-call-site sequenced
temporary, and that cost was judged not worth closing a hole that was not a live
divergence at the time. The **assignment-place index** was the exception: it *was*
a real divergence between the reference compiler and the (now frozen) `tychoc0`
snapshot, and it is cheap and sound to sequence (a place index is never
short-circuited), so it is now pinned left-to-right (§13.4) and excluded above.
**F-string holes are the second exception**, for the same reason: one hole is
never short-circuited against another, so binding each to a sequenced temporary
at the concat site costs nothing a program could observe — and leaving it
unpinned was actively misleading, because the holes' *printed* order is their
source order while their side effects fired in reverse under the host compiler.
A conforming implementation still need not match the unspecified
argument/operand order.

## F.2 Implementation-defined behavior

An implementation MUST document its choice for each of these; none affects the
value semantics of a program.

| Behavior | Reference |
|---|---|
| **Textual rendering of `NaN`/`inf` by `str`** (e.g. `-nan` vs `nan`). The float *values* are IEEE-754 and fully defined; only their string form varies. | [§30.5](17-runtime.md#305-unspecified-behavior) |
| **The concurrent-task ceiling** (default 1024) and whether it is overridable at run time. | [§21](13-concurrency.md#21-spawn-task-wait) |
| **The `parallel for` worker count** (default: one per CPU), whether it is overridable, and **any fixed upper bound on it** (the reference caps the chunk count at 64, so `ncpu()` above 64 is not the width used). | [§22](13-concurrency.md#22-parallel-for) |
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
- the accept/reject decision for every program — which programs must be accepted
  and which MUST be rejected is fixed by this specification and checked against
  the fixture corpus of [Appendix E](appendix-e-conformance.md)
  ([§1.3](00-conventions.md#13-conformance)). (Through 2026-07-25 this invariant
  was stated as a *two-implementation conformance oracle*, agreement between
  `tychoc` and the self-hosted `tychoc0`. `tychoc0` is now frozen and diverging —
  see [§1.2](00-conventions.md#12-the-reference-implementation) — so the
  requirement is stated against the specification and its fixtures, where it
  always belonged.)

> **Reference-implementation note (not a spec allowance).** The required 64-bit
> `int` width above is normative for *every* conforming implementation; it is
> **not** implementation-defined. The reference compiler (`tychoc`)
> realizes `int` as a **fixed-width 64-bit** C type — `typedef int64_t tycho_int;`
> in the emitted prelude, which is the single width authority for `int`, the
> `int`-carried `char` representation, array/slice length headers, map keys and
> the FFI crossing signatures — and emit `long long`-suffixed integer literals so
> that constant arithmetic is evaluated at 64-bit rank. `int64_t` does not vary
> with the C data model, so the lowering is 64-bit on **LP64**, **LLP64** and
> **ILP32** alike, and a build in which it were not is rejected outright by the
> always-on `_Static_assert(sizeof(tycho_int)==8, "tycho int must be 64 bits");`
> carried in the same prelude. The reference implementation therefore conforms to
> the 64-bit `int` requirement on all three data models; no target is excluded.
>
> *Extent of the evidence (so the claim is not read as broader than it is).* The
> data-model independence above is a property of `int64_t` plus the static
> assertion, and holds by construction. It is additionally **gated empirically on
> ILP32**: `make ilp32` rebuilds the emitted C of the whole fixture suite with
> `gcc -m32` and re-runs it against the unmodified 64-bit goldens on every CI run
> (`scripts/ci.sh`), so a width regression fails the build (see
> [Appendix E §5.2.1](appendix-e-conformance.md#5-types)). That lane runs an ILP32
> data model on an x86-64 host; it is not a test on 64-bit Windows hardware, and
> **LLP64 is asserted architecturally, not measured**. The ILP32 lane also
> deliberately omits the sanitizer pass (no 32-bit ASan runtime under multilib);
> ASan coverage comes from the 64-bit `make test` lane.
