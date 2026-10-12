# Maps

> **Memory:** maps own their entries. Storing a value copies it in;
> reading one copies it out. `m[k] += 1` and `push(m[k], v)` update
> in place.

A map associates keys with values: `[K: V]`. The key type `K` is `string`, `int`, or a
hashable composite (see [Keys](#keys)); the value type `V` is *any* type. Like everything
in Tycho, a map is a value — assigning one copies it, two maps compare entry-wise with
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
map operation is operator, keyword, or method syntax (`m[k]`, `m.get`, `in`, `delete`,
`len`, `keys`):

```tycho
counts := ["ada": 1]
counts["ada"] = counts.get("ada", 0) + 1   # equivalent to counts["ada"] += 1
```

That accumulator looks like it rebuilds the map every step — `counts.get` then a store — but
the compiler updates it in place. The loop stays **O(n) total**, the same trick as string append.

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
the stored value is independent of the one you put in. Maps themselves are values too: `b := counts` is a
copy, `==`/`!=` compare entry-wise, a `[string: int]` parameter is a read-only borrow (copy it first, or take it `inout`), and a returned map lives as long as the caller needs it. An
`inout [string: int]` lets a callee update the caller's map in place — a counter
threaded through calls — just like an `inout` array.

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
rejected. A map is not yet usable as a key.

---

How this is implemented — monomorphized runtimes, copy-in/copy-out,
slot-pointer lowering — is in [design-maps](../internals/design-maps.md),
for maintainers.
