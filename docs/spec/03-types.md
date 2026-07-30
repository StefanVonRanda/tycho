# 5. Types

Every Tycho expression has a **type**, determined at its own site by the
inference rules of [§6](04-inference.md). Every value has **value semantics**:
assignment, argument passing, and return copy the value deeply, so two variables
never share storage ([§9](07-memory-model.md)). This chapter
defines the types themselves — their values, ranges, and identity — and defers
their construction and operations to the chapters noted.

Tycho is defined with **exact scalar widths** independent of any compilation
target ([§1.1](00-conventions.md#11-scope)). A conforming implementation MUST
provide the widths and behavior stated here even where its backend's native
types differ (for example, a C backend MUST realize `int` as a 64-bit type even
on a target where C `long` is 32 bits).

> Provenance: scalar tags `src/tychoc.c:536-557`; C lowering `c_type`
> `:1307-1347`; equality/ordering `:5875-5908`; newtype decl `parse_typedecl`
> `:3935-3954`.

## 5.1 The type-identity model

Two types are **the same type** iff they are identical (the reference
implementation interns every type to a single integer id and compares by
equality). Type constructors fall into two groups:

- **Nominal** — `struct`, `enum`, `newtype`, and `handle` types are identified
  by their declaration. Two distinct declarations are distinct types even with
  identical structure; a newtype is distinct from its underlying type (§5.4).
- **Structural** — arrays, fixed-size arrays, tuples, maps, `Option`, `Result`,
  `soa`, function types, and the channel/task handle types are identified by
  their structure. Two of them are the same type iff they are built from the
  same constructor applied to the same component types.

This distinction is normative: it determines exactly when two values may be
compared, assigned, or unified across `match`/`if` arms (§5.5, [§6](04-inference.md)).

## 5.2 Scalar types

### 5.2.1 `int`

`int` is a **64-bit two's-complement signed integer**, with values `−2^63`
through `2^63 − 1`. This width is **required**, not target-dependent: a
conforming implementation MUST realize `int` at exactly 64 bits through a
**fixed-width 64-bit lowering** — a C backend MUST emit a fixed-width 64-bit type
(`int64_t` / `long long`), never a type whose width varies by platform such as C
`long` (§F.3, [Appendix F](appendix-f-impl-defined.md)). Arithmetic that overflows this range **wraps** with
two's-complement semantics; overflow is fully defined and never traps
([§30](17-runtime.md)). Division and modulo by zero abort;
`(−2^63) / −1` aborts (quotient overflow) while `(−2^63) % −1` is `0`. The
only integer literal is decimal ([§3.9.1](01-lexical.md#391-integer-literals)),
denoting a non-negative value; `−2^63` is obtained only by computation.

### 5.2.2 `float`

`float` is an **IEEE-754 binary64** (double-precision) value. Its arithmetic,
rounding, and special values (signed zero, infinities, NaN) follow IEEE-754.
`/` is true division and does not trap.

Division never traps: `0.0/0.0` is `NaN`, `1.0/0.0` is `+inf`, and `-1.0/0.0` is
`-inf` (probed on both compilers). `NaN` is unordered — `NaN == NaN` is `false`,
and every ordering comparison with a `NaN` operand is `false`. The float *values*
are fully defined by IEEE-754; only the *textual* form `str` produces for
`NaN`/`inf` (e.g. `-nan`) derives from the C library and is implementation-defined
([Appendix F](appendix-f-impl-defined.md)).

### 5.2.3 `bool`

`bool` has the two values `true` and `false`. It is comparable with `==`/`!=`
and usable with `str` (via `has_str`), but is **not ordered** (§5.5).

### 5.2.4 `char`

`char` is a single **byte**, an integer value `0` through `255`. It is written
as a character literal ([§3.9.3](01-lexical.md#393-character-literals)) and
arises by inference; there is no `char` type keyword. `char` interoperates
narrowly with `int`: `char ± int` has type `char`, and the result **wraps to a
byte** (`0..255`, like `u8`) so the value never escapes the type's range. `char`
is comparable and ordered (§5.5). `str(char)` yields the one-byte **glyph**
string (so a `char` interpolates in an f-string); `to_int(char)` / `to_u32(char)`
yield the byte **value**.

Indexing a `string` does **not** produce a `char`: `s[i]` yields `int` (§5.2.5).
The `char`-typed reader is the builtin `char_at(s, i)`
([§29.5](16-builtins.md#295-strings)), so `char_at(s, 1) == 'e'` is the way to
compare a string's byte against a character literal — `s[1] == 'e'` is a type
error, because `int` and `char` are distinct types and Tycho does not compare
across types (§13.2).

### 5.2.5 `string`

`string` is an **immutable, length-counted, byte-safe** sequence of bytes. Its
length is stored explicitly, so interior `NUL` bytes are preserved and all
length-sensitive operations (comparison, concatenation, search, indexing) use
the stored length, not a `NUL` terminator. Indexing `s[i]` yields the `i`-th
byte as an `int` in `0..255` and aborts if `i` is out of bounds; a `string` is
not assignable through an index (`s[i] = v` is a compile error). Operations are
detailed in §16.

`s[i]` yields `int`, not `char`, and this is **normative and deliberate**: the
byte is most often used arithmetically or as an array index, and a `char` result
would wrap `s[i] ± n` to `0..255` where `int` does not — a silent change of
meaning for existing code. The cost is that `s[i] == 'e'` does not type-check.
The builtin `char_at(s, i)` ([§29.5](16-builtins.md#295-strings)) closes that
gap: it is the SAME byte read with the SAME out-of-bounds abort, differing only
in its static type (`char`), so `char_at(s, i) == 'e'` is the supported spelling
and `to_int(char_at(s, i)) == s[i]` holds for every in-range `i`. Neither form is
preferred for performance; they compile to the same call.

### 5.2.6 `bytes`

`bytes` is an **immutable binary buffer** — the same length-counted, byte-safe
representation as `string` but a **distinct type**. It is produced by
`to_bytes` from a `string` (a zero-cost reinterpret) or from an `[int]` of byte
values (each truncated `& 0xFF`; §8) — the latter being the only way to construct
a binary `bytes` with interior `0x00` bytes. There is no `bytes` literal. `bytes` exists
primarily to cross the FFI boundary as a `(pointer, length)` pair
([§24](14-ffi.md)).

**Operators.** `bytes` supports exactly the operator set `string` does, and with
the same meanings, because it is the same buffer:

| Form | Result | Notes |
|---|---|---|
| `len(b)` | `int` | byte count, from the length header (O(1)) |
| `b[i]` | `int` | the `i`-th byte as `0..255`, **never** a 1-length `bytes` — the same read as `s[i]` ([§5.2.5](#525-string)), same out-of-bounds abort, and **not** a place (`b[i] = v` is a compile error) |
| `b[i:j]` | `bytes` | a fresh sub-buffer; clamps out-of-range bounds exactly as a string slice does ([§16.6](12-aggregates.md#166-slices-xsab)) |
| `a + b` | `bytes` | concatenation |
| `b + 'c'` | `bytes` | appends the char's single byte; one-directional, like `string + char` ([§13.2](09-expressions.md#132-operators)) |
| `a == b`, `a != b` | `bool` | byte-wise, by the length headers |

There is **no implicit conversion between `string` and `bytes`** in either
direction: `b + "s"` and `s + b` are type errors. The boundary is crossed only by
`to_bytes` / `to_str`, both zero-cost reinterprets (§8). Arithmetic other than
`+` is a type error naming this set. `bytes` has no other operators: iteration
(`for x in b`) and `in` are not provided.

Constructing a `bytes` from computed byte values — including an interior `0x00`,
which a `string` cannot hold — reading bytes back out by index, taking a
sub-buffer, and reinterpreting to `string` (the two share the length-counted
buffer):

```tycho
fn main():
    b := to_bytes([72, 105, 0, 255])     # 'H' 'i' NUL 0xFF; each element & 0xFF
    s := to_str(b)                        # reinterpret: same buffer, byte-safe
    println(str(len(b)))                  # 4 — the interior NUL is preserved
    println(str(s[0]) + " " + str(s[2]) + " " + str(s[3]))
    println(str(b[2]) + " " + str(len(b[1:3])) + " " + str(b[1:3][1]))
    j := b + to_bytes([33]) + '?'        # bytes + bytes, then bytes + char
    println(str(len(j)) + " " + str(j[4]) + " " + str(j[5]))
```

```output
4
72 0 255
0 2 0
6 33 63
```

### 5.2.7 Fixed-width integers `u8`/`u16`/`u32`/`u64`, `i8`/`i16`/`i32`/`i64`

The fixed-width integer family gives **unsigned** (`u8`, `u16`, `u32`, `u64`) and
**signed** (`i8`, `i16`, `i32`, `i64`) integers of exactly 8, 16, 32, and 64 bits.
Each is a distinct type from `int` and from every other; they do **not** mix
implicitly with `int` or with each other. Arithmetic **wraps at the type's width**
(defined — modular for the unsigned types, two's-complement for the signed via
`-fwrapv`). The right shift `>>` is **logical** on the unsigned types and
**arithmetic** (sign-preserving) on the signed; a shift count `≥` the width yields
`0` and a negative count aborts ([§13.2](09-expressions.md#132-operators));
division and modulo by zero abort. A value is produced by the matching `to_u8` …
`to_i64` conversion or by adapting an integer literal in that type's context (§8).
`int` (64-bit signed) remains the default for general arithmetic; the fixed-width
types are for bit-level work, packing, and matching a C ABI. They cross the FFI
boundary as their exact C width ([§24](14-ffi.md)).

### 5.2.8 `f32`

`f32` is an **IEEE-754 binary32** (single-precision) value, distinct from
`float`. It is produced by `to_f32` or by adapting an integer or float literal
in an `f32` context (§8); it promotes to `float` (binary64) for `str`.

### 5.2.9 `ptr`

`ptr` is an **opaque FFI pointer** (`void*`). Tycho never dereferences it; it
supports only the `null` literal, being passed to and returned from `extern`
functions, `null`-comparison, and `is_null` ([§24](14-ffi.md)).

## 5.3 Composite types

The type-level definition of each composite is given here; its construction,
indexing, mutation, and place semantics are specified in Part VI (§16–§19) and its arena behavior in [§9–§11](07-memory-model.md).

### 5.3.1 Arrays `[T]`

`[T]` is a **growable, value-semantic array** of elements of type `T`; `T` may
be any type. Two array types are the same iff their element types are the same.
An empty array literal requires an element type (`[]int`). Indexing is
bounds-checked (out-of-bounds aborts). Element types `void` and `bool` are
**not** permitted directly as a bracket-array element type in a type position
(a `[bool]` is rejected at type-parse); a `bool` array is expressed through
other means where needed.

> Note: confirmed in source (dynamic `[T]` `src/tychoc.c:1688-1689`, fixed `[N]T`
> `:1671-1673`); detailed in
> [§16.7](12-aggregates.md#167-element-type-restriction).

### 5.3.2 Fixed-size arrays `[N]T`

`[N]T` is a **fixed-size array** of exactly `N` elements, stored inline and
copied by value. `N` is a positive integer literal or an `int` `const`; it is
part of the type, so `[3]int` and `[4]int` are distinct types and `len` is the
compile-time constant `N`. A fixed-size array supports neither `push`/`pop` nor
slicing. The generic form `[$N]T` is a const generic (§7.4).

### 5.3.3 Tuples

A tuple `(T1, …, Tn)` is an **anonymous product** of 2 to 8 elements, identified
structurally. Its elements are accessed by position (`t.0`, `t.1`) and are
assignable places. Multiple return values and destructuring use tuples
(§17).

**A tuple is the shape for "a value AND a classification."** This is worth stating
outright because the two obvious alternatives both work and both cost more: an
`inout` out-parameter (§11.3) makes the caller declare a dummy to receive, and a
wrapper `struct` adds a nominal type per call site. A tuple needs neither, and
`a, b := f()` destructuring reads at the call site as two results rather than one
object. Since §6.2(7) a tuple **literal**'s elements are checked against the
declared element types, so a `Result` element may be written inline
(`return (Err(Timeout), buf)`) without a typed local.

```tycho
# The value and the verdict leave together; nothing is a sentinel.
fn parse_port(s: string) -> (int, bool):
    if len(s) == 0:
        return (0, false)
    n := 0
    for i := 0; i < len(s); i += 1:
        d := s[i] - 48
        if d < 0 or d > 9:
            return (0, false)
        n = n * 10 + d
    if n > 65535:
        return (0, false)
    return (n, true)

fn main():
    p, ok := parse_port("8080")
    println(str(p) + " " + str(ok))
    q, bad := parse_port("80x0")
    println(str(q) + " " + str(bad))
```

```output
8080 true
0 false
```

Worked examples in the corelib, because the shape is easy to miss when the type
section is the only place it appears: `strings.split_once(s, sep) -> (before, after)`
(`corelib/strings/strings.ty:193`), `path.split_path(p) -> (dir, base)`
(`corelib/path/path.ty:95`), `datetime.parse_offset(s, at) -> (int, bool)` — the
value-and-verdict form above (`corelib/datetime/datetime.ty:248`),
`bignum.divmod(a, b) -> (Big, Big)` (`corelib/bignum/bignum.ty:265`), and
`httpd.read_request_capped(fd, cap) -> (Result(Request, ReqErr), string)`
(`corelib/httpd/httpd.ty:242`), which carries a classification *and* the raw bytes
that produced it. For a C function that must classify a payload the FFI has its own
shape, since a tuple does not cross the boundary — see
[§24.1.1](14-ffi.md#2411-returning-a-payload-and-a-classification).

### 5.3.4 Structs

A `struct` is a **nominal product type** with named fields, constructed
positionally in field-declaration order. A field may be recursive **only
through a container** (e.g. `children: [Node]`); a direct by-value self-field
(`next: Node` or `next: Option(Node)` directly) is rejected, because an inline
self-embedding would be infinitely sized (§17).

### 5.3.5 Maps `[K: V]`

A map `[K: V]` associates keys of type `K` with values of type `V`. The **value
type `V` is unrestricted** (any type). The **legal key types** are exactly:

- `string`;
- `int`;
- a **newtype** whose underlying type is `int` or `string`;
- a **fieldless enum** (hashed by its variant tag); and
- a **composite** (`struct`, tuple, or array) all of whose leaf types are
  hashable — where a hashable leaf is `int`, `float`, `bool`, `char`, `string`,
  `bytes`, or (recursively) a struct/tuple/array of hashable leaves.

All other key types are rejected, including a bare `float`, `bool`, `char`, or
`bytes` key, an enum variant carrying a payload, and a map-typed key. Note that
`char` and `bytes` are hashable as composite *leaves* but are **not** permitted
as a top-level key type.

Map operations (`m[k]` as a place, absent-key read yielding the value's zero,
`k in m`, `delete m[k]`, `m.get`) are specified in §18.

> Provenance: `map_of` `src/tychoc.c:1242-1271`; `key_hashable` `:1194-1208`.

### 5.3.6 Enums, `Option`, `Result`

An `enum` is a **nominal sum type**; each variant is globally uniquely named and
may carry a payload of up to 8 types. `Option(T)` is the built-in enum with
variants `Some(T)` and `None`; `Result(T, E)` is the built-in enum with variants
`Ok(T)` and `Err(E)`. Enums are consumed by exhaustive `match` and support
`or_return` for `Option`/`Result` (§19). Recursive payloads are
permitted (they are arena-allocated, hence finite).

### 5.3.7 `soa`

`soa [S]` is a **struct-of-arrays** collection whose element type `S` MUST be a
struct; it presents the same value-semantic array interface while storing each
field in its own backing array (§17).

### 5.3.8 Function types

`fn(P1, …, Pn) -> R` is a **first-class function type** (up to 8 parameters),
identified structurally. A parameter type may not be `void`. A function *value*
is a closure over captured state (§15); function values are **not comparable**
(§5.5). A function that has an `inout` parameter
cannot be used as a first-class value.

### 5.3.9 Typed handles

A `handle` type is a **nominal, affine, opaque FFI resource** — a `void*` with a
declared C free function that runs at scope exit. A handle value cannot be
copied, stored in any aggregate, captured by a closure or `parallel for`, or
returned from a Tycho function (§25). The concurrency handle types
`Task(T)` and `Channel(T)` are similarly affine and non-storable
([§20](13-concurrency.md)).

### 5.3.10 `bounded[N]T`

`bounded[N]T` is an **inline, fixed-capacity, variable-count** collection: it
holds between 0 and `N` elements of type `T` stored *inside* its container —
never in a separate arena buffer — and is copied by value. It differs from
[`[N]T`](#532-fixed-size-arrays-nt) in carrying a runtime element count, and
from [`[T]`](#531-arrays-t) in that the count MUST NOT exceed the compile-time
capacity `N`. The capacity is part of the type: `bounded[4]int` and
`bounded[8]int` are distinct types, and both are distinct from `[4]int` and
from `[int]`.

The capacity MUST be written either as a positive integer **literal** or as the
name of a positive `int` `const` — the same two spellings a fixed-size
[`[C]T`](#532-fixed-size-arrays-nt) accepts (§5.3.2). A capacity of `0`, a
non-integer literal, an absent capacity, a negated literal (`-1` is a unary
minus applied to a literal, not a literal), a name that is not a `const`, and a
`const` whose value is not a positive integer MUST all be rejected.

The element type `T` MUST NOT be `bool` or `void`, mirroring the bracket-array
element-type restriction of §5.3.1. An affine handle type — including
`Task(T)` and `Channel(T)` — MUST NOT appear as a `bounded` element, because a
handle cannot be stored in any aggregate (§5.3.9). **Every other type is a
valid element**, including an aggregate one: a `struct`, a tuple, a map, a
`soa`, an `Option`, a `Result`, `bytes`, a fixed-size array `[N]E`, a dynamic
array `[E]`, a function type, an enum, and a nested `bounded[M]E`. A `bounded`
holding an aggregate is valid in every stored position — a local, a parameter,
a struct field and a return type.

Because a `bounded[N]T` stores its elements **inline**, `T` MUST be a type of
known size at that point: a type whose own definition reaches back to the
`bounded` that contains it (`struct Node: kids: [2]Node`) is an infinite type
and MUST be rejected, exactly as a directly self-containing struct is (§5.3.2
applies the same rule to `[N]T`). Use a dynamic array or an `Option` for the
indirection.

`len` yields the **runtime count**, not `N`. `push` appends and, when the
collection is already full, **aborts** the program rather than growing; that
trap is the point of the type. An array literal longer than `N` is rejected at
compile time. Indexing, index assignment, `==`, value copy, `str`, and `for … in`
iteration behave as they do for a fixed-size array. `pop`, slicing, and
`reserve` MUST be rejected on a `bounded` value.

> Provenance: the `bounded` branch of `parse_type_inner`,
> `src/tychoc.c:1903-1920@"bounded"` (capacity `:1866-1875`, element
> restriction `:1877-1878`); its twin
> `compiler/tychoc0.ty:1916-1947@"bounded"`, whose `const` capacity is
> deferred as `[b#W]T` and resolved in `mangle_type` (`:3301@[b#`),
> with the unresolved-name guard at `:11908-11912@[b#`. The affine-element
> rejection is a type-intern choke point in `src/tychoc.c`
> (`arrc_sized_b` `src/tychoc.c:736-748@arrc_sized_b`, messages `:636@task_container_err` and `:676@chan_container_err`) and an
> explicit check at `compiler/tychoc0.ty:1890-1896@ck_affine_part`.
> Rejections: slice `src/tychoc.c:4996-4997`, `pop` `:5647-5648`, `reserve`
> `:5839@reserve does not apply to a bounded`, over-long literal `:6023-6026`. The full-push trap is emitted at
> `:11197-11200`. Fixtures: `tests/bounded.ty`, `tests/bounded_const_cap.ty`,
> `tests/reject/fixarr_into_bounded_arg.ty`,
> `tests/reject/bounded_chan_elem.ty`, `tests/reject/bounded_task_elem.ty`,
> `tests/reject/bounded_nonconst_cap.ty`,
> `tests/reject/bounded_const_cap_zero.ty`,
> `tests/reject/bounded_elem_bool.ty`. The aggregate-element surface is covered
> by `tests/bounded_elems.ty` (struct, tuple, map, nested `bounded`, `bytes`,
> `[N]E`, `Option`, `Result` — each as a local, a parameter, a struct field and
> a return type) and `tests/fixarr_aggregate.ty` for the `[N]T` twin. The
> inline element is emitted inside the by-value containment DFS — `[N]T` and
> `bounded[N]T` are ordered with the struct/tuple/Option bodies rather than with
> the pointer-shaped arrays (`src/tychoc.c:10598-10688`, with `inline_arrc`/
> `needs_body_first` at `:10590-10596`; tychoc0's `comp_dep_types`
> `compiler/tychoc0.ty:10241-10268` and `emit_comp_body` `:10278-10302`) — which is what makes an aggregate element compile; the
> infinite-type rejection falls out of the same DFS
> (`tests/reject/inline_arr_self_elem.ty`).

## 5.4 Newtypes

```ebnf
TypeDecl ::= "type" IDENT "=" Type NEWLINE
```

A newtype `type X = U` introduces a **distinct** type `X` over an underlying
type `U`. `U` MUST be one of: `int`, `float`, `string`, `bool`, an array type, a
map type, or a struct type. It MUST NOT be an enum, a tuple, a sized numeric
(`u8`…`u64`/`i8`…`i64`/`f32`), `char`, `bytes`, `ptr`, an `Option`/`Result`, a
function type, a `soa`, a channel, a task handle, a typed handle, or another
newtype. The permitted list is closed, so a shape named in neither list — `soa`
was one — is refused by the same rule.

`X` is type-incompatible with `U` and with every other newtype: passing a `U`
where `X` is expected, or mixing `X` with `U` in arithmetic, is a compile error.
The distinctness is enforced only in the type system; a newtype is **erased in
lowering** (an `X` over `float` is represented exactly as a `float`, at zero
cost). Arithmetic, ordering, `==`, and `str` on a newtype are permitted only
between two values of the *same* newtype, and the result keeps the newtype.
Unwrapping to the underlying value uses the base-specific `to_int`/`to_float`/
`to_str`/`to_bool` or the generic `to_under` (§8). A newtype over `int` or
`string` is a valid map key carrying its wrapped identity (§5.3.5).

> Provenance: underlying restriction `src/tychoc.c:3947-3949`; its twin
> `newtype_under_ok` `compiler/tychoc0.ty:3126-3139`, called from
> `parse_newtype` `:3141-3154`. Fixtures: `tests/reject/newtype_under_option.ty`
> and its eleven siblings (`_result`, `_enum`, `_soa`, `_newtype`, `_ptr`,
> `_bytes`, `_u8`, `_f32`, `_tuple`, `_fnty`, `_handle`) — one per rejection,
> because the compiler halts at the first error.

## 5.5 Equality and ordering

**Equality (`==`, `!=`).** Two values may be compared iff they have the **same
type** (§5.1); `void` values are not comparable. Equality is **structural and
deep**: scalars compare by value; `string`, `bytes`, arrays, structs, tuples,
maps, enums, `Option`, and `Result` compare by content, recursing through nesting
— so `a == b` holds exactly when `b` is an independent deep copy of `a`.
**Function values are not comparable:** a closure has no structural equality (two
closures with different captured environments but identical behavior are
indistinguishable, and comparing thunk pointers would be a non-structural leak),
so applying `==`/`!=` directly to a function value is a **compile error** — Go,
Swift, and Odin all reject `fn == fn`. Compare the values a function produces
instead. A function stored *inside* an aggregate does not block that aggregate's
equality: the aggregate still compares structurally, and a function field within
it compares by identity. Comparing values of two different types is a compile
error.

**Ordering (`<`, `>`, `<=`, `>=`).** Both operands MUST have the same type, and
that type's underlying scalar MUST be one of `int`, `char`, `float`, `string`,
`u32`, `u64`, or `f32` (or a newtype over one of these). Structs, tuples,
arrays, maps, enums, and `bool` are **not** ordered. String ordering is
byte-lexicographic.

One asymmetry follows and is intentional: `bool` is comparable and `str`-able but
is not ordered. (`char` is comparable, ordered, and `str`-able — its `str` is the
one-byte glyph.)

> Provenance: `src/tychoc.c:5875-5908` (equality/ordering resolver); function-
> value identity equality `:8765@identity equality`.
