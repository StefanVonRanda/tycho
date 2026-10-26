# Tycho performance claim

This page is the reviewed claim behind the benchmark suite. The generated data
is in [`RESULTS.md`](RESULTS.md) and [`RESULTS-clang.md`](RESULTS-clang.md);
[`gen/results.json`](gen/results.json) and
[`gen/results-clang.json`](gen/results-clang.json) are the saved measurements.

## Comparison class

Tycho is a memory-managed language. Its release criterion is therefore:

- **must remain competitive with the best of Go, Java and Swift**, and
- **reports C and Rust as native-performance diagnostics, not blockers**.

The suite covers 18 workloads, three sizes per workload, six language ports
where the workload is meaningful, and whole-process time plus peak RSS.
Specialized rows (`trie`, `dijkstra`, `lru`, `interp`, `json_tree`) now have
Java and Swift ports. `binary/arena`, `dispatch/expert`, and `maptree/arena`
are explicit control variants without managed ports.

## Headline results

Using large-size geometric means, Tycho is faster than every managed
implementation tested:

| backend | vs C | vs Rust | vs Go | vs Java | vs Swift |
|---|---:|---:|---:|---:|---:|
| GCC time | 0.70x | 0.50x | 0.40x | 0.45x | 0.18x |
| clang time | 0.63x | 0.53x | 0.37x | 0.37x | 0.18x |

Tycho also generally uses less memory than the managed implementations:

| backend | vs C | vs Rust | vs Go | vs Java | vs Swift |
|---|---:|---:|---:|---:|---:|
| GCC memory | 0.86x | 0.88x | 0.50x | 0.05x | 0.51x |
| clang memory | 0.87x | 0.88x | 0.50x | 0.05x | 0.52x |

Ratios are Tycho / competitor. Values below 1.00 are better for Tycho.

## Enforced gate

`make bench-suite-gate` fails on:

- checksum mismatches or failed benchmark rows;
- missing managed coverage, except the explicit control variants above;
- any row outside **1.10x** of the best Go/Java/Swift time or memory on at
  least one backend;
- aggregate self-regression above **1.15x** against
  [`gen/baseline.json`](gen/baseline.json);
- repetition spread above **2.5x** on long rows.

C and Rust ratios are printed but are advisory. Refresh a reviewed baseline
only with:

```sh
make bench-suite-baseline
```

## Remaining managed losses

The remaining edges are small and explicit:

- `window/int` versus Go, at 1–10 ms absolute times;
- large `window/string` and large `json` versus Java;
- medium `interp` versus Java;
- large memory on `trie` and `json_tree` versus Go, and on `winagg/default`
  and `invindex/map-growth` versus Swift.

## Known native gaps

C and Rust are still faster on some pointer-heavy or recursive workloads,
most visibly `json_tree`, `trie` and `dijkstra`. Tycho is not positioned as a
replacement for C/Rust when a kernel has a hard native latency or RSS ceiling;
use an FFI/native component for that kernel instead.

## Real programs

Concurrency, SQLite, JSON and external-tool checks are tracked separately in
[`REALWORLD.md`](REALWORLD.md). Server load comparison is still an explicit
gap.

These numbers measure selected idiomatic and expert implementations, not
ecosystem size, compile time, tooling maturity, tail latency, or library
availability. They support a managed-language performance claim, not universal
superiority over every language or program.
