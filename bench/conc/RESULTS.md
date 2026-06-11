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
| hier | 2.8 MB | 242 ms | 10888890 | deep copy in + deep copy out (value semantics) |
| C    | 1.5 MB | 630 ms | 10888890 | raw pointer handoff through a mutex ring, consumer frees |
| Go   | 7.7 MB |  91 ms | 10888890 | string body shared under GC |
| Rust | 2.1 MB | 143 ms | 10888890 | ownership move through a lock-wrapped mpsc |

CC-5 replaced the mutex ring with a **Vyukov bounded MPMC queue**: each ring
cell carries a sequence counter and its own arena, a sender claims a cell with
one CAS and runs the deep copy with NO lock held (the claim makes the cell
exclusive until the publish), and waiting is a spin -> yield -> timed-park
ladder whose parked-waiter count gates the wake path — the uncontended fast
path does zero syscalls. That took hier from 850 ms (mutex ring, copies under
the lock) to 242 ms.

Honest reading:

1. **hier now beats the hand-written C mutex ring 2.6x** while still paying
   two type-aware deep copies per message that C doesn't — the lock-free
   claim/copy/publish structure more than buys back the copy cost. The
   copies remain the entire price of "no dangling payload is expressible"
   (covering every element type, not just strings).
2. **The remaining 2.7x to Go** (242 vs 91 ms) is the userspace scheduler:
   a goroutine handoff is a context-free switch, while hier's consumers are
   OS threads that spin/yield between messages and contend on one dequeue
   counter. Rust's 143 ms (lock-wrapped mpsc) now sits between hier and Go.
   Closing further would mean batched dequeues or a thread-pool runtime —
   possible, but no longer a memory-model question.

Run-to-run variance on pipeline is ~±50 ms (4 consumers contending on the
dequeue CAS); parreduce is stable. Memory: hier holds ~2.8 MB — bounded by
design (cap x payload via per-cell arenas), 2.8x below Go's GC-driven 7.7 MB.

`select` (CC-5) ships with the same machinery: recv arms + `default` +
`closed` over a non-blocking `try_recv`, with a bounded pause ladder when
every arm is open-but-empty (worst-case wake latency ~50us; a select cannot
park on N condvars). See `tests/conc/select.hi`.

## Caveats

- Single-machine, single-run numbers; rerun on your hardware
  (`make bench-conc` checks the cross-language checksum every run).
- Message-passing is a throughput-per-message benchmark; at this payload
  size (6-11 bytes) per-message overhead dominates. Larger payloads shrink
  Go's relative lead and grow the copy cost share for hier.
- hier channels deliberately spend the copies; if a workload cannot afford
  them, the answer in hier is `parallel for` (no per-item synchronization),
  not a shared-memory escape hatch.
