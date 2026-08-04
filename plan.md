# What comes next

> 2026-08-04: the three owner-directed optimization phases are complete —
> `core:intern` (9686567), `core:pool live()` (f9a2a2c), copy diagnostics
> (e289dbe + the return-site fix 95c3d43). New owner-directed phase: arena
> memory observability — close the three gaps the runtime names in its own
> comments (recycle hits uncounted, per-scope attribution deferred, block size
> never swept) and publish the numbers the existing `TYCHO_ARENA_STATS`
> instrument can already produce.

## Phase 1 — arena observability: counters, per-scope labels, block knob, measured numbers

The arena runtime (`runtime/tycho_rt.c`) says, in comments, exactly what it
cannot yet tell us: recycle hits are not counted (`arena_alloc`'s recycle path
returns before the stats counters — `st_alloc_calls` counts bumps only),
per-scope attribution is "deferred (ponytail: global first)", and the 64 KiB
block size is never swept. Three small changes + one measurement pass:

1. **Recycle counters** (`runtime/tycho_rt.c`): count every alloc request vs
   the requests served from the free lists (tiny + large), and the bytes
   recycled; print `recycle: N of M allocs from free lists (X%), Y of Z
   bytes` in the stats dump. The answer to "how much does FBIP reuse save?"
   becomes an instrumented number instead of a bench-RSS delta.
2. **Per-scope labels** (`src/tychoc.c`): stamp each loop scratch arena
   (`_scr%d = arena_child(...)` at the S_WHILE/S_FOR/S_FORRANGE emit sites,
   `src/tychoc.c:11292,11323,11411`) with `"<proc>:<line>"` so residency
   reports per statement, not per function. Bump `TYCHO_NLBL` 1024 → 4096
   (per-scope labels multiply the label count; the dump already reports lost
   attributions honestly if the table still fills).
3. **Block-size knob** (`runtime/tycho_rt.c`): `TYCHO_BLOCK=<bytes>` env
   override, read once in the constructor like `TYCHO_ARENA_STATS`, applied in
   `arena_new`. Makes block-size sweeps rebuild-free.
4. **Measure and publish**: run the standalone bench programs (all of
   `bench/*.ty`, `bench/{interp,gcscan,json,window,trie,dijkstra,lru}/`) with
   `TYCHO_ARENA_STATS=full` at the default block size; a block-size mini-sweep
   (`TYCHO_BLOCK=8192/65536/262144` on interp + json + lru) for peak live and
   wall; write the numbers into a new "Arena internals, measured" section of
   `docs/performance.md`.

Gate: `make test` (~8 min — the runtime is embedded in every generated
program and the compiler emits the new stamps, so the full suite is the
reddening lane) + `make corelib` + `make selfhost-check` (the emitted-C shape
changes; the fixed point must re-prove) + doc gates for the performance doc.
The 21 `src/tychoc.c` citations in the docs will shift again — re-point them
mechanically (this is the third such shift; unavoidable while the edits land
above cited regions).
Expected: suite count unchanged (589), selfhost byte-identity holds, and the
perf doc gains a measured table where the recycle rate, block reuse and
per-scope residency are real numbers.

## Not in scope

- Changing the block size default: the sweep reports, it does not retune.
  Retuning is a follow-on if a workload shows a clear winner.
- Any change to arena allocation policy (the recycle heuristics themselves).
- The build-tool candidate remains shelved.

> Phase 1 evidence — 2026-08-04: all four sub-items landed, all gates green.
> `make test` 589/0 (unchanged), `make corelib` all green,
> `make selfhost-check` green (the emitted-C shape changed — the per-scope
> stamps — and the fixed point still holds), doc gates ok.
>
> (1) Recycle counters: `st_alloc_reqs`/`st_recycle_hits`/`st_recycle_bytes`
> in `arena_alloc` (`runtime/tycho_rt.c:589`), printed as a `recycle:` line in
> the dump. First numbers, default block size: held-tree workloads recycle ~0%
> (interp/json/gcscan/trie — the working set is live), loop-carried rebuilds
> 4.5–16.7% (strarr_build 1.0M/22.0M, optstr_build 16.7%, structarr_build
> 9.1%), lru 18% of its 89 allocs. The "how much does FBIP save" question now
> has an instrumented answer per workload.
> (2) Per-scope labels: `_scr%d.name = "proc:line"` at the three loop-arena
> emit sites (`src/tychoc.c:11293,11324,11412`); `TYCHO_NLBL` 1024→4096.
> json shows `main:25` bumping 16.4 MiB across 850k allocs at a 344 B peak —
> the per-iteration reset, visible per statement. interp has no loops (pure
> recursion), so it shows function rows only — expected.
> (3) `TYCHO_BLOCK=<bytes>` env override, read once at startup
> (`runtime/tycho_rt.c:466`), applied in `arena_new`; sweeps are now rebuild-
> free. Block sweep (8/64/256 KiB on interp+json+lru): peak LIVE identical
> across sizes, slack ≤4% either way, wall flat — 64 KiB not retuned.
> (4) `docs/performance.md` gains "Arena internals, measured": the internals
> table, the three readings (tight at scale — 0.1–1.3% slack, 99.8% block
> reuse; recycle is workload-shaped; loop scratch peaks near zero), the block
> sweep table, the `TYCHO_BLOCK` note.
> Citations re-pointed a fourth time (runtime +27, src/tychoc.c +1/+2) — 29
> refs; the two wide 15-program ranges survived by luck and were left alone.
