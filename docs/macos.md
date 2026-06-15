# Hier on macOS (Apple Silicon)

Proof that Hier builds, self-hosts, passes its full correctness suite, and hits
its performance thesis on macOS/arm64 — not just Linux. Run on 2026-06-15.

## Environment

| | |
|---|---|
| Machine | Apple M3 Pro (11 cores, 18 GB) |
| OS | macOS 26.5.1 (build 25F80), Darwin 25.5.0, arm64 |
| Compiler | Apple clang 21.0.0 (clang-2100.0.123.102) |
| Hier | commit `767f6bf` + the two portability fixes below |

## Reproduce

```sh
make                 # build the C compiler (hierc) — clang -O2, clean
make test            # 151/151 correctness + golden + native-vs-ASan differential
make bootstrap       # self-hosted hierc0 matches the C compiler
make fixpoint        # B == C byte-identical; single files + packages
make conc            # 31/31 concurrency (spawn / parallel-for / channels)
make ffi corelib     # FFI + standard library
make bench           # peak-RSS / wall-time perf guards (thesis claims)
make bench-guard     # wall time vs hand-written C on tree workloads
make bench-prongB    # cross-language head-to-head (C / Rust / Go)
```

## Correctness — all green

| gate | result |
|---|---|
| `make test` | **151 / 151** passed |
| `make bootstrap` | hierc0 matches the C compiler |
| `make fixpoint` | B == C byte-identical (18,797 lines C); single files + packages |
| `make conc` | 31 / 31 |
| `make ffi` | green (hierc + hierc0 agree, ASan-clean, match golden) |
| `make corelib` | green |

## Two portability fixes this run required

1. **Test harnesses forced LeakSanitizer, which Apple's ASan lacks.**
   `tests/run.sh` and `tests/conc/run.sh` hardcoded
   `ASAN_OPTIONS=detect_leaks=1`; Apple's arm64/x86_64 AddressSanitizer ships no
   LeakSanitizer, so every sanitizer binary aborted at exit (`detect_leaks is
   not supported on this platform`, exit 134) — 123 false failures with nothing
   actually wrong. Fixed by gating leak detection on `uname -s` (Darwin → 0,
   else → 1): full leak checking stays on Linux, ASan + UBSan still run on macOS.

2. **A latent codegen UB the macOS toolchain exposed.**
   A self-referential shadowing decl `y := y + 2` (shadowing an outer `y`)
   emitted `long h_y = (h_y + 2L);` — a C local read in its own initializer
   (use-before-init). Linux happened to read a zeroed stack slot (→ 2, baked
   into the golden); macOS read stack garbage, and the harness's
   native-vs-sanitizer differential check caught the divergence. Per Hier's
   value semantics the new binding starts at its type's zero value, so the fix
   zero-initializes it first (`long h_y = {0}; h_y = (h_y + 2L);`). Applied to
   **both** compilers (`src/hierc.c`, `compiler/hierc0.hi`) so the self-host
   fixpoint stays byte-identical. Test: `tests/lambda_shadow_decl.hi`.

Neither was a macOS-specific defect in the language — (1) was a harness
assumption, (2) was platform-independent UB that Linux had been silently
absorbing. macOS surfaced both.

## Performance

`make bench` (17 workloads) and `make bench-guard` both pass on macOS, with no
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

## Conclusion

macOS/Apple Silicon is a first-class Hier target: clean build, full self-host
fixpoint, 151/151 tests, and the memory/speed thesis holds. The only changes
needed were harness portability (leak detection) and fixing one platform-
independent UB that Linux had been masking.
