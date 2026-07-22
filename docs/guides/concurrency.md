# Concurrency in Tycho

Tycho is value-semantic: every call deep-copies its arguments in, copies its result
out, and runs against a private arena. That call convention is already a sound
thread boundary. Concurrency is the same convention run on other threads — after
the copy-in, a task shares zero bytes with its spawner, so race freedom falls out
of value semantics. There's no `Sendable`, no lifetime annotation, and no lock in
the language. (Erlang gets the same guarantee from per-process heaps and message
copying; Swift's Sendable/region machinery exists to patch first-class aliasing,
which Tycho doesn't have.)

This guarantee is over **pure Tycho values** — the world the transpiler owns. It
stops at the FFI boundary: a task calling C that mutates process-global or
`static` C state races just as it would in C, invisibly to the transpiler. Isolate
that state per thread (thread-local storage, as the `core:crypto` shim does) or
serialize the calls. The full analysis is in
[`rfc/ffi-threading-design-review.md`](../rfc/ffi-threading-design-review.md).

The model does **not** remove **deadlock or livelock**: a `recv` with no live
sender and no `close` parks forever (see [Limits](#limits-deliberate)). Data
races and use-after-free fall out of value semantics; deadlock doesn't. The
compiler warns about the three cases it can *prove* — see
[What the compiler warns about](#what-the-compiler-warns-about) — but proving
absence of a live sender in general is not on the table.

The model has four constructs:

- **`spawn` / `wait`** — structured tasks with by-value capture and automatic join.
- **`parallel for`** — fork-join data parallelism with reductions.
- **channels** — bounded lock-free queues, the one shared object.
- **`select`** — multi-channel fan-in.

I'm upfront about the cost: every value crossing a thread boundary is a deep
copy, the same rule as an ordinary call.

Concurrency works in both the C reference transpiler (`tychoc`) and the
self-hosted transpiler (`tychoc0`).

## spawn / wait — structured tasks

```
t := spawn count("a.txt")     # args deep-copied into the task's arena, then the thread starts
u := spawn count("b.txt")
total := wait(t) + u.wait()   # join; result deep-copied into the waiting scope; task arena freed
```

`spawn` takes a **direct call** to a named function — the argument list *is* the
capture list, explicit and by value. The result is `Task(T)`, a type with no
source syntax, so a handle can only live in a local. Tasks are **affine and
structured**, meaning:

- Copying or rebinding a handle, reassigning it, storing it in any container or
  closure, discarding a bare `spawn`, or spawning a builtin / extern / `void`
  function / function with `inout` parameters are all compile errors.
- **Implicit join**: any task still unwaited when its variable's scope exits —
  block end, each loop iteration, `break`, `continue`, early `return`,
  `or_return` — is joined and freed right there. A function can never return
  while its tasks run, and an unwaited task can never leak or detach.
- A second `wait` on the same task dies loudly at runtime
  (`tycho: task already waited`) — never undefined behavior.

## parallel for — fork-join data parallelism

```
total := 0
parallel for i in range(400000000):   # K = ncpu chunk tasks; TYCHO_THREADS overrides
    total += (i * 31 + 7) % 1000003   # reduction: chunk-local partial, folded at the join
```

Works over a range or a collection. The body lifts into a chunk procedure:
captures deep-copy into each task (that's the real per-chunk cost), and reduction
accumulators — `acc = acc + e` / `acc = acc * e` and the `+=`/`*=` forms, on
outer `int`/`float` locals, up to four — start from the operator's identity per
chunk and fold at the in-order join. Integer results are therefore **identical
for any thread count**; float reductions may reassociate, like every parallel
reduce. Everything else fails closed: any other write to an outer variable,
reading an accumulator mid-loop, `break`/`return`/`or_return` at the
parallel-loop level, a `inout` of a capture, and range steps are all compile
errors — there is nothing left to race on. All chunk tasks join inside the
statement.

On the compute-bound reduction this keeps up with C-pthreads here: 37 ms vs C's 36 ms, same peak RSS.

### Bounded fan-out over a channel

`parallel for x in ch:` drains a channel with `K = ncpu()` workers, each pulling
items until the channel is closed **and** drained:

```
jobs := channel(Job, 16)            # cap bounds buffered work — backpressure
pr := spawn produce(jobs, n)        # producer sends, then close(jobs) when done
parallel for j in jobs:             # K = ncpu() workers share the one queue
    results = results + work(j)     # ordinary reduction, folded at the join
```

This is the bounded-fan-out idiom — N items not known up front, at most `cap`
buffered, work spread across `ncpu()` workers — without you sizing a `range`
yourself. Each item is taken by exactly one worker (the MPMC queue), so integer
reductions are deterministic. The producer must `close(ch)` when done or the
workers park waiting for more. It desugars to a `parallel for` over `range(0,
ncpu())` whose body is `for true: select { recv(ch, x): … ; closed: break }`, so
the same fail-closed gates apply and the source must name a variable. `ncpu()`
(the fan-out width, overridable with `TYCHO_THREADS`) is also callable directly.
Worked example: `tests/conc/workers.ty`.

## Channels — the one shared object

```
ch := channel(string, 256)        # bounded; created here, freed at THIS scope's exit
w := spawn consumer(ch)           # fn consumer(ch: Channel(string)) -> int
ch.send("item-" + str(i))         # deep copy IN (blocks when full; dies if closed)
match recv(ch):                   # deep copy OUT -> Option(T)
    Some(s): ...
    None:    ...                  # closed AND drained
close(ch)
n := wait(w)
```

A channel is a bounded **lock-free MPMC queue**, internally synchronized so value
semantics outside it are untouched. `send` deep-copies the payload into the
claimed cell's arena — for *every* element type, not just strings — and `recv`
copies it out into the receiver's arena, returning `Option(T)` where `None` means
closed **and** drained. Channel memory is bounded by capacity × payload (per-cell
arenas recycle on reuse); capacity rounds up to a power of two.

Lifetime is structural. `ch := channel(T, cap)` is the only legal creation form,
and the creating scope frees the channel at exit — sound because the implicit
task joins run first (the finalizer stack is LIFO, so tasks declared after the
channel join before it frees). A channel cannot be returned, stored in a
container/field/closure, or reassigned; `Channel(T)` is valid parameter syntax so
workers can take one. Send-after-close and double-close die loudly.

## select — multi-channel fan-in

```
for true:
    select:
        recv(jobs, j):
            handle(j)
        recv(events, e):
            note(e)
        closed:                   # every listed channel closed AND drained
            break
```

Arms are one or more `recv(ch, x):`, plus at most one `default:` (runs when
nothing is immediately ready, making the select non-blocking) and at most one
`closed:`. Without `default`, select waits on a bounded spin → yield → 1ms-park
ladder — a select cannot park on N condition variables, so worst-case wake
latency is ~1ms, fine for fan-in servers. `break`/`continue` inside an arm bind
to the *user's* enclosing loop.

A `select` is also allowed inside a `parallel for` body, giving parallel
work-stealing consumers: the chunk tasks share the captured channels (a channel
is a scalar handle, passed by value — not deep-copied per chunk), so any chunk
can take any ready item, and a reduction inside an arm folds at the join like any
other. An early exit still cannot cross a chunk boundary, so `return` in an arm is
rejected just as it is anywhere else in a `parallel for` body.

## Measured

`make bench-conc` runs identical logic with cross-checked checksums in all four
languages (AMD Ryzen 7 7735HS, 16 hardware threads — full details and the honest
read in [bench/conc/RESULTS.md](../../bench/conc/RESULTS.md)):

| workload | tycho | C | Go | Rust |
|---|---:|---:|---:|---:|
| parreduce (4×10⁸ int reduce) | 37 ms / 1.6 MB | 36 ms / 1.6 MB | 62 ms | 43 ms |
| pipeline (10⁶ strings, 1→4 consumers) | 73 ms / 2.8 MB | 654 ms (mutex ring) | 91 ms / 7.5 MB | 141 ms |
| pool (10⁶ jobs, `parallel for x in ch:`) | 150 ms / 2.7 MB | 1975 ms (mutex ring) | 157 ms / 2.3 MB | 395 ms |

`parallel for` is at C parity (within a millisecond, same peak RSS). The lock-free channels hold their own against
Go, and are faster than the other reference implementations in this benchmark, while paying two
deep copies per message that the others don't.

On the **pool** workload — a bounded-channel worker pool written as the single
line `parallel for x in ch:` — tycho is at **Go parity, ~5% faster** (150 vs
157 ms) against Go's hand-written `for j := range jobs` + `WaitGroup` pool, both
lock-free on the hot path. The C and Rust figures are just the honest cost of a naive
mutex channel and of std lacking an MPMC channel, respectively (see RESULTS.md);
they aren't a tycho win — the win is that the one-liner gets the tuned Vyukov
queue for free.

## What the compiler warns about

A channel handle can only reach code by being passed as an **argument** — it
cannot be returned, stored in a struct/enum/container, captured by a closure, or
rebound. So the set of code that can touch one channel is a closed graph rooted
at its `ch := channel(T, cap)`, and the compiler can prove that an operation
appears **nowhere** in it. Three such cases warn (both `tychoc` and `tychoc0`,
same message and line):

```
jobs := channel(int, 16)
p := spawn produce(jobs, 50)
parallel for j in jobs:              # producer never calls close(jobs)
    total += j
```
```
warning: close(jobs) is never called, so this `closed:` arm can never run
         (a `parallel for` over a channel ends only when it is closed)
```

- **Nothing ever sends** on a channel that has a *blocking* receive — `recv(ch)`,
  or a `select` arm with no `default:` (a select with `default:` polls, so it
  never parks and never warns).
- **Nothing ever receives** from a channel that is sent to: the send parks once
  the buffer fills.
- **Nothing ever closes** a channel a drain waits on — the `parallel for x in ch`
  hang above.

These are lints, not errors: they never reject a program. And they are *absence*
proofs, never "this path might not send" — so a conditional send, a send behind
a flag, or a send in a loop that may not run all suppress the warning. Anything
the analysis cannot follow (a call it can't resolve) silences the channel
entirely. Missing a real deadlock is expected; warning about a working program
is a bug.

## Limits (deliberate)

These are on purpose, not gaps I forgot about:

- No `async`/`await` colouring, and no actors — per-task arenas plus channels
  already isolate state.
- No mutex or atomic in the language, and no work-stealing runtime: a blocked
  waiter is a parked OS thread.
- Deadlock is your bug. Beyond the three provable cases above, I don't detect
  it: a `recv` whose only sender took a branch that returns early parks forever
  and nothing warns. A panic or abort in any task kills the whole process.

## Verification

`make conc` runs every fixture natively, under AddressSanitizer + LeakSanitizer,
and under ThreadSanitizer against recorded goldens, and checks that `tychoc` and
`tychoc0` produce the same outputs. It's part of `make ci`. The cross-language
benchmarks live in `bench/conc/`.

---

## Appendix: implementation & lineage

For contributors. The feature lives in both transpilers — `src/tychoc.c` and
`compiler/tychoc0.ty` emit different C dialects with identical semantics — plus
once in the shared runtime (`runtime/tycho_rt.c`).

**Runtime.** The block pool is thread-local, so allocation never contends; a
spawned thread flushes its pool before exiting, and `wait` frees a task's arena
tree on the waiting thread. A task is a thread-per-spawn handle carrying its root
arena, return slot, and a `done` flag (the double-wait backstop). A channel is a
Vyukov bounded MPMC ring: each cell has an atomic sequence counter and its own
arena; one CAS claims a cell, the deep copy then runs with no lock held, and a
sequence store publishes. Waiting is spin → `sched_yield` → 1ms timed park, with
a parked-waiter count gating the wake path, so the uncontended path does zero
syscalls and the check-then-park race costs at most one extra retry rather than a
lost wakeup. Generated programs `#define _DEFAULT_SOURCE` so `clock_gettime` /
`sched_yield` / `nanosleep` are visible.

**Transpilers.** `tychoc` interns `Task`/`Channel` as type ranges, registers spawn
sites at resolve (the lambda-lift pattern), and emits one args-struct +
trampoline per site; per-element-type send/recv wrappers bracket `copy_into`
around the runtime claim/commit; the implicit-join finalizers ride a codegen
stack mirroring the scope-arena stack; `parallel for` lifts its body into a chunk
procedure reusing the spawn machinery. `tychoc0` represents the types as strings,
adds `ESpawn`/`SParFor`/`SSelect` AST variants, inlines the channel copies inside
the claim/commit bracket, and emits the finalizers LIFO at each scope exit.

**Lineage.** Structured spawn/await with no function colouring follows Hylo/Val's
structured-concurrency work (Val's first design required sink-only spawn
environments — exactly Tycho's only mode); Tycho skips the `inout`-to-disjoint-parts
machinery and gets the expressiveness from chunk-copy + merge at a copy cost that
*is* the design point. Share-nothing message copying is the Erlang/Pony model, and
the channel core is Vyukov's bounded MPMC queue.
