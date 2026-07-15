# trie — a prefix-tree head-to-head (memory + wall), and where value semantics *lose*

Insert `N = 150000` deterministic words (a shared LCG, so all three ports build the
identical trie) into a prefix tree where **each node owns a small `int -> child` map**,
and report `(node count, word count)` as a byte-identical cross-language checksum. The
whole trie is the memory under test. tycho uses native `[int: Trie]` maps (no
hand-written C); C uses a minimal open-addressing `int -> Node*` map per node; Go uses
the idiomatic `map[byte]*Node`.

## Results

Machine: **AMD Ryzen 7 7735HS** (Zen 3+, x86-64), Linux. Toolchains: tycho via `tychoc`
(C backend) at `-O3`; C via **gcc 15.2.0** at `-O3`; Go **1.26.2**. Median of 3 runs;
peak RSS via `bench/peakrss`. All three emit checksum `229005 117013`.

| lang  | peak RSS | wall   | per node | child storage                |
|-------|---------:|-------:|---------:|------------------------------|
| tycho |  58.7 MB | ~59 ms | ~250 B   | child **structs by value**   |
| C     |  37.8 MB | ~45 ms | ~173 B   | child **pointers** (8 B)     |
| Go    |  33.8 MB | ~66 ms | ~148 B   | `map[byte]*Node` (pointers)  |

(tycho was 119 MB before its per-node maps moved to a compact indexed-dict layout —
see below; the value-vs-pointer gap that remains is structural, not tunable. tycho's
wall is now below Go's.)

## Reading it honestly — a residual value-vs-pointer premium

Unlike the JSON tree (where tycho ≈ C on memory), the trie still costs tycho **~1.55× C
and ~1.7× Go** on RAM — but the wall is now *below* Go's, and the memory premium is less
than half what it was (~3.2× C at 119 MB). What closed most of the gap, and what remains:

- **The per-node map used to over-allocate value slots.** `[int: Trie]` stored whole `Trie`
  values inline in a power-of-two value array; a 1–2-child node sat in a small table with
  empty slots, each the size of a whole inline node (~80 B). The **compact indexed-dict**
  layout removed that: an `int32` index table now points at *dense* value entries sized to
  the live child count, so an empty slot costs 4 B, not 80 B. It also deleted the per-slot
  insertion-order list. That is the 119 → 58.7 MB drop, and the ~40% wall drop with it.
- **Children are still stored by value, not by reference.** Each live child is a whole
  `Trie` struct inline (~80 B) where C and Go store an 8 B child *pointer*. That
  per-live-child difference is what's left — fundamental to value semantics, not a tuning
  knob. A trie is nothing but child slots.

The arena reclaims it all correctly and with zero manual management, and the peak is now
within a small factor of C.

**The honest boundary.** Tycho's value-semantic model lands next to C on **value-shaped**
trees (see `bench/json` — a tagged value tree, tycho 37 MB ≈ C 35 MB) and pays a real
premium on **pointer-shaped / structurally-shared** structures (this trie), where the
natural C/Go representation is a node *referenced* by many parents/maps. You *can* write a
compact tycho trie with a flat node pool and integer-index children — but that is exactly
the manual memory management the model exists to avoid, so it isn't the idiomatic
comparison and isn't what's measured here.

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
**deleted entirely**. That is what took 119 → 58.7 MB. See
`docs/internals/compact-dict-map-design.md`.

## Notes / honest limits

- tycho is ~1.55× C here after the compact indexed-dict layout. The remaining gap is
  **value-vs-pointer storage**, which is fundamental to value semantics, not a tuning knob.
  For pointer-shaped / shared structures, see
  `docs/internals/value-semantics-limits.md` for the recommended idioms.
- Single-machine snapshot; absolute numbers vary by CPU/allocator/GC. Run
  `sh bench/trie/run.sh` to reproduce. Not wired into `make ci` (Go is optional).
