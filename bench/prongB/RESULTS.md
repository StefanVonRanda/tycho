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
koka 3.2.3), peak RSS via `bench/peakrss.c`. **Re-measured after the arena
tuning (block pool + block-retaining `arena_reset`, commit 4b18b89)** — the
exact loop-scratch fast path this doc previously flagged as a TODO. Only the
Hier rows changed (same machine; C/Rust/Go/Koka are unchanged), isolating the
effect of that compiler change:

| language               | peak RSS | wall time | (was) |
| ---------------------- | -------: | --------: | ----: |
| Koka (Perceus RC+reuse)|    14 MB |    268 ms | 14 MB |
| **Hier** (loop-scoped) |  **25 MB** | **202 ms** | 33 MB / 399 ms |
| Hier (idiomatic)       |    25 MB |    206 ms | 33 MB / 400 ms |
| C (malloc/free)        |    33 MB |    804 ms | 33 MB |
| Rust (Box)             |    33 MB |    879 ms | 33 MB |
| Go (GC)                |    33 MB |   1528 ms | 36 MB |

Two compiler changes moved Hier here. **(a) Block-retaining `arena_reset`**
(4b18b89) cut time ~2× (scoped 399→197 ms): each iteration's scratch arena
keeps its block and rewinds it instead of re-`malloc`ing. **(b) Compact node
representation + 8-byte arena alignment** cut peak memory 33→25 MB: an enum
value is now a pointer to a single `{ tag; union of variant fields }` cell (one
allocation per node, fields inline, nullary variants share a static singleton),
replacing the old `{tag, void* payload}` descriptor + separately-allocated
payload. A `Tree` node drops 32→24 B — but only realized once arena alignment
went 16→8 B (Hier's max type alignment is 8: long/double/pointer), since 24 B
rounds back to 32 under 16 B alignment.

**Hier now beats hand-written `malloc`/`free` C, Rust, and Go on BOTH axes** of
binary-trees: lower memory (25 vs 33 MB) and ~4× faster. It is faster than Koka
on wall time (202 vs 268 ms); Koka keeps the memory lead (14 MB) because Perceus
frees the depth-19 stretch tree the instant its refcount hits zero, while the
arena holds it until scope exit — a structural difference, not node width. The
compact-node change closed ~42% of the memory gap to Koka (19→11 MB).

## What this shows

**The implicit-arena model is genuinely systems-grade.** Hier matches C and
Rust on peak memory (33 MB) and *beats hand-written `malloc`/`free` C on wall
time by ~3-4x* (197 ms vs 788 ms), with Rust close behind C and Go ~2x slower
than C. The reason is exactly the thesis: each iteration's tree is
bump-allocated into the loop's scratch arena and reclaimed by a single
`arena_reset` (now an O(1) block rewind), so Hier pays neither C's per-node
`free` traversal, nor Rust's recursive `Drop`, nor Go's GC. Value semantics +
lexical arenas, no reference counts, no GC — and it lands at the *fast* end of
the spectrum.

**Hier now also edges Koka on wall time here, while Koka keeps the memory
crown.** At 14 MB Koka is less than half Hier's 33 MB peak, but at 278 ms it is
now slower than Hier's loop-scoped 197 ms (and ~matches the idiomatic 263 ms).
binary-trees is almost pure allocate-and-immediately-discard — the best case
for reference-counting-with-reuse: Perceus frees each short-lived tree the
instant its count hits zero (so only the one long-lived tree is ever retained)
and its node layout is tighter than Hier's tagged `{tag, payload-ptr}` cell,
which is why Koka holds the memory lead. Hier keeps a whole iteration's tree in
the scratch arena until the next reset, and carries 64 KB-block overhead — so
the one remaining target on this workload is a more compact node representation
(and tighter block sizing). The arena model is not failing here at all: it is
C-class memory and now best-in-class time, with no GC and no per-object
refcount traffic. (Where Hier's *reuse* wins outright — in-place rewrite, the
`bench/treewalk`/`comb_build` cases, and the tree-rewrite workload below — is a
shape binary-trees does not exercise.)

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

| language               | peak RSS | wall time | (was, pre-tuning) |
| ---------------------- | -------: | --------: | ----------------: |
| **Hier** (arenas)      |  **7 MB** | **116 ms** | 7 MB / 379 ms |
| Koka (Perceus RC+reuse)|     7 MB |    183 ms | 7 MB / 184 ms |
| Rust (Box)             |     9 MB |    432 ms | 9 MB / 409 ms |
| C (malloc/free)        |    13 MB |    585 ms | 13 MB / 531 ms |
| Go (GC)                |    21 MB |    840 ms | 21 MB / 845 ms |

**Hier now wins this workload outright — lowest memory AND fastest.** At
7 MB / 116 ms it is tied for lowest peak (with Koka) and faster than every
other language including Koka (183 ms). This is the result the block-retaining
`arena_reset` was meant to produce, and it flipped the prior outcome: the doc
above previously noted "Koka still wins wall time (184 ms) … Hier
bump-allocates a fresh result each iteration and `arena_reset` returns the
blocks to the OS — so the next iteration re-`malloc`s them," and named "an
`arena_reset` that *retains* blocks for reuse" as the fix. With that fix
landed (4b18b89), Hier's per-iteration rewrite cost dropped 379→116 ms: the
loop's scratch arena keeps its block and rewinds it (`off=0`), so each map
re-fills the same memory with zero allocator traffic — the arena analogue of
Perceus's in-place reuse, but as a single pointer reset rather than per-cell
refcount decrements. The remaining Hier target is the compact node
representation (Koka's tighter cell layout is why it ties on memory despite
Hier's O(1) reclaim).

## Caveats / TODO

- Single machine, single run; numbers are representative, not averaged.
- Koka built with `koka -O2` (the `gcc-drelease` variant — optimized C
  backend with NDEBUG; debug *symbols* only, which don't affect runtime).
- Two workloads now; both are tree-shaped. A non-tree workload (e.g. a
  string/array pipeline) would round out the picture.
- **Block-retaining `arena_reset` for loop scratch: DONE** (commit 4b18b89) —
  it cut binary-trees ~2x and tree-rewrite ~3.3x, flipping tree-rewrite to a
  Hier win on both axes and binary-trees to a Hier win on time. The wall-time
  gap to Koka is closed (Hier now leads on both workloads' time).
- Remaining Hier improvement target: a **compact node representation** —
  Koka's tighter cell layout is the sole reason it still holds the memory lead
  on binary-trees (14 vs 33 MB) and ties on tree-rewrite. This is the same
  target prong-B's compiler profiling deprioritized for the *self-compile*
  workload (where it was ~2% of time), but it is the live lever here.
