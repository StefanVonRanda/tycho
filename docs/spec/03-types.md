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

> Provenance: scalar tags `src/tychoc.c:453-469`; C lowering `c_type`
> `:1103-1126`; equality/ordering `:5088-5114`; newtype decl `:3430-3446`.

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
through `2^63 − 1`. Arithmetic that overflows this range **wraps** with
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
narrowly with `int`: `char ± int` has type `char`. `char` is comparable and
ordered (§5.5) but is **not** accepted by `str` — an intentional asymmetry
(§8).

The result has type `char`, but its numeric value is the ordinary integer result
and is **not** reduced to `0..255`: `'a' + 300` is a `char` holding `397` (probed
on both compilers). A `char` produced by arithmetic is therefore not guaranteed to
lie in `0..255`.

### 5.2.5 `string`

`string` is an **immutable, length-counted, byte-safe** sequence of bytes. Its
length is stored explicitly, so interior `NUL` bytes are preserved and all
length-sensitive operations (comparison, concatenation, search, indexing) use
the stored length, not a `NUL` terminator. Indexing `s[i]` yields the `i`-th
byte as an `int` in `0..255` and aborts if `i` is out of bounds; a `string` is
not assignable through an index (`s[i] = v` is a compile error). Operations are
detailed in §16.

### 5.2.6 `bytes`

`bytes` is an **immutable binary buffer** — the same length-counted, byte-safe
representation as `string` but a **distinct type**. It is produced by
`to_bytes` from a `string` (§8); there is no `bytes` literal. `bytes` exists
primarily to cross the FFI boundary as a `(pointer, length)` pair
([§24](14-ffi.md)).

### 5.2.7 `u32` and `u64`

`u32` and `u64` are **unsigned integers of exactly 32 and 64 bits**, with values
`0 .. 2^32 − 1` and `0 .. 2^64 − 1`. Arithmetic wraps modulo `2^32` / `2^64`
(defined); the right shift `>>` is logical; division and modulo by zero abort.
They are distinct types from `int` and from each other; they do not mix
implicitly with `int` or with each other and are produced by the `to_u32` /
`to_u64` conversions or by adapting an integer literal in a `u32`/`u64` context
(§8).

### 5.2.8 `f32`

`f32` is an **IEEE-754 binary32** (single-precision) value, distinct from
`float`. It is produced by `to_f32` or by adapting an integer or float literal
in an `f32` context (§8); it promotes to `float` (binary64) for `str`.

### 5.2.9 `ptr`

`ptr` is an **opaque FFI pointer** (`void*`). Tycho never dereferences it; it
supports only the `null` literal, being passed to and returned from `extern`
functions, `null`-comparison, and `is_null` ([§24](14-ffi.md)).
The sized-integer spellings other than the first-class `u32`/`u64`/`f32` (namely
`u8`, `u16`, `i8`, `i16`, `i32`, `i64`) are **not** first-class types; they are
valid only as `extern` signature spellings (§24).

## 5.3 Composite types

The type-level definition of each composite is given here; its construction,
indexing, mutation, and place semantics are specified in Part VI (§16–§19,
forthcoming) and its arena behavior in [§9–§11](07-memory-model.md).

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

> Editor's note (punch-list #33): a stale diagnostic
> (`src/tychoc.c:1684`,`:4247`) claims "int-keyed maps support only int/float
> values." That message is misleading — `V` is unrestricted. Appendix H logs the
> correction; the reference `maps.md` page is to be updated to match this rule.

Map operations (`m[k]` as a place, absent-key read yielding the value's zero,
`k in m`, `delete m[k]`, `m.get`) are specified in §18.

> Provenance: `map_of` `src/tychoc.c:1037-1065`; `key_hashable` `:989-1002`.

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
is a closure over captured state (§15); function values compare by
**identity**, not structurally (§5.5). A function that has an `inout` parameter
cannot be used as a first-class value.

### 5.3.9 Typed handles

A `handle` type is a **nominal, affine, opaque FFI resource** — a `void*` with a
declared C free function that runs at scope exit. A handle value cannot be
copied, stored in any aggregate, captured by a closure or `parallel for`, or
returned from a Tycho function (§25). The concurrency handle types
`Task(T)` and `Channel(T)` are similarly affine and non-storable
([§20](13-concurrency.md)).

## 5.4 Newtypes

```
TypeDecl ::= "type" IDENT "=" Type NEWLINE
```

A newtype `type X = U` introduces a **distinct** type `X` over an underlying
type `U`. `U` MUST be one of: `int`, `float`, `string`, `bool`, an array type, a
map type, or a struct type. It MUST NOT be an enum, a tuple, a sized numeric
(`u32`/`u64`/`f32`), `char`, `bytes`, `ptr`, an `Option`/`Result`, a function
type, a handle, or another newtype.

`X` is type-incompatible with `U` and with every other newtype: passing a `U`
where `X` is expected, or mixing `X` with `U` in arithmetic, is a compile error.
The distinctness is enforced only in the type system; a newtype is **erased in
lowering** (an `X` over `float` is represented exactly as a `float`, at zero
cost). Arithmetic, ordering, `==`, and `str` on a newtype are permitted only
between two values of the *same* newtype, and the result keeps the newtype.
Unwrapping to the underlying value uses the base-specific `to_int`/`to_float`/
`to_str`/`to_bool` or the generic `to_under` (§8). A newtype over `int` or
`string` is a valid map key carrying its wrapped identity (§5.3.5).

> Provenance: underlying restriction `src/tychoc.c:3439-3441`.

## 5.5 Equality and ordering

**Equality (`==`, `!=`).** Two values may be compared iff they have the **same
type** (§5.1); `void` values are not comparable. For every type except function
types, equality is **structural and deep**: scalars compare by value; `string`,
`bytes`, arrays, structs, tuples, maps, enums, `Option`, and `Result` compare by
content, recursing through nesting — so `a == b` holds exactly when `b` is an
independent deep copy of `a`. **Function values are the sole exception:** they
compare by **identity** (same underlying function and captured environment), not
structurally. Comparing values of two different types is a compile error.

**Ordering (`<`, `>`, `<=`, `>=`).** Both operands MUST have the same type, and
that type's underlying scalar MUST be one of `int`, `char`, `float`, `string`,
`u32`, `u64`, or `f32` (or a newtype over one of these). Structs, tuples,
arrays, maps, enums, and `bool` are **not** ordered. String ordering is
byte-lexicographic.

Two asymmetries follow and are intentional: `char` is comparable and ordered but
is not accepted by `str`; `bool` is comparable and `str`-able but is not ordered.

> Provenance: `src/tychoc.c:5088-5114` (equality/ordering resolver); function-
> value identity equality `:7115`.
