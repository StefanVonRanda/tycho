# Arrays and slices

> **Memory:** Assigning an array copies it fully, so two variables never share
> storage. Passing an array to a function that only reads it costs nothing;
> storing a slice copies it.

An array `[T]` is a growable, ordered sequence of values of one element type. Like every
Tycho value it has value semantics: assigning an array deep-copies it, so the copy and the
original never share storage. The element type `T` can be `int`, `float`, `string`, a
struct, or another array — nested as deep as you like.

```tycho
xs := [10, 20, 30]      # literal
_ys := []int             # empty (element type required)
push(xs, 40)            # append in place
_last := pop(xs)         # remove and return the last element (aborts if empty)
len(xs)                 # length -> int
xs[0]                   # index read (bounds-checked)
xs[0] = 99              # index write (bounds-checked)
_zs := xs                # value semantics: zs is an independent deep copy
```

An out-of-bounds index aborts with a message rather than reading stray memory. The
check is always on. Assignment is a **deep** copy: copying a `[string]`, `[Point]`, or `[[int]]` duplicates the element
bytes, nested structs, and inner buffers too, so mutating the copy never touches the
original.

```tycho
struct Point:
    x: int
    y: int
    tags: [string]

fn bump(n: inout int):
    n = n + 1

ps := [Point(1, 2, []), Point(3, 4, [])]   # array of structs
push(ps, Point(5, 6, []))
_total := ps[1].x + ps[1].y         # index, then read a field

grid := [][int]                    # array of arrays
push(grid, [1, 2, 3])
push(grid, [4, 5, 6])
_cell := grid[0][2]                 # 3
```

## Elements are places

A composite-array element is a **mutable place** — you can write through it in place rather
than rebuilding the whole element. You get the element's place in the array,
bounds-checked; there is no pointer type in Tycho.

```tycho
struct Point:
    x: int
    y: int
    tags: [string]

fn bump(n: inout int):
    n = n + 1

ps := [Point(1, 2, []), Point(3, 4, [])]
grid := [[10, 20, 30], [40, 50, 60]]

ps[0].x = 10                       # a field of an element
push(ps[0].tags, "extra")          # grow an element's array field in place
grid[1][2] = 60                    # a nested-array element
bump(&ps[1].x)                     # an element field as a `inout` argument
```

Value semantics still holds: after `qs := ps`, mutating `ps[0].x` leaves `qs` untouched —
each owns its buffer. The element's owning array must be a mutable variable or field; you
cannot project through a read-only borrowed parameter.

## Crossing function boundaries

An array parameter is a **read-only borrow** — passed without a copy, but you may only read
it. Mutating a borrowed array (a `push` or an index-set) is a compile error; copy it first
(`b := a`) if you want a mutable local, or take the parameter `inout`. A returned array is
promoted into the caller's arena, so it never dangles.

```tycho
fn make_squares(n: int) -> [int]:   # returned: promoted into the caller's arena
    r := []int
    for i := 0; i < n; i += 1:
        push(r, i * i)
    return r

fn sum(a: [int]) -> int:            # parameter: a read-only borrow
    total := 0
    for i := 0; i < len(a); i += 1:
        total = total + a[i]
    return total
```

## Fixed-size arrays (`[N]T`)

`[N]T` is an array whose length `N` is fixed at compile time — an int literal (`[3]float`)
or an int `const` (`const W = 16` then `[W]int`). Unlike the dynamic `[T]` (a heap buffer
that grows with `push`), a `[N]T` is stored **inline** — no heap, no length header — and
copied **by value** (a plain memcpy for scalar elements, deep for heap elements):

```tycho
v: [3]int = [10, 20, 30]     # inline, exactly 3 elements
_w := v                        # a full value copy
v[0] = 99                     # w[0] is still 10 — no sharing
len(v)                        # 3, a compile-time constant (no runtime field)

fn dot(a: [3]float, b: [3]float) -> float:      # passed and returned by value
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
```

- The length is part of the **type**: `[3]int` and `[4]int` are different types, and a
  literal must have exactly `N` elements (`[3]int = [1, 2]` is a compile error). A bare
  `[1, 2, 3]` is a dynamic `[int]` unless the destination type is fixed, in which case it
  coerces (count and element types checked).
