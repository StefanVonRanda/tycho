# Parallel text indexer: hier vs C vs Go

`make bench-indexer`. The **same concurrent program** in three languages —
fan a corpus of files through a work queue to K=4 workers, each tallying a
local term→count map, then merge — over a byte-identical synthetic corpus,
gated by a cross-language checksum. 2026-06-13, AMD Ryzen 7 7735HS, 16 hw
threads, Linux. Best-of-3 wall, measured via bench/peakrss.

Corpus: 400 files × 4000 words, 5000-word vocabulary (`gencorpus.hi`, a fixed
LCG over base-26 words — deterministic bytes on every machine, so all three
binaries index identical input). Checksum `files tokens distinct csum`, where
`csum = Σ len(term)·count` is order-independent, so the nondeterministic
file→worker assignment still yields a single oracle.

| lang | peak RSS | wall | checksum | memory discipline |
|------|---------:|-----:|----------|-------------------|
| hier | 6.9 MB | 36 ms | files=400 tokens=1600000 distinct=5000 csum=4800000 | value semantics: each worker map deep-copied back across `wait()`, zero GC, zero free |
| C    | 3.1 MB |  7 ms | files=400 tokens=1600000 distinct=5000 csum=4800000 | hand-owned: strdup keys, realloc tables, free at the end |
| Go   | 8.6 MB | 22 ms | files=400 tokens=1600000 distinct=5000 csum=4800000 | string bodies shared under the GC |

**hier beats Go on peak memory (6.9 vs 8.6 MB) with no garbage collector and
not one line of memory management in the source.** C is tightest (3.1 MB) and
fastest (7 ms); hier trails on wall (36 ms) — the honest cost of the value
model, where every worker's whole `[string: int]` map is deep-copied out of
the task arena and into the caller's at `wait()`, instead of Go sharing the
backing store under its GC or C handing back a raw pointer. The result is the
memory-model thesis on a realistic systems workload: a multi-threaded
build-and-merge indexer with arenas reclaiming everything per scope, no GC
pauses, no lifetimes, competitive peak memory.

## What the dogfood is exercising

- **Channels** (`channel(string, 256)`, the lock-free Vyukov queue) for the
  producer→worker fan-out;
- **affine tasks** (`spawn`/`wait`) returning a **composite value** — each
  worker hands back a whole `[string: int]` map, deep-copied across the thread
  boundary by value semantics (which is *why* a spawned fn may not take
  `mut`: there is no shared mutable state to thread);
- **the in-place map accumulator** (`m = map_set(m, k, …)`) and **in-place
  string append** (`cur = cur + chr(c)`) inside the worker's channel-drain
  loop;
- **`mut` map merge** — `merge(&m, part)` grows the accumulator in place;
- the filesystem builtins (`list_dir`, `read_file`, and `write_file` in
  `gencorpus.hi`).

## Two O(n²) traps this surfaced (both are the same lesson)

Writing the dogfood took the first naive version from **7.2 GB / 47 s** to
**6.9 MB / 36 ms** by removing two quadratic blow-ups — both the same shape:
*an accumulator that silently fell off the in-place path and reverted to
pure-copy-every-step.*

1. **Compiler bug (fixed in `src/hierc.c`).** The in-place accumulator
   rewrites (`map_set`/string-append/`map_del`) are enabled by
   `collect_accums`, which walked `if`/`for`/`while` bodies but **not `match`
   / `select` arm bodies** (those live in `s->arms[a].body`, not `s->body`).
   So `m = map_set(m, …)` inside the worker's `match recv(ch)` arm — the
   canonical channel-drain idiom — emitted the pure `hier_map_si_set`
   (deep-copy the whole map, then insert), turning the per-token tally into
   O(tokens × distinct) allocation that was never reclaimed until the worker
   returned. `pf_scan_body` and `resolve_block` already traversed arms; only
   `collect_accums` missed them. The self-hosted compiler (`hierc0.hi`)
   already handled match arms (its string-accum pass recurses `SMatch`, and
   its `map_set` rewrite is structural), so this was the C reference catching
   up to it. Fix: `collect_accums` now recurses arm bodies.

2. **Program bug (fixed in `index.hi`).** `merge` originally took the
   accumulator **by value** (`fn merge(into: [string: int], …)`), so
   `into = map_set(into, …)` could not grow in place (a by-value map param
   shares the caller's table; in-place would violate value semantics) and
   deep-copied the whole accumulator on every key — O(distinct²), a flat
   ~1.5 GB because the vocabulary caps `distinct` regardless of corpus size.
   Fix: make `into` **`mut`**, the idiomatic accumulator-as-a-place, which
   restores the in-place put.

The takeaway for the memory model: the value-semantic/arena story depends on
the in-place accumulator optimizations firing for the natural idioms. When
they fire, this indexer is 6.9 MB; when one silently doesn't, it is GBs. The
compiler must make the fast path the one you reach for without thinking — a
`match`-arm accumulator and a `mut` merge are both exactly that.
