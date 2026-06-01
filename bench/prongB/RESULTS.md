# Prong B — head-to-head memory benchmark

Does the implicit-arena + value-semantics model actually hold up against
manual memory (C), an ownership/borrow language (Rust), a GC (Go), on a
memory-heavy workload? This is the empirical half of the thesis: not "it
passes a 40-program suite", but "here are the numbers next to the other
memory models."

## Workload

**binary-trees** (Computer Language Benchmarks Game): allocate one big
long-lived tree, then build and discard a balanced sea of short-lived trees
of increasing depth, checksumming node counts. It is the canonical
allocator / GC / reuse benchmark — almost pure allocate-and-reclaim of
pointer-structured data.

Same algorithm and the **same byte-identical output** in all five binaries
(the cross-language correctness check, like the differential test suite).
`maxDepth = 18`.

- `binary_trees.hi`        — Hier, idiomatic `check(make(d))`
- `binary_trees_scoped.hi` — Hier, transient bound to an inner-loop local
- `binary_trees.c`         — C, manual `malloc`/`free` per node
- `binary_trees.rs`        — Rust, `Box`-owned enum, RAII drop
- `binary_trees.go`        — Go, pointer structs, garbage collected

Run it: `sh bench/prongB/run.sh` (needs `cc`; uses `rustc`/`go` if present).

## Results

Representative single run, one machine (gcc 15.2, rustc 1.93, go 1.26),
peak RSS via `bench/peakrss.c`:

| language               | peak RSS | wall time |
| ---------------------- | -------: | --------: |
| **Hier** (loop-scoped) |  **33 MB** | **409 ms** |
| C (malloc/free)        |    33 MB |    794 ms |
| Rust (Box)             |    33 MB |    844 ms |
| Go (GC)                |    36 MB |   1508 ms |
| Hier (idiomatic nest)  |   153 MB |    765 ms |

## What this shows

**The implicit-arena model is genuinely systems-grade.** Loop-scoped Hier
matches C and Rust on peak memory (33 MB) and is the *fastest of all on wall
time* — faster than hand-written `malloc`/`free` C. The reason is exactly
the thesis: each iteration's tree is bump-allocated into the loop's scratch
arena and reclaimed by a single `arena_reset` (free a short block list), so
Hier pays neither C's per-node `free` traversal, nor Rust's recursive
`Drop`, nor Go's GC. Value semantics + lexical arenas, no reference counts,
no GC — and it lands at the C end of the spectrum.

**The 153 MB idiomatic row is a compiler arena-placement gap, not a model
limit.** In `sum = sum + check(make(d))` the transient tree from `make(d)`
does not escape the statement — `check` returns an `int` — but it is
allocated in the assignment target's arena, and `sum` lives in the *outer*
depth-loop, so the trees accumulate across the whole inner loop and are only
reclaimed when the outer scope resets. Binding the transient to an
inner-loop local (`t := make(d)`) puts it in the inner scratch, reset every
iteration, and the cost collapses to the C-class 33 MB / 409 ms row above —
a one-line source change with identical output.

The proper fix is in the compiler: a heap sub-expression whose allocation
does not flow into the stored value (or into any value outliving the current
scope) should be allocated in the *current* (innermost loop scratch) arena,
not the assignment target's. That would make the idiomatic form C-class
automatically. Tracked as the next fundamentals task.

## Caveats / TODO

- Single machine, single run; numbers are representative, not averaged.
- **Koka (Perceus reference-counting + reuse) is the most direct rival** —
  same FBIP idea via runtime refcounts instead of arenas — but it is not
  installed here. Adding it is the missing comparison.
- One workload. binary-trees stresses allocate/reclaim; a tree-*rewrite*
  workload (closer to `examples/optimize.hi`, exercising the match-arm
  borrow + construction reuse) is a worthwhile second data point.
