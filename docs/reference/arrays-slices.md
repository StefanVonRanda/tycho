# Arrays and slices

> **Memory:** Every `push` copies the element into the array's arena; every `return`
> copies the whole array into the caller's arena. A slice (`xs[a:b]`) is a non-storable
> view — passed as a zero-copy argument, deep-copied on store.

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

An out-of-bounds index aborts with a message rather than reading stray memory. This check is
load-bearing, not belt-and-suspenders: because every value lives in one large arena block, an
out-of-bounds read usually lands on *other live data* in the same block instead of an unmapped
page, so the OS would never fault on it — the corruption would just be silent. The runtime
bounds check is what turns that into a clean abort, which is why it is always on, in both
compilers. Assignment is a **deep** copy: copying a `[string]`, `[Point]`, or `[[int]]` duplicates the element
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
than rebuilding the whole element. This is a *projection*: the transpiler hands you the
element's slot in the backing buffer, bounds-checked, with no pointer ever exposed in Tycho.

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

Each distinct length used is monomorphized into its own instance (just like a `$T` type
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

Reach for it when a count is small and bounded and you want the storage inline — no arena
allocation, and the whole thing copies with its owner. `tools/tycho-grid/` uses one.

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

Affine types stay out, as everywhere else: no handle, `Task(T)` or `Channel(T)` as a field of
an `soa` element. `tools/tycho-sim/` uses one for its entity pools, which is the shape it is
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

Passing a slice to a function that only reads its parameter **costs nothing** — the
descriptor `{ data + a, b - a }` points into `xs`'s buffer, the same borrow an ordinary
array argument already is. But the moment you **store** a slice, **return** it, or **push**
it somewhere, it deep-copies into an owning array, so value semantics still holds: mutating
`xs` afterward never touches the stored copy. That keeps the view non-storable — it can
never outlive or alias the buffer it came from — without any borrow checker. Slices work on
every array type and compose (`xs[1:5][1:3]`). One rule to remember: you cannot pass a slice
of `xs` and a `inout` of `xs` to the same call, since the `inout` could reallocate the viewed buffer.
A string slice `s[a:b]` works too — with the same `s[a:]` / `s[:b]` / `s[:]` forms — but unlike
an array view it **always copies** into a fresh substring (there is no zero-copy string view);
[`substr(s, a, b)`](builtins.md) is the equivalent function form.

---

*Design background:* why value-semantic aggregates stay sound — copied deeply, projected
in place, never aliased — with no borrow checker or whole-program alias analysis, is in
[the aggregates design note](../reference/arrays-slices.md).

---

## Working with arrays and structs

Tycho is an experimental, value-semantic language with implicit hierarchical
arenas and **no pointer or reference type**. This document walks through how
aggregates — arrays and structs, including heap-bearing and nested fields —
behave under those rules, and why I think the memory model stays sound without a
borrow checker or whole-program alias analysis.

The question it answers: *do implicit arenas still hold once the
language has arrays and structs, given strict value semantics and no pointer
type?* I believe the answer is **yes**, subject to five invariants and three honest
costs, both listed below. Two transpiler optimizations (return-slot move,
in-place append) keep the costs from biting in practice; see
[thesis.md](../thesis.md) §4. Arrays, structs, nested struct fields, structural
equality, and `inout` all ship as described — see the [README](../README.md)
and [thesis.md](../thesis.md).

### 1. The model in one paragraph

Every value is a **wholly-owned tree**: a struct owns its fields, an array
owns its elements, recursively down to leaves (`int`, `bool`, `string`
bytes). A value and *all* its interior storage live in exactly **one
arena** — the arena of the variable (slot) that owns the root. There is
**no pointer/reference type**: you cannot name, store, or return a
reference to a value's interior. Every operation that would create a
second name for the same storage instead **copies** (deep, by default).
Mutation happens in place through `inout` parameters, which are an
exclusive borrow for the duration of a call and cannot be stored.

### 2. Types & syntax

```tycho
# arrays — homogeneous, growable
xs := [1, 2, 3]          # [int] inferred
ys := []string            # empty; element type required when empty
first := xs[0]             # index read
xs[0] = 9                 # index write (in place)
n := len(xs)               # length
push(xs, 4)               # append in place  (xs is inout)
last := pop(xs)            # remove last, returns it
println(str(first) + " " + str(n) + " " + str(last) + " " + str(len(ys)))

# structs — nominal, fields are values
struct Point:
    x: int
    y: int

p := Point(1, 2)           # positional construction, fields in declaration order
px := p.x                   # field read
p.y = 5                    # field write (in place)
println(str(px) + " " + str(p.y))

# nesting is just trees of values
struct Line:
    a: Point
    b: Point
    labels: [string]       # a field that is an array, etc.
```

No `null`. "Maybe a value" needs an optional/sum type (see §7); that is a
companion feature, not part of this spec, but the model assumes it exists.

### 3. The five invariants that keep it sound

1. **Single-arena ownership.** A value's root and every byte it
   transitively owns are allocated in one arena — the owner slot's arena.
   `acc.items`'s backing buffer lives wherever `acc` lives.
2. **No aliasing.** `b := a`, passing by value, and `field = v` all
   *copy*. After `b := a`, `a` and `b` share nothing.
3. **Mutation via `inout` only.** `fn f(inout xs: [int])` borrows the
   caller's value exclusively for the call and writes through it. The
   value never changes arena. The borrow is *not a value* — it can't be
   assigned, stored in a field, or returned.
4. **No reference type.** There is nothing whose value is "the address of
   another value." This is the keystone: it is *why* invariant 3's borrow
   can't escape, and why no operation can manufacture a lasting second
   reference.
5. **Cross-arena moves are deep copies.** Returning a value, or assigning
   to a variable in an outer scope, copies the whole tree into the
   destination arena. Correctness never depends on analysis — only speed
   does (see §5).

### 4. Arena mechanics, worked

```tycho
fn read_lines() -> [string]:
    lines := []string
    line := input()
    for line != "":
        push(lines, line)     # 'line' (scratch) is COPIED into lines' buffer
        line = input()
    return lines              # whole array deep-copied into caller's arena

fn main():
    ls := read_lines()         # ls owns its buffer + string bytes, in main's arena
    println(str(len(ls)))
```

- `push` grows `lines`' backing buffer **in `lines`' arena** (geometric
  growth; old buffers are wasted within the arena — bounded, reclaimed
  when the arena ends).
- `line` lives in the loop's scratch arena, reset each iteration. `push`
  copies its bytes into `lines`' buffer, so resetting scratch is safe.
- `return lines` deep-copies the array into the caller's arena, then
  `read_lines`' arena is freed. The returned value is fully independent.

```tycho
fn main():
    acc := []string                 # in main's arena
    for i := 0; i < 1000000; i += 1:   # no digit separators: `1_000_000` does not lex
        push(acc, str(i))           # str(i) temp in scratch; copied into acc
    # scratch stays bounded; acc grows O(n) — intended, not a leak
```

### 5. The three honest costs

1. **Deep copy on cross-arena move.** Returning or out-assigning a big
   value is O(size). Mitigated, never for correctness, by:
   - *Build-in-destination*: if a local is returned, the compiler
     allocates it in the parent arena from the start → the return needs no copy.
     (The leaf version applies today: a return expression targets the
     parent arena.) It stays conservative when a value *may* be returned on some
     path; still sound, still local.
   - *Move on last use*: `b := a` / `b = a` where `a` is a
     uniquely-owned local read exactly once (so this is its last use on every
     path), not inside a loop, and in the same arena as the destination, hands
     off `a`'s buffer instead of deep-copying it. Conservative and static; a
     parameter (which borrows the caller's buffer) is never moved.
   - *Borrow on read*: a by-value parameter the callee only reads is
     passed as a transient borrow (zero copy) — safe because the borrow
     can't be stored (invariant 4) and the caller is suspended.
   The important part: in this model escape analysis is just an **optimization**.
   In a pointer-having language the same analysis is a **soundness
   requirement**. That's why value semantics is viable here.

2. **No recursive types.** `struct Node: next: Node` is infinite-size and
   illegal — there is no pointer to break the cycle. Trees, lists, and
   graphs are expressed as **arrays + integer indices** (a node holds an
   `int` index into a `[Node]`, not a reference). The entire graph is then
   *one value* (the array) with *one* lifetime — which is an excellent fit
   for arenas, and a well-trodden data-oriented pattern. The cost is
   ergonomic: you write `nodes[i].next_idx` instead of `node.next`, and
   you need a sentinel or optional for "no node." A generational-handle
   scheme (a pattern you build, not a language built-in) keeps it safe:
   make handles **generational** (index + a generation
   counter; a stale handle fails its generation check instead of silently
   reading a recycled slot). One extra word per handle buys use-after-free
   *detection* for the index-as-pointer pattern, with no pointer type.

3. **Idiomatic building has to be in place.** The immutable form
   `total = total + str(i)` in a loop leaves dead intermediate buffers in
   the arena (bounded by scope, but wasteful). The form to reach for is
   in-place growth — `push(builder, str(i))` / `inout` — which is
   geometric and tight. "One right way" should make in-place building the
   one way; immutable rebuild stays *correct* but is the slow path.

### 6. Aliasing and lifetime hazards, by scenario

| Scenario | Aliasing/lifetime hazard? | Resolution |
| --- | --- | --- |
| `b := a` (array/struct) | none | deep copy; independent |
| pass by value, read-only | transient borrow aliases source | safe: borrow can't be stored/returned (inv. 4); caller suspended |
| `inout` mutate | exclusive borrow | safe: in place, same arena, borrow non-storable |
| build in loop, return | value escapes scope | deep copy up, or build-in-parent (opt.) |
| push scratch value into outer array | element outlives iteration | safe: push *copies* into outer arena |
| `a[i] = someString` | old element becomes garbage | sound; wasted-in-arena, bounded by `a`'s life |
| array slice `xs[a:b]` | view aliases parent buffer | safe: a view only as a read-only arg (zero-copy borrow, like any array param); storing/returning/pushing it deep-copies, so it is non-storable. A slice + a `inout` of the same var in one call is rejected. |
| substring | view would alias parent buffer | **substr is a copy** (owns its bytes); no aliasing string views (a string slice can't be NUL-terminated) |
| recursive/graph types | would need pointers | not allowed; use array + indices |
| return a reference to a local | classic dangling | **inexpressible** — no reference type exists |

The last row is the key point: the dangling-pointer bug that forces every other
region system to ship escape analysis *for correctness* isn't expressible in
Tycho, so it cannot occur.

### 7. Generics — monomorphized over the built-in container machinery

Tycho has **monomorphized generics**: a `$T` type parameter on a
function or struct, inferred at the use site and stamped out to concrete code at
compile time. They reuse the *same* per-concrete-type interning + emission the
transpiler already runs for its built-in parametric types:

- `[T]` — growable array, any element type T (incl. structs, strings).
- `Option(T)` / `Result(T, E)` — optional / fallible, since there is no `null`
  and no exceptions.
- `[K: V]` — hash map; keys are `string`, `int`, a newtype over either, a fieldless enum, or any hashable composite (struct/tuple/array).

```tycho
fn first(xs: [$T]) -> Option(T):        # generic function
    if len(xs) > 0:
        return Some(xs[0])
    return None

struct Box($T):                          # generic struct
    v: T
```

You can write `struct Box($T)` and `fn id(x: $T) -> T`. The monomorphization
engine such generics need is the same one that stamps out `Option(int)` versus
`Option(string)`. User `$T` definitions reuse it directly rather than adding a
new subsystem, and stay memory-model-neutral (§9).
The full design is in [generics.md](generics.md).

The full set is available: generic functions, generic structs
(construction + `Box(int)` type-position annotations), structured patterns
(`[$T]` → `Option(T)`), `[$K: $V]` map patterns, `where` constraints
(`numeric` / `comparable` / `has_str`), and explicit call-site type args
(`empty$(int)` for the non-inferable case).

Companion features that work alongside generics (none of these is generics):

- **`inout` parameters** (exclusive mutable borrow) — efficient mutation
  without copies or aliasing.
- **`Option(T)` + exhaustive `match`** — no `null`.
- **Slices** (`xs[a:b]`) — a non-storable view (zero-copy when passed
  to a read-only param, deep-copied when stored), no borrow checker needed.
- **`distinct` newtypes** (`type Meters = float`) — a distinct,
  zero-cost type over `int`/`float` (same C rep, type-incompatible with the base
  and other newtypes); arithmetic/ordering/`str` only between the same newtype.

### 8. Allocation strategy: signature-directed escape

One established way to run arenas is to pass each function an implicit arena
parameter and have the callee allocate into the *caller's* arena, so a
returned value is already in the right place — **zero-copy returns**. The
price: a function's throwaway temporaries also live in the caller's arena
until the caller returns, so you have to claw reclamation back with *visible*
tools — named sub-arena blocks and pools. That works, but it puts memory
back in the programmer's face, the opposite of what I want for Tycho.

Tycho can do better **because it has no pointers**. With value semantics and
no reference type, a value escapes a function only by being **returned** or
written through an **`inout`** parameter — both visible in the signature. A
callee cannot stash an argument anywhere that outlives the call (there is
nothing to stash it in). Therefore **escape is decided locally, from
signatures — no whole-program may-alias analysis**. Concretely:

- allocations that flow into the return value → emit in the caller's arena:
  zero-copy return;
- every other allocation in the call → emit in an auto-created sub-arena
  freed at scope exit: tight reclamation, invisibly;
- loop bodies → non-escaping allocations go in the per-iteration scratch
  (reset each iteration); an escaping `push` targets the destination's arena.

This is the synthesis: zero-copy returns *and* per-scope reclamation, with
**no visible memory constructs at all**. It is sound by construction
because, under value semantics, a wrong escape decision can only change
*when* memory is freed, never *whether* a pointer dangles. In a
pointer-having language the same analysis is a correctness obligation with
alias tracking — which is exactly why such languages reach for explicit
tools instead. The no-pointer rule turns that hardest problem into a local
optimization.

### 9. Why generics don't weaken the model

The arena model holds, and **adding generics does not change that**: each `$T`
instantiation is monomorphized to concrete, value-semantic code *before* the
signature-directed escape analysis (§8) runs, so it is ordinary concrete code
with the same locally-decidable lifetimes — nothing generic survives to analyze.
Correctness still rests entirely on **"no reference type + copy on cross-arena
move."** What grows is a substitution pass feeding the *same* per-type
emission the built-in containers already use — not a new generics engine, and
not the memory model.

That keeps every lifetime question locally decidable from signatures, which
is exactly what lets the arenas stay invisible *and* lets the
signature-directed escape strategy (§8) reclaim memory without copies. Arena
allocation itself is well-proven; what Tycho is testing is whether removing pointers
turns the visible memory tools and whole-program analyses such systems usually
need into invisible, local ones — and generics, being monomorphized to concrete
code first, ride along without re-introducing either.
