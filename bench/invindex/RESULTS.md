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

## Map-native form (`#2`: `m[k]` as a place)

The tables above use the *side-array* architecture: a `[string: int]` map (term →
slot) over a separate `[Posting]` array whose elements own the growing lists. That
indirection existed because, before in-place map-value mutation, a `[string: [int]]`
(term → its posting list directly) could only grow a list via
`tidx = map_set(tidx, term, push_a_copy(map_get(tidx, term), doc))` — an O(list)
copy of the whole value on *every* posting, i.e. O(n²) over the build. Unusable at
scale, so the side array carried the lists and the map carried only an int slot.

`#2` makes the map value a mutable place, so the index becomes exactly what it is —
`idx: [string: [int]]`, one posting appended with `push(idx[term], d)` (O(1)
amortized, no copy). Same LCG corpus and a single shared oracle
(`vocab=8000 checksum=540001838890`); `invindex_map.{hi,c,go}` all print it. Peak
RSS (`bench/peakrss.c`), best-of-three, `-O2` / `go build`:

| impl | peak RSS | time |
| ---- | -------: | ---: |
| C  (`Slot{char*; long* docs; n; cap}` table, realloc in place) | **37 MB** | 252 ms |
| Go (`map[string][]int`, `append`) | 58 MB | 308 ms |
| hier (`[string: [int]]`, `push(idx[term], d)`) | 64 MB | 328 ms |

Reading this honestly:

1. **The natural data structure now competes.** hier is ~par with Go (1.10× memory,
   1.06× time) and ~1.7× C memory / ~1.3× C time. This is the *same* build-and-hold
   grow-and-abandon penalty plain arrays show (§ above): each geometric doubling of a
   posting list abandons a buffer the bump arena can't `realloc` in place, where C's
   `realloc` and Go's grow-in-place reclaim it. The map value arrays grow exactly like
   standalone arrays — **no map-specific regression**, just the array story applied
   inside a map.

2. **The `reserve` count-fill mitigation does NOT reach into a map value.** For plain
   arrays, sizing each list once (`reserve`) closed the gap to ~1.04× C. There is no
   `reserve(idx[term], n)`: `m[k]` is a place only for `push` / assign / `op=`, and
   `reserve`'s argument is rejected as an `m[k]` read. So the map-native form is stuck
   on the growth architecture's ~1.7× — the honest boundary the design predicted. A
   future `reserve(m[k], n)` (gate `reserve`'s first arg as a place, like `push`) would
   let a count-then-fill pass reach map values and likely recover the same near-parity.

The win of `#2` here is **expressiveness with competitive cost**: the term → list map
went from O(n²)-and-impractical to O(n) and within ~10% of Go, paying only the arena's
already-documented growth penalty vs C.

## Reproduce

    cc -O2 -o /tmp/peakrss bench/peakrss.c
    for v in invindex invindex_exact; do
      ./hierc bench/invindex/$v.hi -o /tmp/h && /tmp/peakrss /tmp/h
      cc -O2 -o /tmp/c bench/invindex/$v.c 2>/dev/null && /tmp/peakrss /tmp/c
    done
    ( cd bench/invindex && go build -o /tmp/g invindex.go ) && /tmp/peakrss /tmp/g

    # map-native form (#2):
    ./hierc bench/invindex/invindex_map.hi -o /tmp/h && /tmp/peakrss /tmp/h
    cc -O2 -o /tmp/c bench/invindex/invindex_map.c && /tmp/peakrss /tmp/c
    ( cd bench/invindex && go build -o /tmp/g invindex_map.go ) && /tmp/peakrss /tmp/g
