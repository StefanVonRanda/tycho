# Concurrency head-to-head: tycho vs C vs Go vs Rust

`make bench-conc`. Four workloads, identical logic and checksum in every
language, each binary picking K = online cores itself. Fair-bench rule: every
language at its standard optimized build (tycho -O3 via tychoc, C `-O3
-pthread`, `go build` (Go 1.26.2), `rustc -O` (Rust 1.93.0)). Measured via
bench/peakrss (peak RSS + wall). AMD Ryzen 7 7735HS, 16 hw threads, Linux
(parreduce/pipeline 2026-06-11; pool 2026-06-25; mandelbrot 2026-07-16).

## parreduce — compute-bound parallel reduction

Sum of `(i*31 + 7) % 1000003` over 4x10^8 ints; tycho uses `parallel for`
(CC-3: K chunk tasks, identity partials, fold at join), C pthread chunks,
Go goroutines + partials channel, Rust scoped threads.

| lang | peak RSS | wall | checksum |
|------|---------:|-----:|----------|
| tycho | 1.5 MB | 47 ms | 199999822327800 |
| C    | 1.5 MB | 47 ms | 199999822327800 |
| Go   | 2.2 MB | 61 ms | 199999822327800 |
| Rust | 2.2 MB | 43 ms | 199999822327800 |

**tycho is at exact C parity** — same wall, same peak RSS — with Rust ~9%
ahead and Go ~30% behind. The `parallel for` desugar (lift body to a chunk
proc, spawn K OS threads, join in order) costs nothing measurable over
hand-written pthreads at this grain, and per-task arena trees add no
measurable memory over C's stacks. This is the memory-model proof point:
data-parallel compute needs no GC, no scheduler, no runtime — and tycho's
zero-annotation `parallel for` compiles down to exactly the C you would
write.

## mandelbrot — the same reduction, but FLOAT work

parreduce's float sibling. Sum the Mandelbrot escape count (`maxit = 2000`) over
a 1200x1200 grid and count in-set pixels; tycho uses the same `parallel for`
row-chunk reduction, C/Go/Rust the same per-thread-partial pattern as parreduce.
Two things change: the kernel is an **iterated complex map in `f64`** (real
scalar-FP work, not integer modulo), and because that map is chaotic the checksum
is also a **cross-language float-agreement oracle** — a single fused multiply-add
would diverge the escape counts, so the multiply is materialized into a rounded
`double` before the add, making the kernel fusion-proof under plain `-O3` in all
four ports. Median of 5 runs (±2 ms):

| lang | peak RSS | wall | checksum (total_iters in_set) |
|------|---------:|-----:|-------------------------------|
| tycho | 1.8 MB | 186 ms | 504003403 248334 |
| C    | 1.9 MB | 187 ms | 504003403 248334 |
| Go   | 2.3 MB | 189 ms | 504003403 248334 |
| Rust | 2.3 MB | 188 ms | 504003403 248334 |

**All four are within 2% — a dead heat — and print bit-identical output.** Two
readings:

1. **The `parallel for` reduction is free on float work too.** parreduce already
   showed int-reduction parity with C; here the per-pixel kernel is heavier
   scalar FP, so coordination overhead is an even smaller share and all four
   runtimes converge — Rust's ~9% parreduce edge vanishes because the bottleneck
   is now everyone's identical scalar-FP throughput, not thread hand-off. tycho
   holds the lowest peak RSS of the group (per-task arenas, no GC).
2. **The float pipeline agrees across four independent implementations.** A
   chaotic iterated map is the strictest cross-language float check in the suite:
   any reassociation or FMA fusion would drift the escape counts and split the
   checksum. Byte-identical `504003403 248334` from tycho, C, Go, and Rust means
   tycho's emitted `f64` arithmetic is IEEE-faithful, not just close. (Holds on
   x86-64/SSE2, where a `double` local is a true 64-bit value — see Caveats.)

## pipeline — bounded-channel throughput

1 producer -> `channel(string, 256)` -> 4 consumers, 10^6 `item-<i>` string
payloads, checksum = total payload length.

| lang | peak RSS | wall | checksum | payload discipline |
|------|---------:|-----:|---------:|--------------------|
| tycho | 2.8 MB |  73 ms | 10888890 | deep copy in + deep copy out (value semantics) |
| C    | 1.4 MB | 654 ms | 10888890 | raw pointer handoff through a mutex ring, consumer frees |
| Go   | 7.5 MB |  91 ms | 10888890 | string body shared under GC |
| Rust | 2.3 MB | 141 ms | 10888890 | ownership move through a lock-wrapped mpsc |

CC-5 replaced the mutex ring with a **Vyukov bounded MPMC queue**: each ring
cell carries a sequence counter and its own arena, a sender claims a cell with
one CAS and runs the deep copy with NO lock held (the claim makes the cell
exclusive until the publish), and waiting is a spin -> yield -> timed-park
ladder whose parked-waiter count gates the wake path — the uncontended fast
path does zero syscalls. That took tycho from 850 ms (mutex ring, copies under
the lock) to 242 ms.

