# Hier on macOS (Apple Silicon)

Hier is a first-class target on macOS/Apple Silicon: it builds, self-hosts,
passes its full correctness suite, and meets its performance thesis on
macOS/arm64 the same way it does on Linux. This page is a practical setup
guide — what you need, how to build, and what to expect.

## Requirements

| | |
|---|---|
| OS | macOS on Apple Silicon (arm64) |
| Toolchain | Apple `clang` and `make` (install the Xcode Command Line Tools) |

If you don't have the Command Line Tools yet:

```sh
xcode-select --install
```

The numbers and results on this page were produced on the following setup;
yours will differ but should track the same shape.

| | |
|---|---|
| Machine | Apple M3 Pro (11 cores, 18 GB) |
| OS | macOS 26.5.1 (build 25F80), Darwin 25.5.0, arm64 |
| Compiler | Apple clang 21.0.0 (clang-2100.0.123.102) |

## Build and verify

From the repository root:

```sh
make                 # build the C compiler (hierc) — clang -O2, clean
make test            # 153/153 correctness + golden + native-vs-ASan differential
make bootstrap       # self-hosted hierc0 matches the C compiler
make fixpoint        # B == C byte-identical; single files + packages
make conc            # 31/31 concurrency (spawn / parallel-for / channels)
make ffi corelib     # FFI + standard library
make bench           # peak-RSS / wall-time perf guards (thesis claims)
make bench-guard     # wall time vs hand-written C on tree workloads
make bench-prongB    # cross-language head-to-head (C / Rust / Go)
```

`make` alone gives you a working compiler (`./hierc`). The rest verify
correctness, the self-hosting bootstrap, concurrency, FFI, the standard
library, and performance.

## What passes on macOS

| gate | result |
|---|---|
| `make test` | **153 / 153** passed |
| `make bootstrap` | hierc0 matches the C compiler |
| `make fixpoint` | B == C byte-identical (18,806 lines C); single files + packages |
| `make conc` | 31 / 31 |
| `make ffi` | green (hierc + hierc0 agree, ASan-clean, match golden) |
| `make corelib` | green |

## Platform notes

Two things differ between macOS and Linux that are worth knowing about.

**LeakSanitizer is not available on macOS.** Apple's arm64/x86_64
AddressSanitizer ships no LeakSanitizer, so a sanitizer binary built with
`ASAN_OPTIONS=detect_leaks=1` aborts at exit (`detect_leaks is not supported
on this platform`, exit 134). The test harnesses (`tests/run.sh`,
`tests/conc/run.sh`) gate leak detection on `uname -s`: Darwin runs ASan +
UBSan without leak checking, Linux runs the full leak check. This is a
harness difference, not a language difference — leak coverage on Linux is
unchanged.

**Self-referential shadowing decls.** A shadowing declaration like
`y := y + 2`, where an outer `y` is in scope, reads the *enclosing* `y` and
binds a fresh independent value (Go/Odin lexical scoping plus value
semantics): with outer `y = 1` the result is **3**, and the outer `y` is
unchanged. Both compilers evaluate the right-hand side into a temporary
before the new local comes into scope, so the new binding never reads its own
uninitialized storage. See `tests/lambda_shadow_decl.hi` (golden `3`),
`tests/shadow_string.hi` (heap), and `tests/shadow_call.hi`.

## Performance

`make bench` (17 workloads) and `make bench-guard` both pass on macOS with no
platform-specific anomalies — the numbers track the documented Linux thesis
results.

### Wall time vs hand-written C — tree-allocation workloads (`bench-guard`)

| workload | hier | C | ratio (gate < 60%) |
|---|---|---|---|
| binary_trees | 163 ms | 993 ms | **16% of C** |
| maptree | 87 ms | 481 ms | **18% of C** |

### Cross-language head-to-head (`bench-prongB`, vs C / Rust / Go)

All binaries produced byte-identical output within each workload.

Wall time (ms):

| workload | hier | c | rust | go |
|---|---|---|---|---|
| binary_trees | **385** | 1262 | 1351 | 1283 |
| maptree | **371** | 737 | 740 | 713 |
| arr_pipeline | 264 | 249 | 315 | 350 |
| string_pipe | 246 | 234 | 268 | 339 |
| iter_transform | 435 | 362 | 558 | 499 |
| dispatch | 267 | 242 | 320 | 399 |
| json_parse | 1052 | 972 | 831 | 1330 |

Peak RSS (MB) — `hier/C` is the headline thesis metric:

| workload | hier | c | rust | go | hier/C |
|---|---|---|---|---|---|
| binary_trees | 16 | 17 | 17 | 37 | **0.94×** |
| maptree | 7 | 9 | 5 | 24 | **0.78×** |
| arr_pipeline | 6 | 2 | 3 | 10 | 3.00× |
| string_pipe | 1 | 1 | 1 | 6 | 1.00× |
| iter_transform | 3 | 2 | 3 | 10 | 1.50× |
| dispatch | 1 | 1 | 1 | 10 | 1.00× |
| json_parse | 79 | 44 | 52 | 112 | 1.80× |

### Reading the numbers

- **Where the arena model wins big:** tree allocate/discard and persistent-tree
  rewrite — ~3× faster than C/Rust/Go *and* lower RSS, because a bulk arena reset
  beats per-node `malloc`/`free`/GC.
- **Competitive but not ahead:** flat-array and string pipelines tie C/Rust on
  time at modestly higher RSS (the arena holds a scope's allocations until it
  exits, so `arr_pipeline` sits at 3× C's tiny 2 MB).
- **Worst case is bounded:** `iter_transform` (loop-carried reassignment, the
  arena's documented weak spot) is 1.2× C's time / 1.5× its RSS — still ahead of
  Rust and Go.
- **Consistently beats Go on memory** everywhere (GC overhead: Go 6–37 MB where
  hier uses 1–16 MB).

## Summary

macOS/Apple Silicon is a first-class Hier target: clean build, full self-host
fixpoint, 153/153 tests, and the memory/speed thesis holds. The only
macOS-specific adjustment is harness leak detection — Apple's ASan ships no
LeakSanitizer — and that is a harness setting, not a language change.
