# 20. The concurrency model · 21. `spawn`/`Task`/`wait` · 22. `parallel for` · 23. Channels and `select`

Tycho's concurrency is the ordinary call convention run on another thread. The
same rule that governs a call — deep-copy arguments in, deep-copy the result out,
a private arena per activation ([§10](07-memory-model.md)) — is already a sound
thread boundary, so the concurrency constructs need no `Sendable` marker, no
lifetime annotations, and no lock machinery in the language.

> Provenance: `docs/reference/concurrency.md`; runtime `runtime/tycho_rt.c:528-841`
> (channel ring `:759-935`, ordering via the cell `seq` release/acquire `:998@c->seq, c->pos + 1, memory_order_release`,`:1010@memory_order_acquire) - (pos + 1)`).
> The ordering guarantees below (channel delivery order, `select` arm order,
> happens-before, cross-thread `wait`) were pinned from that runtime.

## 20. The concurrency model

Every value that crosses between threads crosses by **deep copy**, exactly as it
would across an ordinary call. A spawned task's arguments are copied into the
task's private storage before it starts running, and its result is copied back
into the waiting scope's storage when it is joined. After the copy-in, a task
shares **zero mutable bytes** with its spawner or with any sibling task.

The normative consequence is **race freedom by construction**: because no two
threads ever hold the same Tycho storage, there are no data races on Tycho values
— with no annotations required. The one deliberately-shared object is a channel
(§23), whose send/receive operations are themselves defined to deep-copy the
payload across the boundary, preserving the no-shared-storage invariant. This
guarantee covers Tycho values only and **does not extend across the FFI
boundary** ([§26](14-ffi.md)): a C function that touches process
global or `static` state can race exactly as it would in C.

**Ordering (happens-before).** Each cross-thread transfer establishes a
synchronizes-with relationship, so the transferred value — and everything the
source sequenced before it — is visible to the receiving side:

- **`spawn` → body:** a task's arguments are deep-copied before its thread
  starts, so the body observes them and every write sequenced before the `spawn`.
- **`send` → `recv`:** a `recv` that returns a value `v` synchronizes-with the
  `send` that produced `v` (a release store paired with an acquire load on the
  ring cell), so every write the sender sequenced before that `send` is visible
  to the receiver after that `recv` returns.
- **task → `wait`:** a task's body runs-before the `wait` (or implicit join) that
  observes its result, so the waiter sees all of the task's writes.

## 21. `spawn`, `Task`, `wait`

`spawn f(args)` takes a **direct call** and starts `f` on a new thread, yielding a
`Task(T)` handle where `T` is `f`'s return type. The argument list is the capture
list: each argument is deep-copied into the task's private root storage before
the thread begins. `wait(t)` (also `t.wait()`) joins the task, deep-copies its
result into the waiting scope, and yields it.

A `Task(T)` handle is **affine and structured**:

- it MUST NOT be copied, reassigned, stored in a container, captured by a
  closure, or discarded;
- it MUST be waited **at most once** (a second `wait` aborts at run time);
- any task still running when its handle's scope exits — at block end, `break`,
  `continue`, an early `return`, or `or_return` — is **implicitly joined** there.

Consequently a function can never return while a task it spawned is still
running, and a spawned task can neither leak nor detach.

The number of concurrently-live (started-but-not-joined) tasks has a **hard
ceiling** (default 1024; an implementation MAY expose an override, as the
reference does via `TYCHO_MAX_TASKS`). Exceeding the ceiling aborts — a
fork-bomb fails closed rather than exhausting the system. It is a ceiling, not a
pool: every admitted task runs immediately, so a task blocked waiting for another
cannot be starved.

A `Task` handle cannot be copied, reassigned, stored in a container, captured by
a closure, or passed as an argument (it is affine and non-storable). It therefore
can never reach a thread other than the one that created it, so a task is always
waited on its spawning thread; waiting a task from another thread is not
expressible in the language.

