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
| tycho | 119.2 MB | ~98 ms | ~520 B   | child **structs by value**   |
| C     |  37.8 MB | ~41 ms | ~173 B   | child **pointers** (8 B)     |
| Go    |  33.8 MB | ~63 ms | ~148 B   | `map[byte]*Node` (pointers)  |

(tycho was 127 MB before the composite-map initial-capacity tuning below; the
value-vs-pointer gap that remains is structural, not tunable.)

## Reading it honestly — this is a workload tycho loses

Unlike the JSON tree (where tycho ≈ C on memory), the trie costs tycho **~3.2× C and
~3.5× Go**. This is not the arena; it is **value semantics meeting a pointer-shaped
structure**, and it is worth stating plainly:

- **Children are stored by value, not by reference.** `[int: Trie]` holds whole `Trie`
  values inline in the map's value array — each ~80 B (its own map header + flag). C and
  Go store an 8 B child *pointer*. That is a ~10× difference *per child slot*, and a trie
  is nothing but child slots.
- **The per-node map over-allocates.** A composite map's backing arrays start at capacity
  8 and grow by doubling, but a typical trie node has only 1–2 children — so most of that
  8-slot value array (8 × ~80 B) is allocated and empty. C/Go's maps over-allocate too,
  but of *pointers*, so the waste is 10× cheaper.

So the two costs compound: value-inline storage × capacity-8 over-allocation. The arena
reclaims it all correctly and with zero manual management — but the *peak* is what it is.

**The honest boundary.** Tycho's value-semantic model lands next to C on **value-shaped**
trees (see `bench/json` — a tagged value tree, tycho 37 MB ≈ C 35 MB) and pays a real
premium on **pointer-shaped / structurally-shared** structures (this trie), where the
natural C/Go representation is a node *referenced* by many parents/maps. You *can* write a
compact tycho trie with a flat node pool and integer-index children — but that is exactly
the manual memory management the model exists to avoid, so it isn't the idiomatic
comparison and isn't what's measured here.

## Tuning the composite-map initial capacity (8 → 4)

The capacity-8 start over-allocated for trie nodes that hold 1–2 children, so the
composite-map initial capacity was lowered to **4** (both compilers). That took the trie
from **127 MB → 103 MB** (−19%) with **no change to `bench/json`** (37 MB either way —
json's map values are 8 B pointers, so its abandoned intermediate arrays are cheap; the
trie's are 80 B structs).

A later, unrelated change raised the baseline to **119 MB**: every map gained an intrusive
insertion-order list (`nxt`/`prv` per slot, the O(1)-delete machinery — see `bench/lru`),
adding ~16 B per slot. The trie holds millions of tiny maps and never deletes, so it pays
that cost with no benefit — an honest consequence of making map delete O(1) and
churn-bounded for the workloads that *do* delete.

We also measured capacity **2**, and it was *worse* — back up to ~127 MB. The reason is
specific to the arena and worth recording: a smaller initial capacity forces more
**rehashing**, and the arena cannot reclaim the abandoned intermediate backing arrays
until scope exit, so they accumulate. Capacity 4 is the sweet spot here: it holds the
common 2-child node **without rehashing** while halving the empty-slot waste of cap-8.
The optimum trades empty-slot waste (large cap) against abandoned-rehash-array waste
(small cap) — an arena-specific balance a malloc/GC allocator doesn't face.

## Notes / honest limits

- Even at cap-4, tycho is ~3.2× C here. The remaining gap is **value-vs-pointer storage**,
  which is fundamental to value semantics, not a tuning knob. For pointer-shaped / shared
  structures, see `docs/internals/value-semantics-limits.md` for the recommended idioms.
- Single-machine snapshot; absolute numbers vary by CPU/allocator/GC. Run
  `sh bench/trie/run.sh` to reproduce. Not wired into `make ci` (Go is optional).
