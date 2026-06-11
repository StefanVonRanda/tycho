# Concurrency for hier — research & staged design

Status: CC-0 through CC-5 SHIPPED IN BOTH COMPILERS.
CC-5: channels became a LOCK-FREE Vyukov bounded MPMC ring — per-cell
sequence atomics + per-cell arenas, the deep copy runs in the claimed cell
with no lock held, spin -> sched_yield -> 1ms-timed-park waiting with a
parked-waiter count gating the wake path (zero syscalls uncontended;
capacity rounds up to a power of two). Pipeline benchmark: 850 -> 242 ms,
now 2.6x FASTER than the hand-written C mutex ring while still paying both
deep copies; remaining 2.7x to Go is its userspace scheduler (see
bench/conc/RESULTS.md). Plus `select` over recv arms with optional
`default:` and `closed:` (all channels closed+drained) arms, built on a
non-blocking try_recv + a bounded pause ladder (~50us worst-case wake;
a select cannot park on N condvars); emitted as a goto loop so user
break/continue in arm bodies bind to the user's enclosing loop. The
generated programs need _DEFAULT_SOURCE before the includes (strict
-std=c11 hides clock_gettime/sched_yield/nanosleep — the runtime defines
it). Both compilers; select.hi in the parity differential.
Earlier status: hierc0 (self-hosted)
reproduces the full model in its own emitted-C dialect: ESpawn/SParFor AST
variants (every exhaustive match extended), the lift pass assigns spawn-site
ids, extracts parallel-for chunk procs (__par<N>) and registers SpawnInfo/
ParInfo side tables, the driver emits HSpawnA_<i> args structs + hier_spawn_<i>
trampolines, channel send/recv inline the type-aware copies (cp_field) inside
the runtime's mutex-held begin/commit bracket, CC-2 finalizers ride "!"-prefix
sentinels in the names env (LIFO at gen_block ends, return paths, break/
continue via bbases[loopdepth-1]), and the preamble carries the pthread
runtime with a __thread block pool. Method-call STATEMENTS (ch.send(v)) were
added to hierc0's parser along the way. Gated by `make conc` (now ALSO a
hierc0 parity differential per positive fixture, and part of `make ci` step
5/8); fixpoint stays byte-identical (lambda/conc-free functions skip the
lift). Reject fixtures stay hierc-gated (repo precedent for negative paths).
Earlier hierc-only status:
CC-4 channels: `ch := channel(T, cap)` (decl-only creation), `send(ch,v)` /
`recv(ch) -> Option(T)` (None = closed+drained) / `close(ch)` + UFCS forms;
`Channel(T)` param type syntax for workers. Bounded MPMC ring with PER-SLOT
arenas (memory bounded by cap x payload — fixes tycho's monotonic channel-
arena growth), type-aware copy_into for EVERY element type on send (into the
slot arena) and recv (into the receiver's arena), both under the channel
mutex (generated per-type wrappers around runtime begin/commit pairs). The
channel is the ONE shared object — freed at its creating scope's exit, sound
because CC-2's implicit joins land first (LIFO finalizer order). Fail-closed:
no return/containers/struct fields/enum payloads/newtypes/closure-capture/
reassignment/inline-creation; send-on-closed and double-close die loudly.
CC-3 `parallel for i in range(a,b)` / `parallel for x in xs` (foreach rides
the existing desugar): the body lifts to a chunk proc `__par<N>(__plo, __phi,
caps...) -> partials` spawned K = hier_ncpu() times (HIER_THREADS overrides)
through the CC-1 trampoline; captures deep-copy into each task root (copy-in
per chunk); reductions `acc = acc + e` / `acc = acc * e` (and +=/*=) on outer
int/float locals run against chunk-local identity partials and fold at the
in-order join. Any other outer write, break/return/or_return at parallel
level, inout-of-capture, acc reads, and range steps are compile errors. Int
reductions are K-independent (verified HIER_THREADS=1/7/8 identical); ~6.8x
on 8 cores for a compute-bound loop. Earlier stages:
`spawn f(args)` / `Task[T]` / `wait(t)` / `t.wait()`, thread-per-spawn,
copy-in/copy-out, thread-local block pool. CC-2 affine tasks: a handle cannot
be copied/re-bound/reassigned/discarded (compile errors); every scope exit
(block end, loop iteration, break/continue, early return/or_return, proc end)
implicitly joins+frees the tasks dying there (gen_block + return_frees +
loop-entry marks); a waited handle stays alive to its scope exit so a second
wait dies loudly at runtime ("task already waited") instead of UB. Gated by
`make conc` (tests/conc/: native + ASan/LSan + TSan positives, 9 reject + 1
abort fixtures); kept OUT of tests/*.hi and `make ci` until hierc0 parity
lands (the fixpoint differential runs those fixtures through both compilers).
hierc0 parity and CC-3+ still open. Goal: a concurrency model that
*preserves the thesis* — value semantics, implicit arenas, zero manual
memory management, no GC/RC — rather than bolting Go/Rust machinery on top.

Sources studied:
- **Hylo** (formerly Val): structured concurrency on mutable value semantics.
  Tour page (docs.hylo-lang.org/language-tour/concurrency), Teodorescu's
  Overload 181 "Concurrency: From Theory to Practice" (concore2full),
  hylo-lang discussions #746 (spawn requires Sinkable env) and #1604,
  Racordon et al. "Implementation Strategies for Mutable Value Semantics"
  (JOT 21, 2022).
- **tycho** (local, ~/github/tycho): shipped goroutine-style `spawn:` +
  bounded `Channel(T,N)` + `select:` over pthreads, per-thread TLS arenas
  (`__thread TychoArena*`, runtime_c.zig:745-756), channel-owned arena that
  deep-copies str payloads on send (RESULTS_concurrency.md).
- Background: Swift Sendable/SE-0414 regions, Pony per-actor heaps + deny
  capabilities, Erlang share-nothing copy, Cilk/Rayon scoped fork-join.

---

## 1. The core insight

hier's call convention already IS a sound thread boundary:

- Arguments are **deep-copied** into the callee's world (value semantics).
- The callee allocates only in **its own arena tree** (per-scope arenas,
  src/hierc.c S_RETURN codegen opens `Arena _scope = arena_child(_parent)`).
- The result is **copied out** into the caller's arena (`copy_into`,
  src/hierc.c:3800; closures re-home env via `copyenv` thunk, :3824).

Run that same convention on another OS thread and you get race freedom *by
construction*: the spawned task shares no mutable state with its parent,
because hier values are independent (the MVS paper's "independence of
values" property — hier already has it, more strongly than Hylo, since hier
deep-copies where Hylo borrows). No `Sendable`, no region analysis, no
capability annotations needed — those exist in Swift/Pony/Verona only to
patch first-class aliasing, which hier doesn't have.

Hylo converged on exactly this shape: structured `spawn`/`await`, no
function colouring, no actors, no user-visible mutexes. Val's first
concurrency design (#746) required the spawned environment to be
**sink-only** — i.e. copy/move in, value out — which is precisely hier's
only mode.

Erlang validates the model at industrial scale: per-process heaps,
messages copied on send, share nothing. hier's analogue: per-task arena
trees, values copied at spawn/wait/send.

## 2. What the current implementation forces

From the runtime/compiler audit:

1. **`g_block_pool` (runtime/hier_rt.c:93)** is the ONLY global mutable
   state in the runtime — a freelist of recycled blocks, touched by every
   `block_get`/arena free with no synchronization. Two threads allocating
   ⇒ corruption. Fix (CC-0): make it `__thread` (tycho's exact approach,
   runtime_c.zig:745). Thread-local pools mean zero contention and no
   locks on the hot path; blocks simply don't migrate between threads.
   Cost: a thread's pool dies with it (fine — spawn rejoins, pool freed).
2. **Arenas are not thread-safe and must never be shared.** Per-arena
   `bkt`/`freelist`/`nfree` are unsynchronized. Invariant: an arena tree is
   owned by exactly one thread. The design below never shares one.
3. **`str` is `char*` into an arena** (docs/ffi.md). Handing a str pointer
   to another thread is a use-after-free once the source arena dies.
   Copy-in at spawn / copy-out at wait makes this impossible by
   construction — same rule as the FFI str-return arena-copy.
4. **Closures**: env is deep-copied at construction into the owner arena
   (src/hierc.c E_LAMBDA) and re-homed on escape via `copyenv`
   (src/hierc.c:3824, :3061-3063). Spawning a closure = invoke `copyenv`
   into the task's root arena before the thread starts. The machinery
   exists; spawn is just another escape direction.
5. **Dual-compiler tax**: every surface feature lands in both src/hierc.c
   (~6.4k lines) and compiler/hierc0.hi (~6.3k lines) and must keep
   `make fixpoint` green. This argues hard for a MINIMAL surface: one new
   expression form, one builtin, one runtime section.

## 3. The design: structured fork-join, share-nothing

### Surface (CC-1)

```hier
fn count(path: string) -> int: ...

let t = spawn count("a.txt")     # starts thread; args deep-copied NOW
let u = spawn count("b.txt")
let a = wait(t)                  # blocks; result deep-copied into waiter's arena
let b = wait(u)
```

- `spawn <call-expr>` — the operand is restricted to a direct call (named
  fn, UFCS method, or closure-valued variable). No bare block form: the
  call's argument list IS the capture list, explicit and by-value. This
  sidesteps "what does a spawn block capture" entirely — the answer is
  visible at the call site. (tycho's `spawn:` block + deep-copy-captures
  is the same semantics with implicit capture; explicit is more honest
  and far cheaper to implement twice.)
- Type: `spawn f(args)` where `f(args): T` has type `Task[T]` — a new
  opaque builtin generic like `Option[T]`. No methods, no fields; only
  `wait` consumes it.
- `wait(t: Task[T]) -> T` — joins, copies result out, frees the task's
  arena tree. UFCS gives `t.wait()` for free.

### Semantics / soundness rules

- **Copy-in**: at the spawn site, each argument is `copy_into`'d a *task
  root arena* created by the spawner; only then does the thread start. The
  spawned fn's normal codegen then runs against that root as `_parent`.
  Closure operands additionally run `copyenv` into the task root. After
  copy-in, parent and child share zero bytes.
- **Copy-out**: the spawned fn's return value lands in the task root arena
  (normal return-to-parent codegen). `wait` copies it from the task root
  into the waiter's scope arena, then frees the task root. Identical to
  the FFI str-return rule, generalized by the existing type-aware
  `copy_into`.
- **Tasks are affine, fail-closed**: a `Task[T]` must be waited exactly
  once. Compile-time: reject `Task` in containers/struct fields/returns
  initially (like the FFI composite rejection — fail closed, widen later).
  Runtime backstop: scope exit with an un-waited task ⇒ implicit join
  (never detach — Hylo's rule: dropping a handle must join-or-cancel,
  never leak a running thread touching freed memory). Implicit join also
  makes the construct *structured*: a function that spawns cannot return
  while its children run, preserving local reasoning.
- **No mutex, no atomic, no shared anything** in the language. tycho ships
  none either; users who need shared services own them in one task behind
  channels (CC-4) or via FFI.
- **FFI `ptr`** may cross threads (opaque handle; the C library owns it
  and its thread-safety). Document, don't police — consistent with FFI's
  existing trust boundary.

### Runtime (CC-1)

pthread per spawn. ~80 lines in runtime/hier_rt.c:

```c
typedef struct { pthread_t th; Arena *root; void *ret; } HTask;
/* spawn codegen: root = arena_new(); copy args into root;
   pthread_create(trampoline) -> trampoline calls fn(root, copied args),
   stores result ptr/value into task->ret (allocated in root). */
/* wait codegen: pthread_join; T out = copy_into(T, caller_arena, ret);
   arena_free_tree(root); free pool blocks belonging to that thread? no —
   blocks recycle into the WAITER's TLS pool on free (free happens on the
   waiting thread), which is exactly right. */
```

Thread-per-spawn is the honest v1: no scheduler, no colouring, matches
tycho, and the parfetch benchmark shows OS threads are fine for fan-out
workloads. Hylo's thread-hopping/work-stealing pool (Overload 181) solves
*pool starvation under nested awaits* — a problem thread-per-spawn doesn't
have (a blocked waiter is just a parked OS thread). Revisit pooling only
if/when a benchmark shows spawn cost dominating (CC-5).

### Stages

| Stage | Content | Risk gate |
|---|---|---|
| **CC-0** | `g_block_pool` → `__thread`. No language change. | `make ci` + fuzz; behaviour identical single-threaded. |
| **CC-1** | `spawn` expr + `Task[T]` + `wait`, thread-per-spawn, copy-in/copy-out. Both compilers. | fixpoint green; ASan/TSan test: spawned task mutates copies, parent unaffected; str-across-spawn UAF test under ASan. |
| **CC-2** | Structured enforcement: affine Task checking, implicit join at scope exit, `Task` rejected in composites. | tests/reject negative cases. |
| **CC-3** | `parallel for` over int ranges/arrays: N body-tasks, each body a copy-in/copy-out spawn over its chunk, results merged by copy. (Hylo's `spawn(bulk:)`.) Sugar over CC-1 — no new runtime. | bench: tree/invindex parallel speedup; memory parity story. |
| **CC-4** | `Channel[T]`, bounded, blocking send/recv. Send = `copy_into` the channel-owned arena (mutex-guarded); recv = copy out + recycle. hier does this *better* than tycho: tycho deep-copies only str payloads (its RESULTS_concurrency.md documents the dangling-pointer footgun for other types); hier's type-aware `copy_into` closes it for every T uniformly. | TSan; fuzz kind: spawn+channel weave. |
| **CC-5** | (only if benchmarks demand) worker pool / work-stealing; `select`. | — |

### Explicit non-goals (thesis-preserving by omission)

- No `async`/`await` colouring (Hylo and tycho both reject it).
- No shared-memory primitives (mutex/atomic/RwLock) in the language.
- No `inout`-to-disjoint-parts parallelism (Hylo's concurrent quicksort
  trick): hier has no borrow machinery; chunk-copy + merge in CC-3 gives
  the same expressiveness at a copy cost — that copy is *the thesis*,
  measure it honestly in benchmarks rather than hide it.
- No actors: per-task arenas + channels already give actor isolation
  without a new abstraction.

## 4. The benchmark story (prove-the-arena north star)

Concurrency is also a *memory-model* proof point: per-task arena trees
mean parallel hier has no shared allocator contention (TLS pools), no GC
pauses, and per-task peak memory = task working set. Head-to-heads worth
building once CC-3 lands: parallel tree build, parallel invindex shard +
merge, parfetch-style I/O fan-out (vs Go goroutines, Rust scoped threads,
tycho spawn). The honest caveat to map: copy-in/copy-out cost on large
arguments/results vs Go's free sharing — the boundary analogue of the
invindex build-and-hold finding.

## 5. Open questions

1. Spawn of a closure that captures a closure (nested env re-home) —
   `copyenv` is recursive via `copy_into` so this *should* fall out; needs
   a dedicated test, not an assumption.
2. `Task[T]` in the self-hosted compiler's type rep: cheapest as a new
   one-payload builtin kind alongside Option/Result, reusing their codegen
   paths.
3. Does CC-2's affine check need dataflow, or is "wait-in-same-scope or
   implicit-join" enough for v1? (Lean fail-closed: same-scope only.)
4. Panics/aborts inside a task: v1 = whole-process abort (matches current
   single-thread abort semantics); revisit with Result-returning wait.
5. TLS (`__thread`) portability for the bootstrap path — hierc0 emits C;
   `__thread` is GCC/Clang-universal on Linux. WASM (if ever) needs the
   tycho-style fallback.
