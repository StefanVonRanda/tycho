# Types

> **Thesis context:** The type system exists to show the arena model handles diverse value
> types (scalars, newtypes, bytes) without weakening. Newtypes test that the
> arena-based deep copy respects type identity; bidirectional inference tests that
> type-driven allocation decisions are never deferred.

Tycho is statically typed, and every value has a type known at compile time. This page
covers the scalar types, the `bytes` buffer, distinct `type` newtypes, and how
bidirectional inference lets you leave most annotations off without ever introducing a
type variable. The compound types — arrays, structs, maps, tuples, enums — each have
their own page.

## Scalars

| Type | Values |
| --- | --- |
| `int` | 64-bit signed integer |
| `float` | 64-bit IEEE-754 double |
| `bool` | `true` / `false` |
| `string` | an immutable, length-counted byte string |
| `char` | a single byte (`0`–`255`) |

`int` and `float` never mix implicitly — there is no automatic widening. Convert
explicitly with `to_float(n)` and `to_int(x)` (the latter truncates toward zero). The
operators each type supports are in [Basics](basics.md#expressions).

### `char`

A `char` is one byte, written with a quoted literal — `'x'`, with the escapes
`\n \t \r \0 \\ \'`. It interoperates with `int` deliberately and narrowly:

- `char ± int` is a `char` (a byte offset — `'a' + 1` is `'b'`), and stays within a byte.
- `string + char` appends the byte **in place**, without allocating, so building a string
  one character at a time is a byte-write, not an allocation per character:

```
s := ""
for d in range(0, 10):
    s = s + ('0' + d)          # zero-allocation one-byte append per digit
```

`char` does not silently become `int` elsewhere — `str(c)` and `c == n` (mixing `char`
and `int`) are type errors, so the byte/number distinction is never lost by accident.

### `bytes`

`bytes` is an immutable binary buffer. Unlike `string`, it is not NUL-terminated, so
interior `\0` bytes survive — which is what you want for hashing, crypto, and binary I/O.
`to_bytes(s)` and `to_str(b)` bridge the two. `bytes` also crosses the [FFI](ffi.md)
boundary as a `(pointer, length)` pair.

## String interpolation

An `f"..."` string interpolates `{expr}` holes, desugaring to ordinary concatenation —
`f"point=({p.x},{p.y}) sum={a + b}"` becomes `"point=(" + str(p.x) + ...`. A plain
`"..."` is **never** interpolated, so it needs no brace escaping; inside an `f"..."`,
`{{` and `}}` are literal braces. A hole may hold any expression — including one with its
own string literals or a nested f-string — and must evaluate to an `int`, `float`, `bool`
(printed `true`/`false`), or `string`.

## Distinct newtypes (`type`)

`type Meters = float` declares a **distinct** type: same runtime representation as its
underlying type (zero cost — a `Meters` *is* a `double` in the generated C), but
type-incompatible with `float` and with every other newtype. This is Odin's
`distinct` — Tycho has no *transparent* alias, so `type` always means distinct.

```
type Meters = float
type Seconds = float

fn area(w: Meters, h: Meters) -> Meters:
    return w * h               # arithmetic stays in Meters

fn main():
    w := Meters(3.0)
    a := area(w, Meters(4.0))
    print(str(a))              # 12.0 -- str sees the underlying float
    # area(3.0, 4.0)           -> error: a float is not a Meters
    # w + Seconds(1.0)         -> error: can't mix two newtypes
```

A newtype supports its base type's **arithmetic, ordering, `==`, and `str`** — but only
between two values of the *same* newtype, so `Meters` and `Seconds` never mix by accident.
That is the point: zero-cost unit and ID safety. Construct one with `Meters(x)`; recover
the raw value with `to_int` / `to_float` / `to_str` / `to_bool` per the underlying scalar.

The underlying type may also be an **aggregate** — `type Ids = [int]`,
`type Env = [string: int]`, `type Pos = Pt` (a struct). Wrap with `Ids(v)`, unwrap with the
generic `to_under(x)` (zero-cost, works on any newtype), and value semantics carries
through the wrapper unchanged. A newtype over `string` or `int` is also a valid
[map key](maps.md#keys).

## Type inference (bidirectional)

A local infers forward from its initializer — `x := e` gives `x` the type of `e`. In the
other direction, every position with a known destination type — declarations, assignments,
call arguments, `return`, stores, literal elements — *checks* the expression against it.
This is Pierce–Turner local inference, and it lets the annotation-light forms work:

```
xs : [int] = []              # bare [] takes the expected array type
counts(em, [])               # ...in argument position too
g := f + 2                   # an int literal adapts to a float context (f : float)
iter.map(xs, fn(x): x * 2)   # lambda params + return from the expected fn type

ys := []                     # a bare decl stays pending...
push(ys, 3)                  # ...until its first grounding use types it ([int])
```

There are **no type variables and no unification**: every expression is typed at its own
line, either synthesized from its parts or checked against a known destination. Errors
stay local, and the memory model's type-driven decisions are never deferred. The cost is
that a value with *no* way to determine its type is rejected rather than guessed — a bare
`x := None` or `x := []` with no grounding use is a compile error; annotate it. Function
signatures are always explicit: they are the module's interface and the inference's seeds.
