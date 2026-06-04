# Inverted-index build — head-to-head (hier vs C vs Go)

A *build-and-hold* workload: build a deterministic inverted index from a corpus
of 300 000 documents × 12 words drawn from an 8 000-term vocabulary (~3.6 M term
occurrences, ~3.6 M distinct (term, doc) postings), then run one boolean-AND
query and checksum the whole index. Every language runs the identical algorithm
over the identical LCG sequence, so all three print the same oracle line:

    vocab=8000 checksum=7236429 and01=0

Peak RSS (via `bench/peakrss.c`) and best-of-three wall time, `-O2` / `go build`:

| impl | peak RSS | time | memory model |
| ---- | -------: | ---: | ------------ |
| C    |    71 MB | 338 ms | manual `malloc`/`realloc`/`free` + a hand-written string→slot hash map |
| Go   |    98 MB | 438 ms | GC + built-in `map[string]int`, `append`-grown slices |
| hier |   127 MB | 436 ms | implicit arenas + `[string:int]` map over a `[Posting]` array |

## Reading this honestly

This is a workload the arena model **loses** on memory — ~1.8× C, and above
Go's GC — and it is worth stating plainly, because it is the exact mirror image
of where the arena wins (binary-trees, json-parse: transient/tree structures the
arena bulk-frees, see `../prongB/RESULTS.md`).

**Why.** The index is *built once and held* — there is no scope exit to
bulk-free, so the arena's headline advantage never fires. What is left is its
allocation cost on thousands of *independently growing* arrays (each Posting's
`docs`/`freqs` lists). Two structural facts compound:

1. **A bump arena cannot `realloc` in place.** Growing a posting list means
   allocating a new, larger buffer and copying — the old buffer is abandoned.
   The runtime *does* hand it back (`arena_recycle` on grow, verified in
   `runtime/hier_rt.c:hier_arr_int_push`).
2. **But the recycle free-list reuses by best fit in `[n, 2n]`.** A small
   abandoned buffer can't satisfy a later, larger growth request, so
   size-mismatched dead buffers accumulate as resident memory — the arena never
   returns blocks to the OS mid-run. `malloc`/`free` (size classes, coalescing)
   recycle a freed buffer of any size for any request, so C holds less dead
   space; Go's GC compacts none here but its `map`/slice growth is tighter.

This is a **constant-factor** overhead (geometric slack + mismatched dead
buffers, both proportional to the live data), not a leak: the same program under
LeakSanitizer is clean (`examples/invindex.hi`), and the structure is freed in
full at scope exit. hier is **competitive on time** (equal to Go, ~1.3× C),
where the bump allocator's speed offsets the extra copying.

**Takeaway.** The arena is the right default for the transient- and tree-shaped
allocation that dominates compilers, parsers, and request handlers; a
long-lived, many-growing-arrays index is the case where a general allocator's
size-classed reuse wins, and an arena would need size-class free-lists or
block-level reclamation to match. Mapping that boundary is the point.

## Reproduce

    cc -O2 -o /tmp/peakrss bench/peakrss.c
    ./hierc bench/invindex/invindex.hi -o /tmp/ii && /tmp/peakrss /tmp/ii
    cc -O2 -o /tmp/iic bench/invindex/invindex.c && /tmp/peakrss /tmp/iic
    ( cd bench/invindex && go build -o /tmp/iig invindex.go ) && /tmp/peakrss /tmp/iig
