# Inverted-index build — head-to-head (hier vs C vs Go)

A *build-and-hold* workload: build a deterministic inverted index from a corpus
of 300 000 documents × 12 words drawn from an 8 000-term vocabulary (~3.6 M term
occurrences, ~3.6 M distinct (term, doc) postings), then run one boolean-AND
query and checksum the whole index. Every language runs the identical algorithm
over the identical LCG sequence, so all variants print the same oracle line:

    vocab=8000 checksum=7236429 and01=0

Peak RSS (via `bench/peakrss.c`), best-of-three, `-O2` / `go build`. Two program
architectures: **growth** (the naive idiom — `push`/`append`/`realloc` grow each
posting list geometrically) and **count-fill** (a first pass counts each term's
postings; every list is then allocated ONCE at its exact size, no growth):

| impl | growth | count-fill |
| ---- | -----: | ---------: |
| C    | 71 MB / ~340 ms | **57 MB** / 533 ms |
| Go   | 96 MB / 440 ms  | — |
| hier | 127 MB / 415 ms | **59 MB** / 596 ms |

## Reading this honestly

Two facts, and neither is "hier wins":

1. **On the naive (growth) architecture, the arena loses ~1.8× C** (127 vs 71 MB).
   This is the build-and-hold case the arena is worst at: the index is held to
   the end, so its bulk-free advantage never fires, and a bump arena cannot
   `realloc` in place — every geometric growth abandons a buffer. The runtime
   recycles them (`arena_recycle` on grow), but the last doubling of each list
   abandons a large buffer no *later* list needs (demand-expired), so they sit as
   resident memory where `malloc`'s size-classed free lists would reclaim them.
   (Confirmed: raising the recycle free-list cap 32→256 changed nothing — these
   buffers have no taker, it isn't a findability problem a size-classed list
   would fix.)

2. **With the architecture matched (count-fill), the gap essentially closes: 59 MB
   vs 57 MB — ~1.04× C, near parity.** Sizing each list once eliminates both the
   2× geometric slack and the abandonment. So on this workload the *architecture*
   dominates the *memory model*: allocate-once is ~par with C in either; naive
   growth is where the bump arena pays.

## What `reserve` is, and whether it dents the thesis

`reserve(arr, n)` preallocates an array's buffer to exact capacity `n` (it
doesn't change the length; later `push`es up to `n` don't grow). It is a
**capacity hint** — the exact analogue of Rust's `Vec::with_capacity`, Go's
`make([]T, 0, n)`, C++'s `vector::reserve`. It frees nothing, exposes no pointer,
cannot alias or dangle; the buffer is still arena-owned and auto-reclaimed at
scope exit. So it is *not* a step back toward manual memory management, and it
does not break the value-semantic / implicit-arena thesis: memory stays implicit
and safe.

What it *does* do is sharpen an honest caveat. The thesis is "memory is implicit,
safe, and automatically reclaimed, with performance competitive with manual
management" — **not** "optimal memory with zero thought." This workload shows the
boundary precisely: by default (no hint) the arena is correct and time-competitive
but ~1.8× heavier here; to reach peak memory on a long-lived, many-growing-array
structure you reach for the same `reserve` hint you would in Rust or Go, and then
it lands at parity with C. The arena removes the *unsafe* and *manual-lifetime*
burden, not the occasional capacity hint every language keeps for the hot path.

hier trails C by ~1.2× on **time** in count-fill (the second counting pass); the
bump allocator's speed offsets the extra copying in growth, where it equals Go.

## Reproduce

    cc -O2 -o /tmp/peakrss bench/peakrss.c
    for v in invindex invindex_exact; do
      ./hierc bench/invindex/$v.hi -o /tmp/h && /tmp/peakrss /tmp/h
      cc -O2 -o /tmp/c bench/invindex/$v.c 2>/dev/null && /tmp/peakrss /tmp/c
    done
    ( cd bench/invindex && go build -o /tmp/g invindex.go ) && /tmp/peakrss /tmp/g
