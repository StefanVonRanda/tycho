# Sampling profiler (`prof_shim.c` + `profile.sh`)

A dependency-free statistical CPU-time profiler for hier-compiled programs, for
environments where the usual tools don't work:

- **`perf`** needs `kernel.perf_event_paranoid <= 2` (root to lower); blocked in
  many sandboxes/containers (`= 3`, plus a "no new privileges" flag that stops
  `sudo` escalating even with a password).
- **`valgrind`/callgrind** may not be installed and can't be without root.
- **`gprof` (`-pg`) lies on this codebase.** Its `mcount` adds a fixed overhead
  to *every function call*, so a tiny, branch-predicted, million-times-called
  function (`is_variant`, `count_str_occ`, the bounds-check helpers) is charged
  that overhead and *looks* like 25–33% of runtime — when at `-O2` it's ~0.3%.
  Chasing that ghost cost real effort (see `docs/perf.md`). **Trust this sampler
  (or `-O2` wall-clock deltas), not gprof self-times, for hot tiny functions.**

## How it works

`ITIMER_PROF` fires `SIGPROF` on *consumed CPU time* (not wall clock). The signal
handler records the interrupted instruction pointer plus a few stack words
(return-address candidates) — async-signal-safe array stores, no `mcount`, no
per-call overhead. At exit `dladdr` resolves each sample to `leaf <- caller` and
appends to `/tmp/prof_syms.txt`; many runs accumulate, then `sort | uniq -c`
gives the breakdown. The self-compile is sub-50 ms, so one run is too short to
sample — hundreds of runs are aggregated.

## Use

```sh
make hierc
tools/prof/profile.sh compiler/hierc0.hi compiler/hierc0.hi 600 self
```

`emitter=self` profiles the **self-hosted** codegen (hierc0 emitting itself);
`hierc` (default) profiles hierc's emission. They can differ a lot — hierc emits
a `strlen`-bounds-checked `hier_str_get` for `s[i]` where hierc0 emits a direct
index, so the same `.hi` profiles differently depending on who built it.

## What it found

Pointed straight at the real self-compile hotspot that gprof had hidden:
`scan_token` recomputing `len(src)` (a full `strlen` of the source) once per
token — O(tokens × length) = O(n²). Threading the already-known length in cut
the self-hosted self-compile **62 → 33 ms** with no change to bounds-checking.
After that, the remaining cost is diffuse `memcpy`/`malloc` from value-semantic
copies — no single dominant function.
