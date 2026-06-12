# Concurrency in hier

Status: **shipped, in both compilers** (CC-0 through CC-5). Gated by
`make conc` — every positive fixture runs native, ASan+LSan, and
ThreadSanitizer against goldens, and the hierc-built `hierc0` must reproduce
the same outputs (the parity differential) — wired into `make ci` (step 5/8).
Benchmarked against C, Go, and Rust in `bench/conc/`
([RESULTS.md](../bench/conc/RESULTS.md)).

## 1. The model

hier's call convention — arguments deep-copied in, result copied out, a
private arena per call — is already a sound thread boundary. Concurrency is
that same convention run on other threads. After copy-in, a task shares zero
bytes with its spawner, so race freedom falls out of value semantics: no
`Sendable`, no lifetimes, no locks in the language. (Erlang gets the same
guarantee from per-process heaps and message copying; Swift's
Sendable/region machinery exists only to patch first-class aliasing, which
hier doesn't have.)

### spawn / wait (structured tasks)

```
t := spawn count("a.txt")     # args deep-copied into the task's root arena, then the thread starts
u := spawn count("b.txt")
total := wait(t) + u.wait()   # join; result deep-copied into the waiting scope; task arena freed
```

`spawn` takes a **direct call** to a named function — the argument list IS
the capture list, explicit and by value (closure arguments work: their env
re-homes into the task root via `copyenv`). The result is `Task(T)`, a type
with no source syntax, so it can only live in a `let`-bound local.

Tasks are **affine and structured**:

- compile errors: copying/re-binding (`u := t`), reassignment, storage in any
  container/aggregate/closure, discarding a bare `spawn`, spawning builtins/
  externs/`void` fns/fns with `inout` params;
- **implicit join**: any task still unwaited when its variable's scope exits
  — block end, each loop iteration, `break`, `continue`, early `return`,
  `or_return` — is joined and freed right there. A function can never return
  while its tasks run; an unwaited task can never leak or detach;
- a second `wait` (e.g. wait-in-a-loop on an outer task) dies loudly at
  runtime — `hier: task already waited` — never UB: the named handle's
  struct outlives its wait (only the arena tree is reclaimed eagerly) and is
  freed by the scope-exit finisher.

### parallel for (fork-join data parallelism)

```
total := 0
parallel for i in range(400000000):   # K = ncpu chunk tasks; HIER_THREADS overrides
    total += (i * 31 + 7) % 1000003   # reduction: chunk-local partial, folded at the join
```

Works over ranges and collections (`parallel for x in xs:` rides the foreach
desugar). The body lifts into a chunk procedure: captures deep-copy into each
task (the honest per-chunk cost), reduction accumulators — `acc = acc + e` /
`acc = acc * e` (and `+=`/`*=`) on outer int/float locals, up to 4 — start
from the op's identity per chunk and fold at the in-order join. Integer
results are therefore **identical for any thread count**; float reductions
may reassociate, like every parallel reduce. Everything else is fail-closed:
any other outer write, reading an accumulator mid-loop, `break`/`return`/
`or_return` at parallel-loop level, `inout`-of-capture, and range steps are
compile errors. All chunk tasks join inside the statement.

Measured: **exact C-pthreads parity** (same wall, same peak RSS) on the
compute-bound reduction; ~6.8x on 8 threads.

### channels (the one shared object)

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

A channel is a bounded **lock-free MPMC queue** (see §2), internally
synchronized so value semantics outside it are untouched. `send` deep-copies
the payload into the claimed cell's arena — for *every* element type, which
closes the dangling-payload class tycho documents for its str-only copies —
and `recv` copies out into the receiver's arena. Channel memory is bounded
by capacity × payload (per-cell arenas recycle on reuse); capacity rounds up
to a power of two.

Lifetime is structural: `ch := channel(T, cap)` is the only legal creation
form; the creating scope frees the channel at exit, which is sound because
CC-2's implicit joins run first (the finalizer stack is LIFO — tasks
declared after the channel join before it frees). Channels cannot be
returned, stored in containers/fields/payloads/newtypes/fn-value types,
captured by closures, or reassigned. `Channel(T)` is valid parameter syntax
so workers can take one. Send-after-close and double-close die loudly.

### select (multi-channel fan-in)

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

Arms: one or more `recv(ch, x):`, plus at most one `default:` (runs when
nothing is immediately ready — makes the select non-blocking) and at most
one `closed:`. Without `default`, select waits on a bounded
spin → yield → 50µs-sleep ladder (a select cannot park on N condvars;
worst-case wake latency ~50µs — fine for fan-in servers, stated rather than
hidden). `break`/`continue` inside an arm bind to the *user's* enclosing
loop (select compiles to a goto loop, not a C `for(;;)`).

## 2. Implementation map

Everything exists twice — `src/hierc.c` and `compiler/hierc0.hi` emit
different C dialects with identical semantics — plus once in the shared
runtime (`runtime/hier_rt.c`, embedded into hierc's output; hierc0 emits the
same runtime text from its preamble).

