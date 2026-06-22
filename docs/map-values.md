# Maps with arbitrary value types

A Tycho map `[K: V]` may hold *any* value type: `[string: string]`,
`[string: Point]`, `[int: [int]]`, even a nested map `[string: [string: int]]`.
This note explains how that works under value semantics and implicit arenas,
and why it needs no user `$T` generics. The user-facing surface is in the
README's [Maps](../README.md#maps-string-v-int-v) section; in-place mutation of a
value (`push(m[k], v)`, `m[k] += 1`) has its own note,
[map-mutation.md](map-mutation.md).

## How any value type works without generics

Tycho monomorphizes its containers rather than boxing (and user `$T` generics are
themselves erased to concrete code — see [generics.md](generics.md)), so the
value type cannot be abstracted at runtime. Maps use the same answer arrays use
for `[Struct]` and `[[T]]`: **monomorphization**. The compiler generates one
concrete map runtime per `(key, value)` pair the program actually uses, named
after that pair.

A program using `[string: Point]` and `[int: [int]]` gets exactly two map
runtimes; one that uses neither pays for neither. There is no boxing, no tag, no
dynamic dispatch — a `[string: Point]` stores `Point`s inline, the same layout a
hand-written map would have.

## Heap-value lifetimes

The model is value semantics: a map *owns* its entries, and every value that
crosses the map boundary is deep-copied — the same rule that governs every other
heap value in the language. For a value type that owns heap bytes (a `string`, a
struct with a `string` field, an array, another map) there are two copy
directions:

- **In** (an `m[k] = v` store or a map literal): the value is deep-copied into
  the *map's* arena, so it lives as long as the map and is independent of the
  source variable. Mutating the original afterwards never touches the stored
  copy.
- **Out** (`map_get`): the stored value lives in the map's arena, which may be a
  borrow that is freed before the result is used. So `map_get` returns an
  **independent deep copy** into the caller's arena — it survives later inserts
  that rehash the table, and it survives the map itself being freed. The default
  argument is heap-copied the same way.

Because both directions copy, value semantics holds end to end:

```
v := map_get(m, "a", default)
m["a"] = other                 # rehashes, moves slots
use(v)                         # still the old value — v is its own copy
```

Copying a whole map (`b := m`) deep-copies every value, so the two maps share no
storage. `==` is deep, entry-wise value equality, independent of insertion
order. These follow from the copy-in/copy-out discipline; they are not special
cases.

## What it covers

For both key kinds — `[string: V]` and `[int: V]` — the value `V` may be:

- a scalar (`int`, `float`, `string`, `bool`),
- a struct (`[string: Point]`),
- an array (`[int: [int]]`),
- a nested map (`[string: [string: int]]`).

All operations work uniformly regardless of `V`:
`m[k] = v` / `map_get` / `k in m` / `delete m[k]` / `keys` / `len`, deep value
`==`, the in-place accumulator, and `mut`. `map_get`'s default and the stored
value take `V`; the key takes `K`. The only restriction is on the **key**:
`string`, `int`, a newtype over one of those, or a fieldless enum — no other key
type (see the README's
[newtypes](../README.md#distinct-newtypes-type) and
[Maps](../README.md#maps-string-v-int-v) sections).

## Implementation notes

These are for contributors; users need only the surface above.

- **tychoc** (`src/tychoc.c`): composite value types are interned in a side table
  (`g_maptypes`, base id `T_MAPC_BASE`) and emitted as one monomorphic runtime
  per pair by `emit_aggregate`, mirroring the existing scheme for `[Struct]`
  arrays. The scalar maps are hand-written; composite value types share the
  interned-and-emitted path independently of them.
- **tychoc0** (`compiler/tychoc0.ty`): maps are generated per `(k, v)` by
  `gen_map_type` / `gen_map_fns`, so the composite work is the heap-value
  deep-copy on put/get and value-type mangling, not a new code path. Type
  declarations are emitted in dependency order (a map's value type before the
  map), so a map-valued map like `[string: [string: int]]` names its inner type
  first.
- **Key schemes**: string keys use a NULL/tombstone sentinel; int keys carry a
  separate occupancy array, so `0` is a usable key rather than a sentinel.
- **Bool values** fold onto the int runtime rather than getting their own.
