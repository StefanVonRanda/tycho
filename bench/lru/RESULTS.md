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
| tycho | 40.4 MB  | 569 ms | `1076255 536832857539`   |
| C     | 11.2 MB  | 178 ms | `1076255 536832857539`   |
| go    | 21.3 MB  | 432 ms | `1076255 536832857539`   |

## Verdict — time-competitive, memory ~4× C after two fixes

tycho's value-semantic LRU is **time-competitive** (~3× C, ahead of Go) and its memory is
**~4× C** — in the same band as the trie and dijkstra, no longer an outlier. Getting here
took two fixes, both driven by this bench:

1. **O(n) → O(1) map delete.** `delete m[k]` was O(map size): the in-place tombstone is
   O(1), but maintaining the insertion-ordered `keys()` (the hash-flooding-DoS hardening)
   linearly scanned + compacted a flat `ord` key-array on every delete. With ~1.4M
   evictions on a 200k map this was O(n·deletes) — **39.8 s**. Replacing `ord` with an
   intrusive doubly-linked list over the table slots made it an O(1) unlink (now 0.57 s).

2. **Delete-churn memory — tombstone-free deletion.** Even with O(1) delete, the first
   version held **222 MB** (~19× C). The live set is only ~200k entries (~7 MB of backing),
   so this was an arena *hold-cost*, not a data-shape cost: deletes left **tombstones**
   (`occ=2`), `used` climbed to cap/2, and the map *rehashed-at-same-cap to purge them* —
   each purge allocating a fresh table in the map's owning arena (`main`'s scope, never
   reset in the loop) and abandoning the old, so ~13 generations of ~17 MB accumulated.
   The fix removes the cause: delete now does **linear-probe backward-shift** (the same
   tombstone-free deletion the C port uses), pulling later entries back into the gap and
   relinking the slot order-list on each move. With no tombstones, `used == live`, so the
   table never rehashes-to-purge — it only grows with the live set. Peak RSS dropped to
   **40 MB**. (An interim `arena_recycle`-the-purged-table mitigation reached 57 MB; it is
   superseded and removed, since backward-shift eliminates the purge rehashing entirely.)

Proof the remaining cost is the churn-free live set, not the delete volume: a no-eviction
variant holding 600k *live* entries uses ~98 MB, while this 200k-live delete-heavy run uses
40 MB — memory tracks the live set. The residual ~4× C is the open-addressing backing
(load-factor slack + the order list), the same header-cost family as the trie.

Both fixes ship in **all map families** — the four fixed ones (`[string:int/float]`,
`[int:int/float]`) and the composite `[K: struct]` / struct-key families — so a struct-valued
or struct-keyed map under equally heavy delete-churn stays bounded too.

NOT wired into `make ci` (a head-to-head dogfood, like `bench/dijkstra`). The Go port is
skipped automatically when `go` is absent.