**Runtime.**
- The block pool (`g_block_pool` / `g_pool`) is `__thread` (CC-0): allocation
  never contends; blocks may be freed on a different thread than they were
  allocated on (wait() frees the task tree on the waiting thread — a block is
  just malloc'd memory and lands in the releasing thread's pool); a spawned
  thread flushes its pool before exiting.
- `HTask { pthread_t; Arena root; void *ret; int done; }` — thread-per-spawn;
  the `done` flag is the double-wait backstop.
- `HChan` is a **Vyukov bounded MPMC ring** (CC-5): each `HCell` has an
  atomic sequence counter and its own `Arena`. One CAS claims a cell; the
  deep copy runs in the claimed cell with **no lock held** (claim ⇒ exclusive
  until the seq store publishes); waiting is spin → `sched_yield` → 1ms timed
  park, with a parked-waiter count gating the wake path — the uncontended
  path does zero syscalls. The timed park turns the check-then-park race
  into at most one extra retry, never a lost wakeup. `hier_chan_try_recv`
  (for select) returns got / open-but-empty / closed-and-drained.
- Generated programs `#define _DEFAULT_SOURCE` before the includes — strict
  `-std=c11` would hide `clock_gettime`/`sched_yield`/`nanosleep`.

**hierc.** `Task`/`Channel` are interned type ranges; spawn sites register at
resolve (the lambda-lift pattern) and `gen_program` emits one `HSpawnA_<i>`
args struct + `hier_spawn_<i>` trampoline per site; per-element-type
`hier_chan_{send,recv,tryrecv}_<i>` wrappers wrap `copy_into` around the
runtime claim/commit brackets; CC-2 finalizers live on a codegen stack
mirroring the scope-arena stack (`gen_block` ends, `return_frees`,
break/continue loop marks); `parallel for` lifts its body into a `__par<N>`
chunk proc reusing the spawn-site machinery.

**hierc0.** Types are strings (`"Task(T)"`, `"Channel(T)"`); `ESpawn`/
`SParFor`/`SSelect` AST variants (every exhaustive match extended — the
compiler's non-exhaustive-match errors enumerate the sites); the lift pass
assigns spawn ids, extracts chunk procs, and fills `SpawnInfo`/`ParInfo`
side tables; channel copies are inlined (`cp_field`) inside the claim/commit
bracket; CC-2 finalizers ride `"!"`-prefixed sentinel entries in the names
env, emitted LIFO at every scope exit. Method-call statements
(`ch.send(v)` as a statement) were added to the parser for this.

**Negative paths.** The reject fixtures (`tests/conc/reject/`) gate hierc
only, matching the repo's precedent for `tests/reject` — hierc0 implements
the soundness-critical validations but not every diagnostic. Abort fixtures
(`tests/conc/abort/`) lock the runtime die messages in both.

## 3. Measured

`make bench-conc` (identical logic + cross-checked checksums in all four
languages; AMD Ryzen 7 7735HS, 16 hw threads — details and honest reading in
[bench/conc/RESULTS.md](../bench/conc/RESULTS.md)):

| workload | hier | C | Go | Rust |
|---|---:|---:|---:|---:|
| parreduce (4×10⁸ int reduce) | **37 ms / 1.6 MB** | 36 ms / 1.6 MB | 62 ms | 43 ms |
| pipeline (10⁶ strings, 1→4 consumers) | **73 ms / 2.8 MB** | 654 ms (mutex ring) | 91 ms / 7.5 MB | 141 ms |

`parallel for` is at exact C parity. The lock-free channels beat everything —
including Go — while paying two deep copies per message that the others
don't. What looked like a 2.7× "goroutine scheduler gap" (242 ms before) was
false sharing on hier's side: `enq`/`deq` packed on one cache line, plus
cells straddling lines. Padding them apart (Vyukov's original layout) closed
it — not the locking, not a scheduler, and not a memory-model question.

## 4. Design lineage

- **Hylo / Val** (Teodorescu's structured concurrency, the MVS papers):
  structured spawn/await with no function colouring; Val's first design
  required sink-only spawn environments — exactly hier's only mode. hier
  skips Hylo's `inout`-to-disjoint-parts (no borrow machinery; chunk-copy +
  merge gives the expressiveness at a copy cost that *is* the thesis).
- **tycho** (`~/github/tycho`): shipped pthread spawn + bounded channels +
  select, `__thread` arenas — validated the shape; hier generalizes its
  str-only channel copy to every type and bounds channel memory per-cell.
- **Erlang/Pony**: share-nothing message copying at industrial scale.
- **Vyukov's bounded MPMC queue**: the CC-5 channel core.

## 5. Non-goals (thesis-preserving by omission)

- No `async`/`await` colouring; no actors (per-task arenas + channels already
  isolate); no mutex/atomic in the language; no work-stealing runtime (a
  blocked waiter is a parked OS thread — fine without nested-await pools).
- Deadlock (recv with no live sender and no close) is the user's bug and is
  not detected. A panic/abort in any task kills the whole process.
- Reject-grade diagnostics live in hierc; see §2.

## 6. Possible next levers (perf only, model is closed)

- Batched dequeues / per-consumer sub-rings to cut the dequeue-CAS contention
  behind the remaining Go gap; send arms in `select`; per-channel waiter
  registration to replace select's pause ladder with real parking.
