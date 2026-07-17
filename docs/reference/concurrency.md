# Concurrency

> **Thesis context:** Concurrency tests whether the copy-in/copy-out call convention extends
> to threads without adding Send/Sync, lifetime annotations, or locks. It does — each task
> gets its own arena, arguments deep-copied in, result copied out — proving the arena model
> is thread-safe by construction where it owns the memory. Stops at the FFI boundary.

Tycho's call convention — arguments deep-copied in, the result copied out, a private arena per
call — is already a sound thread boundary. So concurrency is just that same convention run on
another thread: after the copy-in, a task shares zero bytes with its spawner, and race freedom
falls out of value semantics. There's no `Sendable`, no lifetimes, and no locks in the language.

## The safety envelope

The race-freedom guarantee has a precise edge, and it's worth stating before the mechanics: it
covers **pure Tycho values** — the world the compiler owns and deep-copies — and nothing past
the [FFI](ffi.md). Inside that envelope you get share-nothing concurrency for free. The moment a
task calls into C, you step outside it, and the compiler can no longer vouch for what you touch.

> **Crossing into C drops the guarantee.** If a C function reads or writes process-global or
> `static` state, Tycho can't see that sharing. Two tasks calling it race exactly as they would
> in plain C — the transpiler type-checks only the boundary (scalars, strings, opaque pointers),
> never C's internal state.

Concretely: two tasks calling an unshimmed stateful C library — say OpenSSL writing into a shared
`static` buffer — race on that buffer even though both sides look like isolated Tycho. The fix is
to keep the shared state per thread or serialize the calls. The `core:crypto` shim does the
former: its return buffer is `static __thread char *g_out` (`corelib/crypto/crypto_shim.c:35`),
so each OS thread gets its own. When you write your own FFI shim, design it the same way — assume
nothing about the C side's thread-safety and isolate it explicitly. The full analysis is in
[`docs/rfc/ffi-threading-design-review.md`](../rfc/ffi-threading-design-review.md).

## `spawn` and `wait`

```
fn count(path: string) -> int: ...

t1 := spawn count("a.txt")     # args deep-copied into the task's own arena
t2 := spawn count("b.txt")     # runs in parallel
total := wait(t1) + t2.wait()  # blocks; result deep-copied back out
```

`spawn f(args)` takes a direct call — the argument list *is* the capture list, explicit and by
value — and yields a `Task(T)` handle. Tasks are **affine and structured**: a handle cannot be
copied, reassigned, stored in a container, captured by a closure, or discarded; it is waited at
most once (a second `wait` aborts), and any task still running when its variable's scope exits —
block end, `break`/`continue`, early `return`, `or_return` — is implicitly joined there. A
function can never return while its tasks run, and an un-waited task can never leak or detach.

## `parallel for`

```
total := 0
parallel for i in range(1000000):   # K = ncpu() chunk tasks (TYCHO_THREADS overrides)
    total += score(i)               # reduction: chunk-local partials, folded at join
```

`parallel for` (over a range or a collection) lifts the body into a chunk procedure: captures
deep-copy into each task, and a reduction accumulator (`+` or `*` on `int`/`float`) starts from
the operator's identity per chunk and folds at the in-order join — so an integer result is
identical for any thread count. Any *other* write to an outer variable is a compile error: there
is nothing to race on.

## Channels

A channel is the one deliberately shared object — a bounded, **lock-free** queue (a Vyukov MPMC
ring). Value semantics outside it are untouched: `send` deep-copies the payload into the claimed
cell's arena, and `recv` copies out into the receiver's arena, returning `Option(T)` where `None`
means **closed and drained**.

```
ch := channel(string, 256)          # bounded; the creating scope frees it
w := spawn consumer(ch)             # fn consumer(ch: Channel(string)) -> int
ch.send("item-" + str(i))           # deep copy in (blocks when full)
match recv(ch):                     # deep copy out
    Some(s):
        ...
    None:                           # closed and drained
        ...
close(ch)
n := wait(w)
```

Channel memory is bounded by capacity × payload, for every element type. Channels cannot be
returned, stored, or captured; the creating scope frees the channel after the implicit joins
above it, so the handle can never dangle. A `channel(T, cap)` call is legal only as a
declaration's direct right-hand side.

## `select`

`select` waits on several channels at once — `recv` arms, plus an optional `default`
(non-blocking) and `closed` (every listed channel closed and drained) arm:

```
for true:
    select:
        recv(jobs, j):
            handle(j)
        recv(events, e):
            note(e)
        closed:
            break
```

The cost is upfront, not hidden: every value crossing a thread boundary is a deep copy — the
same rule as an ordinary call.

---

*Design, staging, and the measured numbers* (the reduction matching C-pthreads, the 1M-message
pipeline) are in [the concurrency design note](../guides/concurrency.md); the test suite runs golden
output under ASan/LSan **and** ThreadSanitizer, in both compilers, as part of `make ci`.
