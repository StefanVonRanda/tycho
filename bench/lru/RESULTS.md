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

Machine: AMD Ryzen 7 7735HS, x86-64, Linux. tycho + C at `cc -O3`, Go at `go build`.
Peak RSS via `getrusage` (`bench/peakrss.c`); best-of-N wall. Run: `sh bench/lru/run.sh`.

| lang  | peak RSS | time   | checksum (hits sum)      |
|-------|----------|--------|--------------------------|
| tycho | 57.1 MB  | 679 ms | `1076255 536832857539`   |
| C     | 11.5 MB  | 137 ms | `1076255 536832857539`   |
| go    | 21.5 MB  | 297 ms | `1076255 536832857539`   |

## Verdict — time-competitive, memory a few× C after two fixes

tycho's value-semantic LRU is **time-competitive** (~5× C, ahead of Go) and its memory
is **~5× C** — in the same band as the trie and dijkstra, no longer an outlier. Getting
here took two fixes, both driven by this bench:

1. **O(n) → O(1) map delete.** `delete m[k]` was O(map size): the in-place tombstone is
   O(1), but maintaining the insertion-ordered `keys()` (the hash-flooding-DoS hardening)
   linearly scanned + compacted a flat `ord` key-array on every delete. With ~1.4M
   evictions on a 200k map this was O(n·deletes) — **39.8 s**. Replacing `ord` with an
   intrusive doubly-linked list over the table slots made it an O(1) unlink (now 0.68 s).

2. **Delete-churn memory.** Even with O(1) delete, the first version held **222 MB** (~19×
   C). The live set is only ~200k entries (~7 MB of backing), so this was an arena
   *hold-cost*, not a data-shape cost: deletes tombstone (`occ=2`), `used` climbs to
   cap/2, and the map *rehashes-at-same-cap to purge tombstones*. Each purge allocated a
   fresh table in the map's owning arena (`main`'s scope, never reset in the loop) and
   abandoned the old — and the capped large-buffer freelist was clogged by growth-phase
   buffers, so the steady-state tables were never reused and ~13 generations of 17 MB
   accumulated. Fix: on a **same-cap purge only**, hand the old table's buffers back to
   the arena (`arena_recycle`, guarded by `arena_owns`) so the next purge reuses them;
   skipping the growth case keeps the freelist unclogged. That dropped peak RSS to 57 MB.
   (The same-cap guard is load-bearing: recycling unconditionally re-clogs the freelist
   and only reaches ~193 MB.)

Proof the remaining cost is the churn, not the shape: a no-eviction variant holding 600k
*live* entries uses ~98 MB, while this 200k-live delete-heavy run uses 57 MB — memory now
tracks the live set, not the delete volume.

Both fixes are in the fixed map families (`[string:int/float]`, `[int:int/float]`).
Composite-valued maps (`[K: struct]`) keep the same O(1) delete but not yet the same-cap
recycle, so a struct-valued map under equally heavy delete-churn would still grow — a
documented follow-up, not exercised by common workloads. The deeper root cure (tombstone-
free **backward-shift deletion**, as the C port uses — no purge rehashing at all) was
considered and left as a larger, higher-risk change.

NOT wired into `make ci` (a head-to-head dogfood, like `bench/dijkstra`). The Go port is
skipped automatically when `go` is absent.
