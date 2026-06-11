# hier benchmarks — the memory model, across the workload space

The thesis: a **value-semantic + implicit-arena** memory model gives **C-class
memory and predictable, pause-free reclamation, with no manual frees and no GC** —
and the way to prove that is not a passing test suite but numbers next to C, Rust,
Go (GC), and Koka (Perceus RC) on memory-heavy work, with **byte-identical output**
per workload. This file is the map; each row links to a `RESULTS.md` with detail.

All peak RSS via `getrusage` (`bench/peakrss.c`); best-of-N wall. Build regime:
each language at its standard release optimization — hier and C at `cc -O3`,
`rustc -C opt-level=3`, `go build`, `koka -O2` (its max). The authoritative
numbers under this regime are in each workload's `RESULTS.md`. Both hier
compilers (the C reference `hierc` and the self-hosted `hierc0`) are measured
where shown.

## Axis 1 — memory (peak RSS)

| workload | what it stresses | hier vs others | verdict | run |
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

## Axis 2 — latency (GC-pause predictability)

| workload | what it stresses | result | verdict | run |
|----------|------------------|--------|---------|-----|
| **latency** | steady churn, pause behavior | hier/C **0 GC pause**; Go 2927 collections / ~211 ms | C's pause-free predictability, Go's no-manual-management | `bench-latency` |
| **gcscan** | GC scan cost under a large live set | hier/C never scan; Go cheap at default GOGC, but `GOGC=10` matches hier's RAM only at 2.5× wall | Go faces a memory-vs-CPU tradeoff hier/C don't | `bench-gcscan` |

## The honest envelope

- **Wins / C-class:** transient churn, deep recursion, fixed-size retention, a real
  parser, a real SQLite workload, and pause-free latency — often *beating* hand-
  written C on time (trees, json-parse) because reclamation is one O(1) arena reset
  instead of N frees.
- **Recovered:** the two cases that were once clean defeats, both fixed by static
  FBIP reuse derived from value semantics (Koka's Perceus result without runtime
  refcounts), in **both** compilers:
  - loop-carried reassign (`iter-transform`): 3.5 GB → 4 MB (**MM-8**, whole-var
    reassign recycle).
  - eviction of **heap-bearing** records (`window`): 47 MB → 4.2 MB, ~14× C →
    ~1.3× C (**MM-9**, per-element overwrite recycle + segregated free-list).
    Fixed-size records already tied.
- **Loses:** no remaining clean defeat on these workloads. Hold-and-grow peak
  (`invindex`, `arr_pipeline`) is ~1.3–2× C and needs sizing/`reserve`, like every
  language — a cost, not a defeat.

Not cleanly benchmarkable, and why (honest negative space):
- **Cache locality of pointer-chasing** (a long linked-list traversal). It isn't a
  hier idiom: value semantics can't move a cursor out of a match-arm borrow
  (`cur = rest` deep-copies the tail), and a multi-million-deep recursive enum
  overflows any recursive descent. hier steers you to **arrays** (contiguous in
  every language → no locality gap) and **bounded-depth trees** (already in
  `binary-trees`, where the arena's contiguous layout is part of why hier beats C
  25 vs 33 MB). So the arena's locality benefit is real but already captured, not a
  separable number.
- ~~**Concurrency/parallelism**~~ — no longer a gap: `spawn`/`wait`,
  `parallel for`, and channels shipped (both compilers), and `conc/`
  measures them head-to-head — `parallel for` lands at exact C-pthreads
  parity on the compute-bound reduction; the channel pipeline maps the
  honest cost of the per-message deep copies vs C/Go/Rust. See
  `conc/RESULTS.md`.

See [../docs/thesis.md](../docs/thesis.md) for the model; each subdirectory's
`RESULTS.md` for the per-workload analysis.
