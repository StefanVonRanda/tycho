# Performance and memory

The reviewed claim and its enforced gate are in
[`bench/PERFORMANCE.md`](../bench/PERFORMANCE.md); the generated multi-size
tables are [`bench/RESULTS.md`](../bench/RESULTS.md) (GCC) and
[`bench/RESULTS-clang.md`](../bench/RESULTS-clang.md). This page explains what
the numbers mean and how the arena model is kept cheap. Every benchmark checks
its output against a golden; cross-language *ratios* are the claim, since
absolute times move with machine and toolchain.

## What to run

- `make bench-suite` — the 18-workload, three-size, six-language suite;
  `make bench-suite-gate` enforces the claim in `bench/PERFORMANCE.md`.
- `make bench` — the memory guards: each `bench/*.ty` program must stay under a
  peak-RSS bound. The in-place append (`acc = acc + x` growing one buffer) holds
  ~1.5 MB where the unoptimized path needs ~825 MB at the same N, so the 32 MB
  bound sits firmly between working and broken.
- `make bench-backends` — the same sources under GCC and clang. Tycho emits C,
  so a Tycho-vs-C number means something only when both use the same backend;
  report the backend with every number.
- `make bench-floor` — the empty-process floor. Rows within a few ms of it
  (`window int`, for one) measure process startup, not throughput.
- `bench/transpile/run.sh` — how long `tychoc` takes to turn one `.ty` file
  into C. It verifies the compile succeeds before timing it and reports the
  minimum of N runs.

## A real program at flat memory

[`examples/json.ty`](../examples/json.ty) is a ~200-line recursive-descent JSON
parser over a recursive `Json` sum type, with no `malloc`/`free`/refcount/GC in
the source. Parsing 5,000,000 documents in a loop holds a flat ~10 MB: each
document's tree is reclaimed when its iteration's arena resets. Clean under
ASan + LeakSanitizer.

## How the per-scope cost is kept down

The arena model wins where it was designed to (the `acc = acc + s` loop becomes
an in-place O(n) append) and pays a per-scope tax everywhere else: every block
scope and call does `arena_child` / `arena_reset` / `arena_free`. Five
mechanisms keep that tax off the hot path:

- **Block pool** (`runtime/tycho_rt.c`): freed blocks go to a thread-local
  free-list instead of `free`, and allocation takes from it first. Per-scope
  block churn becomes pointer traffic with no page re-faulting.
- **Block-retaining reset**: a loop's scratch arena keeps its head block and
  rewinds it, so a one-block-per-iteration loop does no pool traffic.
- **Elided child arenas**: `if`/`match` blocks use the enclosing scope's arena;
  escaping values promote to `_parent` either way.
- **Borrowed read-only params**: a heap-bearing struct parameter the body never
  mutates is borrowed, not deep-copied.
- **Push-loop fusion**: a loop that only `push`es to a local array keeps the
  descriptor in C locals across the loop.

The profiler notes behind these are in
[internals/runtime-tuning.md](internals/runtime-tuning.md).

## Arena internals, measured

`TYCHO_ARENA_STATS=full <program>` prints a one-shot summary at exit: peak live
bytes, total bump-allocated bytes, the free-list recycle rate, OS-reserved
bytes, the block-pool reuse rate, and a per-statement residency table (see
[debugging.md](debugging.md#where-the-memory-lives-tycho_arena_stats)). At the
default 64 KiB block:

| workload | peak live | OS reserved | slack | block reuse | free-list recycle |
|----------|-----------|-------------|-------|-------------|-------------------|
| interp (5.6M-node tree interpreter) | 250.6 MiB | 250.8 MiB | 0.08% | 99.8% | 0 of 59.4M (held tree) |
| json (recursive-descent parse) | 37.6 MiB | 38.1 MiB | 1.3% | 99.8% | ~0 of 5.5M (held tree) |
| gcscan (large held set of small objects) | 63.5 MiB | 64.1 MiB | 0.9% | 37.3% | 0 of 6.0M (held) |
| lru (delete-heavy map churn) | 33.0 MiB | 33.3 MiB | 0.9% | 0.0% | 16 of 89 (18.0%) |
| optstr_build (loop-carried rebuild) | 144 B | 64.0 KiB | — | 50% | 1.0M of 6.0M (16.7%) |
| loop_scratch (per-iteration scratch) | 40 B | 64.0 KiB | — | 50% | 0 of 10.0M |

- **At scale the model is tight**: large workloads hold their live set within
  0.1–1.3% of what they reserve. The 64 KiB block shows up as slack only on toy
  programs.
- **Free-list recycling is workload-shaped**: zero where the working set stays
  live (trees), up to ~17% of allocations where loop-carried rebuilds hand dead
  buffers back.
- **Loop scratch peaks near zero**: the per-iteration `arena_reset` keeps a
  loop's live bytes at one iteration's worth — `loop_scratch` makes 10M
  allocations and peaks at 40 B.

`TYCHO_BLOCK=<bytes>` overrides the block size per run. It is a memory-only
knob: across 8–256 KiB, peak live is identical, slack moves by ≤4%, and wall
time is flat, so the 64 KiB default stands.

## Speed

Against idiomatic per-node C/Rust allocation, Tycho wins the allocation-heavy
tree workloads on time and memory; an expert C arena beats it on `binary` time
while Tycho keeps the memory edge. Tycho trails C and Rust on flat array
pipelines, and bounds checks are not the reason: with every index site's check
stripped from the C, those pipelines run no faster than with every check kept
(`TYCHOC_NO_BOUNDS_ELISION=1`), and the one check the default build keeps on
`window int` costs nothing measurable at 200M iterations. `--bounds-report`
lists the index sites a build keeps.

## Where it costs

Pointer-shaped data (graphs, doubly-linked lists) costs more unless you use the
flat-pool idiom: hold all nodes in one array and link them by integer index.
Arenas reclaim at *scope exit*, not incrementally, so a long-lived scope holds
its transients until it returns — scope them in an inner function. The full
loss column, with the idiom for each case, is in
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).
