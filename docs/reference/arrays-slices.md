# Arrays and slices

> **Thesis context:** Arrays are the primary test of cross-arena deep copy for contiguous
> sequences. Every `push` copies the element into the array's arena; every `return` copies
> the whole array into the caller's arena. Slices (`xs[a:b]`) test that a non-storable view
> can exist without a borrow checker — passed as a zero-copy arg, deep-copied on store.

An array `[T]` is a growable, ordered sequence of values of one element type. Like every
Tycho value it has value semantics: assigning an array deep-copies it, so the copy and the
original never share storage. The element type `T` can be `int`, `float`, `string`, a
struct, or another array — nested as deep as you like.

```
xs := [10, 20, 30]      # literal
ys := []int             # empty (element type required)
push(xs, 40)            # append in place
last := pop(xs)         # remove and return the last element (aborts if empty)
len(xs)                 # length -> int
xs[0]                   # index read (bounds-checked)
xs[0] = 99              # index write (bounds-checked)
zs := xs                # value semantics: zs is an independent deep copy
```

An out-of-bounds index aborts with a message rather than reading stray memory. This check is
load-bearing, not belt-and-suspenders: because every value lives in one large arena block, an
out-of-bounds read usually lands on *other live data* in the same block instead of an unmapped
page, so the OS would never fault on it — the corruption would just be silent. The runtime
bounds check is what turns that into a clean abort, which is why it is always on, in both
compilers. Assignment is a **deep** copy: copying a `[string]`, `[Point]`, or `[[int]]` duplicates the element
bytes, nested structs, and inner buffers too, so mutating the copy never touches the
original.

```
ps := [Point(1, 2), Point(3, 4)]   # array of structs
push(ps, Point(5, 6))
total := ps[1].x + ps[1].y         # index, then read a field

grid := [][int]                    # array of arrays
push(grid, [1, 2, 3])
cell := grid[0][2]                 # 3
```

## Elements are places

A composite-array element is a **mutable place** — you can write through it in place rather
than rebuilding the whole element. This is a *projection*: the transpiler hands you the
element's slot in the backing buffer, bounds-checked, with no pointer ever exposed in Tycho.

```
ps[0].x = 10                       # a field of an element
push(ps[0].tags, "extra")          # grow an element's array field in place
grid[1][2] = 60                    # a nested-array element
bump(&ps[1].x)                     # an element field as a `mut` argument
```

Value semantics still holds: after `qs := ps`, mutating `ps[0].x` leaves `qs` untouched —
each owns its buffer. The element's owning array must be a mutable variable or field; you
cannot project through a read-only borrowed parameter.

## Crossing function boundaries

An array parameter is a **read-only borrow** — passed without a copy, but you may only read
it. Mutating a borrowed array (a `push` or an index-set) is a compile error; copy it first
(`b := a`) if you want a mutable local, or take the parameter `mut`. A returned array is
promoted into the caller's arena, so it never dangles.

```
fn make_squares(n: int) -> [int]:   # returned: promoted into the caller's arena
    r := []int
    for i in range(n):
        push(r, i * i)
    return r

fn sum(a: [int]) -> int:            # parameter: a read-only borrow
    total := 0
    for i in range(len(a)):
        total = total + a[i]
    return total
```

## Slices (`xs[a:b]`)

`xs[a:b]` is a sub-range of an array — `xs[a:]` runs to the end, `xs[:b]` from the start,
`xs[:]` is the whole thing — with every bound checked (`0 ≤ a ≤ b ≤ len`). A slice is an
ordinary array value, so its cost depends entirely on what you do with it:

```
xs := [10, 20, 30, 40, 50]
print(str(sum(xs[1:4])))      # passed to a read-only param: a ZERO-COPY view -> 90
mid := xs[1:4]                # stored: a deep copy, owning its own buffer
```

Passing a slice to a function that only reads its parameter **costs nothing** — the
descriptor `{ data + a, b - a }` points into `xs`'s buffer, the same borrow an ordinary
array argument already is. But the moment you **store** a slice, **return** it, or **push**
it somewhere, it deep-copies into an owning array, so value semantics still holds: mutating
`xs` afterward never touches the stored copy. That keeps the view non-storable — it can
never outlive or alias the buffer it came from — without any borrow checker. Slices work on
every array type and compose (`xs[1:5][1:3]`). One rule to remember: you cannot pass a slice
of `xs` and a `mut` of `xs` to the same call, since the `mut` could reallocate the viewed buffer.
A string slice `s[a:b]` works too — with the same `s[a:]` / `s[:b]` / `s[:]` forms — but unlike
an array view it **always copies** into a fresh substring (there is no zero-copy string view);
[`substr(s, a, b)`](builtins.md) is the equivalent function form.

---

*Design background:* why value-semantic aggregates stay sound — copied deeply, projected
in place, never aliased — with no borrow checker or whole-program alias analysis, is in
[the aggregates design note](../arrays-structs.md).
