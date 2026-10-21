# tycho benchmarks — the memory model, across the workload space

The thesis: a **value-semantic + implicit-arena** memory model gives **C-class
memory and predictable, pause-free reclamation, with no manual frees and no GC**.
The evidence is numbers next to C, Rust, Go (GC), Java (GC) and Swift (ARC)
on memory-heavy work, with **byte-identical output** per workload —
not a passing test suite. This file is the map; each row links to a `RESULTS.md`
with detail.

All Linux figures below were recorded on **AMD Strix Halo / Ryzen AI MAX+ 395,
Fedora Linux 44, x86-64** (16 cores, 32 hardware threads); macOS second-machine
results are called out separately in each `RESULTS.md`. Figures were refreshed
2026-10-20 for the O1–O7 sweep and the O6 fix; rows not rerun in that sweep
retain their earlier measurements.

The O5/O6 before-controls are deliberately pathological: the trie path control
peaks at ~1.67 GB, treeupdate at ~2.25 GB and editorupdate at ~9 GB. Run one at
a time under a hard cap, e.g. `(ulimit -v 12582912; ...)`, and never alongside
`make ci` or another benchmark. A missed cap once took `bench/lru` to 121 GB
and OOM-killed the host; the scalar-map fix and the 256 MB LRU guard are
recorded in `bench/lru/RESULTS.md`.

## Axis 1 — memory (peak RSS)

