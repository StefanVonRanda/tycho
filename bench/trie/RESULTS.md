# trie — a prefix-tree head-to-head (memory + wall), and where value semantics *lose*

Insert `N = 150000` deterministic words (a shared LCG, so all ports build the
identical trie) into a prefix tree where **each node owns a small `int -> child` map**,
and report `(node count, word count)` as a byte-identical cross-language checksum. The
whole trie is the memory under test. tycho uses native `[int: Trie]` maps (no
hand-written C); `trie_pool.ty` is the **same trie in the flat-pool + integer-index
idiom** (see its header); C uses a minimal open-addressing `int -> Node*` map per node;
Go uses the idiomatic `map[byte]*Node`.

## Results

Machine: **AMD Ryzen 7 7735HS** (Zen 3+, x86-64), Linux. Toolchains: tycho via `tychoc`
(C backend) at `-O3`; C via **gcc 15.2.0** at `-O3`; Go **1.26.2**. Median of 3 runs;
peak RSS via `bench/peakrss`. All four emit checksum `229005 117013`.

| lang  | peak RSS | wall  | per node | child storage                      |
|-------|---------:|------:|---------:|------------------------------------|
| tycho |  58.9 MB | ~62 ms | ~270 B   | child **structs by value**         |
| pool  |  41.4 MB | ~54 ms | ~190 B   | child **indices** (8 B) into one flat `[Trie]` |
| C     |  37.8 MB | ~50 ms | ~173 B   | child **pointers** (8 B)           |
| Go    |  33.9 MB | ~76 ms | ~155 B   | `map[byte]*Node` (pointers)        |

(tycho was 119 MB before its per-node maps moved to a compact indexed-dict layout — see
below. The **pool** row is the same trie with children stored as indices into a flat
node array — the idiom `docs/internals/value-semantics-limits.md` recommends for
pointer-shaped data, the same trick `bench/dijkstra` measures at ~1.3× C. It lands at
**1.10× C**, wall below C's. The one manual step is `reserve(pool, N)` — a dynamically
grown pool retains every doubling buffer in the arena and measures *worse* than the
idiomatic form (60.1 MiB); reserving the spine once makes it a single allocation.)

## Reading it honestly — the idiomatic premium, and the model's measured way out

The idiomatic `[int: Trie]` costs tycho **~1.55× C and ~1.7× Go** on RAM — the wall is
below Go's, and the memory premium is less than half what it was (~3.2× C at 119 MB).
What closed most of the gap, and what remains:

- **The per-node map used to over-allocate value slots.** `[int: Trie]` stored whole `Trie`
  values inline in a power-of-two value array; a 1–2-child node sat in a small table with
  empty slots, each the size of a whole inline node (~80 B). The **compact indexed-dict**
  layout removed that: an `int32` index table now points at *dense* value entries sized to
  the live child count, so an empty slot costs 4 B, not 80 B. It also deleted the per-slot
  insertion-order list. That is the 119 → 58.7 MB drop, and the ~40% wall drop with it.
- **Children are stored by value, not by reference.** Each live child is a whole `Trie`
  struct inline (~80 B) where C and Go store an 8 B child *pointer*. That per-live-child
  difference is the idiomatic form's remaining premium. It is structural for the idiomatic
  form — and **closable by the language's own idiom**: the pool variant stores an 8 B child
  *index* (C's pointer size) in each map and one flat `[Trie]` array for the nodes, cutting
  the premium from ~1.55× C to **~1.10× C** with the same checksum. A trie is nothing but
  child slots; storing the child's *location* instead of the child is the whole trick, and
  value semantics allows it without aliasing (indices, not pointers).

The arena reclaims all of it correctly and with zero manual management.

**The honest boundary.** Tycho's value-semantic model lands next to C on **value-shaped**
trees (see `bench/json` — a tagged value tree, tycho 37 MB ≈ C 35 MB) and pays a real
premium on **pointer-shaped / structurally-shared** structures *when written with the
idiomatic by-value form* (this trie). The model is not trapped by that premium: the
flat-pool + integer-index idiom — recommended in
`docs/internals/value-semantics-limits.md`, demonstrated by `bench/dijkstra`, and now
measured on the trie itself — lands within ~10% of C's memory at C-level wall. The
idiomatic form stays the write-it-without-thinking default; the pool form is what you
reach for when the structure is the hot path, exactly as C/Go reach for their node
allocators.

## History: value-array capacity tuning, then the compact layout

Before the compact indexed-dict layout the value array *was* the store, so its initial
capacity drove peak. Lowering the composite-map start from 8→4 took the trie **127 → 103
MB**; cap-2 was *worse* (~127 MB) because the extra **rehashing** abandoned intermediate
backing arrays the arena can't reclaim until scope exit. A later O(1)-delete insertion-order
list (`nxt`/`prv`, +16 B/slot — see `bench/lru`) then raised the baseline to **119 MB**,
paid by the trie's millions of never-deleting maps for no benefit.

The compact indexed-dict layout supersedes both concerns at once: value entries are now
**dense** (sized to live child count, so value-array capacity no longer multiplies
empty-slot waste — the cap-8-vs-cap-4 tradeoff is gone) and the per-slot order list is
**deleted entirely**. That is what took 119 → 58.7 MB.

## Notes / honest limits

- The idiomatic form is ~1.55× C after the compact indexed-dict layout — the
  value-vs-pointer premium of storing whole children by value. The pool variant
  (`trie_pool.ty`) closes it to ~1.10× C with one `reserve`, at the cost of the
  index-threading the idiom requires. For pointer-shaped / shared structures, see
  `docs/internals/value-semantics-limits.md` for the recommended idioms.
- `reserve` is load-bearing for the pool variant: without it the growing `[Trie]` retains
  every doubling buffer in the arena (60.1 MiB — *worse* than idiomatic). This is the
  arena's honest trade: capacity growth is retained until scope exit, so size-once.
- Single-machine snapshot; absolute numbers vary by CPU/allocator/GC. Run
  `sh bench/trie/run.sh` to reproduce. Not wired into `make ci` (Go is optional).
