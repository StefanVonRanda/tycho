# Compact-dict map representation — design note / ready-to-pick-up task

> **STATUS: COMPLETE + verified (2026-07-15).** The compact indexed-dict layout
> (int32 index table → dense insertion-ordered entries) + tombstone/backward-shift
> delete + allocation-free in-place compaction is now live across **all four code
> sites**: the runtime fixed families `tycho_map_{ii,si,sf,if}` (`runtime/tycho_rt.c`),
> the tychoc composite generator `tycho_mapc%d` (`src/tychoc.c`, all 3 key variants),
> and the tychoc0 generic `map_<k>_<v>` generator (`compiler/tychoc0.ty`). The
> `nxt/prv` intrusive order list (and tychoc0's separate `ord` key array) are gone —
> the entries array IS the insertion order.
>
> **Measured wins (this machine — AMD Ryzen 7 7735HS, x86-64, Linux; tycho via `tychoc`):**
>
> | bench | RAM before | RAM after | Δ | checksum |
> |---|---|---|---|---|
> | trie (`[int:Trie]`) | 119.2 MB | 58.8 MB | **−51%** (3.2×C → ~1.55×C) | identical |
> | invindex (`[string:V]`) | 127 MB | 59.5 MB | **−53%** | identical |
> | lru (`[int:int]`, delete-churn) | 40.4 MB | 32.7 MB | **−19%** (bounded, no 222 MB cliff) | identical |
> | json (small values) | 37.0 MB | 37.1 MB | flat (no regression) | identical |
> | dijkstra (value-shaped) | 41.0 MB | 40.3 MB | flat | identical |
>
> **Gate:** `make fixpoint` GREEN (B==C byte-identical + split E==F self-host);
> `make test` 334/0; typeparity 4608/4608, eqparity 512/512, parforparity 25/25,
> unaryparity 30/30; corelib GREEN; conc 36/0. `make fuzz` — the only failures (48)
> and the `ffi` lane failure are the **pre-existing** sized-int-identifier
> tychoc/tychoc0 divergence (confirmed at the base commit; 0 map-related). Plus a
> standalone prototype (`bench/trie/compact_proof.c` companion) that was
> ASan/UBSan-clean + 8M-op differential-fuzzed vs a reference model. Delete/churn
> bound re-derived and re-verified on `bench/lru`.
>
> **STATUS (original): PROPOSED, not started (2026-07-14).** A proof-of-concept C model
> (below) says a compact/indexed map layout roughly **halves** the per-node map
> memory and cuts wall **~40%** on the trie workload — closing most of both the
> value-semantic 3× memory gap *and* the trie/lru wall gap to Go in one change.
> The payoff is measured; the implementation is weeks (every map family + both
> compilers + a delete redesign), so this is a scoped task, not a quick win.
>
> Internal working note. Companion to [value-semantics-limits.md](value-semantics-limits.md)
> (the 3× storage boundary), [map-hash-dos-plan.md](map-hash-dos-plan.md) (the
> seeded keyed hashing + insertion-order store this must preserve), and the
> `bench/trie` / `bench/lru` RESULTS. Supersedes the "typed sub-pools" idea for
> the memory story (that was rejected on evidence — it buys locality, not storage;
> see [ROADMAP](../../ROADMAP.md). This is the real lever.

## 1. The problem

`[K: V]` today is open-addressing where the **value array holds V inline**, plus
an intrusive doubly-linked insertion-order list (`nxt`/`prv` per slot, from
[map-hash-dos-plan.md](map-hash-dos-plan.md) + the O(1)-delete rework). On a trie
(`[int: Trie]`, each node a small map), two costs compound and dominate:

- **Empty value slots are expensive.** callgrind + a fanout census on the 150k-word
  trie (229,005 nodes): **40% of nodes are leaves, 86% have ≤1 child.** A 1-child
  node sits in a cap-4 table with **3 empty value slots, each the size of a whole
  inline node** (~80 B in tycho). Go/C store an 8 B *pointer* per slot, so their
  empty-slot waste is ~10× cheaper.
- **The order list is dead weight here.** `nxt`/`prv` (~16 B/slot) is maintained
  on every insert but the trie never deletes, so it pays the O(1)-delete machinery
  for nothing (already noted in `bench/trie/RESULTS.md`).

Result: trie is **3.2× C / 3.5× Go on RAM** and **1.55× Go on wall**; `bench/lru`
(map-churn) is ~1.2× Go. Profiling (`callgrind`) attributes ~70% of trie to
per-node map machinery (insert ops + `mapc0_with_cap` allocation + table zeroing).
This is *not* a copy-elision opportunity (that was interp, fixed in `cc5a11f`) —
it is the map representation itself.

## 2. The proof (why it's worth doing)

A faithful standalone C model (`bench/trie/compact_proof.c`) builds
the **identical** 229,005-node trie two ways, both `malloc`-and-**hold** every
allocation (incl. abandoned rehash tables) to model tycho's arena, which frees
nothing until scope exit:

| per-node map layout | held | peak RSS | wall |
|---|---|---|---|
| **current** (value-inline table + `nxt`/`prv` order list) | 49.7 MB | 60 MB | 80 ms |
| **compact** (int32 index table → dense entries, order = insertion) | 23.5 MB | 30 MB | 47 ms |
| **delta** | **−53%** | **−50%** | **−41%** |

Extrapolating the *ratio* to real tycho (the model is lighter than tycho's actual
map, so treat absolutes as directional and the ratio as the signal — if anything
conservative, since tycho's per-map overhead is ~2.4× the model, i.e. *more*
empty-slot bytes to reclaim):

- memory ~119 → **~56 MB** (3.2× C → ~1.5× C)
- wall ~104 → **~61 ms ≈ Go's 67 ms (parity)**

The compact C model itself (30 MB / 47 ms) already lands between Go (34/67) and
malloc-per-node C (38/48) — the layout is fundamentally sound.

## 3. Proposed layout

Two arrays per map (the Python-3.6 / Swift "indexed dict" shape):

- **Index table** — `int32[cap_idx]`, open-addressing, stores `entry_index + 1`
  (0 = empty). Sized for load factor; grows by doubling. Empty slots cost **4 B**,
  not `sizeof(V)`.
- **Entries** — dense `{K key; V val}[]`, sized to the entry **count** (grows by
  doubling), in **insertion order**.

Lookup: hash key → probe the index table → `entries[idx-1]`. Insert: append to
`entries`, write its index into the probed index slot. **`keys()` / insertion
order come free** from walking `entries` — the separate `nxt`/`prv` list is
**deleted entirely**, which removes both its per-slot memory and its per-insert
maintenance (part of the wall win).

Retains everything the current map guarantees:
- **Seeded keyed hashing** (SipHash-1-3 strings / SplitMix64 ints, seeded once at
  `main`) — unchanged; it keys the index probe. Insertion-order `keys()` stays
  **seed-invisible** (order is the entries array, not hash order).
- **Composite keys** (struct/tuple/array/fieldless-enum, deep hash + deep `==`) —
  unchanged; the index just holds ints, the entry holds the composite key.
- **`k in m`, `map_get`, place-mutation `&m[k]` / `m[k]=v` / `m[k].f=x`** — all
  route through the same `entries[idx-1]` slot.

## 4. The one real design wrinkle: `delete`

Today's delete is **tombstone-free backward-shift** + a churn bound that kept
`bench/lru` at 40 MB (see the map-delete work / `hier-map-delete-on`). A dense
insertion-ordered entries array can't backward-shift without either breaking order
or O(n) compaction. Options, with the constraint that **`keys()` insertion order
MUST be preserved** (so swap-last-into-hole is out — it reorders):

1. **Tombstone the entry** (mark `val` dead, clear its index slot), keep `entries`
   stable so order is intact. `len` (live count) tracked separately.
2. **Compact + rebuild index when tombstones exceed a fraction** of `entries`
   (e.g. ½) — an amortized O(1) churn-bounded compaction, the analog of today's
   purge-rehash bound. This is what keeps `lru` from growing unbounded; the LRU
   222→40 MB fix must be **re-derived in the new layout** and re-verified by
   `bench/lru`.

`keys()` skips tombstoned entries. This preserves O(1) amortized delete + insertion
order + the churn bound. It is the part most likely to need iteration — prototype
the delete path against `bench/lru` early.

## 5. Implementation surface

- **Runtime fixed families** — `tycho_rt.c`: `map_ii`, `map_if`, `map_si`,
  `map_sf`, string-keyed, etc. (`_with_cap` / `_set` / `_get` / `_del` / `keys` /
  iteration) rewritten to the two-array layout.
- **Codegen'd composite families** — the `tycho_mapc%d` generators for composite
  *values* and composite *keys*, emitted by **both** compilers. GOTCHA
  ([hier-composite-map-values]): the type-aware tychoc0 generators can't live in
  the one-way `rt` package, so both `src/tychoc.c` and `compiler/tychoc0.ty` carry
  their own copy — they must stay byte-identical (fixpoint) and semantically
  identical (parity).
- **Every place-mutation and read path** that indexes a map descriptor directly
  (push into `m[k]`, `delete m[k]`, `&m[k]`, `m[k] op= v`).

## 6. Validation plan (the bar this must clear)

- `make fuzz` (differential tychoc vs tychoc0 + ASan/UBSan) — the primary
  soundness gate for a core data-structure rewrite.
- `make fixpoint` B==C byte-identical, **with different per-process seeds** (the
  seed-invisibility invariant from map-hash-dos-plan).
- All 4 parity lanes + `make test` + `corelib`/`conc`/`ffi`.
- `tests/map_insorder` (insertion order) + a new tombstone/compaction regression.
- **`bench-guard` + `bench/{trie,lru,json,invindex}`**: trie/lru memory *and* wall
  must improve (the whole point); `bench/lru` must keep its churn bound (no
  regression to the pre-40 MB cliff); `json` (8 B pointer values) must not regress.

## 7. Effort, risk, decision

- **Effort: weeks.** Every map family × both compilers + the delete redesign.
- **Risk: high** — it is *the* core container, and delete + composite-key + seeded
  hashing + byte-identical dual-compiler emit all have to stay correct.
- **Payoff: measured** — roughly halves trie memory (3.2× C → ~1.5× C) and brings
  trie/lru wall to ~Go parity; it is the single change that closes both the value-
  semantic 3× RAM story and the remaining map-workload gap to Go.

**Recommendation:** pick this up when map memory/perf is the priority. Sequence:
(1) port `cdproof.c`'s layout into one runtime family (e.g. `map_ii`) behind the
existing API; (2) prototype delete against `bench/lru`; (3) if the bench win holds
and `lru` stays bounded, roll out to all families + tychoc0 + the full gate. Bail
cheaply after step 2 if the delete path erodes the win.
