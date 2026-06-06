# Sliding-window eviction — the arena's boundary (honest)

This is the workload class that attacks the value-semantic-arena thesis at its
weakest point, so it gets measured head-on rather than skipped. A stream of N
records where only the last W are live (a sliding window / eviction / LRU shape).
malloc and a GC reclaim each evicted record, so peak RSS tracks the **window**.
A no-individual-free arena keeps every record it ever allocated, so for
**heap-bearing** records peak RSS tracks the **stream**.

## Results (2,000,000-item stream, 50,000 window; `sh bench/window/run.sh`)

Records are heap-bearing strings (`"rec" + i%100000`); all ports print the same
checksum `15777800`. Peak RSS via `bench/peakrss`.

| record kind            | hier | C | Go |
|------------------------|-----:|--:|---:|
| **string (heap)**      | **47.3 MB** | 3.3 MB | 7.6 MB |
| **int (fixed-size)**   | **2.2 MB** | ~bounded | ~bounded |

## The finding — honest, and precise

- **Heap-bearing window records: hier loses ~14× to C, ~6× to Go.** Storing a
  string into the ring deep-copies it into the ring's arena (value semantics — the
  array must own it, or it would dangle). Overwriting a slot drops the *reference*
  to the evicted string, but the arena frees only at scope exit, so its **bytes
  stay**. Peak RSS ≈ the whole 2M-record stream, not the 50k window. This is the
  fundamental cost of "no individual free."
- **Fixed-size window records: hier is bounded (2.2 MB), ties C/Go.** An `int` (or
  a struct of scalars) lives *inside* the ring slot, so overwriting reuses the
  slot — no per-element heap, nothing accumulates. So the boundary is specifically
  about **heap-bearing** records under eviction, not sliding windows in general.

## Why this matters (and the mitigations)

This is the genuine flip side of the wins in `../prongB` (binary-trees: hier 25 MB
**beats** C's 33 MB; transient pipelines competitive) and `../dbquery` (hier ≈ C on
a real DB workload). The arena is excellent for **transient, scope-shaped** lifetimes
and for **fixed-size** retained data; it is the **wrong tool** for evicting
heap-bearing records from a long-lived collection, where peak grows with the stream.

Mitigations, in order of how idiomatic they are:
1. **Pack records into fixed-size slots** (encode the value into a fixed-width
   field / a parallel scalar array) → bounded, ties C/Go. Loses the string
   ergonomics.
2. **Accept it** when the stream is bounded — 47 MB for 2M records is modest in
   absolute terms; many workloads never evict.
3. **Periodic compaction into a fresh arena** is the textbook fix, but hier has **no
   way to reset or free an arena mid-function** (frees happen only at scope exit /
   per-loop-iteration). So a true in-place sliding window of heap records can't be
   compacted without restructuring the whole computation into scope-sized batches —
   a real ergonomic limit of the model, recorded here rather than hidden.

**Verdict:** the thesis is "competitive C-class memory with no manual management,"
and that holds across transient churn, deep recursion, fixed-size retention, and
real-library work. It does **not** hold for eviction of heap-bearing records — and
that boundary is now mapped with numbers, alongside the wins, so the picture isn't
cherry-picked.

**Reproduce:** `make bench-window` (skips Go if absent).
