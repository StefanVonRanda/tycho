# Maps with arbitrary value types

A Hier map `[K: V]` may hold *any* value type: `[string: string]`,
`[string: Point]`, `[int: [int]]`, even a nested map `[string: [string: int]]`.
This note explains how that works under value semantics and implicit arenas,
and why it needs no generics. The user-facing surface is in the README's
[Maps](../README.md#maps-string-v-int-v) section; in-place mutation of a value
(`push(m[k], v)`, `m[k] += 1`) has its own note,
[map-mutation.md](map-mutation.md).

## The shape of the problem

Maps began as four hand-written types — `[string: int]`, `[string: float]`,
`[int: int]`, `[int: float]` — one C runtime each. Every other value type was a
gap: to map a word to its posting list you stored an index into a side array
instead of the `[int]` you wanted.

Hier has no generics (a deliberate choice — see
[inference.md](inference.md)), so the value type cannot be abstracted at
runtime. The answer is the same one arrays already use for `[Struct]` and
`[[T]]`: **monomorphization**. The compiler generates one concrete map runtime
per `(key, value)` pair the program actually uses, named after that pair. A
program using `[string: Point]` and `[int: [int]]` gets exactly two extra map
runtimes; one that uses neither pays for neither. There is no boxing, no tag,
no dynamic dispatch — a `[string: Point]` stores `Point`s inline, the same
layout a hand-written map would have.

## The hard part: heap-value lifetimes

The model is value semantics: a map *owns* its entries, and every value that
crosses the map boundary is deep-copied, the same rule that governs every other
heap value in the language. For a value type that owns heap bytes — a `string`,
a struct with a `string` field, an array, another map — that means two copy
sites, and getting either wrong is a dangling pointer:

- **In** (`map_set`, a map literal, an `m[k] = v` store): the value is
  deep-copied into the *map's* arena, so it lives as long as the map and is
  independent of the source variable. Mutating the original afterwards never
  touches the stored copy.
- **Out** (`map_get`): the stored value lives in the map's arena, which may be
  a borrow that is freed before the result is used. So `map_get` returns an
  **independent deep copy** into the caller's arena — it survives later inserts
  that rehash the table, and it survives the map itself being freed. The
  default argument is heap-copied the same way.

Because both directions copy, value semantics holds end to end:

```
v := map_get(m, "a", default)
m = map_set(m, "a", other)     # rehashes, moves slots
use(v)                         # still the old value — v is its own copy
```

Copying a whole map (`b := m`) deep-copies every value, so the two maps share
no storage. `==` is deep, entry-wise value equality, independent of insertion
order. These follow from the copy-in/copy-out discipline; they are not special
cases.

## What it covers

For both key kinds — `[string: V]` and `[int: V]` — the value `V` may be:

- a scalar (`int`, `float`, `string`, `bool`),
- a struct (`[string: Point]`),
- an array (`[int: [int]]`),
- a nested map (`[string: [string: int]]`).

All operations work uniformly regardless of `V`:
`map_set` / `map_get` / `map_has` / `map_del` / `keys` / `len`, deep value
`==`, the in-place accumulator rebind, and `mut`. `map_get`'s default and
`map_set`'s value take `V`; the key takes `K`. The only remaining restriction
is on the **key**: `string`, `int`, a newtype over one of those, or a fieldless
enum — no other key type yet (see the README's
[newtypes](../README.md#distinct-newtypes-type) and
[Maps](../README.md#maps-string-v-int-v) sections).

## Verification

The feature lands in both compilers — the C reference compiler and the
self-hosted `hierc0` — and is held to the project's standard correctness bar:
`tests/map_values.hi` (golden output under AddressSanitizer + LeakSanitizer),
the `hierc`/`hierc0` differential, and `make fixpoint` (B≡C self-emission). The
fuzzer generates string- and struct-valued maps so its value-semantics oracle
can catch a deep-copy that aliases when it should not — a class of bug the
output differential alone cannot see, because both compilers could share it.

## Implementation notes

These are for contributors; users need only the surface above.

- **hierc** (`src/hierc.c`): composite value types are interned in a side table
  (`g_maptypes`, base id `T_MAPC_BASE`) and emitted as one monomorphic runtime
  per pair by `emit_aggregate`, mirroring the existing scheme for `[Struct]`
  arrays. The four original scalar maps stay hand-written and byte-identical —
  the composite path is purely additive, so no existing output churned.
- **hierc0** (`compiler/hierc0.hi`): maps were already generated per `(k, v)`
  by `gen_map_type` / `gen_map_fns`, so the work was the heap-value deep-copy on
  put/get and value-type mangling, not a new code path. Type declarations are
  emitted in dependency order (a map's value type before the map), so a
  map-valued map like `[string: [string: int]]` names its inner type first.
- **Key schemes**: string keys use a NULL/tombstone sentinel; int keys carry a
  separate occupancy array, so `0` is a usable key rather than a sentinel.
- **Bool values** fold onto the int runtime rather than getting their own.
