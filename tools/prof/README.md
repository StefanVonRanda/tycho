# Sampling profiler (`prof_shim.c` + `profile.sh`)

A dependency-free statistical CPU-time profiler for tycho-compiled programs. I
wrote it for environments where the usual tools don't work:

- **`perf`** needs `kernel.perf_event_paranoid <= 2` (root to lower); blocked in
  many sandboxes/containers (`= 3`, plus a "no new privileges" flag that stops
  `sudo` escalating even with a password).
- **`valgrind`/callgrind** may not be installed and can't be without root.
- **`gprof` (`-pg`) lies on this codebase.** Its `mcount` adds a fixed overhead
  to *every function call*, so a tiny, branch-predicted, million-times-called
  function (`is_variant`, `count_str_occ`, the bounds-check helpers) is charged
  that overhead and *looks* like 25–33% of runtime — when at `-O2` it's ~0.3%.
  Chasing that ghost cost real effort. **Trust this sampler (or `-O2` wall-clock
  deltas), not gprof self-times, for hot tiny functions.** The measured figures
  are here rather than elsewhere on purpose: `docs/perf.md` states the rule
  but no longer carries the numbers, which were taken against a workload that can
  no longer be run.

## How it works

`ITIMER_PROF` fires `SIGPROF` on *consumed CPU time* (not wall clock). The signal
handler records the interrupted instruction pointer plus a few stack words
(return-address candidates) — async-signal-safe array stores, no `mcount`, no
per-call overhead. At exit `dladdr` resolves each sample to `leaf <- caller` and
appends to `/tmp/prof_syms.txt`; many runs accumulate, then `sort | uniq -c`
gives the breakdown. The self-compile is sub-50 ms, so one run is too short to
sample — hundreds of runs are aggregated.

## Use

## What it found

It pointed straight at the real self-compile hotspot gprof had hidden:
`scan_token` recomputing `len(src)` (a full `strlen` of the source) once per
token — O(tokens × length) = O(n²). Threading the already-known length in cut
a large compile **62 → 33 ms** with no change to bounds-checking.
After that, the remaining cost is diffuse `memcpy`/`malloc` from value-semantic
copies — no single dominant function.