## 22. `parallel for`

`parallel for` applies to a **counting** or a **foreach** loop
([§14.4](10-statements.md#144-loops)) and runs its iterations across worker
threads. The iteration space is split into chunks; the reference implementation
uses `min(ncpu(), N)` chunks, MAY expose an override (`TYCHO_THREADS`), and MAY
impose a fixed upper bound on the chunk count — the reference bounds it at **64**,
so above 64 the fan-out is narrower than `ncpu()` reports
([§29.9](16-builtins.md#299-concurrency)).

**The program MAY choose the width: `parallel(W) for …`.** `W` is an ordinary
expression of type `int`, evaluated **once**, at the spawn site, before any
chunk starts. It replaces `ncpu()` in the rule above; the `min(…, N)` and the
64-chunk bound still apply, so `parallel(8) for i in 0..<3:` runs 3 chunks.
An explicit width **overrides `TYCHO_THREADS`** — the variable is the default
for a program that did not choose. A width outside `1..64` is refused: a
literal at compile time, and a computed one aborts at run time rather than
clamping, because a clamped width answers a different question than the one
asked. Omitting the width is unchanged in every respect.

Each chunk's captured values are deep-copied into it, **with one exception: a
`Channel(T)` capture is a scalar handle and is passed by value.** Every chunk
therefore shares the one queue rather than receiving a private copy of it. This
is not a detail of the reference implementation — it is what makes a worker pool
expressible at all, and an implementation MUST NOT deep-copy a captured channel.

The counting spelling is `parallel for i in 0..<N:`, and this is the **only**
context in which `0..<N` is legal — a sequential `for i in 0..<N:` is a compile
error naming the three-clause form as its replacement ([§14.4](10-statements.md#144-loops)).
`0` MUST be a literal `0` (a non-zero or computed lower bound is a compile
error), `N` is an ordinary `int` expression evaluated **once** at the fan-out,
the bound is **exclusive**, and the step is implicitly `1` — so, unlike the
`range(a, b, step)` form this replaced, there is no step to be zero and nothing
to diagnose. `parallel for x in xs:` is the foreach spelling and `xs` MUST be a
bare name (an array or a `Channel`), not an expression.

The three-clause and infinite `for` shapes **cannot** be parallel: a chunk is a
statically shaped sub-range of a known iteration space, and a three-clause
loop's post clause is arbitrary code, so its iteration count is not knowable
before the loop runs. `parallel for init; cond; post:` and `parallel for:` are
compile errors.

The only outer-scope write a `parallel for` body may perform is into a
**reduction accumulator** — a variable combined with `+` or `*` (on `int` or
`float`). Each chunk starts the accumulator from the operator's identity and the
chunk results are folded at an **in-order join**. Therefore:

- an **integer** reduction produces a result **identical for any thread count**
  (integer `+`/`*` are associative and exact); and
- a **floating-point** reduction **MAY reassociate**, so its result MAY differ
  across thread counts (floating-point `+`/`*` are not associative).

Any other write to an outer-scope variable from a `parallel for` body is a
**compile error** — there is nothing to race on.

### 22.1 Channels in a `parallel for` body

The reduction accumulator is the only way a `parallel for` body may *write* an
outer variable, so it is not a way to route a per-item result out of the loop.
A **channel** is. A `parallel for` body MAY `send` on, and MAY `recv` from, a
channel captured from the enclosing scope — directly or through a `select`
([§23.2](#232-select)) — and MUST be able to do so, because the capture is the
shared handle of the paragraph above rather than a per-chunk copy:

- `send(ch, v)` from the body is how a per-item result leaves the loop; it is
  not an outer-scope write and is not subject to the reduction rule. Every
  chunk sends into the same queue, and the channel's capacity is the
  loop's backpressure.
- `recv(ch)` from the body takes the next item from the same queue. Each item
  is taken by **exactly one** chunk (§23.1's MPMC guarantee), so a reduction
  folded from received items is deterministic for integer `+`/`*` regardless of
  how the chunks interleave.

`parallel for x in ch:` (§22's foreach spelling over a `Channel`) is defined in
terms of exactly this: it is equivalent to a `parallel for` over `0..<ncpu()`
whose body loops on `recv(ch)` and stops when the channel is closed **and**
drained. The producer MUST `close(ch)`; without it the chunks never finish and
the statement never joins.

The fail-closed rules of §22 are unchanged inside such a body: `break`,
`return` and `or_return` at the parallel-loop level are still compile errors, so
an early exit can never cross a chunk boundary.

> Provenance: `0..<N` parsed at `src/tychoc.c:3615-3640`; parallel-only refusal
> `src/tychoc.c:4021@par_here`; literal-zero refusal `src/tychoc.c:4024@ival != 0`;
> any other loop shape under `parallel` refused at `src/tychoc.c:3885@S_FORRANGE`
> (it is the only node the chunker accepts). Chunk fan-out `K = min(ncpu(), N)`
> `src/tychoc.c:10794-10795`, capped at 64 by `src/tychoc.c:11506@_pk > 64`
> (the chunk-handle array `src/tychoc.c:11507@_pts[64]` is the reason for the
> number); each chunk is a real OS thread,
> `runtime/tycho_rt.c:790@pthread_create`. A capture is deep-copied only when
> `src/tychoc.c:11517@type_is_heap(ct)` holds, and `type_is_heap`
> (`src/tychoc.c:1413-1435`) has no channel arm, so a `Channel(T)` capture is
> passed by value — one queue shared by every chunk.

## 23. Channels and `select`

### 23.1 Channels

`channel(T, cap)` creates a **bounded, lock-free** multi-producer/multi-consumer
queue of `T`, legal only as the direct right-hand side of a declaration. The
requested capacity `cap` MUST be at least 1 and is rounded up to a power of two
(minimum ring size 2). A `Channel(T)` cannot be returned, stored in a container,
or captured; it is freed when its creating scope exits (after implicit task
joins).

- `send(ch, v)` deep-copies `v` into the channel and blocks while the channel is
  full. Sending on a closed channel aborts.
- `recv(ch)` blocks for the next value and yields `Option(T)`: `Some(v)` for a
  received value (deep-copied into the receiver's storage), or `None` when the
  channel is **closed and drained**.
- `close(ch)` closes the channel; subsequent `recv`s drain remaining values and
  then yield `None`. Closing an already-closed channel aborts.

Channel memory is bounded by `capacity × payload`: payload storage is per-cell
and reclaimed as ring slots are reused.

**Ordering.** A channel delivers each sent value exactly once, and values are
received in the order their `send` calls linearized: each `send` atomically
acquires the next enqueue ticket, and each `recv` takes the next dequeue ticket
in that same order. Consequently a **single** sender's values are received in the
order it sent them (FIFO). Among **concurrent** senders, the relative order of
their values is whichever order those sends linearized — a race the language does
not otherwise order. With multiple receivers, each value is delivered to exactly
one receiver, in ticket order.

### 23.2 `select`

`select` waits on multiple channels:

- a `recv(ch, x):` arm runs when `ch` has a value, binding it to `x`;
- an optional `default:` arm makes the `select` non-blocking: it runs when at
  least one channel is still open but no `recv` arm is ready. The all-closed
  condition is terminal and takes priority over `default` — if every listed
  channel is closed and drained, the `closed` arm runs (if present) and the
  `select` exits *without* running `default`;
- an optional `closed:` arm runs when every listed channel is closed and drained.

A `select` MUST have at least one arm.

Ready arms are tried in **listed (lexical) order**, and the first ready arm is
taken; `select` is not fair, so an earlier-listed ready channel is always
preferred over a later one. When no `recv` arm is ready and no `default` is
present, `select` blocks (spinning, then yielding, then briefly sleeping) until
one becomes ready or the `closed` condition holds.
