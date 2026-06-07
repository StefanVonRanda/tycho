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

> **Re-measured at `-O3` (2026-06-07): numbers hold within noise** — this is a
> memory/`realloc`-bound build, not compute-bound, so the optimizer barely moves it.
> Side-array growth hier 126 MB/414 ms, C 72/331, Go 96/437; count-fill hier 60/561,
> C 57/500. Map-form growth hier 64/302, C 37/245, Go 60/293; count-fill hier 32/517,
> C 30/400, Go 67/522. All ratios (and the conclusions below) are unchanged.

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

As with the side-array form, two architectures — **growth** (`push(idx[term], d)`
grows each posting list geometrically) and **count-fill** (a first pass counts each
term's postings with `cnt[term] += 1`, then `reserve(idx[term], count)` sizes every
list ONCE before filling — `reserve` reaches into the map value, also `#2`):

| impl | growth | count-fill |
| ---- | -----: | ---------: |
| C    (`Slot{char*; long* docs; n; cap}` table) | 37 MB / 252 ms | **29 MB** / 420 ms |
| Go   (`map[string][]int`) | 58 MB / 308 ms | 64 MB / 533 ms |
| hier (`[string: [int]]`) | 64 MB / 328 ms | **31 MB** / 547 ms |

Reading this honestly:

1. **The natural data structure now competes.** On growth, hier is ~par with Go
   (1.10× memory, 1.06× time) and ~1.7× C memory / ~1.3× C time — the *same*
   build-and-hold grow-and-abandon penalty plain arrays show (§ above): each geometric
   doubling of a posting list abandons a buffer the bump arena can't `realloc` in place,
   where C's `realloc` reclaims it. The map value arrays grow exactly like standalone
   arrays — **no map-specific regression**, just the array story applied inside a map.

2. **`reserve(idx[term], n)` reaches into a map value and closes the gap.** Sizing each
   posting list once (a count pass + `reserve`) drops hier from 64 MB to **31 MB —
   ~1.07× C (29 MB), near parity**, exactly as `reserve` did for plain arrays. So the
   count-fill mitigation is *not* stuck outside maps: `m[k]` is a place for `reserve`
   too, and a count-then-fill build reaches the same near-C memory inside a map. (Go's
   count-fill is *heavier* here, 64 MB — the extra count map plus map+slice header
   overhead outweighs the per-slice cap saving; hier and C both win on the arena/inline
   layout.) hier trails ~1.3× C on time (the second counting pass + per-push hashing),
   the same time/space trade the array count-fill makes.

The win of `#2` here is **expressiveness with competitive cost**: the term → list map
went from O(n²)-and-impractical to O(n) and ~par Go on growth, and with one `reserve`
count pass to ~1.07× C — the build-and-hold boundary the side-array form documented,
now reached by the *natural* data structure with no manual lifetime management.

## Reproduce

    cc -O2 -o /tmp/peakrss bench/peakrss.c
    for v in invindex invindex_exact; do
      ./hierc bench/invindex/$v.hi -o /tmp/h && /tmp/peakrss /tmp/h
      cc -O2 -o /tmp/c bench/invindex/$v.c 2>/dev/null && /tmp/peakrss /tmp/c
    done
    ( cd bench/invindex && go build -o /tmp/g invindex.go ) && /tmp/peakrss /tmp/g

    # map-native form (#2), growth and count-fill:
    for v in invindex_map invindex_map_exact; do
      ./hierc bench/invindex/$v.hi -o /tmp/h && /tmp/peakrss /tmp/h
      cc -O2 -o /tmp/c bench/invindex/$v.c && /tmp/peakrss /tmp/c
      ( cd bench/invindex && go build -o /tmp/g $v.go ) && /tmp/peakrss /tmp/g
    done
