# Real-program validation

The generated suite is the primary managed-language scorecard. This page tracks
the checks that use longer-running or structurally different programs, where a
microbenchmark can miss runtime, concurrency or library costs.

## Concurrency (`make bench-conc`)

Current run, whole-process peak RSS and wall time, identical checksums:

| workload | Tycho | C | Go | Rust |
|---|---:|---:|---:|---:|
| parreduce | **3.1 MB / 24 ms** | 1.9 MB / 26 ms | 1.8 MB / 42 ms | skipped |
| mandelbrot | 2.7 MB / 123 ms | 1.8 MB / 123 ms | 2.3 MB / 138 ms | 2.4 MB / **121 ms** |
| pipeline | 3.1 MB / **63 ms** | 1.6 MB / 1559 ms | 7.0 MB / 96 ms | 1.9 MB / 265 ms |
| pool | 4.2 MB / **175 ms** | 1.8 MB / 2355 ms | 2.6 MB / 343 ms | 1.9 MB / 720 ms |

Tycho is at or ahead of Go on all four time rows, while C/Rust retain a memory
edge. The C/Rust time losses in `pipeline`/`pool` are the cost of the naive
mutex/condvar ports; the README records that caveat.

## Database (`bench/dbquery`)

The generated suite has all six ports. At the large size:

| | Time | Peak RSS |
|---|---:|---:|
| Tycho | 1115 ms | 8.2 MB |
| C | 1094 ms | 8.2 MB |
| Rust | 3164 ms | 8.3 MB |
| Go | 1559 ms | 15.1 MB |
| Java | 2262 ms | 154.2 MB |
| Swift | 3178 ms | 22.4 MB |

Tycho is within ~2% of C and ahead of the managed ports.

## JSON/config

The large `json` row exercises document generation, parsing and traversal:

| | Time | Peak RSS |
|---|---:|---:|
| Tycho | 2516 ms | 264.7 MB |
| Java | 2514 ms | 3512.2 MB |
| C | 3710 ms | 230.9 MB |
| Rust | 3128 ms | 232.7 MB |
| Go | 5159 ms | 441.6 MB |
| Swift | 13521 ms | 284.6 MB |

Tycho is effectively tied with Java on time and uses far less memory; it beats
Go and Swift comfortably. `json_tree` is the stronger tree-shaped stressor and
is covered separately in the generated suite.

## CLI and external programs

`make external-check` runs `tools/tycho-hash` under varied worker counts and
checks byte-identical SHA-256 results plus seven error paths. It is a
correctness/determinism gate, not a cross-language time claim.

## Server workloads

The server test lanes verify behavior, routing, shutdown and HTTP guards, but
there is not yet a managed-language load comparison. That remains an explicit
gap rather than an implied win.

## Reading rule

Real-program rows are kept separate from the generated suite. A win here does
not excuse a managed-suite regression, and a native C/Rust gap here remains a
diagnostic unless it reproduces in the managed comparison.
