# Maps

> **Memory:** Entries are deep-copied in and deep-copied out, the same cross-arena
> discipline as arrays. `m[k]` is a mutable place, and the in-place accumulator
> (`m[k] += 1`) is covered by the unique-ownership analysis.

A map associates keys with values: `[K: V]`. The key type `K` is `string`, `int`, or a
hashable composite (see [Keys](#keys)); the value type `V` is *any* type. Like everything
in Tycho, a map is a value — assigning one deep-copies it, two maps compare entry-wise with
`==`, and a map crosses a function boundary as a read-only borrow unless taken `inout`.

```tycho
counts := ["ada": 1, "alan": 2]   # a [string: int], typed from the literal
_empty := []string: int            # an empty map (key and value types required)

counts["grace"] = 5               # add or overwrite
_has := "ada" in counts            # membership test -> bool
_n := len(counts)                  # entry count -> int
delete counts["alan"]             # remove a key (a no-op if absent)
```

The type follows from the literal or an annotation: `["a": 1]` is `[string: int]`,
`["a": "b"]` is `[string: string]`, `[1: 2]` is `[int: int]`.

## `m[k]` is a place

`m[k]` is not just a read — it is a **place** you can write through, and writing to a
missing key inserts the value type's zero first. So the common accumulator patterns are each
one line:

```tycho
counts := []string: int
counts["ada"] += 1                # count occurrences (zero-initialized on first sight)
index := ["tycho": [1]]
push(index["tycho"], 2)           # grow a [string: [int]] value in place
```

Read as an rvalue, `m[k]` returns the value **by copy** and never inserts: a scalar value
returns the value type's zero for a missing key, a composite value returns a deep copy. When
you need a non-zero default for a missing key, `m.get(k, default)` returns `default` on a
miss — the same read, spelled as a method. `m.get(k)` (no default) is exactly `m[k]`. Every
map operation is now operator, keyword, or method syntax (`m[k]`, `m.get`, `in`, `delete`,
`len`, `keys`) — no snake_case map functions:

```tycho
counts := ["ada": 1]
counts["ada"] = counts.get("ada", 0) + 1   # equivalent to counts["ada"] += 1
```

That accumulator looks like it rebuilds the map every step — `counts.get` then a store — but
because value semantics proves `counts` is uniquely owned at that point, the compiler
mutates it in place. The loop is **O(n) total**, the same in-place trick as string append.

## Iterating

`keys(m)` returns the live keys as an array (`[string]` or `[int]`, matching `K`) in
**insertion order** — the order keys were first inserted. Iterate that to walk the map;
`k in m` only tests membership, it does not iterate:

```tycho
counts := ["ada": 1, "alan": 2]
for k in keys(counts):
    println(k + " = " + str(counts[k]))
```

## Values are deep, maps are values

The value type may be anything — `[string: Point]`, `[string: [int]]`, `[int: [int]]` — and
the value is deep-copied in and out like any other heap value, so a stored array or struct
is independent of the one you put in. Maps themselves are values too: `b := counts` is a
deep copy, `==`/`!=` compare entry-wise (`a == b` exactly when `b` is an independent copy of
`a`), a `[string: int]` parameter is a read-only borrow (mutating it is a compile error —
copy it first, or take it `inout`), and a returned map is promoted into the caller's arena. An
`inout [string: int]` lets a callee share and mutate the caller's map in place — a counter
threaded through calls — exactly like a `inout` array.

## Keys

Beyond `string` and `int`, a key may be any of:

- a **newtype** over `string` or `int` (`[UserId: int]`) — the map carries the declared key
  type, so a raw base value is rejected; `keys()` returns the wrapped `[UserId]`.
- a **fieldless enum** (`[Color: int]`) — stored and hashed by its tag, deterministic and
  never pointer-dependent; `keys()` rebuilds the wrapped variants. (An enum with a payload
  variant is rejected: equal tags would not imply equal values.)
- a **struct, tuple, or array** (`[Point: int]`, `[(int, string): V]`, `[[int]: V]`) —
  stored inline and hashed deeply over its fields, so equal values are equal keys.
  `m[Point(1, 2)]`, `m[(1, "a")]`, and `m[[1, 2, 3]]` each name a stable entry.

Composite keys work in the literal form too (`[Point(1, 2): 10]`). Any key whose leaves are
all hashable works; a key carrying a non-hashable leaf (a function value, a handle) is
rejected. *Not yet:* a map as a map's key.

---

*Design background:* why an in-place `m[k]` mutation stays value-safe, and how heap-valued
entries are kept alive, is in [the map-values and map-mutation design notes](../reference/maps.md).

---

## Values in a map

A Tycho map `[K: V]` can hold *any* value type: `[string: string]`,
`[string: Point]`, `[int: [int]]`, even a nested map `[string: [string: int]]`.
This note explains how that works under value semantics and implicit arenas,
and why the map runtime doesn't need any `$T` generics of its own to support any
value type (monomorphization handles it). The user-facing surface is the
[Maps reference](../reference/maps.md); in-place mutation of a
value (`push(m[k], v)`, `m[k] += 1`) has its own note,
[map-mutation.md](maps.md).

### How any value type works without generics

Tycho monomorphizes its containers instead of boxing (and user `$T` generics are
themselves erased to concrete code — see [generics.md](generics.md)), so the
value type can't be abstracted at runtime. Maps use the same answer arrays use
for `[Struct]` and `[[T]]`: **monomorphization**. The transpiler generates one
concrete map runtime per `(key, value)` pair the program actually uses, named
after that pair.

A program using `[string: Point]` and `[int: [int]]` gets exactly two map
runtimes; one that uses neither pays for neither. There's no boxing, no tag, no
dynamic dispatch — a `[string: Point]` stores `Point`s inline, the same layout a
hand-written map would have.

### Heap-value lifetimes

The model is value semantics: a map *owns* its entries, and every value that
crosses the map boundary gets deep-copied — the same rule that governs every other
heap value in the language. For a value type that owns heap bytes (a `string`, a
struct with a `string` field, an array, another map) there are two copy
directions:

- **In** (an `m[k] = v` store or a map literal): the value is deep-copied into
  the *map's* arena, so it lives as long as the map and is independent of the
  source variable. Mutating the original afterwards never touches the stored
  copy.
- **Out** (`m.get`): the stored value lives in the map's arena, which may be a
  borrow that is freed before the result is used. So `m.get` returns an
  **independent deep copy** into the caller's arena — it survives later inserts
  that rehash the table, and it survives the map itself being freed. The default
  argument is heap-copied the same way.

Because both directions copy, value semantics holds end to end:

```tycho
fn use(n: int):
    println(str(n))

m := ["a": 1]
default := 0
other := 2

v := m.get("a", default)
m["a"] = other                 # rehashes, moves slots
use(v)                         # still the old value — v is its own copy
```

Copying a whole map (`b := m`) deep-copies every value, so the two maps share no
storage. `==` is deep, entry-wise value equality, independent of insertion
order. These fall out of the copy-in/copy-out discipline; they aren't special
cases.

### What it covers

For both key kinds — `[string: V]` and `[int: V]` — the value `V` may be:

- a scalar (`int`, `float`, `string`, `bool`),
- a struct (`[string: Point]`),
- an array (`[int: [int]]`),
- a nested map (`[string: [string: int]]`).

All operations work uniformly regardless of `V`:
`m[k] = v` / `m.get` / `k in m` / `delete m[k]` / `keys` / `len`, deep value
`==`, the in-place accumulator, and `inout`. `m.get`'s default and the stored
value take `V`; the key takes `K`. The key `K` is `string`, `int`, a newtype over
either, a fieldless enum, or any **hashable composite** (struct/tuple/array, hashed
deeply over its fields); only a map itself is not yet usable as a key (see the
[Maps](../reference/maps.md) reference page).

### Implementation notes

These are for contributors; users only need the surface above.

---

## Mutating through a map

Once a map can hold any value type ([map-values.md](maps.md)), the next
question is how to *grow* one of those values without copying it. This note
covers `m[k]` — indexing a map both as a write target and as a read — and why it
stays sound with no borrow checker. The user-facing map surface is the
[Maps reference](../reference/maps.md).

### Why in-place mutation

With value semantics, changing a stored composite value would otherwise mean
reading it out, mutating the copy, and reinserting it — and the reinsert copies
the whole value every time. For a `[string: [int]]` inverted index that's O(n)
per append, quadratic over a build. Instead, `m[k]` names the value's storage
slot directly, so the mutation happens *in place*:

```tycho
idx := []string: [int]
term := "arena"
doc := 7

push(idx[term], doc)                  # grow the value array in its own slot
```

That turns `[string: [int]]` into a real inverted index with no side array.

### The two roles of `m[k]`

`m[k]` means different things on the two sides of an assignment, and the
distinction is deliberate.

**As a place** (the target of a write, compound, or projection) it resolves to
the value's slot in the map and mutates it in place:

```tycho
m := []string: [int]
k := "row"
v := 1
i := 0
x := 9

m[k] = [v]           # plain store of a (heap-copied) value
push(m[k], v)        # grow an array-valued slot
m[k][i] = x          # write into a nested array value
```

If the key is absent, the slot is **auto-inserted with the value type's zero**
and then projected into — the same convenience as C++'s `operator[]`. The zero
is whatever an empty of that type is: `0` / `0.0` / `""` / `false` for a scalar,
an empty array, or a zeroed struct. So `push(m["new"], 1)` on a fresh map
creates the empty `[int]` and then pushes; `m["new"] += 1` starts from `0`.

**As a read** (an rvalue, for a scalar value type) it's a **pure** lookup that
returns the value's zero on a missing key and **never inserts** — no
hidden write lurking in a read. That's what makes the counter idiom read
naturally:

```tycho
words := ["ada", "alan", "ada"]
cnt := []string: int
for w in words:
    cnt[w] = cnt[w] + 1     # read cnt[w] (0 if absent), store back
_miss := cnt["absent"]       # 0, and "absent" is NOT added to the map
```

A composite (array/struct/map) value isn't returned by a bare read — use
`m.get(k, default)` for those, so the copy-out is explicit. (The compound
forms above, e.g. `cnt[w] += 1`, are the more direct way to write the counter.)

### Why it can't dangle

`m[k]` as a place is a **transient**: Tycho has no way to take a reference to a
value, so `m[k]` can only show up *as* (or *inside*) a single mutation target,
and it can't be stored, bound, or carried past that one statement. That one
property is what makes it sound:

- The find-or-insert that locates the slot may rehash the table, but it does so
  *before* handing back the slot. The mutation that follows runs with no intervening
  map operation, so the slot can't move under it.
- `push(m[k], v)` grows the *value's* buffer, not the map's table, so it
  triggers no rehash of the map.
- Value semantics already proves the slot is the sole owner of its value
  (nothing else aliases it), so mutating it in place is invisible to everyone
  else — `cp := m; cp[k] = x` leaves `m` untouched, because `cp` took its own
  deep copy.

A key expression that has side effects is evaluated exactly once per statement,
even for a compound op that reads and writes the same slot:

```tycho
fn next_key() -> string:
    return "k"

tally := []string: int
tally[next_key()] += 1     # next_key() runs once, not twice
```

`reserve(m[k], n)` reaches a map's array-value slot too, so a posting list can
be pre-sized.

### Example

A `[K: [K2: V]]` accumulator is one statement. The inner map is created on first
touch, so there is no initialisation step and no extract-mutate-reassign:

```tycho
counts := []string: [string: int]
counts["the"]["fox"] += 1
counts["the"]["fox"] += 1
println(str(counts["the"]["fox"]))
```

```output
2
```

The test suite hits every form: a `[string: [int]]` index built with
`push(m[k], v)`, a nested `m[k][i] = x`, an `[int: int]` counter with `m[k] += 1`
from a zero start, a struct value via `m[k].field = x`, single-eval of a
call-bearing key, the value-semantics check that a copied map's in-place mutation
leaves the original alone, and the pure read across scalar value types and both
key kinds.

### Implementation notes

For contributors. `m[k]` as a place lowers to a slot-pointer accessor —
`*tycho_mapc<id>_slotptr(owner, &m, k)` — that does the find-or-insert (setting
the zero value on insert) and returns the slot address. The existing place
machinery (assignment, index-set, field-set, `push`/`reserve`/`pop`) reuses that
accessor through the recursive place-lowering pass, so the deeper chains
(`m[k].field`, `m[k][i]`) compose for free. A compound op single-evaluates the
slot pointer and hoists a call-bearing key to a temporary, reusing the same
double-eval guard the compound array-index path already had. A scalar rvalue
read lowers to a plain `m.get` with the type's zero as the default.
