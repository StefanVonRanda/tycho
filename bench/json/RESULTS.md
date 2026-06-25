# json — a JSON-tree head-to-head (memory + wall)

Parse a large JSON document into a **generic tagged value tree** and fold a checksum
over it, the same workload in tycho / C / Go. The parsed tree is the memory under
test: tycho's implicit **arena** (the whole tree bulk-freed at scope exit) vs C's
**per-node `malloc`** vs Go's **GC** tracing every node. tycho uses the in-tree
`core:json` package — no hand-written C. A byte-identical checksum across the three
ports is the cross-language correctness check.

**Workload.** A document of `N = 50000` objects:
`[{"id":i,"cat":i%32,"amt":(i*2654435761)%1000,"name":"u<i%1000>"}, …]` (~2.5 MB of
text). Each port builds the same string, parses it into its native generic value tree,
and sums `id+cat+amt + len(name)` over every record. The tree (50k objects × 4 fields ≈
0.3M heterogeneous nodes) stays live across the fold, so peak RSS reflects the whole
tree plus the input string.

## Results

Machine: **AMD Ryzen 7 7735HS** (Zen 3+, x86-64), Linux. Toolchains: tycho via `tychoc`
(C backend) at `-O3`; C via **gcc 15.2.0** at `-O3`; Go **1.26.2**. Median of 3 runs;
peak RSS via `bench/peakrss` (`ru_maxrss`). All three emit checksum `1275919372`.

| lang  | peak RSS | wall   | memory management        |
|-------|---------:|-------:|--------------------------|
| tycho | 37.0 MB  | ~40 ms | none (implicit arena)    |
| C     | 35.3 MB  | ~31 ms | manual `malloc` per node |
| Go    | 28.5 MB  | ~57 ms | garbage collector        |

## Reading it honestly

- **tycho ≈ C on memory** (37 vs 35 MB, ~5%) — the arena tree carries no per-node
  bookkeeping beyond the nodes themselves, so it lands right next to a hand-written C
  tree, **with zero manual memory management** (no `malloc`/`free`, no destructors, no
  GC). This is the thesis point: value semantics + implicit arenas match C's memory
  profile on a real allocation-heavy workload.
- **tycho is ~30% slower than C on wall** (40 vs 31 ms). The arena's bump allocation is
  itself cheap; the gap is the boxing — every `JNum`/`JStr` is a heap value-cell — plus
  the string-building in the generator. Still comfortably **faster than Go** here.
- **Go uses ~20% less peak** (28.5 MB) but is **~2× slower** (57 ms). Its GC reclaims
  the ~2.5 MB transient input string mid-run (which tycho's arena and the un-freeing C
  port both hold to the end), trading wall time for a lower peak.

So on a generic JSON tree: tycho is competitive with hand-written C on both axes and
ahead of Go on time, while writing none of the memory management. The arena's
"build-and-hold" characteristic (transients live until scope exit) is what keeps its
peak a touch above Go's, not the tree representation itself.

## Notes / honest limits

- `core:json` parses numbers as **integers** (a documented simplification — see
  `corelib/json/json.ty`), so the workload uses integer fields. A float-number variant
  would change the parse cost but not the tree-shape memory story.
- This is a single-machine snapshot; absolute numbers vary by CPU/allocator/GC tuning.
  Run `sh bench/json/run.sh` to reproduce. Not wired into `make ci` (Go is optional).
