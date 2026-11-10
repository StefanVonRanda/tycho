# LRU cache — tycho vs C vs Go

A fixed-capacity (200k) least-recently-used cache driven by a shared LCG op-stream
(5M ops, 70% get / 30% put over a 600k keyspace, so the cache stays full and every
miss evicts). Each port folds hits + returned values into a checksum; the policy
(move-to-front on touch, evict the LRU tail when full) is identical, so the checksum
is byte-identical across all three. The cache is the memory under test.

- **tycho**: value-semantic index pool — one `[Node]` array (prev/next are `int`
  indices, not pointers) + an `[int:int]` map (key → slot); the LRU tail slot is
  overwritten in place on eviction. No hand-written C, no manual frees.
- **C**: the textbook design tycho can't express — a hash map (open addressing,
  linear-probe **backward-shift** delete, no tombstones) + a doubly-linked recency
  list over a node pool.
- **Go**: the builtin `map[int]int` (key → slot) + a node slice with index links.

## Numbers

Machine: AMD Strix Halo / Ryzen AI MAX+ 395, x86-64, Fedora Linux 44. tycho + C at `cc -O3`, Go at `go build`.
Peak RSS via `getrusage` (`bench/peakrss.c`); best-of-N wall. Run: `sh bench/lru/run.sh`.

| lang  | peak RSS | time   | checksum (hits sum)      |
|-------|----------|--------|--------------------------|
| tycho | 19.8 MB  | ~580 ms | `1076255 536832857539`   |
| C     | 11.5 MB  | ~150 ms | `1076255 536832857539`   |
| go    | 21.4 MB  | ~336 ms | `1076255 536832857539`   |

(tycho row measured 2026-08-04 with the two `reserve` lines the bench now ships —
see below. The recorded 32.6 MB / 303 ms was the pre-reserve idiomatic run;
wall is parity with that run, memory dropped a third.)

### 2026-10-20 repeat (post-O6 fix)

`sh bench/lru/run.sh` with `tychoc1`; same checksum:

| lang | peak RSS | time |
|------|---------:|-----:|
| tycho | **20.4 MB** | **139 ms** |
| C | 11.4 MB | 123 ms |
| go | 20.6 MB | 159 ms |

The O6 scalar-map regression had made this row a 121 GB OOM; the fix is
described below.

### 2026-11-10 repeat (1/8 map compaction)

`sh bench/lru/run.sh` with the current `tychoc1`; same checksum:

| lang | peak RSS | time |
|------|---------:|-----:|
| tycho | **14.4 MB** | **236 ms** |
| C | 11.9 MB | 240 ms |
| go | 21.1 MB | 324 ms |

`reserve(idx, 400000)` is gone. `reserve(idx, cap)` now suffices because the map
compacts tombstones at **1/8 dead** instead of 1/2. At 1/2, a churn-heavy map
reached its reserved entry capacity with ~cap live entries, so the rule never
fired before it grew; the old arrays stayed in the arena and the peak was ~20
MB. At 1/8 the reserved array is reused, and the cache holds 200k nodes in
14.4 MB. Time is parity with C because the extra compactions are cheaper than
the growth they prevent.

## Verdict — ahead of Go, 1.2× C after the 1/8 compaction

tycho's value-semantic LRU is now **14.4 MB to C's 11.9 MB (1.2×)** and
**236 ms to C's 240 ms**, while staying well ahead of Go (21.1 MB / 324 ms).
Before the 2026-11-10 fix it was 20.4 MB / 139 ms (1.8× C memory); the
memory step came from `reserve(idx, cap)` plus the 1/8 compaction rule, not from
changing the LRU algorithm.

The older 1.8× result and the four fixes that produced it are kept below as the
historical record. Compare within a row, not across dates.


Getting to ~1.8× took four fixes, all driven by this bench:

1. **O(n) → O(1) map delete.** `delete m[k]` was O(map size): the in-place tombstone is
   O(1), but maintaining the insertion-ordered `keys()` (the hash-flooding-DoS hardening)
   linearly scanned + compacted a flat `ord` key-array on every delete. With ~1.4M
   evictions on a 200k map this was O(n·deletes) — **39.8 s**. It became O(1) once insertion
   order stopped living in a compacted flat array.

2. **Delete-churn memory.** Even with O(1) delete, an early version held **222 MB** (~19× C).
   The live set is only ~200k entries (~7 MB of backing), so this was an arena *hold-cost*,
   not a data-shape cost: same-cap rehash-to-purge left ~13 abandoned table generations of
   ~17 MB in `main`'s never-reset arena. Bounding the purge (so `used` tracks the live set,
   never rehashing-to-purge) dropped peak to **~40 MB**.

3. **Compact indexed-dict layout (latest).** Folding insertion order into a *dense* entries
   array removed the per-slot order list entirely, and delete moved to a **tombstone +
   amortized compaction** scheme — the index is rebuilt when tombstones exceed a fraction of
   the entries, preserving `keys()` order and the churn bound. Peak dropped **40 → 32.6 MB**
   and wall **569 → 303 ms**, now under Go. The layout is described in
   [value-semantics-limits.md](../../docs/internals/value-semantics-limits.md) (§1).

Proof the remaining cost is the churn-free live set, not the delete volume: a no-eviction
variant holding 600k *live* entries uses more memory than this 200k-live delete-heavy run —
memory tracks the live set. The residual ~2.8× C is the index table's open-addressing
load-factor slack, the same header-cost family as the trie, not a pointer blowup.

These fixes ship in **all map families** — the four fixed ones (`[string:int/float]`,
`[int:int/float]`) and the composite `[K: struct]` / struct-key families — so a struct-valued
or struct-keyed map under equally heavy delete-churn stays bounded too.

**O6 scalar-map regression (fixed 2026-10-20).** The first O6 cut used the
shallow-read path for `[int:int]` reads inside an `inout` helper; the next write
then copied the 200k-entry map, so this benchmark grew from ~20 MB to **121 GB**
and OOM-killed the host. Scalar/string values have no nested mutable part to
protect, so the share path is now gated on a share helper. This row is **20.4 MB
/ 139 ms**; `tests/cow_scalar_share.sh` asserts neither compiler emits
`get_share` for the shape, and `bench/lru/run.sh` fails above 256 MB or the
2 GiB address-space cap.

NOT wired into `make ci` (a head-to-head dogfood, like `bench/dijkstra`). The Go port is
skipped automatically when `go` is absent.
