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

## Arena internals, measured

The peak-RSS tables above are the memory model's *output*. What the model does
underneath — how much it reserves, how much it reuses, where the live set sits —
is observable directly: `TYCHO_ARENA_STATS=full <program>` prints a one-shot
summary at exit (`runtime/tycho_rt.c@stats_dump`): peak live bytes (the working-set
high-water), total bump-allocated bytes, the free-list recycle rate, OS-reserved
bytes over blocks, the block-pool reuse rate, and a per-function — now
per-statement, see below — residency table. Measured 2026-08-04 on this machine
over the bench workloads, at the default 64 KiB block size:

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
   liveness-driven recycle (`runtime/tycho_rt.c@arena_recycle`) is zero
   where the working set is held (interp/json/gcscan/trie — trees stay live),
   and up to 17% of allocations where loop-carried rebuilds hand dead buffers
   back (optstr_build, strarr_build, structarr_build, lru). Before the counter
   existed the only evidence was bench-RSS deltas; the per-workload rate is
   now an instrumented number.
3. **Loop scratch peaks near zero.** The per-iteration `arena_reset` keeps a
   loop's live bytes at one iteration's worth: `loop_scratch` bumps 10M
   allocations and peaks at 40 B. Per-statement attribution (`_scrN.name =
   "proc:loopN"`, a per-proc loop counter — stable under re-formatting, emitted
   at the S_WHILE/S_FOR/S_FORRANGE codegen sites) shows it: on json, a loop
   labeled `main:loop0` bumps 16.4 MiB across 850k allocs with a 344 B peak —
   the per-iteration reset, now visible per statement instead of folded into
   the function total.

**Block size is a memory-only knob.** `TYCHO_BLOCK=<bytes>` overrides the
64 KiB default per run (read once at startup, `runtime/tycho_rt.c:520@TYCHO_BLOCK`). A
sweep on the three big workloads, same machine:

| block | interp peak/OS | json peak/OS | lru peak/OS | wall (interp/json/lru) |
|-------|----------------|--------------|-------------|------------------------|
| 8 KiB | 250.6 / 251.5 MiB | 37.6 / 37.8 MiB | 33.0 / 33.0 MiB | 1.11 s / 0.14 s / 0.64 s |
| 64 KiB | 250.6 / 250.8 MiB | 37.6 / 38.1 MiB | 33.0 / 33.3 MiB | 1.10 s / 0.13 s / 0.66 s |
| 256 KiB | 250.6 / 251.0 MiB | 37.6 / 38.9 MiB | 33.0 / 34.3 MiB | 1.11 s / 0.14 s / 0.65 s |

Peak *live* is identical across sizes — the block size never moves the working
set, only the slack (≤4% either way here), and wall time is flat. The 64 KiB
default is not retuned.

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
