# raytrace — float math over by-value structs (wall time), the compute-bound bench

Render 1600×1200 primary rays through a scene of 41 spheres with a diffuse + sky
shader, and fold the image into an integer checksum instead of a PPM string. The
work under test is **float arithmetic over `Vec3` structs passed by value**: every
ray runs `vsub`/`vdot`/`vscale`/`vnorm` (three-`f64` structs) and calls
`hit_sphere` once per (pixel, sphere) pair — ~79M ray-sphere tests, each a handful
of `f64` ops plus a `sqrt`. No heap is touched per ray, so this isolates the
**cost of tycho's by-value struct call convention**, not allocation.

A `Vec3` owns no heap, so tycho's "deep-copy on pass" is a 24-byte shallow move —
the same thing C does when it passes a `struct {double x,y,z;}` by value. The
question this bench answers: does that copy stay free, or does the call convention
show up against C where the compiler inlines it away?

## Results

Machine: **AMD Ryzen 7 7735HS** (Zen 3+, x86-64), Linux. Toolchains: tycho via
`tychoc` (C backend) at its default **-O3**; C via **gcc 15.2.0** at `-O3
-ffp-contract=off`; Go **1.26.2** (default). Median of 5 runs; peak RSS via
`bench/peakrss`. All three emit checksum **`6889824139`** — byte-identical, so the
float pipeline agrees bit-for-bit across the three ports.

| lang  | peak RSS | wall    | struct handling                          |
|-------|---------:|--------:|------------------------------------------|
| tycho |   1.3 MB | ~145 ms | `Vec3` by value — deep-copy call convention |
| C     |   1.5 MB | ~146 ms | `Vec3` by value — inlined                 |
| Go    |   2.3 MB | ~419 ms | `Vec3` by value — inlined                 |

## Reading it honestly

**Tycho lands at C parity — ~0.99× C on wall time (145 vs 146 ms, inside run-to-run
noise), with zero manual memory management and byte-identical output.** Go is
~2.9× slower than both.

This is the data point the suite was missing, and the cleanest of the set:

- The three shipped benchmarks stress *memory* shapes — dijkstra (index-adjacency,
  ~1.3× C), trie (pointer-linked, ~1.55× C), interp (recursive-enum, wins). raytrace
  is **compute-bound**: no per-ray heap, so peak RSS is near-baseline for everyone
  (tycho is actually the lowest — the arena's fixed reservation, no image buffer),
  and **wall time is the headline metric**.
- Parity is the *expected* result once you take the model seriously — a heap-free
  struct is exactly the case value semantics is supposed to make free — but it had
  never been measured. "Free in theory" and "0.99× C measured" are different claims;
  this bench makes the second one. The by-value `Vec3` argument compiles to the same
  register/stack traffic as C's by-value struct; there is no boxing, no arena
  allocation, and no deep-copy work to do because there is nothing heap-owned to
  copy.
- It also demonstrates the float surface end-to-end (`sqrt`, `to_int`/`to_float`,
  `f64` throughout) produces the same bits as C and Go on a real numeric workload,
  not just a toy.

So the model's cost is representational, not computational: it shows up when data
is *pointer-shaped and shared* (the trie's by-value children), and it vanishes on
*flat numeric compute over small value structs*, where tycho is indistinguishable
from C.

## Notes / honest limits

- **Correctness oracle.** The image is deterministic, so the checksum is a
  reproducible oracle; a byte-identical value across three independent float
  implementations is a strong agreement check. The C reference is built
  `-ffp-contract=off` and tycho's emitted C uses gcc's default `-ffp-contract=fast`
  — they still matched at these inputs (the per-ray arithmetic gcc chose not to
  fuse across the `vdot`/`hit_sphere` call boundaries). This is an *empirical* match
  at this scene, not a guarantee that every float program is bit-portable; a
  program that relied on FMA fusion could diverge. The bench deliberately uses only
  `sqrt` (IEEE correctly-rounded) and no trig (`sin`/`cos` are not
  correctly-rounded and differ across libm/Go).
- **Compute-bound by construction.** Folding to a checksum (not a PPM string) is
  what isolates the float/struct cost from the O(n) string-accumulator the
  `examples/raytrace.ty` version exercises. Different question, different program.
- Single-machine snapshot; absolute numbers vary by CPU/allocator. Run
  `sh bench/raytrace/run.sh` to reproduce. Not wired into `make ci` (Go is
  optional); this is an evidence bench, like dijkstra.
- **Dogfood find:** none — the program compiled and ran correctly on `tychoc` first
  try, and the checksum matched C/Go on the first run. (The two-compiler fixpoint
  already covers tychoc0 agreement for this source shape.)
