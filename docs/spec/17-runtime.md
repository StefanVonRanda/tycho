# 30. Defined runtime behavior and failure

This chapter defines what happens at run time for the conditions a program can
reach: which **wrap** (defined), which **abort** cleanly, which **clamp**, and
the small register of conditions left **unspecified**. Tycho's guiding principle
is to **fail closed** — a doubtful operation aborts cleanly rather than
proceeding into undefined behavior — and the unspecified register is kept
minimal, consistent with the abstract-exact policy ([§1.1](00-conventions.md)).

An **abort** terminates the program with a non-zero exit status after writing a
diagnostic to standard error. The *presence* of the abort is normative; the
diagnostic text is not ([§1.1](00-conventions.md)).

> Provenance: `runtime/tycho_rt.c` (`tycho_idiv`/`imod` `:97-106`, bounds
> `:795-814`,`:1066-1080`, `pop` `:1060-1064`, `substr` `:816-826`, string
> header `:781-793`, map hashing `:1412-1461`, channels/tasks `:286-591`);
> `docs/internals/integer-overflow.md`. Behaviors marked *(probed)* were
> confirmed on both compilers (spec-plan.md §6a).

## 30.1 Defined wraparound

- **Signed integer overflow** (`int`) is **defined** two's-complement wraparound.
  It never traps. (The reference realizes this by compiling with `-fwrapv`.)
- **Unsigned overflow** (`u32`, `u64`) wraps modulo `2^32` / `2^64`, by
  definition.
Integer overflow and an over-wide shift count (`≥` the operand's bit width,
defined as `0`) are the arithmetic conditions that are fully defined rather than
aborting; every other numeric edge either aborts (§30.2 — including a negative
shift count, an out-of-range `chr`, and an out-of-range `to_int(float)`) or is
unspecified (§30.5).

## 30.2 Conditions that abort

A conforming implementation MUST abort on each of the following:

- **Integer division or modulo by zero** (`int`, `u32`, `u64`). Additionally
  `(−2^63) / −1` aborts (quotient overflow); `(−2^63) % −1` yields `0` and does
  not abort.
- **A negative shift count** (`<<`, `>>`). A count `≥` the operand's bit width
  does *not* abort — it is defined as `0` (§30.1, §13.2).
- **A `range` step of zero** — a literal `0` step is a compile error; a step that
  evaluates to `0` at run time aborts (§10).
- **`chr(n)` with `n` outside `0..255`** — the byte value is out of range (§16).
- **`to_int(f)` of a `NaN` or out-of-range `float`/`f32`** (§8.5). The sized
  integer conversions are total (no abort).
- **Array index out of bounds**, on both read (`a[i]`) and write (`a[i] = v`).
- **String index out of bounds** (`s[i]`). (`s[i] = v` is a *compile* error —
  strings are immutable.)
- **`pop` from an empty array.**
- **`reserve` with a negative or excessive capacity.**
- **`split` with an empty separator.**
- **Channel misuse:** `send` on a closed channel; a second `close`; a channel
  capacity below 1.
- **Task misuse:** a second `wait` on a task; exceeding the concurrent-task
  ceiling; a thread-creation failure.
- **Out of memory.**
- **`die(msg)`** — the explicit user abort ([§14.8](10-statements.md#148-die-and-termination)).

## 30.3 Conditions that clamp

- **`substr(s, start, end)`** clamps out-of-range bounds rather than aborting:
  `start < 0` becomes `0`, `end > len` becomes `len`, and `end < start` yields an
  empty result. This is the one bounds situation that does not abort.

## 30.4 Defined string and map behavior

- **Byte-safe strings.** `string` and `bytes` carry an explicit length; every
  length-sensitive operation uses that length, not a `NUL` terminator, so
  interior `NUL` bytes are preserved and compared. `len` is a constant-time
  header read. `s[i]` yields the byte as an `int` in `0..255`, never negative.
- **Hash-flooding resistance.** Maps use a keyed hash (SipHash in the reference)
  seeded from a per-process random key, so an adversary cannot craft key
  collisions without the key. This is a defined security property of maps, not
  an implementation detail a program may defeat. `keys(m)` returns keys in
  insertion order.

## 30.5 Unspecified behavior

The following are **unspecified**: this specification imposes no requirement, and
a conforming program MUST NOT depend on them. They are collected in
[Appendix F](appendix-f-impl-defined.md).

- **Argument and operand evaluation order** within one expression (§13.4;
  *probed* — inherited from the target, not sequenced by Tycho). **Exception:** a
  side-effecting **index in an assignment place** (`a[f()] = g()`) IS sequenced
  **left-to-right** — the index runs before the RHS, identically in both
  compilers — so that one case is specified, not target-inherited, and a program
  MAY depend on it.
- **Floating-point reduction reassociation** in `parallel for` (§22): a
  floating-point reduction result MAY differ across thread counts. (An integer
  reduction is deterministic and is *not* unspecified.)
- **The textual rendering of `NaN`/`inf` by `str`** (e.g. `-nan` vs `nan`) is
  **implementation-defined** (derived from the C library). The float *values*
  themselves are IEEE-754 and fully defined (§5.2.2).
- **Using a typed handle after `close(h)`** — passes null to C; a logic bug, not
  memory corruption, and not compile-rejected (§25).
- **Any behavior on the far side of the FFI boundary** — C-side global/`static`
  races and misuse are outside every Tycho guarantee (§26).

Aside from this register, Tycho has essentially no implementation-defined or
undefined behavior: value semantics removes the aliasing UB of pointer languages,
defined integer wrap removes overflow UB, and the fail-closed aborts of §30.2
convert the remaining partial operations into clean termination.
