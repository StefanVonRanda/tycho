# Sliding-window eviction — the arena's former boundary, now closed (MM-9)

This is the workload class that attacks the value-semantic-arena thesis at its
weakest point, so it gets measured head-on rather than skipped. A stream of N
records where only the last W are live (a sliding window / eviction / LRU shape).
malloc and a GC reclaim each evicted record, so peak RSS tracks the **window**.
A naive no-individual-free arena keeps every record it ever allocated, so for
**heap-bearing** records peak RSS would track the **stream** — this was the
arena's one honest loss (~14× C). **MM-9 closes it**: see below.

## Results (2,000,000-item stream, 50,000 window; `sh bench/window/run.sh`)

Records are heap-bearing strings (`"rec" + i%100000`); all ports print the same
checksum `15777800`. Peak RSS via `bench/peakrss`.

| record kind            | hier (hierc) | hier (hierc0) | C | Go |
|------------------------|-----:|-----:|--:|---:|
| **string (heap)** before MM-9 | 47.3 MB | ~stream | 3.3 MB | 7.6 MB |
| **string (heap)** after MM-9  | **4.2 MB** | **3.6 MB** | 3.3 MB | 7.6 MB |
| **int (fixed-size)**          | 2.3 MB | 2.3 MB | ~bounded | ~bounded |

From **14× C to ~1.3× C** — now under Go, ~par C, with no manual frees and no GC.

Re-measured at `-O3` (2026-06-07): peak RSS unchanged (string hier 4.3 MB, C 3.2 MB,
Go 7.5 MB; int hier 2.3 MB — all flag-independent). Wall, best-of-3: string hier
106 ms, C 77 ms, Go 81 ms; int hier 3 ms. `run.sh` now builds at `-O3`.

## What MM-9 does

Overwriting a string-array slot (`ring[k] = rec`) now **recycles the evicted
element's buffer** back to the array's arena, so the next store reuses the bytes
instead of the arena growing with the whole stream. Two pieces:

1. **Element-overwrite recycle.** At the store, the old element is dead and
   uniquely owned (value semantics: reads deep-copy out, so nothing else
   references it). The buffer is handed to `arena_recycle`. Sound guards: the new
   value is built first (the RHS may read the slot, e.g. `s[k] = s[k] + x`);
   `arena_owns` recycles only buffers this arena actually allocated (never an
   interned literal or a cross-arena string); the freshly-stored buffer is never
   recycled (`old != new`). hierc: `runtime/hier_rt.c` `hier_arr_str_set`. hierc0:
   the `SFieldAssign` emission, which also moves the element from malloc (owner 0)
   into the array's arena so it is recyclable.
2. **Segregated free-list.** A single capped (32) best-fit free-list dropped ~half
   the dead chunks here (instrumented: ~1.04M of 2M) because an eviction window
   needs the free-list to hold ~W chunks, and a linear best-fit scan can't be
   uncapped. Replaced with a per-8-byte-size-class free-list (16 inline classes
   for tiny objects): O(1) push/pop, no cap, no scan. Larger chunks keep the
   best-fit path, so MM-8 (iter-transform) large-buffer reuse is unchanged.

This is FBIP-grade reuse from **static** value semantics — Perceus-style
in-place reuse without reference counts — extended from whole-variable reassign
(MM-8) to per-element overwrite.

## The fixed-size case (unchanged, for contrast)

**Fixed-size window records: hier is bounded (2.3 MB), ties C/Go.** An `int` (or a
struct of scalars) lives *inside* the ring slot, so overwriting reuses the slot —
no per-element heap, nothing accumulates. The heap-bearing case now matches it.

## Verdict

The thesis — "competitive C-class memory with no manual management" — now holds
**including** eviction of heap-bearing records, the one case it previously did
not. Verified: checksum matches C/Go on both compilers; clean under ASan/UBSan;
`make test` 97/0; `make fixpoint` B==C; differential fuzz clean;
`tests/elem_recycle.hi` is the distinct-value alias regression.

**Reproduce:** `make bench-window` (skips Go if absent).