- Indexing is bounds-checked against the static `N`; `==` compares element-wise; a `[N]T`
  can be a struct field (stored inline), a by-value parameter, and a return value.
- Growth operations (`push`, `pop`, slices) do not apply — the size is fixed. Use a dynamic
  `[T]` when the length varies.

### Generic over size (`[$N]T`)

A function can be **generic over the length** of a fixed array. A parameter written `[$N]T`
introduces a size parameter `$N` that the call infers from the argument, and inside the body
`N` is an ordinary `int` constant equal to that length:

```tycho
fn sum(xs: [$N]int) -> int:      # N is inferred from the argument
    total := 0
    for i := 0; i < N; i += 1:   # N is a compile-time int in the body
        total = total + xs[i]
    return total

a: [3]int = [10, 20, 30]
b: [4]int = [1, 2, 3, 4]
print(str(sum(a)))               # 60   (one instance, N = 3)
print(str(sum(b)))               # 10   (a second instance, N = 4)
```

Each distinct length used compiles to its own specialized copy of the function (just like a `$T` type
parameter), so `sum(a)` and `sum(b)` compile to two specialized functions. Size and element
parameters compose: `[$N]$T` infers **both** the length and the element type. A `$N` repeated
across parameters must bind to one length — `fn dot(a: [$N]int, b: [$N]int)` requires its two
arguments to be the same size, checked at the call.

- The argument must be a **fixed** array — a `[3]int`, not a dynamic `[int]` (a dynamic array
  has no compile-time length to infer `N` from). A bare `[1, 2, 3]` literal is dynamic unless
  its destination type is fixed.
- `$N` is inferred from an argument, so a return-only `[$N]T` (no parameter mentions `N`) is a
  compile error — there is nothing to infer it from.
- A `[$N]T` is a template type: it is meaningful only as a function parameter, never in a
  stored position (a struct field, an enum payload, a newtype).

## Inline, fixed-capacity (`bounded[N]T`)

`bounded[N]T` holds **0 to N** elements stored *inside* the container — never in a separate
arena buffer — and is copied by value with whatever holds it. It differs from `[N]T` in
carrying a runtime count, and from `[T]` in that the count can never exceed the compile-time
capacity `N`. The capacity is part of the type: `bounded[4]int` and `bounded[8]int` are
distinct, and both differ from `[4]int` and `[int]`.

```tycho
fn main():
    b: bounded[4]int = [1, 2]      # an array literal initialises it
    push(b, 3)
    println(str(len(b)) + " " + str(b[2]))   # 3 3
```

- The capacity is a positive integer **literal** or the name of a positive `int` `const` —
  the same two spellings `[C]T` accepts. `0`, a negative, a non-`const` name, and a `const`
  that is not a positive integer are each rejected.
- The element type may not be `bool` or `void`, and may not be an affine handle (including
  `Task(T)` and `Channel(T)`). Everything else is allowed, including a nested `bounded[M]E`.
- An initialiser with more elements than the capacity is a **compile** error; a `push` past
  it is a clean runtime abort (`push to a full bounded[2]`), not a reallocation.

`pop` and slicing are **not** supported on a `bounded` — the refusals name the
workaround (`pop is not supported on a bounded[...] yet; read the last element via
len()-1 and rebuild`). A `bounded` is therefore a stack you push and index, not a
queue you can dequeue from the front.

Use it when a count is small and bounded and you want the storage inline — no separate
allocation, and the whole thing copies with its owner. `tools/tycho-grid/` uses one.

## SIMD vectors (`vector[N]T`)

`vector[N]T` is a `[N]T` whose arithmetic lowers to **one machine vector operation** instead
of a loop over its lanes. Everything else is the same as `[N]T`: an inline value, copied with
its owner, built from an array literal, indexed and index-assigned, `==` lane by lane, `len`
is `N`, and `str` prints it like an array. It can be a struct field.

```tycho
struct Particle:
    pos: vector[4]f32
    id: int

fn main():
    a: vector[4]f32 = [1.0, 2.0, 3.0, 4.0]
    b: vector[4]f32 = [10.0, 20.0, 30.0, 40.0]
    c := a * to_f32(2.0) + b          # one vector multiply, one vector add
    println(str(c))
    c.(0, 1) = c.(1, 0)               # swizzle: swap two lanes at once
    println(str(c.x) + " " + str(c.y) + " " + str(c.w))
    p := Particle(c, 7)
    p.pos.z = to_f32(0.5)
    println(str(p.pos))
```

