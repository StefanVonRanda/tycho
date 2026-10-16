# Allocation churn / aggregate GC-pause statistics

A long-running service cares about jitter as well as total time and memory.
This benchmark measures allocation churn and **aggregate GC statistics**, not
request or round latency distributions. It does not establish pause-free
execution, tail percentiles or a worst-case latency bound for any language.

Workload: 2000 rounds, each builds and discards a 100k-element working set. Tycho
resets a loop scratch arena for reuse; C `free`s its allocation; Go's GC collects
the garbage. All three print the same checksum (`599376507`). Reset rewinds the
head block and walks overflow blocks into a reuse pool; only the single-block
fast path is O(1), not arbitrary arena cleanup. Pooled blocks can remain resident.

## Results (`make bench-latency`)

Native builds at `-O3` (each language's standard optimized build: tycho `-O3`,
C `-O3`, `go build`). Historical measurements, re-measured 2026-06-07; these are
not measurements of the current working tree.

| lang | peak RSS | wall | GC statistics |
|------|---------:|-----:|---------------|
| **tycho** | 4.5 MB | 267 ms | **none — no GC** |
| C    | 2.3 MB | 121 ms | **none — no GC** |
| Go   | 11.4 MB | 1831 ms | **2927 collections, ~211 ms total pause** |

Go reports collection count and cumulative stop-the-world pause time through
`runtime.ReadMemStats`. Tycho and C have no collector, so no GC pause; their
allocation, cleanup and scheduling delays are not measured separately here.

### macOS (Apple Silicon, Darwin 25.5, clang 21 / go 1.26.4 — 2026-06-09)

Another recorded run (`sh bench/latency/run.sh`, checksum `599376507`):
tycho **4.4 MB / 243 ms, zero GC**; C 2.1 MB / 300 ms, zero GC; Go 18.0 MB / 986 ms
with **2650 collections, ~71 ms total pause**. Fair `-O3` best-of-3
(`bench/fair_full.sh`): tycho 4.4 MB/115 ms, C 2.1 MB/69 ms, Go 16.7 MB/557 ms.
These totals likewise do not show tail latency. (Peak-RSS unit bug in the runner
was fixed first — see the [prongB results](../prongB/RESULTS.md).)

## The point

- **Tycho has no GC pause and needs no manual `free` in this program.** Scope
  cleanup is explicit in generated code, but its cost depends on block count and
  bookkeeping. Platform allocation, page faults and scheduling can still stall it.
- **Go reports ~211 ms cumulative stop-the-world pause across 2927 collections.**
  That is neither 211 ms of continuous blocking nor a per-request tail-latency
  measurement, and it is not total time spent doing concurrent GC work.
- Tycho's recorded total wall is below Go's here (267 vs 1831 ms, ~6.8×). This
  comparison does not isolate GC cost: Tycho and Go grow their arrays, while C
  allocates the exact size once. Compiler, allocator and growth-loop costs also
  contribute; the wall-time difference cannot all be attributed to GC pauses.

## Where this sits in the picture

Together with the memory benchmarks this adds one limited axis of evidence:
- **memory** (prongB / dbquery / winagg / invindex / window): measured RSS for
  specific workloads, not a universal memory bound or an OS-release guarantee.
- **latency** (here): total wall/RSS and Go's aggregate GC counters. No per-round
  samples, p95/p99/p999, maximum cleanup time or request-load experiment are
  recorded by this runner. Those require a separate measurement before making
  service tail-latency claims.

**Reproduce:** `make bench-latency` (skips Go if absent). A successful run checks
checksums and reports totals; it does not certify jitter-free execution.
