# Map design notes (maintainers)

Why the user-facing behavior in [reference/maps.md](../reference/maps.md)
is sound. Users don't need this page.

## Values in a map

A map `[K: V]` holds any value type: `[string: string]`,
`[string: Point]`, `[int: [int]]`, even `[string: [string: int]]`.
The transpiler generates one concrete map runtime per `(key, value)`
pair the program uses. A program using `[string: Point]` and
`[int: [int]]` gets two runtimes; one that uses neither pays for neither.
No boxing, no tag, no dynamic dispatch.

Lifetimes follow the same rule as every other heap value:

- **In** (`m[k] = v`, map literal): the value is copied into the map's
  scope, so it lives as long as the map and is independent of the source.
- **Out** (`m.get`): returns an independent copy, so it survives later
  inserts that rehash the table, and survives the map being freed.

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

Copying a whole map (`b := m`) copies every value. `==` compares
entry-wise, independent of insertion order.

Covers scalars, structs, arrays, and nested maps, uniformly across
`m[k] = v` / `m.get` / `in` / `delete` / `keys` / `len` / `==` /
accumulator / `inout`. Key `K` is `string`, `int`, a newtype over
either, a fieldless enum, or a hashable composite. A map is not yet
usable as a key.

## Mutating through a map

`m[k]` has two roles:

- **As a write target**, it names the value's slot, so `push(m[k], v)`
  and `m[k] += 1` update in place. Missing keys insert the value type's
  zero first (`0`, `""`, empty array, zeroed struct).
- **As a read** (scalar value types), it is a pure lookup: missing keys
  return the zero, with no insert.

```tycho
words := ["ada", "alan", "ada"]
cnt := []string: int
for w in words:
    cnt[w] = cnt[w] + 1
```

It can't dangle: `m[k]` only appears inside a single mutation target
and can't be stored or carried past that statement. Rehash happens
before the slot is handed back; `push(m[k], v)` grows the value buffer,
not the table. A key with side effects runs once per statement.

Lowering: `m[k]` as a place becomes a slot-pointer accessor that does
find-or-insert and returns the slot address. Assignment, index-set,
field-set, and `push`/`reserve`/`pop` reuse it, so `m[k].field` and
`m[k][i]` compose. A scalar read lowers to `m.get` with the zero default.