| workload | what it stresses | tycho vs others | verdict | run |
|----------|------------------|----------------|---------|-----|
| **binary-trees** | transient alloc/discard, deep recursion | 13.6 MB vs C 33.7, Rust 33.4, Go 37.8 | **beats hand-written C** on both axes (one arena reset/iter) | `bench-prongB` |
| **tree-rewrite** | rewrite pass over a tree | 6.4 MB vs C 13.8, Rust 9.2, Go 22.8 | lowest of C/Rust/Go; Java/Swift not ported to this row | `bench-prongB` |
| **array-pipeline** | bulk transform | 4.4 MB vs C 3.5, Rust 3.1, Go 7.6 | ~1.25× C mem; **19 vs 21 ms** in the 2026-10-20 repeat — O7 closed the old ~2× gap | `bench-prongB` |
| **string-pipeline** | string building | 1 MB | parity | `bench-prongB` |
| **json-parse (real)** | recursive-descent parse-and-discard | 66.7 MB vs C 58.1, Rust 58.9, Go 109.8 | **fastest of C/Rust/Go**, ~1.15× C memory (whole tree held per pass) | `bench-prongB` |
| **iter-transform** | loop-carried reassign (was the arena's worst case) | 4 MB vs C 3 | **3.5 GB → 4 MB** mem (static FBIP reuse, no refcount); time 6.7×→**2.3× C** after push-loop fusion | `bench-prongB` |
| **invindex** | build-and-hold growth | ~1.7× C, → ~1.07× with `reserve` | honest hold-cost; sizing closes it | `invindex/` |
| **winagg** | per-window churn-and-discard | 7.3 MB vs C 5.3 (~1.4×), Rust 5.8, Go 12.3 | hold-cost on per-key lists; win on bulk-free teardown | `winagg/` |
| **dbquery (real SQLite)** | host data-handling around a real C lib | 4.4 MB ≈ C 4.3 < Go 7.8 | C-class on real DB work, no manual frees | `bench-dbquery` |
| **window** | sliding-window **eviction** | string: 4.2 MB vs C 3.3 (~1.3×) after **MM-9**; int: 2.3 MB (tie) | **was the clean loss (14×), now closed** — element-overwrite recycle | `bench-window` |
| **gcscan** | large held set of small objects (per-object overhead) | 125.6 MB vs C 78.1, Go 130.1 (shipped `tychoc1`; reference compiler 65.1) | time win (58 vs 77 ms, below Go), memory gap on the shipped compiler; the reference matches the historical 64.7 MB | `bench-gcscan` |
| **json-tree** | a tagged value tree held across a fold | 37 MB (reference) / 50.1 MB (shipped `tychoc1`) vs C 35.1, Go 27.3 | reference ≈ C; shipped compiler retains more on this tree — **value-shaped** data, zero manual mgmt | `json/` |
| **trie** | pointer-linked recursive nodes (each owns a child map) | 58.3 MB vs C 37.6, Go 33.6; pool 34.0; persistent path 12.4 vs 1.63 GB control | **~1.55× C idiomatic — closed to ~0.9–1.1× C by the index idiom**: children stored by value, not by pointer; the compact indexed-dict map layout removed the empty-slot + order-list waste (was ~3.2× / 119 MB). The flat-pool + integer-index idiom (`trie_pool.ty`, one `reserve`) lands at **34.0 MB**; O6 path-copying makes the persistent twin **12.4 MB** vs **1.63 GB** for the copying control | `trie/` |
| **dijkstra** | graph as an adjacency list of **indices** | 46.4 MB vs C 30.7, Go 33.1 | ~1.5× C — the index idiom makes the graph value-shaped (the trie's bridge) | `dijkstra/` |
| **lru** | fixed-cap cache, delete-heavy `[int:int]` map churn | 20.4 MB vs C 11.4, Go 20.6 | **~1.8× C, ahead of Go** — the `reserve` idiom (pre-size the pool + map for the known cap) cut the retained growth intermediates, 32.6 → 18.6 MiB peak live (9 allocations); earlier map fixes: O(n)→O(1) delete, bounded purge-rehash churn (was 222 MB), compact indexed-dict layout (40→32.6 MB); the O6 scalar-map fix restored this row (121 GB OOM → 20.4 MB) | `lru/` |
| **interp** | tree-walking interpreter, recursive-enum AST (5.6M nodes) | 252 MB vs C 512, Go 540 | **~0.5× C — beats C and Go**: the arena has no per-node malloc header (C) or GC metadata (Go) on a recursive value-shape with no per-node maps — the trie's inverse | `interp/` |

## Axis 2 — latency (GC-pause predictability)

| workload | what it stresses | result | verdict | run |
|----------|------------------|--------|---------|-----|
| **latency** | steady churn, pause behavior | tycho/C **0 GC pause**; Go 2927 collections / ~211 ms | C's pause-free predictability, Go's no-manual-management | `bench-latency` |
| **gcscan** | GC scan cost under a large live set | tycho/C never scan; Go cheap at default GOGC, but `GOGC=10` matches tycho's RAM only at 2.5× wall | Go faces a memory-vs-CPU tradeoff tycho/C don't | `bench-gcscan` |

## Allocator caller experiments

`make allocator-directory` compares directory construction; `make allocator-callers`
checks three real applications and traces HTTP/database reader lifetimes against
simpler native controls. Both are opt-in; the latter needs POSIX socketpairs.
Results, ownership constraints and the **no second integration** decision live in
[the storage plan](../docs/internals/plan-value-storage-places.md#caller-level-follow-up-measured-gate-no-second-integration-2026-10-15).

`make allocator-peaks` attributes simultaneous ownership at actual application
capacity peaks and compares the docs/ZIP byte-builder changes with historical,
presized and smaller-native controls, plus ablations. It needs POSIX `execinfo`;
plain timing/RSS and diagnostic profiles are separate. See the
[peak-driven follow-up](../docs/internals/plan-value-storage-places.md#peak-driven-follow-up-hard-framing-cap-and-ordinary-byte-builders-2026-10-15).

`make allocator-compiler` compares compiler caller shapes: adapter presizing,
smaller native blocks, fragment staging with `arrays.join`, a named append
builder, and direct consumption of the loaded Program, including ablations.
It freezes one compiler input, checks C/diagnostic identity on six inputs, and
separates profiles from interleaved plain timing/RSS. Historical caller shapes
are reconstructed from current source by default; `--before TREE` pins an actual
prior tree. POSIX `execinfo` is required. See the
[compiler caller results](../docs/internals/plan-value-storage-places.md#compiler-callers-remove-the-adapter-and-recognize-the-builder-2026-10-15).

## The shape dimension — value-shaped vs pointer-shaped data

The clearest predictor of whether tycho matches C turned out to be the **shape** of the
data, not the domain of the workload:

- **Value-shaped** (owned in one place, not shared) — a JSON value tree (`json/`: 37 vs
  35 MB), a SQLite result set (`dbquery/`), a flat array pipeline. tycho lands **≈ C**,
  writing none of the memory management. The model at its best.
- **Pointer-shaped** (a node referenced/shared through pointers) — a trie whose nodes each
  own a child map (`trie/`: **58.7 vs 38 MB, ~1.55× C** — down from ~3.2× before the
  compact indexed-dict map layout). Value semantics store children *by value*, not by
  reference, so every *live* edge costs a whole node rather than an 8-byte pointer, and
  there is no sharing. This is a **standing cost, fundamental to mutable value
  semantics** — the most mature MVS language (Hylo) hits the identical wall,
  not a tycho implementation shortfall.
- **The bridge** — express the same graph as an **adjacency list of integer indices**
  (`dijkstra/`: **46.4 vs 30.7 MB, ~1.5× C**, ≈ Go). Indices are value-shaped, so the graph
  drops from ~3.2× to ~1.5× C. This is the idiom `docs/internals/value-semantics-limits.md`
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
~1.55× C (was ~3.2× before the compact map layout); the index idiom (`dijkstra`) turns a
pointer graph back into flat value-shaped
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
  nodes that each own a child map is ~1.55× C (`trie/`, down from ~3.2×), because value semantics store
  children by value, not by pointer. It is fundamental to the model (Hylo hits the same
  wall), and *mitigated* — not erased — by the index idiom (`dijkstra/`: ~1.5× C).
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
  `parallel for`, and channels shipped, and `conc/`
  measures them head-to-head — `parallel for` lands at exact C-pthreads
  parity on the compute-bound reduction, and the lock-free channels beat a
  hand-written C mutex ring ~9× (73 vs 654 ms) while still paying the per-message
  deep copies. On the `pool` workload — a bounded-channel worker pool written as
  one line, `parallel for x in ch:` — tycho is at Go parity (~5% faster, 150
  vs 157 ms) against Go's hand-written `range`-over-channel + `WaitGroup`
  pool, both lock-free on the hot path. See `conc/RESULTS.md`.

See [../docs/thesis.md](../docs/thesis.md) for the model; each subdirectory's
`RESULTS.md` for the per-workload analysis.
