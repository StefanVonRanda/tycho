# Performance and memory

Every program in this file runs on `tychoc`, its output checked against a
golden. Cross-language *ratios* matter more than absolute times, which depend
on the machine.

## A real program at flat memory

[`examples/json.ty`](../examples/json.ty) is a 220-line recursive-descent JSON
parser over a recursive `Json` sum type — real recursion, zero
`malloc`/`free`/refcount/GC in the source. Parsing **5,000,000** documents in a
loop holds at a **flat 10 MB**: each document's tree is reclaimed when its loop
iteration's arena resets. Clean under ASan + LeakSanitizer.

## Historical memory snapshot

> This memory table predates the Java/Swift ports; the current managed-memory
> time comparison is in the GC/ARC section below. The older Koka column is kept
> only as history.

Per-workload timings are in [`bench/prongB/RESULTS.md`](../bench/prongB/RESULTS.md).
Peak resident memory (MB) — every binary computes the same checksum; lower is
better. Figures below are rounded per-round snapshots; the precise log, with
wall times and hardware, is that RESULTS file.

| workload         | tycho |  C | Rust | Go (GC) | Koka (Perceus) |
| ---------------- | ----: | -: | ---: | ------: | -------------: |
| binary-trees     |  **13** | 33 |   33 |      36 |             14 |
| tree-rewrite     |   **6** | 13 |    9 |      22 |              7 |
| array-pipeline   |     6 |  3 |    3 |       6 |             14 |
| string-pipeline  |     1 |  1 |    2 |       3 |              2 |

On the allocation-heavy tree workloads Tycho uses the least memory of the five —
40% of C's on binary-trees, half on tree-rewrite — with no GC and no reference
counting, only lexical arenas and value semantics. Memory is the thesis metric,
and it is reached with zero manual management.

### Current GC / ARC rows

Six core workload ports are built for **OpenJDK 25 (GC)** and **Swift 6.3.2
(ARC)**; the service rows below extend the same comparison. Wall time, same outputs, best-of-three:

| workload | Tycho | Java (GC) | Swift (ARC) |
|---|---:|---:|---:|
| binary-trees | **93 ms** | 335 ms | 2375 ms |
| array-pipeline | **20 ms** | 72 ms | 50 ms |
| dispatch | **5 ms** | 66 ms | 156 ms |
| json-parse | **601 ms** | 698 ms | 3072 ms |
| winagg | **82 ms** | 277 ms | 374 ms |
| latency | **134 ms** | 411 ms | 163 ms |

Tycho is faster than both managed-memory runtimes on every port. Java's JVM
baseline makes its peak RSS uninformative for the small rows; the table is a
time comparison. Koka results elsewhere in the repository are historical and
are no longer the headline managed-memory comparison.

Service-row times (ms; same checksums, best-of-three where the runner batches):

| row | Tycho | C | Rust | Go | Java | Swift |
|---|---:|---:|---:|---:|---:|---:|
| winagg | **82** | 97 | 176 | 296 | 277 | 374 |
| latency | 134 | **127** | **103** | 1031 | 411 | 163 |
| invindex growth | **252** | 255 | 282 | 334 | 381 | 631 |
| invindex count-fill | **333** | 383 | 449 | 626 | 460 | 893 |
| invindex map growth | **160** | 187 | 206 | 213 | 277 | 372 |
| invindex map count-fill | 308 | **307** | 364 | 367 | 363 | 661 |
| window string | **29** | 57 | 50 | 58 | 51 | 111 |
| window int | 2 | **1** | **0** | 2 | 16 | 5 |
| gcscan | **47** | 82 | 104 | 125 | 101 | 135 |
| dbquery | **231** | 233 | - | 254 | - | - |

Rust is ahead only on `latency` and the fixed-size `window int` microcase. Tycho
is faster than C on most service rows; C is ahead on `latency` (127 vs 134 ms)
and ties/aheads on the one-millisecond `window int` row. Tycho is faster than
Java/Swift on every row they cover. `dbquery` has no Rust/Java/Swift port on
this host, so those `-` cells are not managed-runtime claims.

Rust is included on `latency`: **103 ms**, ahead of Tycho’s 134 ms and C’s
127 ms. Tycho is within the 10% manual-memory band against C but outside it
against Rust, so the Rust loss is real and not hidden.

### Relative service scorecard

`Tycho / competitor`; below 1.00 means Tycho is faster. These are the same
service runs above, plus `latency` and `gcscan` from their own harnesses.
Java/Swift memory is excluded from memory claims because their peak RSS includes
the runtime baseline; `window int`, `window string` and other rows under 10 ms
include process startup and are directional only.