A follow-up pass closed the rest with **cache-line layout, not a scheduler**:
`enq` and `deq` were adjacent fields, so the producer's enqueue CAS and all
four consumers' dequeue CASes false-shared one line — worst exactly when the
ring runs near-empty (consumers outpace the producer) and both sides hammer
the same head cell. Padding `enq`/`deq` onto their own lines (Vyukov's
original layout) took 242 -> ~85 ms, and line-aligning the cells themselves
(`HCell` is exactly 64 bytes, but malloc only guarantees 16, so cells
straddled lines) took it to ~73 ms. A profile drove both: 22% of CPU was
`sched_yield` from starved consumers respinning, all of it downstream of the
line ping-pong. (`pthread_cond_signal` instead of `broadcast` in the wake
path also landed — one cell published wakes one waiter — measured neutral
here but strictly less herd under heavy parking.)

Honest reading:

1. **tycho is now the fastest of the four** (73 ms vs Go's 91) while still
   paying two type-aware deep copies per message that the others don't —
   the lock-free claim/copy/publish structure plus the cache-conscious
   layout more than buy back the copy cost. The copies remain the entire
   price of "no dangling payload is expressible" (covering every element
   type, not just strings).
2. **The "scheduler gap" to Go turned out not to be one.** What looked like
   goroutine-handoff magic was false sharing on tycho's side; OS threads with
   a spin -> yield -> timed-park ladder are fully competitive at this
   message rate once the hot counters stop sharing lines. Batched dequeues /
   a thread pool remain available levers, but nothing here demands them.

parreduce is stable; pipeline run-to-run variance is now ~±5 ms. Memory:
tycho holds ~2.8 MB — bounded by design (cap x payload via per-cell arenas),
2.7x below Go's GC-driven 7.5 MB.

`select` (CC-5) ships with the same machinery: recv arms + `default` +
`closed` over a non-blocking `try_recv`, with a bounded pause ladder when
every arm is open-but-empty (worst-case wake latency ~50us; a select cannot
park on N condvars). See `tests/conc/select.ty`.

## pool — worker pool vs Go's channel pool

1 producer -> `channel(int, 256)` -> K = online-cores workers, 10^6 integer
jobs, each worker running the same 50-step MINSTD kernel
(`x = x*48271 % (2^31-1)`); checksum = sum of the kernel over all jobs
(order-independent). The contrast with `pipeline` is the **worker side**: tycho
spawns no consumers by hand — the whole pool is one line, `parallel for j in
jobs:`, which fans out K workers that share the queue and drain it until closed.
Go is the classic `for j := range jobs` pool behind a `WaitGroup`; C is a
textbook mutex+condvar bounded ring; Rust shares one `Receiver` behind an
`Arc<Mutex<>>` (std has no MPMC channel). Median of 3 runs (±5 ms):

| lang | peak RSS | wall | checksum | worker side |
|------|---------:|-----:|---------:|-------------|
| tycho | 2.7 MB |  150 ms | 1073744035926657 | `parallel for j in jobs:` — runtime fans out K, lock-free Vyukov queue |
| C    | 1.5 MB | 1975 ms | 1073744035926657 | hand-rolled mutex+condvar ring, signal per item |
| Go   | 2.3 MB |  157 ms | 1073744035926657 | `for j := range jobs` + `WaitGroup`, runtime channel |
| Rust | 2.1 MB |  395 ms | 1073744035926657 | `Arc<Mutex<Receiver>>` (std mpsc), lock serializes recv |

Honest reading:

1. **Against Go — the comparison this benchmark is for — tycho is at parity,
   ~5% faster here** (150 vs 157 ms) at comparable memory (2.7 vs 2.3 MB), and
   the tycho side is a single `parallel for j in jobs:` against Go's
   hand-written worker loop + `WaitGroup`. Both ride a tuned runtime channel
   (tycho's lock-free Vyukov MPMC, Go's runtime chan); the new sugar adds no
   measurable cost over the channel itself.
2. **C is ~13x slower — and that is the honest cost of a naive channel, not a
   tycho win.** The textbook mutex+condvar ring signals a condvar on every
   send and every recv, so K workers + the producer thrash one lock and storm
   the futex 2x10^6 times. A hand-tuned lock-free C ring (exactly what tycho's
   runtime *is*) would compete — the point is tycho ships that ring, so the
   one-liner gets it for free. Same pattern as `pipeline` (C's mutex ring at
   654 ms there).
3. **Rust's `Arc<Mutex<Receiver>>` (~2.6x) pays for std lacking an MPMC
   channel**: every worker locks the shared receiver to take a job, serializing
   the hand-off. `crossbeam`'s channel would close most of the gap; this is the
   std-only idiom.

The kernel is real per-item CPU work (5x10^7 MINSTD steps total), so this
measures pool coordination *plus* compute, not pure hand-off — which is why the
lock-contention tail (C, Rust) shows up so starkly while tycho and Go, both
lock-free on the hot path, track each other.

## Caveats

- Single-machine numbers (pool is a 3-run median); rerun on your hardware
  (`make bench-conc` checks the cross-language checksum every run).
- Message-passing is a throughput-per-message benchmark; at this payload
  size (6-11 bytes) per-message overhead dominates. Larger payloads shrink
  Go's relative lead and grow the copy cost share for tycho.
- tycho channels deliberately spend the copies; if a workload cannot afford
  them, the answer in tycho is `parallel for` (no per-item synchronization),
  not a shared-memory escape hatch.
- mandelbrot's bit-identical checksum assumes x86-64/SSE2 (all ports here), where
  a `double` local rounds to 64 bits so the materialized `m` blocks FMA fusion.
  On a target with excess intermediate precision (x87) or a `-ffast-math`-class
  build, the chaotic escape counts could drift and the checksum split — that would
  be a float-portability signal, not a bug in any one port.
