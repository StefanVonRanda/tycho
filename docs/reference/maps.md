# Maps

> **Thesis context:** Maps test that the arena model works for associative containers with
> in-place value mutation. `m[k]` as a mutable place exercises the same cross-arena copy
> discipline as arrays — entries deep-copied in, deep-copied out — while the in-place
> accumulator (`m[k] += 1`) tests that unique-ownership analysis applies to map slots.

A map associates keys with values: `[K: V]`. The key type `K` is `string`, `int`, or a
hashable composite (see [Keys](#keys)); the value type `V` is *any* type. Like everything
in Tycho, a map is a value — assigning one deep-copies it, two maps compare entry-wise with
`==`, and a map crosses a function boundary as a read-only borrow unless taken `inout`.

```
counts := ["ada": 1, "alan": 2]   # a [string: int], typed from the literal
empty := []string: int            # an empty map (key and value types required)

counts["grace"] = 5               # add or overwrite
"ada" in counts                   # membership test -> bool
len(counts)                       # entry count -> int
delete counts["alan"]             # remove a key (a no-op if absent)
```

The type follows from the literal or an annotation: `["a": 1]` is `[string: int]`,
`["a": "b"]` is `[string: string]`, `[1: 2]` is `[int: int]`.

## `m[k]` is a place

`m[k]` is not just a read — it is a **place** you can write through, and writing to a
missing key inserts the value type's zero first. So the common accumulator patterns are each
one line:

```
counts[w] += 1                    # count occurrences (zero-initialized on first sight)
push(index[term], doc)            # grow a [string: [int]] value in place
totals[user].balance = 0          # mutate a struct-valued entry's field
```

Read as an rvalue, `m[k]` returns the value **by copy** and never inserts: a scalar value
returns the value type's zero for a missing key, a composite value returns a deep copy. When
you need a non-zero default for a missing key, `map_get(m, k, default)` is the one remaining
map *function* — everything else (`m[k]`, `in`, `delete`, `len`, `keys`) is operator or
keyword syntax:

```
counts[w] = map_get(counts, w, 0) + 1    # equivalent to counts[w] += 1
```

That accumulator looks like it rebuilds the map every step — `map_get` then a store — but
because value semantics proves `counts` is uniquely owned at that point, the compiler
mutates it in place. The loop is **O(n) total**, the same in-place trick as string append.

## Iterating

`keys(m)` returns the live keys as an array (`[string]` or `[int]`, matching `K`) in
**insertion order** — the order keys were first inserted. Iterate that to walk the map;
`k in m` only tests membership, it does not iterate:

```
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
entries are kept alive, is in [the map-values and map-mutation design notes](../map-values.md).