| workload | vs C | vs Rust | vs Go | vs Java | vs Swift |
|---|---:|---:|---:|---:|---:|
| winagg | **0.85x** | **0.47x** | **0.28x** | **0.30x** | **0.22x** |
| invindex growth | **0.99x** | **0.89x** | **0.75x** | **0.66x** | **0.40x** |
| invindex count-fill | **0.87x** | **0.74x** | **0.53x** | **0.72x** | **0.37x** |
| invindex map growth | **0.86x** | **0.78x** | **0.75x** | **0.58x** | **0.43x** |
| invindex map count-fill | 1.00x | **0.85x** | **0.84x** | **0.85x** | **0.47x** |
| window string | **0.51x** | **0.58x** | **0.50x** | **0.57x** | **0.26x** |
| window int | 2.00x | - | 1.00x | **0.12x** | **0.40x** |
| dbquery | **0.99x** | - | **0.91x** | - | - |
| latency | 1.06x | 1.30x | **0.13x** | **0.33x** | **0.82x** |
| gcscan | **0.57x** | **0.45x** | **0.38x** | **0.47x** | **0.35x** |
| **geometric mean** | **0.91x** | **0.72x** | **0.52x** | **0.45x** | **0.39x** |

The aggregate is useful but not a claim that every row wins: Tycho loses to
C on `latency` (1.06x slower) and loses to Rust on `latency` (1.30x slower).
`window int` is a sub-millisecond row; do not read its 2.00x as a stable
throughput loss. Missing cells remain `-`.

The empty-process floor on this Linux host is `<1 ms` for Tycho/C/Rust/Go,
11 ms for Java and 1 ms for Swift (`make bench-floor`). Rows near that floor
are not evidence about throughput.


The core seven-row `prongB` scorecard gives the complementary view (geometric
mean over its complete rows): time **0.63x vs C, 0.38x vs Rust, 0.28x vs Go,
0.28x vs Java, 0.11x vs Swift**; memory (C/Rust/Go only) **0.80x, 0.74x,
0.28x**. The tree rows pull that average below 1.0; the flat rows are the
honest controls where C/Rust can win.

### Backend sensitivity

Tycho emits C, so a Tycho-vs-C number is only meaningful when both are built
by the same backend. The current headline rows use GCC. Re-running the
`latency` harness with `CC=clang` changes the shape substantially:

| backend | Tycho | C | Rust (rustc/LLVM) |
|---|---:|---:|---:|
| GCC | 127 ms | 124 ms | 100 ms |
| clang | 159 ms | 102 ms | 105 ms |

Tycho is C-class under GCC but loses to C and Rust under clang; this is a
code-generation/backend issue, not a reason to hide the row. Use
`sh bench/backends.sh` for the same-source comparison and report the backend
with every number. A clang-specific Tycho codegen fix is a separate target.

### Service-row coverage

`Y` means the row is implemented and run by the service harness; `-` means it
is explicitly unported.

| row | Tycho | C | Rust | Go | Java | Swift |
|---|---|---|---|---|---|---|
| winagg | Y | Y | Y | Y | Y | Y |
| latency | Y | Y | Y | Y | Y | Y |
| invindex growth | Y | Y | Y | Y | Y | Y |
| invindex count-fill | Y | Y | Y | Y | Y | Y |
| invindex map growth | Y | Y | Y | Y | Y | Y |
| invindex map count-fill | Y | Y | Y | Y | Y | Y |
| window string | Y | Y | Y | Y | Y | Y |
| window int | Y | Y | Y | Y | Y | Y |
| gcscan | Y | Y | Y | Y | Y | Y |
| dbquery | Y | Y | - | Y | - | - |

`dbquery` remains unported for Rust/Java/Swift: the Rust port would need a
libsqlite3 FFI binding, this host has no SQLite JDBC driver for Java, and no
Swift SQLite module. The `-` cells are queued work, not wins.

The service-shaped `winagg` row (per-window map of growing lists) now runs at
**82 ms versus C 97 ms**, Rust 176 ms, Go 296 ms, Java 277 ms and Swift 374 ms;
peak RSS is 7.7 MB versus C 5.9 MB. It is the row the map/list hash and
read-only-borrow work targeted.

## Arena internals, measured

The peak-RSS tables above are the memory model's *output*. What the model does
underneath — how much it reserves, how much it reuses, where the live set sits —
is observable directly: `TYCHO_ARENA_STATS=full <program>` prints a one-shot
summary at exit (`runtime/tycho_rt.c@stats_dump`): peak live bytes (the working-set
high-water), total bump-allocated bytes, the free-list recycle rate, OS-reserved
bytes over blocks, the block-pool reuse rate, and a per-function — now
per-statement, see below — residency table. At the default 64 KiB block size:

