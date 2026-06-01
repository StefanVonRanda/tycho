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
| **Hier** (idiomatic)   |  **33 MB** | **419 ms** |
| Hier (loop-scoped)     |    33 MB |    400 ms |
| C (malloc/free)        |    33 MB |    794 ms |
| Rust (Box)             |    33 MB |    844 ms |
| Go (GC)                |    36 MB |   1508 ms |

(Before the transient-placement compiler fix, the idiomatic Hier row was
153 MB / 765 ms — see below. Both Hier forms are now identical and C-class.)

## What this shows

**The implicit-arena model is genuinely systems-grade.** Loop-scoped Hier
matches C and Rust on peak memory (33 MB) and is the *fastest of all on wall
time* — faster than hand-written `malloc`/`free` C. The reason is exactly
the thesis: each iteration's tree is bump-allocated into the loop's scratch
arena and reclaimed by a single `arena_reset` (free a short block list), so
Hier pays neither C's per-node `free` traversal, nor Rust's recursive
`Drop`, nor Go's GC. Value semantics + lexical arenas, no reference counts,
no GC — and it lands at the C end of the spectrum.

**The idiomatic form is now C-class too (compiler fix landed).** Originally
it cost 153 MB: in `sum = sum + check(make(d))` the transient tree from
`make(d)` does not escape the statement — `check` returns an `int` — but it
was allocated in the assignment target's arena, and `sum` lives in the
*outer* depth-loop, so the trees accumulated across the whole inner loop and
were reclaimed only when the outer scope reset. The fix: when an
assignment's result is non-heap (a scalar), nothing heap escapes the
statement, so the RHS — and its heap transients — is built in the *current*
scope (the innermost loop scratch, reset every iteration) instead of the
target's arena. `bench/transient` guards it (~1 MB with the fix vs ~201 MB
without). No source change needed: the idiomatic `check(make(d))` now lands
at 33 MB on its own.

The heap-target half is handled too (second fix landed): a user function
call's *arguments* are transients — value semantics guarantees the call's
return value is freshly owned and never aliases an argument — so they are now
built in the current scope regardless of where the result goes. Even with a
heap accumulator, `acc = describe(check(make(d)))` keeps the big `make(d)`
tree (a call argument) in the loop scratch (`bench/heap_transient`: ~2 MB vs
~201 MB). What remains is narrower: a transient feeding a *constructor* field
genuinely escapes into the stored value (correctly kept), and binop operands
could also be scoped — minor next to the call-argument case.

## Caveats / TODO

- Single machine, single run; numbers are representative, not averaged.
- **Koka (Perceus reference-counting + reuse) is the most direct rival** —
  same FBIP idea via runtime refcounts instead of arenas — but it is not
  installed here. Adding it is the missing comparison.
- One workload. binary-trees stresses allocate/reclaim; a tree-*rewrite*
  workload (closer to `examples/optimize.hi`, exercising the match-arm
  borrow + construction reuse) is a worthwhile second data point.
