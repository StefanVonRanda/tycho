# Compiler-optimization benchmarks

These fixtures measure the phases in
[`ROADMAP.md`](../../ROADMAP.md#compiler-optimization-phases-widening-the-domain-2026-10-19).
The rule is the same for every phase: **the same compiler, one control switch,
byte-identical output, and peak RSS plus median wall time**.

## Protocol

1. Build the compiler under test (`make -s tychoc1`, or pass `TYCHOC=./tychoc`).
2. Build `bench/peakrss.c` once.
3. Build the fixture twice: once with the control switch and once without it.
   For a codegen phase the control is `TYCHOC_NO_<OPT>=1`; for a runtime
   representation change (O1) the "before" is a binary built from the
   pre-change runtime, since a per-copy environment check would tax the
   optimized path.
4. Run each binary five times under `peakrss`, keep the median RSS and time.
5. Assert both binaries print the same output, then run `make test`,
   `make corelib`, `make parse-check` and the sanitizer lanes.
6. Record the result in [`RESULTS.md`](RESULTS.md) with the machine, compiler
   version and exact commands.

A codegen control is deliberately read **at codegen time**. That keeps the
before and after on the same source tree, compiler binary and host; a second
checkout is not evidence of an optimization.

## Fixtures

| phase | fixture | what it measures |
|---|---|---|
| O1 | [`bench/litarr_build.ty`](../litarr_build.ty) | literal pushes into a projected `[string]` |
| O2 | [`bench/opt/bigparam/main.ty`](bigparam/main.ty) | large `[N]T` read-only parameters (reference ABI parity) |
| O3 | [`bench/opt/movebuild/main.ty`](movebuild/main.ty) | loop-local last-use moves into `push` |
| O4 | [`bench/opt/sliceparse/main.ty`](sliceparse/main.ty) | borrowed in-scope string slices |
| O5 | [`bench/opt/treeupdate/main.ty`](treeupdate/main.ty) | `t = insert(t, k)` destination passing |
| O5 | [`bench/opt/editorupdate/main.ty`](editorupdate/main.ty) | long-lived editor rope updated through `t = set(t, i, v)` |
| O6 | [`bench/trie/path/main.ty`](../trie/path/main.ty) | conservative shared-bit CoW / path copying |
| O7 | [`bench/prongB/arr_pipeline.ty`](../prongB/arr_pipeline.ty) + [`bench/opt/smallmap/main.ty`](smallmap/main.ty) | loop-grown array and map-literal presizing (SROA evaluated) |
| experiment | [`bench/opt/entityids/main.ty`](entityids/main.ty) | raw index vs `core:pool` index+generation handles |
| fresh push | [`bench/opt/pushfresh/main.ty`](pushfresh/main.ty) | build a fresh pushed value in the receiver's arena |
| experiment | [`bench/opt/layouts/main.ty`](layouts/main.ty) | AoS vs SoA for an all-field entity update |

O1–O7 are shipped in 0.8.8; O2 is reference/bootstrap parity, while the
packaged self-hosted compiler already passes `[N]T` as a descriptor. Their
before/after is in [`RESULTS.md`](RESULTS.md).
