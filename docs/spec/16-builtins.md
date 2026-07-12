# 29. Builtins

A **builtin** is a function that is part of the Tycho *language* rather than of
its standard library: it is in scope in every module with no `import`, and its
name resolves without qualification. This chapter is the complete, normative
catalog of the builtins — grouped by category, each with its argument and result
types and a one-line semantics, and each marked **Sig** or **magic** (§29.2).

The construction, mutation, and place semantics of the aggregates these builtins
operate on are specified in Part VI (§16–§19); their arena and
value-copy behavior in [§9](07-memory-model.md); the conversions' full
accepts→yields table in [§8.2](06-conversions.md#82-explicit-conversion-builtins).
Operators and keywords that read as calls but are **not** builtins — `m[k]`,
`k in m`, `delete m[k]`, `for x in xs`, `&place`, `or_return` — are specified in
their own chapters and are out of scope here.

> Provenance: `Sig` builtins `src/tychoc.c:3818-3849`; conversion magic
> `:4716-4787`; `len` `:4789-4794`; `keys`/`push`/`pop`/`reserve` `:4845-4930`;
> `m.get` sugar `:4397-4408`,`:4477-4488`; `zero$` `:4355-4371`; concurrency
> magic `:4671-4714`; `map_*` removal `:2100-2103`; `die` codegen `:7421-7423`.

## 29.1 Builtins are part of the language

Every builtin named in this chapter is **always available**. A conforming
implementation MUST make it resolvable in any module with no import and no
package qualifier, and MUST NOT require a builtin to be declared, imported, or
enabled by a flag. Builtins share the ordinary call syntax; they are
distinguished from user functions only by being predefined.

The line between the **language** and the **corelib** is drawn precisely:

- A name is a **builtin** (part of the language) iff `register_builtins`
  (`src/tychoc.c:3818-3849`) registers it **or** a case in `resolve_expr` /
  codegen special-cases it by name (§29.2). Such a name is available with no
  import.
- Every other predefined function is a **corelib** procedure, reached only
  through `import "core:…"` and specified in [Part XVIII](README.md) (the
  standard library). Corelib is *layered on* the language; it is
  not part of it.

This distinction is normative: it fixes which names a conforming implementation
MUST provide unconditionally (the builtins) versus which it MAY split across the
core and extended tiers ([§1.3](00-conventions.md#13-conformance)).

## 29.2 `Sig` builtins and `magic` builtins

Builtins fall into two kinds, and each entry below is marked with one:

- **Sig** — registered in `register_builtins` with a **fixed, monomorphic
  signature** and resolved by ordinary signature lookup, arity check, and
  argument type-checking, exactly like a user function (§15). A
  `Sig` builtin is never overloaded and takes its arguments by value.
- **magic** — recognized **by name in `resolve_expr`** (and in codegen) *before*
  signature lookup. A magic builtin MAY be polymorphic (its result or accepted
  types depend on an argument's type), MAY have a restricted parse form (e.g.
  `channel` only as a declaration RHS), MAY require an lvalue argument, or MAY
  lower to a literal or an operator rather than a call. Because it is intercepted
  ahead of the `Sig` table, a magic builtin is not an ordinary signature and its
  checking rules are stated per entry.

Three names — `str`, `to_ptr`, `to_i32` — carry **both** a `Sig` registration
and a `resolve_expr` special-case; the special-case governs. Classification here
follows observable behavior: `str` is polymorphic and is marked **magic**;
`to_ptr` and `to_i32` keep a single fixed `int` signature and are marked **Sig**.

> Editor's note (Appendix H): the reference page `docs/reference/builtins.md` is
> **incomplete** — it omits `eprint`, `is_null`, `to_ptr`, `to_i32`, `to_u32`,
> `to_u64`, and `to_f32`. This chapter is the complete set;
> the gap is logged for Appendix H and the reference page is to be updated to
> match.

## 29.3 I/O and process

| Builtin | Signature | Kind | Semantics |
|---|---|---|---|
| `print(s)` | `string -> void` | Sig | Write `s`'s bytes to stdout; no implicit newline. |
| `println(s)` | `string -> void` | Sig | `print(s)` followed by a single `"\n"`. String-only, like `print`; use `println(str(x))` for a non-string. |
| `eprint(s)` | `string -> void` | Sig | Write `s`'s bytes to stderr; no newline, no exit. |
| `input()` | `-> string` | Sig | Read one line from stdin, with the trailing newline stripped. |
| `read_all()` | `-> string` | Sig | Read all of stdin as one `string`. |
| `args()` | `-> [string]` | Sig | The command-line arguments; `args()[0]` is the program name. |
| `getenv(name)` | `string -> string` | Sig | The environment variable's value, or `""` if unset. |
| `die(msg)` | `string -> void` | Sig | Print `msg` to stderr and terminate the process with exit status `1`; never returns (§29.12). |

`print`, `println`, and `eprint` accept a `string` only; they do not implicitly
stringify. All eight are `Sig` builtins with fixed signatures.

> Provenance: `src/tychoc.c:3822-3840`; `eprint` codegen `:7342`; `die` codegen
> `:7421-7423`.

## 29.4 Conversions

Tycho performs **no implicit value conversion** ([§8](06-conversions.md)); every
value-level conversion is one of the builtins below. The full **accepts→yields**
table — the exact set of source types each accepts and the type each yields — is
[§8.2](06-conversions.md#82-explicit-conversion-builtins) and is **not**
duplicated here; this section states only each builtin's kind and one-line role.

| Builtin | Role | Kind |
|---|---|---|
| `str(x)` | Polymorphic value → `string` for `int`, `u32`, `u64`, `f32`, `bool`, `float`, `string` (through `base_of`); identity on `string`; **rejects `char`**. | magic |
| `to_int(x)` | `float`/`u32`/`u64`/`f32` or an `int`-newtype → `int` (float truncates toward zero). | magic |
| `to_float(n)` | `int`/`u32`/`u64`/`f32` or a `float`-newtype → `float`. | magic |
| `to_u32(x)` / `to_u64(x)` / `to_f32(x)` | Any numeric scalar (`int`/`char`/`float`/`u32`/`u64`/`f32`, through `base_of`) → the sized type. | magic |
| `to_str(x)` | `bytes`, or a `string`-newtype → `string` (zero-cost reinterpret). | magic |
| `to_bytes(x)` | `string`/`bytes` (through `base_of`) → `bytes` (same buffer, distinct type). | magic |
| `to_bool(x)` | A `bool`-newtype → `bool` (unwrap). | magic |
| `to_under(x)` | Any newtype → its underlying type (generic zero-cost unwrap). | magic |
| `chr(n)` | `int -> string`: the one-byte string for byte value `n` (`0`–`255`). | Sig |
| `to_ptr(n)` | `int -> ptr`: an opaque FFI sentinel pointer, never dereferenced ([§24](14-ffi.md)). | Sig |
| `to_i32(n)` | `int -> int`: sign-extend the low 32 bits of a returned C `int` (FFI, [§24](14-ffi.md)). | Sig |
| `is_null(p)` | `ptr -> bool`: test an opaque FFI pointer for `NULL` ([§24](14-ffi.md)). | Sig |

The base-specific `to_int`/`to_float`/`to_str`/`to_bool` and the generic
`to_under` are the newtype unwrappers ([§8.3](06-conversions.md#83-newtype-unwrapping),
[§5.4](03-types.md#54-newtypes)). `str(char)` is a hard error, an intentional
asymmetry ([§5.5](03-types.md#55-equality-and-ordering),
[§8.4](06-conversions.md#84-hard-errors)).

> Provenance: conversion magic `src/tychoc.c:4716-4787`; `chr`/`to_ptr`/`to_i32`/
> `is_null` `Sig` `:3830`,`:3841-3843`.

## 29.5 Strings

`string` is a byte buffer ([§5.2.5](03-types.md#525-string)); every string
builtin is **byte-oriented**, not Unicode-aware. `len` is the overloaded length
builtin shared with arrays, bytes, maps, and `soa` (§29.6); on a `string` it
yields the byte count.

| Builtin | Signature | Kind | Semantics |
|---|---|---|---|
| `len(s)` | `string -> int` | magic | Byte length. Overloaded (§29.6). |
| `substr(s, a, b)` | `(string, int, int) -> string` | Sig | A fresh copy of the byte range `[a, b)`; out-of-range bounds are **clamped**, not an error. |
| `find(s, sub)` | `(string, string) -> int` | Sig | Byte index of the first occurrence of `sub`, or `-1`. |
| `split(s, sep)` | `(string, string) -> [string]` | Sig | Split on a non-empty separator; `n` separators yield `n+1` fields. An **empty** `sep` aborts at run time (§29.12). |

> Provenance: `substr`/`find`/`split` `Sig` `src/tychoc.c:3833-3835`; `len` magic
> `:4789-4794`.

## 29.6 Arrays

All four array builtins are **magic**. `len` is overloaded across `[T]`,
`string`, `bytes`, maps, and `soa`. `push`, `pop`, and `reserve` require a
**mutable** aggregate: their first argument MUST resolve to a place whose root is
a mutable variable or an owned field — a read-only borrowed parameter is
rejected — so they may grow or shrink the value in the owning arena.

| Builtin | Signature | Kind | Semantics |
|---|---|---|---|
| `len(a)` | `[T] -> int` | magic | Element count. Also accepts `string`, `bytes`, a map, or a `soa`. |
| `push(a, v)` | `([T], T) -> void` | magic | Append `v` in place; `a` MUST be a mutable array (or `soa`) place. Grounds a still-pending array type to `[typeof(v)]`. |
| `pop(a)` | `[T] -> T` | magic | Remove and return the last element; aborts at run time if empty. `a` MUST be mutable. |
| `reserve(a, n)` | `([T], int) -> void` | magic | Capacity hint: preallocate room for `n` elements. `len` is unchanged and pushing past `n` still grows; an unallocatable capacity aborts. Restricted to arrays of scalars, structs, tuples, or nested arrays (not `soa`); also accepts a map place `m[k]`. |

> Provenance: `len` `src/tychoc.c:4789-4794`; `push` `:4852-4885`; `pop`
> `:4887-4904`; `reserve` `:4906-4929`.

## 29.7 Maps

Map key/value rules and the map operators are in [§5.3.5](03-types.md#535-maps-k-v)
and §18. The two map builtins proper are `keys` and the `.get`
method sugar; both are **magic**.

| Builtin | Signature | Kind | Semantics |
|---|---|---|---|
| `keys(m)` | `[K: V] -> [K]` | magic | The map's live keys as an array, for iteration; a newtype key stays wrapped. |
| `m.get(k)` | `([K: V], K) -> V` | magic | Value for `k`, or the value-type **zero** on a miss. Desugars to the `m[k]` rvalue read. |
| `m.get(k, d)` | `([K: V], K, V) -> V` | magic | Value for `k`, or the default `d` if absent. |

`m.get` is **method sugar** on a map receiver: `m.get(k)` rewrites to the `m[k]`
rvalue path and `m.get(k, d)` to the internal default-read node, so the emitted
code is identical to the operator forms. It resolves on a map receiver only; on
any non-map type `get` falls through to ordinary UFCS resolution.

The map operators `m[k]` (as a place and as an absent-key read yielding the
value's zero), `k in m`, and `delete m[k]` are **not** builtins — they are
operators/keywords specified in §18. The former functions
`map_set`, `map_get`, `map_has`, and `map_del` have been **removed**: a
user-written call to any of them is a **hard parse error** directing the author
to `m[k] = v`, `m.get(k, default)`, `k in m`, and `delete m[k]` respectively.
(The identically-named nodes that the desugars build internally are not
user-callable.)

> Provenance: `keys` `src/tychoc.c:4845-4851`; `m.get` sugar `:4397-4408`,
> `:4477-4488`; `map_*` removal (parse error) `:2100-2103`.

## 29.8 Type builtins

| Builtin | Signature | Kind | Semantics |
|---|---|---|---|
| `zero$(T)` | `-> T` | magic | The zero value of a **defaultable** scalar type `T`. |

`zero$(T)` uses the explicit type-argument call form (`name$(T, …)`, §7.1) and is
the one builtin that consumes it specially. `T` MUST be one of the four
defaultable scalar types — `int` (`0`), `float` (`0.0`), `bool` (`false`),
`string` (`""`) — matching the `defaultable` `where`-predicate
([§7.2](05-generics.md), [§7.5](05-generics.md)); a **newtype** or any other type
is rejected (fails closed). It takes **no** value arguments and is lowered to the
scalar zero literal, so it needs no runtime support. It exists to seed a generic
accumulator (`acc := zero$(T)`) that must work on an empty input.

There is **no** `empty$(T)` builtin. An `empty()` returning `[$T]` is an ordinary
user-written generic, and `empty$(int)` is merely the `name$(…)` call form
applied to it ([§7.5](05-generics.md)).

> Provenance: `zero$` `src/tychoc.c:4355-4371`; `defaultable` predicate `:6191`.

## 29.9 Concurrency

The concurrency model — tasks, channels, and their ordering guarantees — is
[§20](13-concurrency.md); this section catalogs only the builtins
and their static rules. `Task(T)` and `Channel(T)` are affine, non-storable
handle types with no type syntax ([§5.3.9](03-types.md#539-typed-handles)); the
builtins below are their only consumers.

| Builtin | Signature | Kind | Semantics |
|---|---|---|---|
| `wait(t)` | `Task(T) -> T` | magic | Join a spawned task and yield its result, deep-copied into the waiting scope. The argument MUST be a task variable or a `spawn` expression; exactly one `wait` per task. |
| `channel(T, cap)` | `-> Channel(T)` | magic | Create a bounded channel of capacity `cap` (an `int`). **Legal only as the direct RHS of a declaration** (`ch := channel(T, cap)`); any other position is a compile error. |
| `send(ch, v)` | `(Channel(T), T) -> void` | magic | Deep-copy `v` into the channel; blocks when full; aborts if the channel is closed. |
| `recv(ch)` | `Channel(T) -> Option(T)` | magic | Blocking receive (deep-copied out); `None` means the channel is closed **and** drained. |
| `close(ch)` | `Channel(T) -> void` | magic | Close a channel: receivers drain then see `None`. Also `close(h)` on a **handle** variable ([§25](14-ffi.md)): run its destructor early and suppress the scope-exit free. |
| `ncpu()` | `-> int` | Sig | The `parallel for` fan-out width (online CPUs; overridable by `TYCHO_THREADS`). |

`send`, `recv`, and `close` also have method-sugar forms on a channel-typed local
(`ch.send(v)`, `ch.recv()`, `ch.close()`), rewritten to the free-call forms; `wait`
likewise as `t.wait()`. `close` is overloaded across a channel and an FFI handle;
`ncpu` is the sole `Sig` builtin here.

> Provenance: `wait` `src/tychoc.c:4671-4678`; `channel` `:4682-4689`; `send`
> `:4690-4698`; `recv` `:4699-4704`; `close` `:4705-4714`; `ncpu` `Sig` `:3829`;
> task/channel method sugar `:4372-4386`.

## 29.10 Filesystem and time

All five are `Sig` builtins with fixed signatures. The file and directory
builtins **fail soft** — an unopenable path yields the empty result rather than
aborting.

| Builtin | Signature | Kind | Semantics |
|---|---|---|---|
| `read_file(path)` | `string -> string` | Sig | The whole file as a `string`, or `""` if it cannot be opened. |
| `write_file(path, s)` | `(string, string) -> bool` | Sig | Write `s`'s exact bytes, truncating; `false` if it cannot be opened. |
| `list_dir(path)` | `string -> [string]` | Sig | Entries excluding `.` and `..` (filesystem order); empty if the directory cannot be opened. |
| `clock()` | `-> int` | Sig | Monotonic nanoseconds — differences are meaningful; the absolute value is not. |
| `now()` | `-> int` | Sig | Wall-clock seconds since the UNIX epoch. |

> Provenance: `src/tychoc.c:3827-3828`,`:3836-3838`.

## 29.11 Float math (libm)

The float-math builtins are the **entire** libm surface exposed as language
builtins — exactly the four below, all `Sig`, all over `float`:

| Builtin | Signature | Kind |
|---|---|---|
| `sqrt(x)` | `float -> float` | Sig |
| `pow(x, y)` | `(float, float) -> float` | Sig |
| `floor(x)` | `float -> float` | Sig |
| `fabs(x)` | `float -> float` | Sig |

Other numeric functions — `min`, `max`, `clamp`, and the trigonometric functions
— are **not** builtins; they are provided by the standard library (§31)
and require an import.

> Provenance: `src/tychoc.c:3844-3848`.

## 29.12 Abnormal termination

`die(msg)` (§29.3) is the **only** user-callable abort in the language: it prints
`msg` to stderr and terminates the process with exit status `1`, and MUST NOT
return. There is **no** `assert`, `panic`, or `abort` builtin — no such name is
registered in `register_builtins` and none is special-cased in `resolve_expr`
(verified by searching `src/tychoc.c`).

The runtime does abort on its own for defined error conditions — an
out-of-bounds index, a `pop` on an empty array, division or modulo by a zero
*value*, a `split` on an empty separator, a `send` on a closed channel, and the
other cases enumerated in [§30](17-runtime.md). Those aborts are
**internal**: they are emitted by the runtime, not exposed as callable functions,
and a conforming program cannot invoke them directly. This is the language's
**fail-closed** posture ([§1.3](00-conventions.md#13-conformance)) — abnormal
conditions terminate rather than proceed into undefined behavior.

> Provenance: `die` `Sig` `src/tychoc.c:3831`, codegen `:7421-7423`; no
> `assert`/`panic`/`abort` name in `register_builtins` `:3818-3849` or the
> `resolve_expr` magic block `:4355-4930`.
