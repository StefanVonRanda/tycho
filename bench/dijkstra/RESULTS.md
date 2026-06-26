# dijkstra — single-source shortest paths (memory + wall), and where the index idiom wins

Build a sparse random graph (`N = 300000` nodes, out-degree 4, ~1.2M edges from a shared
LCG) as an **adjacency list of indices**, run Dijkstra from node 0 with a hand-built binary
min-heap, and fold the reachable distances into a checksum. The adjacency is the memory
under test: tycho's value-semantic `[[Edge]]` vs C's `Edge**` vs Go's `[][]Edge`. tycho uses
no hand-written C. Distances are tie-break independent, so any correct shortest-path
reproduces the checksum — a byte-identical `reach sum` across the three ports is the
correctness oracle.

## Results

Machine: **AMD Ryzen 7 7735HS** (Zen 3+, x86-64), Linux. Toolchains: tycho via `tychoc`
(C backend) at `-O3`; C via **gcc 15.2.0** at `-O3`; Go **1.26.2**. Median of 3 runs; peak
RSS via `bench/peakrss`. All three emit checksum `73529 21230611` (73529 of 300000 nodes
reachable — the out-component of this random digraph).

| lang  | peak RSS | wall   | adjacency storage              |
|-------|---------:|-------:|--------------------------------|
| tycho |  41.0 MB | ~64 ms | `[[Edge]]` — values, by index  |
| C     |  31.5 MB | ~52 ms | `Edge**` — malloc'd per node   |
| Go    |  34.0 MB | ~61 ms | `[][]Edge` — slices            |

## Reading it honestly — the opposite of the trie

This is the result `value-semantics-limits.md` predicts and the trie's companion: tycho is
**~1.3× C on memory and ~1.2× on wall, ≈ Go** — *competitive*, not the ~3.2× the
pointer-linked trie cost. The reason is representational:

- A graph stored as an **adjacency list of integer indices** (the flat-pool idiom we
  recommend for pointer-shaped data) is **value-shaped**: each `Edge` is `{to: int, w: int}`
  with no shared pointers, and the edge arrays are flat and contiguous in all three
  languages. So the bulk of the memory — ~1.2M edges × 16 B — is laid out identically, and
  tycho lands right next to C, **with zero manual memory management**.
- The residual ~1.3× is **per-adjacency-list bookkeeping**, not capacity waste: tycho's
  `[Edge]` carries a 24-byte descriptor (data / len / cap) per node × 300k nodes, where C
  keeps an 8-byte pointer plus a small length. Out-degree 4 fits a cap-4 array exactly, so
  there is **no `push` doubling waste** here — `reserve` doesn't move the number (we tried).

So the dogfood validates the idiom: rewriting a graph from pointer-linked nodes to
index-into-pool adjacency moves tycho from ~3.2× C (trie) to ~1.3× C (this) — the model is
competitive on graph algorithms when the graph is expressed the value-semantic way.

## Notes / honest limits

- A non-trivial program — adjacency build, a hand-written binary heap over `(dist, node)`
  tuples, lazy-deletion Dijkstra — compiled and ran **correctly on both compilers first try**,
  byte-identical. The dogfood's one find was a compiler gap, since fixed: `reserve` rejected
  composite-element arrays in tychoc (`reserve([Edge], n)`) while tychoc0 accepted them — a
  parity gap, now closed (`tests/reservecomposite.ty`).
- Single-machine snapshot; absolute numbers vary by CPU/allocator/GC. Run
  `sh bench/dijkstra/run.sh` to reproduce. Not wired into `make ci` (Go is optional).