```output
[12.0, 24.0, 36.0, 48.0]
24.0 12.0 48.0
[24.0, 12.0, 0.5, 48.0]
```

- `N` is a literal or an `int` `const`, a power of two from 2 to 64. `T` is `int`, `float`
  or `f32`. The count is part of the type, and `vector[4]f32` is a different type from
  `[4]f32`: the two cannot be mixed in one expression.
- Arithmetic is element-wise with the same operators as `[N]T`, including a scalar on
  either side. Integer `/` and `%` keep the divide-by-zero check and so run lane by lane.
- **Swizzling:** `v.(i, j)` names several lanes at once and assigns them simultaneously.
  A vector of up to four lanes also has `.x .y .z .w`, or `.r .g .b .a`.
- A vector wider than the machine's register still works, split across registers, and the
  compiler warns (`vector[4]float` is 32 bytes against a 16-byte baseline register).
  `vector[4]f32` fits one.
- `math.min`, `math.max` and `math.clamp` take a vector and work **lane-wise**, as one
  vector compare and blend per bound: `math.clamp(v, lo, hi)` with three `vector[4]f32`
  clamps all four lanes at once. All arguments are the same vector type; a scalar bound
  is refused, so write `lo` as a vector.

Worked example: [`tools/tycho-grade/`](../../tools/tycho-grade/main.ty) grades a TGA with one
`vector[4]f32` multiply-add and one `math.clamp` per pixel, and swaps BGRA↔RGBA with
`px.(r, b) = px.(b, r)`.

The exact rules are in [spec §5.3.11](../spec/03-types.md#5311-vectornt).

## Struct-of-arrays (`soa [T]`)

`soa [Point]` stores a struct's fields as **parallel arrays** rather than an array of
structs — the same values, transposed, so a pass touching one field walks contiguous memory.
It is indexed and appended like an array (`s[i].x`, `push(s, Point(1, 2))`); the transposition
is a representation choice the language makes for you, not a different API.

**The type and the empty literal are spelled differently** — the type is `soa [Point]`, the
empty value is `soa []Point`:

```tycho
struct Point:
    x: int
    y: int

fn main():
    s := soa []Point          # the empty literal: brackets first, then the type
    push(s, Point(1, 2))
    println(str(s[0].x) + " " + str(len(s)))   # 1 1
```

Handles, `Task(T)`, and `Channel(T)` cannot be fields of an `soa` element, as
elsewhere. `tools/tycho-sim/` uses one for its entity pools, which is the shape it is
for — many entities, a pass at a time over one field.

## Slices (`xs[a:b]`)

`xs[a:b]` is a sub-range of an array — `xs[a:]` runs to the end, `xs[:b]` from the start,
`xs[:]` is the whole thing — with every bound checked (`0 ≤ a ≤ b ≤ len`). A slice is an
ordinary array value, so its cost depends entirely on what you do with it:

```tycho
import "core:arrays"

xs := [10, 20, 30, 40, 50]
print(str(arrays.sum(xs[1:4])))   # passed to a read-only param: a ZERO-COPY view -> 90
_mid := xs[1:4]                    # stored: a deep copy, owning its own buffer
```

Passing a slice to a function that only reads its parameter **costs nothing** — it
reads directly from `xs`'s storage, the same borrow an ordinary
array argument already is. But the moment you **store** a slice, **return** it, or **push**
it somewhere, it deep-copies into an owning array, so value semantics still holds: mutating
`xs` afterward never touches the stored copy. A slice can never outlive the array it
came from. Slices work on
every array type and compose (`xs[1:5][1:3]`). One rule to remember: you cannot pass a slice
of `xs` and a `inout` of `xs` to the same call, since the `inout` could reallocate the viewed buffer.
A string slice `s[a:b]` works too — with the same `s[a:]` / `s[:b]` / `s[:]` forms — but unlike
an array view it **always copies** into a fresh substring (there is no zero-copy string view);
[`substr(s, a, b)`](builtins.md) is the equivalent function form.

---

*Design background:* why deep-copied aggregates stay sound with no borrow checker is in [the aggregates design note](../internals/design-aggregates.md).
