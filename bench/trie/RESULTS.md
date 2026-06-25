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
| tycho | 127.4 MB | ~89 ms | ~583 B   | child **structs by value**   |
| C     |  37.8 MB | ~41 ms | ~173 B   | child **pointers** (8 B)     |
| Go    |  33.8 MB | ~63 ms | ~148 B   | `map[byte]*Node` (pointers)  |

## Reading it honestly — this is a workload tycho loses

Unlike the JSON tree (where tycho ≈ C on memory), the trie costs tycho **~3.4× C and
~3.8× Go**. This is not the arena; it is **value semantics meeting a pointer-shaped
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

## Notes / honest limits

- The capacity-8 start is a contributor (not the whole story) and is a future tuning lever
  — a smaller initial composite-map capacity would cut the empty-slot waste at the cost of
  more rehashing on denser maps. The value-vs-pointer storage is fundamental to value
  semantics and would remain.
- Single-machine snapshot; absolute numbers vary by CPU/allocator/GC. Run
  `sh bench/trie/run.sh` to reproduce. Not wired into `make ci` (Go is optional).
