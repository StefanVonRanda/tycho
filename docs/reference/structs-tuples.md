# Structs and tuples

> **Memory:** A struct with heap-bearing fields (strings, arrays) is deep-copied on a
> cross-arena move by the recursive `copy_into`, at every nesting level. Tuples
> participate in the same return-slot optimization as single values.

Both are product types — a value made of several fields. A `struct` names its fields and is
declared as a type; a tuple is anonymous and is how a function returns more than one value.
Both are values, deep-copied on every bind, so two never share storage.

## Structs

```tycho
struct Point:
    x: int
    y: int

struct Rect:
    lo: Point
    hi: Point

fn area(r: Rect) -> int:
    return (r.hi.x - r.lo.x) * (r.hi.y - r.lo.y)

fn main():
    a := Point(1, 2)            # positional construction, fields in declaration order
    r := Rect(a, Point(4, 6))
    r.lo.x = 100               # nested field write, in place
```

A field name follows the same rule as any other name: it may not be a
[reserved word](basics.md#declarations-and-assignment), which includes the type names —
`bytes: int` or `string: int` is refused.

A field may be `int`, `float`, `bool`, `string`, an array (including an array of structs —
even of the struct being defined, so `children: [Node]` builds a recursive tree), an
`Option` (a nullable field, `age: Option(int)`), or another struct. A field that would make
the struct infinitely large *by value* — `next: Option(Node)` directly inside `Node` — is a
compile error; use indirection through an array (`[Node]`), whose elements are arena-allocated.

Structs are values, and the copy is **deep**: a field that owns heap bytes (a `string` or
array, at any nesting depth) is duplicated too, so copying a struct copies its whole tree and
two struct variables never share storage.

```tycho
struct Person:
    name: string
    tags: [string]

a := Person("Ada", ["x"])
b := a                         # deep copy: b.name and b.tags are independent
b.name = "Alan"                # never touches a
```

Fields are read with `p.x` and written with `p.x = v`, including nested (`r.lo.x = v`) and an
array-field element (`p.tags[0] = v`). Construction is positional, in declaration order, and
a struct must be declared before it is used as a type. Two structs compare by value with
`==`/`!=` — field-wise, recursing into nested structs, arrays, and strings — so `a == b` is
true exactly when `b` is an independent copy of `a`. A struct may be a "method" receiver; see
[Functions](functions.md#methods-ufcs).

### Layout: `packed` and `align(N)`

An ordinary struct's layout is up to the implementation. Two declaration attributes pin it.

**`packed struct`** removes all padding: its size is exactly the sum of its field sizes, each
field sits right after the one before, and it is the one aggregate that converts to and from
`bytes`, little-endian, for file formats and wire protocols.

```tycho
packed struct Ins:          # exactly 9 bytes: 1 + 4 + 4
    op: u8
    a: i32
    b: i32

fn main():
    i := Ins(to_u8(3), to_i32(-1), to_i32(7))
    raw := to_bytes(i)
    back := from_bytes$(Ins)(raw)
    println(str(size_of$(Ins)) + " " + str(len(raw)) + " " + str(back.a))
```

```output
9 9 -1
```

Every field must be fixed-width: `int`, `float`, `bool`, `char`, a sized numeric
(`u8`…`u64`, `i8`…`i64`, `f32`), a newtype over one, or another packed struct. A `string`,
array, map, `Option` or other heap-owning field is refused. `from_bytes$` aborts unless the
length is exactly `size_of$(T)`; see [builtins](builtins.md).

**`align(N) struct`** raises the struct's alignment to `N` bytes (a power of two, at most 8,
the arena's own alignment). Its size rounds up to a multiple of `N`, so every element of an
array of it stays aligned. It never lowers alignment, and `packed` and `align` on one struct
are refused.

`align(N)` cannot be observed from inside Tycho: `size_of$` only accepts a packed struct.
It matters at the C boundary, and checking it means reading the output of
`tychoc --emit-c`.

Worked examples: `BmpHeader` in [`corelib/raster/raster.ty`](../../corelib/raster/raster.ty) and
`TgaHeader` in [`tools/tycho-grade/`](../../tools/tycho-grade/main.ty). Neither uses `align(N)`, for
the reason above.

Rules: [spec §17.1a](../spec/12-aggregates.md#171a-packed-layout).

## Tuples and multiple return values

A tuple `(T1, ..., Tn)` (2–8 elements) is an anonymous product — the way a function returns
more than one thing. `return a, b` builds one, and you **destructure** it at the call:

```tycho
fn divmod(a: int, b: int) -> (int, int):
    q := a / b
    return q, a - q * b           # builds the tuple (q, remainder)

fn main():
    quot, rem := divmod(17, 5)     # destructure -> quot = 3, rem = 2
```

Tuples are first-class values, not only a return convention: store one whole
(`t := divmod(17, 5)`), index it by position (`t.0`, `t.1`), write a literal (`p := (10, 20)`),
pass it as an argument or a struct field, and compare two with `==` (element-wise). Any
element type works, including heap ones (`(string, [int])`); a tuple is deep-copied on bind
like everything else, so two are independent. Destructuring comes in two forms — `a, b := f()`
declares fresh locals, `a, b = f()` assigns into existing variables. A tuple element is also a
writable place: `t.0 = v` updates it in place, and value semantics is preserved — a copy taken
beforehand is unaffected.

---

*Design background:* if you want the why — why deep-copied aggregates stay sound, and why a
struct that would contain itself by value is rejected (use indirection through an array) — see
[the aggregates design note](../reference/arrays-slices.md).
