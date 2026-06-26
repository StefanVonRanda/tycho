# tycho benchmarks — the memory model, across the workload space

The thesis: a **value-semantic + implicit-arena** memory model gives **C-class
memory and predictable, pause-free reclamation, with no manual frees and no GC** —
and the way to prove that is not a passing test suite but numbers next to C, Rust,
Go (GC), and Koka (Perceus RC) on memory-heavy work, with **byte-identical output**
per workload. This file is the map; each row links to a `RESULTS.md` with detail.

All peak RSS via `getrusage` (`bench/peakrss.c`); best-of-N wall. Build regime:
each language at its standard release optimization — tycho and C at `cc -O3`,
`rustc -C opt-level=3`, `go build`, `koka -O2` (its max). The authoritative
numbers under this regime are in each workload's `RESULTS.md`. Both tycho
compilers (the C reference `tychoc` and the self-hosted `tychoc0`) are measured
where shown.

## Axis 1 — memory (peak RSS)

| workload | what it stresses | tycho vs others | verdict | run |
|----------|------------------|----------------|---------|-----|
| **binary-trees** | transient alloc/discard, deep recursion | 25 MB vs C 33, Go 35, Koka 14 | **beats hand-written C** (one arena reset/iter) | `bench-prongB` |
| **tree-rewrite** | rewrite pass over a tree | 7 MB, lowest with Koka, beats C/Rust/Go | win | `bench-prongB` |
| **array-pipeline** | bulk transform | 6 MB vs C 3 | ~2× C mem (slack, closes to ≤C with `reserve`); time **1.25× C** after push-loop fusion | `bench-prongB` |
| **string-pipeline** | string building | 1 MB | parity | `bench-prongB` |
| **json-parse (real)** | recursive-descent parse-and-discard | 67 MB vs C 58 | **fastest of all 5**, ~1.15× C memory (whole tree held per pass) | `bench-prongB` |
| **iter-transform** | loop-carried reassign (was the arena's worst case) | 3 MB vs C 2 | **3.5 GB → 3 MB** mem (static FBIP reuse, no refcount); time 6.7×→**2.3× C** after push-loop fusion | `bench-prongB` |
| **invindex** | build-and-hold growth | ~1.7× C, → ~1.07× with `reserve` | honest hold-cost; sizing closes it | `invindex/` |
| **winagg** | per-window churn-and-discard | ~par C, beats Go | win on bulk-free teardown | `winagg/` |
| **dbquery (real SQLite)** | host data-handling around a real C lib | 4.4 MB ≈ C 4.3 < Go 7.8 | C-class on real DB work, no manual frees | `bench-dbquery` |
| **window** | sliding-window **eviction** | string: 4.2 MB vs C 3.3 (~1.3×) after **MM-9**; int: 2.3 MB (tie) | **was the clean loss (14×), now closed** — element-overwrite recycle | `bench-window` |
| **gcscan** | large held set of small objects (per-object overhead) | 64.8 MB vs C 77.9, Go 119.8 | win — arena has no per-object header (C) or GC metadata (Go) | `bench-gcscan` |
| **json-tree** | a tagged value tree held across a fold | 37 MB vs C 35, Go 28.5 | ≈ C — **value-shaped** data, zero manual mgmt | `json/` |
| **trie** | pointer-linked recursive nodes (each owns a child map) | 119 MB vs C 38, Go 34 | **~3.2× C — the standing loss**: children stored by value, not by pointer (up from 103 MB / ~2.7×: each map now carries an O(1)-delete order list, +16 B/slot the trie's many tiny maps pay without ever deleting) | `trie/` |
| **dijkstra** | graph as an adjacency list of **indices** | 41 MB vs C 31.5, Go 34 | ~1.3× C — the index idiom makes the graph value-shaped (the trie's bridge) | `dijkstra/` |
| **lru** | fixed-cap cache, delete-heavy `[int:int]` map churn | 40 MB vs C 11, Go 21 | ~4× C — drove two map fixes: O(n)→O(1) delete (slot-linked order) + tombstone-free backward-shift deletion (no purge-rehash churn; was 222 MB before); memory tracks the live set, not delete volume | `lru/` |
| **interp** | tree-walking interpreter, recursive-enum AST (5.6M nodes) | 252 MB vs C 512, Go 540 | **~0.5× C — beats C and Go**: the arena has no per-node malloc header (C) or GC metadata (Go) on a recursive value-shape with no per-node maps — the trie's inverse | `interp/` |

## Axis 2 — latency (GC-pause predictability)

| workload | what it stresses | result | verdict | run |
|----------|------------------|--------|---------|-----|
| **latency** | steady churn, pause behavior | tycho/C **0 GC pause**; Go 2927 collections / ~211 ms | C's pause-free predictability, Go's no-manual-management | `bench-latency` |
| **gcscan** | GC scan cost under a large live set | tycho/C never scan; Go cheap at default GOGC, but `GOGC=10` matches tycho's RAM only at 2.5× wall | Go faces a memory-vs-CPU tradeoff tycho/C don't | `bench-gcscan` |

## The shape dimension — value-shaped vs pointer-shaped data

The clearest predictor of whether tycho matches C is the **shape** of the data, not the
domain of the workload:

- **Value-shaped** (owned in one place, not shared) — a JSON value tree (`json/`: 37 vs
  35 MB), a SQLite result set (`dbquery/`), a flat array pipeline. tycho lands **≈ C**,
  writing none of the memory management. The model at its best.
- **Pointer-shaped** (a node referenced/shared through pointers) — a trie whose nodes each
  own a child map (`trie/`: **119 vs 38 MB, ~3.2× C**). Value semantics store children *by
  value*, not by reference, so every edge costs a whole node rather than an 8-byte pointer,
  and there is no sharing. This is a **standing cost, fundamental to mutable value
  semantics** — the most mature MVS language (Hylo) hits the identical wall
  (`docs/internals/hylo-mvs-research.md`), not a tycho implementation shortfall.
- **The bridge** — express the same graph as an **adjacency list of integer indices**
  (`dijkstra/`: **41 vs 31.5 MB, ~1.3× C**, ≈ Go). Indices are value-shaped, so the graph
  drops from ~3.2× to ~1.3× C. This is the idiom `docs/internals/value-semantics-limits.md`
  prescribes for pointer-shaped data, measured on a real algorithm.
- **Recursive but cheap-per-node** — a tree-walking interpreter's AST (`interp/`: **252 vs
  C 512, Go 540 MB, ~0.5× C — the lowest of the three**). This is *also* a large recursive
  owned tree, like the trie — but each node is a plain enum cell, not a node that owns a
  map. With no per-node side table, the arena's structural advantage is pure win: no
  per-node `malloc` header (C ~16 B/node over ~8.6M nodes) and no GC metadata (Go). So the
  trie's cost is not "recursion" or "pointers" — it is the **per-node map**; strip that and
  a recursive value-shape is where the arena is *strongest*.

So the honest rule, sharpened by the trie/interp pair: the cost is **per-node side tables**
(a map/soa owned by every node), not recursion or pointers per se. A recursive tree of
plain cells (`interp`) *beats* C; the same tree with a map at every node (`trie`) is
~3.2× C; the index idiom (`dijkstra`) turns a pointer graph back into flat value-shaped
data when the premium matters. Value semantics + arenas match-or-beat C on value-shaped
data and pay a real premium only where each node carries its own heap side table.

## The honest envelope

- **Wins / C-class:** transient churn, deep recursion, fixed-size retention, a real
  parser, a real SQLite workload, and pause-free latency — often *beating* hand-
  written C on time (trees, json-parse) because reclamation is one O(1) arena reset
  instead of N frees. The strongest single result is a **recursive structure with no
  per-node side table**: a tree-walking interpreter's AST (`interp/`) is **~0.5× C and
  ~0.47× Go memory** — the arena carries no per-node `malloc` header or GC metadata over
  millions of nodes (the inverse of the trie's per-node-map loss).
- **Recovered:** the two cases that were once clean defeats, both fixed by static
  FBIP reuse derived from value semantics (Koka's Perceus result without runtime
  refcounts), in **both** compilers:
  - loop-carried reassign (`iter-transform`): 3.5 GB → 4 MB (**MM-8**, whole-var
    reassign recycle).
  - eviction of **heap-bearing** records (`window`): 47 MB → 4.2 MB, ~14× C →
    ~1.3× C (**MM-9**, per-element overwrite recycle + segregated free-list).
    Fixed-size records already tied.
- **Loses:** **pointer-linked data structures** are a genuine standing cost — a trie of
  nodes that each own a child map is ~3.2× C (`trie/`), because value semantics store
  children by value, not by pointer. It is fundamental to the model (Hylo hits the same
  wall), and *mitigated* — not erased — by the index idiom (`dijkstra/`: ~1.3× C).
  Hold-and-grow peak (`invindex`, `arr_pipeline`) is ~1.3–2× C and needs sizing/`reserve`,
  like every language. Neither is a *correctness* defeat — both are honest memory costs of
  the model, stated as plainly as the wins.

Not cleanly benchmarkable, and why (honest negative space):
- **Cache locality of pointer-chasing** (a long linked-list traversal). It isn't a
  tycho idiom: value semantics can't move a cursor out of a match-arm borrow
  (`cur = rest` deep-copies the tail), and a multi-million-deep recursive enum
  overflows any recursive descent. tycho steers you to **arrays** (contiguous in
  every language → no locality gap) and **bounded-depth trees** (already in
  `binary-trees`, where the arena's contiguous layout is part of why tycho beats C
  25 vs 33 MB). So the arena's locality benefit is real but already captured, not a
  separable number.
- ~~**Concurrency/parallelism**~~ — no longer a gap: `spawn`/`wait`,
  `parallel for`, and channels shipped (both compilers), and `conc/`
  measures them head-to-head — `parallel for` lands at exact C-pthreads
  parity on the compute-bound reduction, and the lock-free channels beat a
  hand-written C mutex ring 2.6x while still paying the per-message deep
  copies. On the `pool` workload — a bounded-channel worker pool written as
  one line, `parallel for x in ch:` — tycho is at Go parity (~5% faster, 150
  vs 157 ms) against Go's hand-written `range`-over-channel + `WaitGroup`
  pool, both lock-free on the hot path. See `conc/RESULTS.md`.

See [../docs/thesis.md](../docs/thesis.md) for the model; each subdirectory's
`RESULTS.md` for the per-workload analysis.
