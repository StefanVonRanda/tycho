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

Representative single run, one machine (gcc 15.2, rustc 1.93, go 1.26,
koka 3.2.3), peak RSS via `bench/peakrss.c`:

| language               | peak RSS | wall time |
| ---------------------- | -------: | --------: |
| Koka (Perceus RC+reuse)|    14 MB |    271 ms |
| **Hier** (idiomatic)   |  **33 MB** | **400 ms** |
| Hier (loop-scoped)     |    33 MB |    399 ms |
| C (malloc/free)        |    33 MB |    784 ms |
| Rust (Box)             |    33 MB |    840 ms |
| Go (GC)                |    37 MB |   1537 ms |

(Before the transient-placement compiler fix, the idiomatic Hier row was
153 MB / 765 ms — see below. Both Hier forms are now identical and C-class.)

## What this shows

**The implicit-arena model is genuinely systems-grade.** Hier matches C and
Rust on peak memory (33 MB) and *beats hand-written `malloc`/`free` C on wall
time* (400 ms vs 784 ms), with Rust close behind C and Go ~2x slower. The
reason is exactly the thesis: each iteration's tree is bump-allocated into
the loop's scratch arena and reclaimed by a single `arena_reset` (free a
short block list), so Hier pays neither C's per-node `free` traversal, nor
Rust's recursive `Drop`, nor Go's GC. Value semantics + lexical arenas, no
reference counts, no GC — and it lands at the C end of the spectrum.

**Koka (Perceus) wins this particular benchmark — honestly.** At 14 MB /
271 ms it is both lower-memory and faster than Hier here. binary-trees is
almost pure allocate-and-immediately-discard, which is the best case for
reference-counting-with-reuse: Perceus frees each short-lived tree the instant
its count hits zero (so only the one long-lived tree is ever retained), and
Koka's node layout is tighter than Hier's tagged `{tag, payload-ptr}` cell.
Hier instead keeps a whole iteration's tree in the scratch arena until the
next reset, and carries 64 KB-block overhead. The gap is not the arena model
failing — Hier is still C-class with no GC and no per-object refcount traffic
— but it marks two concrete improvement targets: a more compact node
representation, and tighter arena block sizing. (Where Hier's *reuse* wins —
in-place rewrite, the `bench/treewalk`/`comb_build` cases — is a workload
binary-trees does not exercise; see the tree-rewrite TODO below.)

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

## Second workload: tree-rewrite (map a persistent tree)

binary-trees is pure allocate-and-discard. The second workload is a
destructure→reconstruct *rewrite*: build one persistent tree (depth 16), then
200 times map every node to a fresh tree and checksum it, discarding each
result. This is the shape `examples/optimize.hi` runs, and it stresses
*reclamation of a transient rewrite result*: Hier resets the loop scratch in
O(1); a refcounting system decrements every node of the dropped result.

`maptree.{hi,c,rs,go}`, `maptree.kk`. Same byte-identical output (26214400):

| language               | peak RSS | wall time |
| ---------------------- | -------: | --------: |
| Koka (Perceus RC+reuse)|     7 MB |    184 ms |
| **Hier** (arenas)      |  **7 MB** | **379 ms** |
| Rust (Box)             |     9 MB |    409 ms |
| C (malloc/free)        |    13 MB |    531 ms |
| Go (GC)                |    21 MB |    845 ms |

Here Hier is the **lowest-memory** of all (tied with Koka at 7 MB) and second
on time, beating Rust, C, and Go on both axes — a better showing than
binary-trees, because the rewrite result is a transient the arena frees in
O(1) rather than node-by-node. Koka still wins wall time (184 ms): its Perceus
**reuse** turns each iteration's allocate-then-drop into a free-list hit (the
dropped tree's cells are reused in place for the next map, so no fresh
allocation), and its node layout is tighter. Hier bump-allocates a fresh
result each iteration and `arena_reset` returns the blocks to the OS — so the
next iteration re-`malloc`s them. Two concrete Hier targets, the same as
binary-trees flagged: a compact node representation, and an `arena_reset` that
*retains* blocks for reuse instead of freeing them (the loop-scratch fast
path).

## Caveats / TODO

- Single machine, single run; numbers are representative, not averaged.
- Koka built with `koka -O2` (the `gcc-drelease` variant — optimized C
  backend with NDEBUG; debug *symbols* only, which don't affect runtime).
- Two workloads now; both are tree-shaped. A non-tree workload (e.g. a
  string/array pipeline) would round out the picture.
- Hier improvement targets surfaced here: compact node representation;
  block-retaining `arena_reset` for loop scratch; both would narrow the
  wall-time gap to Koka.
