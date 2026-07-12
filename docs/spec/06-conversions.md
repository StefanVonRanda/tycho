# 8. Conversions and casts

Tycho performs **no implicit conversion of values** between types. Distinct
scalar types never mix in an operation, and no value silently widens or narrows.
The only implicit adaptation is of **literals** (§8.1); every value-level
conversion is an explicit builtin call (§8.2).

> Provenance: conversion builtins `src/tychoc.c:4716-4787`; literal adaptation
> in the binary-op and checking paths `:5078-5104`,`:5167-5225`.

## 8.1 Literal adaptation

An integer or floating-point **literal** — an `int`/`float` literal token, not a
typed value or variable — adapts to the type expected by its context. Adaptation
changes the type the literal is compiled to; it never changes a variable's type
and never converts a value at run time. The permitted adaptations are:

- an integer literal → `float` (e.g. `x: float = 3`, or `3` in `3 + y` where `y:
  float`);
- an integer literal → `u32` or `u64`;
- an integer **or** float literal → `f32`;
- otherwise the literal keeps its own type (`int` or `float`).

A `char` literal does **not** adapt. Division or modulo by a **literal** zero is
a compile-time error (a zero *value* at run time aborts, [§30](17-runtime.md),
forthcoming). There is no source-level numeric suffix
([§3.9.1](01-lexical.md#391-integer-literals)); the destination type drives
adaptation.

## 8.2 Explicit conversion builtins

Each conversion is a builtin call with a fixed accepted-type set and result
type. `base_of` in the "accepts" column means the operand's underlying type is
consulted (so a newtype over the listed base is accepted).

| Builtin | Accepts | Yields | Notes |
|---|---|---|---|
| `str(x)` | `int`, `u32`, `u64`, `f32`, `bool`, `float`, `string` (through `base_of`) | `string` | polymorphic; identity on `string`; **rejects `char`** |
| `to_int(x)` | `float`, `u32`, `u64`, `f32`, or an `int`-newtype | `int` | truncates toward zero for `float` |
| `to_float(n)` | `int`, `u32`, `u64`, `f32`, or a `float`-newtype | `float` | |
| `to_u32(x)` / `to_u64(x)` / `to_f32(x)` | any numeric scalar: `int`, `char`, `float`, `u32`, `u64`, `f32` (through `base_of`) | `u32` / `u64` / `f32` | narrowing / reinterpretation |
| `to_str(x)` | `bytes`, or a `string`-newtype | `string` | zero-cost reinterpret (same buffer) |
| `to_bytes(x)` | `string`, `bytes` (through `base_of`) | `bytes` | same buffer, distinct type |
| `to_bool(x)` | a `bool`-newtype | `bool` | newtype unwrap |
| `to_under(x)` | any newtype | its underlying type | generic newtype unwrap, zero-cost |
| `to_ptr(n)` | `int` | `ptr` | FFI sentinel pointer; never dereferenced |
| `to_i32(n)` | `int` | `int` | FFI: sign-extend the low 32 bits of a returned C `int` |

Each takes exactly one argument. `str` is the conversion used by f-string
interpolation ([§3.9.5](01-lexical.md#395-f-string-interpolated-literals)):
because a hole desugars to `str(hole)`, a hole MUST have a type `str` accepts.

> Editor's note (punch-list #34): `str` accepts `u32`/`u64`/`f32`, so an
> f-string hole of those types is valid, though `types.md` lists only
> int/float/bool/string. Reconciled in Appendix H.

## 8.3 Newtype unwrapping

A value of a newtype `X = U` is unwrapped to its `U` value by the base-specific
conversion — `to_int` (`U = int`), `to_float` (`U = float`), `to_str` (`U =
string`), `to_bool` (`U = bool`) — or by the generic `to_under`, which yields
the underlying type of any newtype. Unwrapping is zero-cost (a newtype is erased
in lowering, [§5.5](03-types.md#54-newtypes)).

## 8.4 Hard errors

The following are compile-time type errors, not conversions:

- an arithmetic, comparison, or bitwise operation mixing two distinct scalar
  types (`int + float`, `int == char`, a bitwise/shift/modulo op on mixed
  integer widths) — the operands MUST already share a type;
- `str(c)` for a `char` `c` (an intentional asymmetry — `char` is orderable but
  not string-convertible, [§5.5](03-types.md#55-equality-and-ordering));
- passing a value of a newtype's underlying type where the newtype is expected,
  or vice versa, without an explicit construct/unwrap.

## 8.5 Out-of-range conversions

For an in-range operand, `to_int(f)` truncates `f` toward zero, and each sized
conversion narrows by the modular/reinterpreting rule of §8.2. The result of a
conversion whose source value is **not representable** in the destination type —
a `float` outside the `int` range for `to_int`, or a value that does not fit the
target width for `to_u32`/`to_u64`/`to_f32` — is **unspecified** (probed: it
currently inherits the C target's out-of-range float-to-integer behavior, which
is undefined). A conforming program MUST NOT rely on the result of an
out-of-range conversion ([Appendix F](appendix-f-impl-defined.md)).
