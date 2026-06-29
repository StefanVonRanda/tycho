# Concurrency

Tycho's call convention — arguments deep-copied in, the result copied out, a private arena per
call — is already a sound thread boundary. So concurrency is just that same convention run on
another thread: after the copy-in, a task shares zero bytes with its spawner, and race freedom
falls out of value semantics. There's no `Sendable`, no lifetimes, and no locks in the language.

## Scope of the guarantee

"Race-free by construction" holds for *pure Tycho values* — the world the compiler owns and
deep-copies. It does **not** extend across the [FFI](ffi.md): if a task calls a C function that
touches process-global or `static` C state, Tycho can't see that sharing, and two tasks racing
on it race exactly as they would in C. So isolate such state per thread, or serialize the calls.

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
pipeline) are in [the concurrency design note](../concurrency.md); the test suite runs golden
output under ASan/LSan **and** ThreadSanitizer, in both compilers, as part of `make ci`.
