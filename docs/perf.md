# Performance: the compiler and the C it emits

> This page is about `src/tychoc.c`, `runtime/tycho_rt.c` and the C they emit —
> how the arena model is kept cheap, and what each mechanism does. For the
> language's performance story, read [performance.md](performance.md) and
> [the thesis](thesis.md).
>
> **It carries one reproducible number**, the transpile time below. Everything
> else describes a mechanism rather than timing one. For figures you can
> reproduce, `make bench` runs 17 benchmarks and each `bench/*/RESULTS.md`
> records its own hardware.

## Transpile time

`bench/transpile/run.sh` measures how long `tychoc` takes to turn one `.ty` file
into C. It generates a syntax-stable input, verifies the compile **succeeds**
before timing it (a compile that dies is faster than one that works, so an
unchecked input reports the failure path as a speed-up), discards a warm-up run,
and reports the minimum of N with median and max beside it.

**11 ms** minimum of 10 timed runs over a 4.2k-line input — flat, no regression
across the measured window.

## What to run

- `make bench` runs the 17-benchmark suite. Each `bench/*/RESULTS.md` records
  its own hardware and per-workload numbers.
- `bench/transpile/run.sh` measures transpile time as described above.
- `TYCHO_ARENA_STATS=full <program>` prints a one-shot residency summary at
  exit (peak live bytes, bump-allocated bytes, free-list recycle rate,
  OS-reserved bytes, block-pool reuse, per-statement residency). See
  [debugging.md](debugging.md#where-the-memory-lives-tycho_arena_stats) and
  [performance.md](performance.md#arena-internals-measured).

## What the bounds mean

- **Transpile time** is bounded by the figure above; `make bench` fails on
  regression.
- **Memory** is bounded per workload by the peak-RSS tables in
  [performance.md](performance.md): the in-place append (`acc = acc + x`
  growing one buffer) holds ~1.5 MB where the unoptimized path needs ~825 MB
  at the same N, so the `make bench` guard sits between working and broken.
- **Cross-language ratios, not absolute times**, are the claim throughout:
  absolute wall times move with machine, governor, and toolchain. See
  [performance.md](performance.md).

## How the cost is kept down (summary)

The arena model wins by orders of magnitude where it was designed to (the
`acc = acc + s` loop, rewritten to an in-place O(n) append), and pays a
per-scope tax everywhere else: every block scope and call does
`arena_child` / `arena_reset` / `arena_free`. Four mechanisms keep that tax
off the hot path:

- **Block pool** (`runtime/tycho_rt.c`): freed blocks go to a process-global
  free-list instead of `free`; allocation takes from it first. Per-scope block
  churn becomes O(1) pointer traffic with no page re-faulting; peak live
  memory is unchanged.
- **Block-retaining reset**: a loop's scratch arena keeps its head block and
  rewinds it, so a one-block-per-iteration loop does zero pool traffic.
- **Elided child arenas**: `if`/`match` blocks use the enclosing scope's arena;
  escaping values promote to `_parent` either way. Smaller emitted C, same
  wall-clock and RSS.
- **Borrowed read-only params**: a heap-bearing struct parameter the body never
  mutates is borrowed, not deep-copied — removing the dominant copy cost on
  compiler-shaped workloads (the symbol-table clone).
- **Push-loop fusion**: a loop that only `push`es to a local array keeps the
  descriptor in C locals across the loop, closing most of the gap to
  hand-written C on push-dominated loops.

The tuning diary behind these — profiler notes, blind alleys, and the
`Ctx`-reconstruction and O(n²) fixes — is in
[internals/runtime-tuning.md](internals/runtime-tuning.md).
