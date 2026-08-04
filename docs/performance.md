# Performance and memory

Tycho's claims are measured, not asserted — every program in this file runs on
`tychoc`, its output checked against a golden, and the figures are
machine-specific: the cross-language *ratios* are the claim, not the absolute
times.

## A real program at flat memory

[`examples/json.ty`](../examples/json.ty) is a 220-line recursive-descent JSON
parser over a recursive `Json` sum type — real recursion, zero
`malloc`/`free`/refcount/GC in the source. Parsing **5,000,000** documents in a
loop holds at a **flat 10 MB**: each document's tree is reclaimed when its loop
iteration's arena resets. Clean under ASan + LeakSanitizer.

## Head-to-head, five languages

Measured on an AMD Ryzen 7 7735HS (16 threads), Debian; full toolchains and
per-workload timings in [`bench/prongB/RESULTS.md`](../bench/prongB/RESULTS.md).
Peak resident memory (MB) — every binary computes the same checksum; lower is
better:

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

## Speed

Tycho runs in C's class: it is faster than hand-written C on the
allocation-heavy tree workloads (binary-trees, tree-rewrite) and on the JSON
parser, and it trails C and Rust on the flat array-pipeline (per-element bounds
checks, not the memory model). It is *not* a bid to be the fastest language —
absolute wall times are machine-, governor-, and toolchain-specific, so the
cross-language *ratios* are the claim, not the times. The in-place append (an
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