| workload | peak live | OS reserved | slack | block reuse | free-list recycle |
|----------|-----------|-------------|-------|-------------|-------------------|
| interp (5.6M-node tree interpreter) | 250.6 MiB | 250.8 MiB | 0.08% | 99.8% | 0 of 59.4M (held tree) |
| json (recursive-descent parse) | 37.6 MiB | 38.1 MiB | 1.3% | 99.8% | ~0 of 5.5M (held tree) |
| gcscan (large held set of small objects) | 63.5 MiB | 64.1 MiB | 0.9% | 37.3% | 0 of 6.0M (held) |
| trie (per-node arenas) | 57.2 MiB | 57.5 MiB | 0.5% | 0.1% | 0 of 1.5M (held) |
| dijkstra (index-graph) | 44.3 MiB | 44.4 MiB | 0.2% | 0.3% | 16 of 300k |
| lru (delete-heavy map churn) | 33.0 MiB | 33.3 MiB | 0.9% | 0.0% | 16 of 89 (18.0%) |
| strarr_build (loop-carried rebuild) | 224 B | 64.0 KiB | — | 50% | 1.0M of 22.0M (4.5%) |
| optstr_build (loop-carried rebuild) | 144 B | 64.0 KiB | — | 50% | 1.0M of 6.0M (16.7%) |
| loop_scratch (per-iteration scratch) | 40 B | 64.0 KiB | — | 50% | 0 of 10.0M |

Three things the numbers say that the RSS tables cannot:

1. **At scale the model is tight.** The big workloads hold their live set within
   0.1–1.3% of what they reserve from the OS; the blocks fit the bump almost
   exactly. The 64 KiB block only shows up as slack on toy programs (a 40 B
   peak over a 64 KiB block).
2. **Free-list recycling is workload-shaped, and now countable.** The
   liveness-driven recycle is zero
   where the working set is held (interp/json/gcscan/trie — trees stay live),
   and up to 17% of allocations where loop-carried rebuilds hand dead buffers
   back (optstr_build, strarr_build, structarr_build, lru). Before the counter
   existed the only evidence was bench-RSS deltas; the per-workload rate is
   now an instrumented number.
3. **Loop scratch peaks near zero.** The per-iteration `arena_reset` keeps a
   loop's live bytes at one iteration's worth: `loop_scratch` bumps 10M
   allocations and peaks at 40 B. Per-statement attribution shows it: on json, a loop
   labeled `main:loop0` bumps 16.4 MiB across 850k allocs with a 344 B peak —
   the per-iteration reset, now visible per statement instead of folded into
   the function total.

**Block size is a memory-only knob.** `TYCHO_BLOCK=<bytes>` overrides the
64 KiB default per run (read once at startup). A
sweep on the three big workloads:

| block | interp peak/OS | json peak/OS | lru peak/OS | wall (interp/json/lru) |
|-------|----------------|--------------|-------------|------------------------|
| 8 KiB | 250.6 / 251.5 MiB | 37.6 / 37.8 MiB | 33.0 / 33.0 MiB | 1.11 s / 0.14 s / 0.64 s |
| 64 KiB | 250.6 / 250.8 MiB | 37.6 / 38.1 MiB | 33.0 / 33.3 MiB | 1.10 s / 0.13 s / 0.66 s |
| 256 KiB | 250.6 / 251.0 MiB | 37.6 / 38.9 MiB | 33.0 / 34.3 MiB | 1.11 s / 0.14 s / 0.65 s |

Peak *live* is identical across sizes — the block size never moves the working
set, only the slack (≤4% either way here), and wall time is flat. The 64 KiB
default is not retuned.

## Speed
On allocation-heavy tree workloads and the JSON parser Tycho beats hand-written
C on time and memory; it trails C and Rust on flat array pipelines
(per-element bounds checks, not the memory model). The in-place append (an
`acc = acc + x` loop growing one buffer) is guarded by `make bench`: ~1.5 MB
where the un-optimized path is ~825 MB at the same N, so a 32 MB bound sits
firmly between working and broken.

## Where it costs

The full loss column, with the idiom for each case, is in
[docs/internals/value-semantics-limits.md](internals/value-semantics-limits.md).
The short version: pointer-shaped data (graphs, doubly-linked lists) costs more
unless you use the flat-pool idiom — hold all nodes in one array and link them
by integer index, which is also the cache-friendly layout data-oriented engines
choose on purpose. Arenas reclaim at *scope exit*, not incrementally, so a
long-lived scope holds its transients until it returns — scope them in an inner
function.
