# Composite map keys (`[Struct: V]`, `[(T,…): V]`) — design

Status: **shipped, both compilers**, 2026-06-25 — struct / tuple / array keys
(alongside the prior newtype and fieldless-enum keys). Verified green:
`tests/mapstructkey.ty`, `maptuplekey.ty`, `maparraykey.ty`, `enum_key.ty`,
`newtype_key.ty`; reject tests `tests/reject/enum_key_payload.ty`,
`newtype_key_mix.ty`. This file is the design record, not open work.

> **Order-store superseded 2026-06-26 (semantics unchanged).** The `ord`
> insertion-order list described below (a flat `KT*` array with `tycho_ord_push_*`/
> `tycho_ord_del_*`, del matching by `tycho_eq`) was replaced — in the composite
> `tycho_mapc%d` families too — by an **intrusive doubly-linked list over the table
> slots** (`nxt`/`prv` + `head`/`tail`, shared `tycho_ord_link`/`tycho_ord_unlink`),
> so the order-list update on `delete` is an O(1) unlink instead of an O(n) shift-remove.
> The hash table itself also moved from **tombstone** deletion (`occ=2` / TOMB) to
> **linear-probe backward-shift** (these composite families included), so a delete-heavy
> map no longer rehashes-to-purge and stops accumulating arena generations. `keys()` output
> is byte-identical (insertion order preserved). See
> [map-hash-dos-plan.md](map-hash-dos-plan.md), `bench/lru/RESULTS.md`, and commit `f9bfc34`.

## Goal

Let a map key be a **struct or tuple** (recursively, over hashable fields), closing
the README map-key gap. Before this, keys were `int` / `string` / a newtype over those /
a fieldless enum. Composite *values* were already fully supported; this added the same
generality on the key side.

## Approach — generalize the int-key occupancy scheme

A struct/tuple value has no natural null sentinel, exactly like an `int` key. So the
composite-key table mirrors `TychoMapII` (`runtime/tycho_rt.c:1752`): open-addressing
with a **separate `occ` occupancy byte array** + an `ord` insertion-order list for
`keys()`. Per `(K,V)` pair the compiler already emits one monomorphic runtime; the
composite-key runtime is that scheme with:

| int-key scheme            | composite-key scheme                      |
|---------------------------|-------------------------------------------|
| `long keys[]`             | `K keys[]` (the struct/tuple, stored inline) |
| `tycho_ik_hash(long)`     | generated `tycho_hash_<K>(K)` (deep, NEW)  |
| `k1 == k2`                | generated `tycho_eq_<K>(k1,k2)` (deep, EXISTS — `gen_eq`) |
| key copied by value       | key **deep-copied** in (like composite values) |
| `keys()` returns longs    | `keys()` returns K, deep-copied out        |

Value side (`put`/`get`/`in`/`delete`/`len`/`==`/`mut`) is unchanged from the
existing composite-value machinery.

## The one new function: a generated deep-hash

`tycho_hash_<K>(K)` must be: deterministic, **equal for `==`-equal values**,
well-distributed, and **seeded** (reuse the per-process `tycho_hash_k0` like the
string/int hashers, for the hash-flooding defense — see hash-DoS note). It folds
field hashes structurally, mirroring `gen_eq`'s shape:

- scalar `int`/`bool`/`char`/enum-tag → `tycho_ik_hash(field)`
- `string`/`bytes` → `tycho_si_hash(field)` (keyed SipHash)
- `float` → hash the bit pattern
- nested struct/tuple → its own `tycho_hash_<K'>`
- array/map field → fold element/entry hashes (order-sensitive for arrays;
  order-INSENSITIVE for maps, matching deep `==`)

Combine via a mix step (e.g. `h = h*0x100000001b3 ^ field_h`, seeded init), so field
order matters and `(1,2) != (2,1)`.

## Touch points

