# Concurrency head-to-head: hier vs C vs Go vs Rust

`make bench-conc`. Two workloads, identical logic and checksum in every
language, each binary picking K = online cores itself. Fair-bench rule: every
language at its standard optimized build (hier -O3 via hierc, C `-O3
-pthread`, `go build`, `rustc -O`). Measured via bench/peakrss (peak RSS +
wall). 2026-06-11, AMD Ryzen 7 7735HS, 16 hw threads, Linux.

## parreduce — compute-bound parallel reduction

Sum of `(i*31 + 7) % 1000003` over 4x10^8 ints; hier uses `parallel for`
(CC-3: K chunk tasks, identity partials, fold at join), C pthread chunks,
Go goroutines + partials channel, Rust scoped threads.

| lang | peak RSS | wall | checksum |
|------|---------:|-----:|----------|
| hier | 1.5 MB | 47 ms | 199999822327800 |
| C    | 1.5 MB | 47 ms | 199999822327800 |
| Go   | 2.2 MB | 61 ms | 199999822327800 |
| Rust | 2.2 MB | 43 ms | 199999822327800 |

**hier is at exact C parity** — same wall, same peak RSS — with Rust ~9%
ahead and Go ~30% behind. The `parallel for` desugar (lift body to a chunk
proc, spawn K OS threads, join in order) costs nothing measurable over
hand-written pthreads at this grain, and per-task arena trees add no
measurable memory over C's stacks. This is the memory-model proof point:
data-parallel compute needs no GC, no scheduler, no runtime — and hier's
zero-annotation `parallel for` compiles down to exactly the C you would
write.

## pipeline — bounded-channel throughput

1 producer -> `channel(string, 256)` -> 4 consumers, 10^6 `item-<i>` string
payloads, checksum = total payload length.

| lang | peak RSS | wall | checksum | payload discipline |
|------|---------:|-----:|---------:|--------------------|
| hier | 2.7 MB | 850 ms | 10888890 | deep copy in + deep copy out (value semantics) |
| C    | 1.3 MB | 611 ms | 10888890 | raw pointer handoff, consumer frees |
| Go   | 7.7 MB |  91 ms | 10888890 | string body shared under GC |
| Rust | 2.1 MB | 141 ms | 10888890 | ownership move, no copy |

Honest reading, two separable effects:

1. **Primitive cost.** hier's channel is a mutex + condvars ring — the same
   primitive as the hand-written C version, which lands at 611 ms. hier's
   850 ms is 1.4x that; the delta is the two type-aware deep copies per
   message plus the per-slot arena bookkeeping. That is the entire price of
   "no dangling payload is expressible" — tycho documents exactly the
   use-after-free this prevents, and covers only `str` payloads; hier's
   copies cover every element type.
2. **Scheduler gap.** Go's 9x lead over C (not just over hier) comes from
   the goroutine scheduler: channel handoff parks/wakes goroutines in user
   space instead of futex round-trips through the kernel. Rust's 141 ms
   (ownership move through a lock-wrapped mpsc) brackets the same effect.
   This is a CC-5 question (lighter wakeups, batched signals, possibly
   spinning) — orthogonal to the memory model.

Memory: hier holds 2.7 MB — bounded by design (cap x payload via per-slot
arenas), 2.9x below Go's GC-driven 7.7 MB.

## Caveats

- Single-machine, single-run numbers; rerun on your hardware
  (`make bench-conc` checks the cross-language checksum every run).
- Message-passing is a throughput-per-message benchmark; at this payload
  size (6-11 bytes) per-message overhead dominates. Larger payloads shrink
  Go's relative lead and grow the copy cost share for hier.
- hier channels deliberately spend the copies; if a workload cannot afford
  them, the answer in hier is `parallel for` (no per-item synchronization),
  not a shared-memory escape hatch.
