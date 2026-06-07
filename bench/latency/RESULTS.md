# Latency / GC-pause predictability

The benchmark axis that peak-RSS and total-wall miss: **pause behavior**. A
long-running service that steadily allocates and discards working sets cares not
just about *how much* memory or *how long* in total, but about *jitter* — the
stop-the-world pauses a tracing GC injects. This is the classic "why not just use
Go" question, answered with numbers.

Workload: 2000 rounds, each builds and discards a 100k-element working set. hier
reclaims it with one **O(1) arena reset per round**; C `free`s it; Go's GC must
trace and collect the garbage. All three print the same checksum (`599376507`).

## Results (`make bench-latency`)

Native builds at `-O3` (each language's standard optimized build: hier `-O3`,
C `-O3`, `go build`). Re-measured 2026-06-07.

| lang | peak RSS | wall | GC pauses |
|------|---------:|-----:|-----------|
| **hier** | 4.5 MB | 267 ms | **none — no GC** |
| C    | 2.3 MB | 121 ms | **none — no GC** |
| Go   | 11.4 MB | 1831 ms | **2927 collections, ~211 ms total pause** |

(Go self-reports its GC stats via `runtime.ReadMemStats`; hier and C have zero by
construction — there is no collector.)

## The point

- **hier pays zero GC pause — like C — but with no manual `free`.** Each round's
  working set lives in a per-iteration block arena that is reset in O(1) at the
  loop boundary; reclamation is deterministic and pause-free. That's the thesis in
  one line: **C's predictability with Go's ergonomics.**
- **Go spent ~211 ms of its 1883 ms run inside the garbage collector** (2927
  cycles). Those are tail-latency spikes a real service would feel; hier and C
  don't have them.
- hier even beats Go on **total** wall here (267 vs 1831 ms, ~6.8×) — both grow
  their arrays the same way (hier `push` / Go `append`), but Go carries the GC tax
  on top. C is fastest (121 ms) because it `malloc`s the exact size once (no growth)
  and frees explicitly — the raw-speed ceiling, at the cost of manual memory
  management. hier's growth-loop overhead vs C's exact alloc is a separate,
  known cost (~2.2× C here; push grows geometrically); it is not the pause story.

## Where this sits in the picture

Together with the memory benchmarks this brackets the model on a second axis:
- **memory** (prongB / dbquery / winagg / invindex / window): hier is C-class or
  better on transient churn, deep recursion, fixed-size retention, and real-DB
  work; loses on heap-record eviction (`../window`).
- **latency** (here): hier is pause-free like C, automatic like Go — no
  stop-the-world jitter, no manual frees.

**Reproduce:** `make bench-latency` (skips Go if absent).