- **Parser gate** `src/tychoc.c:1448` (and tychoc0's equivalent): accept struct/tuple
  (and newtype-over-struct) key types; keep rejecting key types that contain a
  non-hashable leaf (a function value; a handle).
- **tychoc** `src/tychoc.c`: intern composite-key map types (`g_maptypes`/`T_MAPC_BASE`
  already keys on `(k,v)`), emit the composite-key runtime in `emit_aggregate`, emit
  `tycho_hash_<K>` alongside the existing `tycho_eq_`/copy emitters.
- **tychoc0** `compiler/tychoc0.ty`: extend `gen_map_type`/`gen_map_fns` with the
  occupancy+hash variant; add the deep-hash generator.
- **Dependency order**: a key type's hash/eq/copy must be emitted before the map that
  uses it (the existing value-before-map ordering already does this for nesting).

## Staging (each stage lands fully, both compilers + fixpoint, before the next)

1. **Struct key, scalar fields** — `[Point: int]` end to end (hash/eq/copy, all ops,
   `keys()`), both compilers, golden + parity.
2. **Tuple key** — `[(int,string): V]`.
3. **Heap-bearing + nested key fields** — `[Person: V]` where `Person` has a `string`;
   nested `[Line: V]` where `Line` has `Point` fields; newtype-over-struct keys.
4. Docs (README:570 + map-values.md), fuzzer coverage.

## Concrete emission recipe (tychoc)

The composite-key runtime is a third branch in the `emit_aggregate` map loop
(`src/tychoc.c:8260`), cloned from the **int-rep occupancy branch** (8264-8341) with
these exact substitutions (let `KT = c_type(key)`):

- `m.keys` type `long*` → `KT*`; `sizeof(long)` → `sizeof(KT)`.
- `tycho_ik_hash(k)` → `tycho_hash_<id>(k)` (generated deep-hash, below).
- key compare `m.keys[i] == k` → `tycho_eq_<key>(m.keys[i], k)` via `gen_eq(key, …)`.
- put stores the key by deep copy: `m->keys[s] = k` → `m->keys[s] = copy_into(key,"a","k")`.
- `ord`/`tycho_ord_push_i`/`tycho_ord_del_i` (long order list) → a `KT`-typed order
  list: store the inserted keys in insertion order for `keys()`/`del`. Cleanest: make
  `ord` a `KT*` with generated `tycho_ord_push_<id>` / `_del_<id>` (del matches by
  `tycho_eq`). `keys()` then pushes `m.ord[j]` (a `KT`) into the `[Key]` array
  (`arr_of(key)`, a `TychoArrC`), deep-copied out like any composite element.
- gate `mapkey_intrep(key)` stays false for a struct/tuple, so add the new branch
  BEFORE the string-key fallthrough; route via a new `mapkey_composite(key)` predicate.

### `tycho_hash_<id>(KT k)` generator (the one new function)

Mirror `gen_eq`'s recursion. Seed from `tycho_hash_k0`. Per field `f`:
`h = h * 1099511628211UL ^ field_hash(f)` (FNV-style mix; field order matters):
- int/bool/char/enum-tag → `tycho_ik_hash(k.f)`
- string/bytes → `tycho_si_hash(k.f)`  (keyed SipHash; byte-safe)
- float → `tycho_ik_hash((long)<bit-cast of k.f>)`
- nested struct/tuple → `tycho_hash_<id'>(k.f)`
- array → fold element hashes in order; map → fold entry hashes order-INSENSITIVELY
  (xor-combine) to match deep `==`.
Init `h` from the seed so different processes hash differently (hash-flood defense).

### Map type interning + ops

`mapc_of(key,val)` already keys on `(key,val)` — no change. `map_of` (942) gains a
branch: a struct/tuple (or newtype-over-struct) key → `mapc_of(key,val)` +
`arr_of(key)` (for `keys()`). The op codegen (`map_fn`, `gen_call` for
`m[k]`/`map_get`/`in`/`delete`/`keys`/`len`/`==`/slotptr) already dispatches by
`MAPC_ID` and is key-type-agnostic, so it needs no change once the runtime exists —
the literal `k` expression is emitted by the normal expr path (a struct value).

## Non-goals

- Float-keyed maps directly (NaN/`-0.0`/`==` hazards) stay out unless a struct field;
  a `float` *field* of a key hashes by bit pattern (documented sharp edge).
- Key types containing a function value or an affine handle stay rejected (no stable
  hashable identity / would violate affinity).
