# Concurrency in Hier

Hier's call convention — arguments deep-copied in, result copied out, a private
arena per call — is already a sound thread boundary. Concurrency is that same
convention run on other threads: after the copy-in, a task shares zero bytes with
its spawner, so race freedom falls out of value semantics. There is no
`Sendable`, no lifetime annotation, and no lock in the language. (Erlang gets the
same guarantee from per-process heaps and message copying; Swift's
Sendable/region machinery exists to patch first-class aliasing, which Hier does
not have.)

The price is honest and stated, not hidden: every value crossing a thread
boundary is a deep copy, the same rule as an ordinary call.

## spawn / wait — structured tasks

```
t := spawn count("a.txt")     # args deep-copied into the task's arena, then the thread starts
u := spawn count("b.txt")
total := wait(t) + u.wait()   # join; result deep-copied into the waiting scope; task arena freed
```

`spawn` takes a **direct call** to a named function — the argument list *is* the
capture list, explicit and by value. The result is `Task(T)`, a type with no
source syntax, so a handle can only live in a local. Tasks are **affine and
structured**:

- Copying or rebinding a handle, reassigning it, storing it in any container or
  closure, discarding a bare `spawn`, or spawning a builtin / extern / `void`
  function / function with `mut` parameters are all compile errors.
- **Implicit join**: any task still unwaited when its variable's scope exits —
  block end, each loop iteration, `break`, `continue`, early `return`,
  `or_return` — is joined and freed right there. A function can never return
  while its tasks run, and an unwaited task can never leak or detach.
- A second `wait` on the same task dies loudly at runtime
  (`hier: task already waited`) — never undefined behavior.

## parallel for — fork-join data parallelism

```
total := 0
parallel for i in range(400000000):   # K = ncpu chunk tasks; HIER_THREADS overrides
    total += (i * 31 + 7) % 1000003   # reduction: chunk-local partial, folded at the join
```

Works over a range or a collection. The body lifts into a chunk procedure:
captures deep-copy into each task (the honest per-chunk cost), and reduction
accumulators — `acc = acc + e` / `acc = acc * e` and the `+=`/`*=` forms, on
outer `int`/`float` locals, up to four — start from the operator's identity per
chunk and fold at the in-order join. Integer results are therefore **identical
for any thread count**; float reductions may reassociate, like every parallel
reduce. Everything else fails closed: any other write to an outer variable,
reading an accumulator mid-loop, `break`/`return`/`or_return` at the
parallel-loop level, a `mut` of a capture, and range steps are all compile
errors — there is nothing left to race on. All chunk tasks join inside the
statement.

On the compute-bound reduction this lands at **exact C-pthreads parity** (same
wall, same peak RSS), ~6.8× on 8 threads.

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
`closed:`. Without `default`, select waits on a bounded spin → yield → 50µs-sleep
ladder — a select cannot park on N condition variables, so worst-case wake
latency is ~50µs, fine for fan-in servers. `break`/`continue` inside an arm bind
to the *user's* enclosing loop.

## Measured

`make bench-conc` runs identical logic with cross-checked checksums in all four
languages (AMD Ryzen 7 7735HS, 16 hardware threads — full details and the honest
reading in [bench/conc/RESULTS.md](../bench/conc/RESULTS.md)):

| workload | hier | C | Go | Rust |
|---|---:|---:|---:|---:|
| parreduce (4×10⁸ int reduce) | **37 ms / 1.6 MB** | 36 ms / 1.6 MB | 62 ms | 43 ms |
| pipeline (10⁶ strings, 1→4 consumers) | **73 ms / 2.8 MB** | 654 ms (mutex ring) | 91 ms / 7.5 MB | 141 ms |

`parallel for` is at exact C parity. The lock-free channels beat everything,
including Go, while paying two deep copies per message that the others do not.
What first looked like a 2.7× scheduler gap (242 ms) turned out to be false
sharing — the enqueue/dequeue counters packed on one cache line, and cells
straddling lines. Padding them apart closed it; it was never the locking, a
scheduler, or a memory-model question.

## Limits (deliberate)

- No `async`/`await` colouring, and no actors — per-task arenas plus channels
  already isolate state.
- No mutex or atomic in the language, and no work-stealing runtime: a blocked
  waiter is a parked OS thread.
- Deadlock (a `recv` with no live sender and no `close`) is the user's bug and is
  not detected. A panic or abort in any task kills the whole process.

## Verification

Concurrency ships in both compilers. `make conc` runs every positive fixture
native, under AddressSanitizer + LeakSanitizer, and under ThreadSanitizer against
recorded goldens, and asserts the self-hosted compiler reproduces the same
outputs (the parity differential); it is part of `make ci`. The cross-language
benchmarks live in `bench/conc/`.

---

## Appendix: implementation & lineage

For contributors. Everything exists twice — `src/hierc.c` and
`compiler/hierc0.hi` emit different C dialects with identical semantics — plus
once in the shared runtime (`runtime/hier_rt.c`).

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

**Compilers.** `hierc` interns `Task`/`Channel` as type ranges, registers spawn
sites at resolve (the lambda-lift pattern), and emits one args-struct +
trampoline per site; per-element-type send/recv wrappers bracket `copy_into`
around the runtime claim/commit; the implicit-join finalizers ride a codegen
stack mirroring the scope-arena stack; `parallel for` lifts its body into a chunk
procedure reusing the spawn machinery. `hierc0` represents the types as strings,
adds `ESpawn`/`SParFor`/`SSelect` AST variants (every exhaustive match is
extended — the compiler's non-exhaustive-match errors enumerate the sites),
inlines the channel copies inside the claim/commit bracket, and emits the
finalizers LIFO at each scope exit.

**Lineage.** Structured spawn/await with no function colouring follows Hylo/Val's
structured-concurrency work (Val's first design required sink-only spawn
environments — exactly Hier's only mode); Hier skips the `mut`-to-disjoint-parts
machinery and gets the expressiveness from chunk-copy + merge at a copy cost that
*is* the thesis. Share-nothing message copying is the Erlang/Pony model at
industrial scale, and the channel core is Vyukov's bounded MPMC queue.

**Possible next levers** (performance only — the model is closed): batched
dequeues or per-consumer sub-rings to cut dequeue-CAS contention behind the
remaining Go gap; send arms in `select`; and per-channel waiter registration to
replace select's pause ladder with real parking.
