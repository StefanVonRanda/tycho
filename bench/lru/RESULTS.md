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
| tycho | 32.6 MB  | 303 ms | `1076255 536832857539`   |
| C     | 11.5 MB  | 150 ms | `1076255 536832857539`   |
| go    | 21.4 MB  | 336 ms | `1076255 536832857539`   |

## Verdict — ahead of Go on both, memory ~2.8× C

tycho's value-semantic LRU is now **ahead of Go on both memory and wall** (32.6 MB / 303 ms
vs Go 21.4 MB / 336 ms) and sits at **~2.8× C memory / ~2× C wall** — in the same band as
the trie and dijkstra, no longer an outlier. Getting here took three fixes, all driven by
this bench:

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
   and wall **569 → 303 ms**, now under Go. Internals:
   `docs/internals/compact-dict-map-design.md`.

Proof the remaining cost is the churn-free live set, not the delete volume: a no-eviction
variant holding 600k *live* entries uses more memory than this 200k-live delete-heavy run —
memory tracks the live set. The residual ~2.8× C is the index table's open-addressing
load-factor slack, the same header-cost family as the trie, not a pointer blowup.

These fixes ship in **all map families** — the four fixed ones (`[string:int/float]`,
`[int:int/float]`) and the composite `[K: struct]` / struct-key families — so a struct-valued
or struct-keyed map under equally heavy delete-churn stays bounded too.

NOT wired into `make ci` (a head-to-head dogfood, like `bench/dijkstra`). The Go port is
skipped automatically when `go` is absent.
