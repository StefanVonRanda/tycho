# Concurrency design notes (maintainers)

Why the behavior in [reference/concurrency.md](../reference/concurrency.md)
is sound. Users don't need this page.

## The FFI edge

The race-freedom guarantee covers the values the compiler copies and stops at
the FFI. The concrete case that motivated the envelope wording: two tasks
calling a stateful C library whose return buffer is a shared `static` race on
that buffer even though both Tycho sides look isolated. The `core:crypto` shim
isolates per thread instead — its return buffer is thread-local
(`corelib/crypto/crypto_shim.c`), so each OS thread gets its own. Shim authors
should assume nothing about the C side's thread-safety and isolate explicitly.
Full analysis in [rfc/ffi-threading-design-review.md](../rfc/ffi-threading-design-review.md).

## Runtime

Thread-per-spawn tasks; the block pool is thread-local so allocation never
contends. A spawned thread flushes its pool before exiting, and `wait` frees
the task's arena tree on the waiting thread. A task handle carries its root
arena, return slot, and a `done` flag (the double-`wait` backstop). Generated
programs define `_DEFAULT_SOURCE` so `clock_gettime` / `sched_yield` /
`nanosleep` are visible.

A channel is a Vyukov bounded MPMC ring: each cell has an atomic sequence
counter and its own arena; one CAS claims a cell, the deep copy then runs with
no lock held, and a sequence store publishes. Per-cell arenas recycle on reuse;
capacity rounds up to a power of two. Waiting is spin → `sched_yield` → 1ms
timed park with a parked-waiter count gating the wake path, so the uncontended
path does zero syscalls and the check-then-park race costs at most one extra
retry. `select` cannot park on N condition variables, hence the ~1ms worst-case
wake latency. The finalizer stack is LIFO, so tasks declared after a channel
join before it frees.

`parallel for x in ch:` desugars to a `parallel for` over `0..<ncpu()` whose
body is `for true: select { recv(ch, x): … ; closed: break }`. `parallel for`
chunks to `ncpu()` (capped at 64) so the ~20µs per-`spawn` cost (measured over
2000 tasks) is paid once per loop, not per iteration: spawn a chunk, not an item.

## Lineage

Structured spawn with no function colouring follows Hylo/Val's
structured-concurrency work; the expressiveness comes from chunk-copy + merge
at a copy cost that is the design point. Share-nothing message copying is the
Erlang/Pony model; the channel core is Vyukov's bounded MPMC queue.

## Measurement

`make bench-conc` runs identical logic with cross-checked checksums in
Tycho, C, Go, and Rust (reduction, string pipeline, channel worker pool).
The reduction matches hand-written pthreads at the same peak RSS; the
channels are competitive with Go's while paying two deep copies per message
the others skip; the pool one-liner gets the tuned queue for free. Full
table and reading in [bench/conc/RESULTS.md](../../bench/conc/RESULTS.md).
