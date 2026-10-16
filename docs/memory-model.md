# The memory model

The practitioner's view of Tycho's memory model: value semantics over implicit
per-scope arenas. Every binding is a value; heap storage is routed to an arena
whose lifetime covers its use, including the caller's arena for returned values
and `inout` updates. Scope exit releases that arena's ownership of its storage,
not necessarily its resident memory back to the OS. There is no reference type
and no garbage collector. The on-ramp is
[from-c-to-arenas.md](from-c-to-arenas.md); the argument is [thesis.md](thesis.md);
what it looks like in code is [arrays and slices](reference/arrays-slices.md).

The stability contract is in the README's status banner; details like arena
sizes are not part of it. Cross-language figures are in
[performance.md](performance.md).

## What the compiler emits

Every function threads an arena hierarchy and frees per scope. The shape, from
the emitted C:

```c
int main(void) {
    Arena _root = arena_new(0);   /* root arena */
    h_main(&_root);
    arena_free(&_root);           /* everything reachable frees here */
}
long h_sc(Arena *_parent, long h_a, long h_b) {
    Arena _scope = arena_child(_parent);
    { long _ret = (h_a + h_b); arena_free(&_scope); return _ret; }
}
```

The rules:

1. `main`'s wrapper creates `_root`, calls into the program, and frees `_root`.
2. Every function gains a leading `Arena *_parent` and opens
   `Arena _scope = arena_child(_parent)`.
3. Every heap allocation takes an arena argument and allocates into it.
4. **Return slot:** a returned heap value is built (or copied) into `_parent`,
   which outlives `_scope`, so the value survives the callee's frame:
   `{ T _ret = <built in _parent>; arena_free(&_scope); return _ret; }`.

The transpiler arranges storage routing. Read-only arguments can borrow storage
for the duration of the call; by-value parameters still behave independently and
may require deep copies, notably for heap-bearing structs. Returned heap values
are built or copied into the caller's arena. Assigning to an outer variable or
mutating through `inout` allocates in *that binding's* owning arena.

Blocks and loop iterations use child scratch arenas where needed, reset or
released at their boundaries. This enables reuse for bounded per-iteration
working sets; it does not bound memory retained in an outer container, nor does
it guarantee constant RSS for arbitrary loops.

### Reuse is not an OS release or a latency guarantee

`arena_free` walks an arena's block chain and puts its blocks in a thread-local
pool (`runtime/tycho_rt.c@block_release_chain`). Those blocks can serve later
allocations, so a large transient can remain resident after its helper returns.
`arena_reset` retains and rewinds the head block, pooling overflow blocks. The
single-block fast path with statistics disabled and no recycle bookkeeping is
O(1); general reset and teardown traverse the affected blocks. Pool lookup can
also scan for a suitable block (`runtime/tycho_rt.c@block_get`).

There is no collector and hence no GC pause, but allocation, block traversal,
platform allocator work, page faults and scheduling can still cause latency
spikes. Neither scope cleanup nor the absence of GC promises bounded latency or
an immediate RSS decrease. `TYCHO_ARENA_STATS` distinguishes bump allocation and
block reuse; its cumulative `OS reserved` counter is not a current RSS reading.

### Escape is lexical, not dataflow

The reason all of this needs no whole-program may-alias analysis is value
semantics. With no aliasing, no closures over references, and no free pointers
(only explicit `inout`), every way a value can outlive its scope is
*syntactically visible*, and its destination is *lexically known at the write
site*: `outer = e` targets `outer`'s home block, `push(outer, v)` targets
`outer`'s home, `return e` targets `_parent`, an argument or `inout` targets the
caller. So the transpiler decides where each allocation lives by reading
signatures and scopes — a local routing decision, not a dataflow trace.

### Why arena migration is sound incrementally

`arena_free` reclaims only memory that was `arena_alloc`'d into that arena; heap
that is still `malloc`'d (for a type not yet placed on an arena) is untouched and
leaks, exactly as a non-arena allocator would. So placing one type family
on arenas while the rest stay on `malloc` is sound: no use-after-free, the placed
type stops leaking, the rest leak as they did. The one irreducible, all-at-once
piece is the threading spine — once any function takes `Arena *_parent`, every
caller must pass one.

## Reclamation techniques

Per-scope release alone isn't enough to match hand-written C or beat a GC.
Several compiler-directed techniques reduce copying and allocation; none uses
reference counts.

- **Transient placement.** A statement whose result is scalar or discarded builds
  its heap temporaries in a per-statement arena that is freed immediately. So a
  discarded `print(... check(make(19)) ...)` no longer leaves its tree resident
  for the rest of the function — its arena storage becomes reusable at statement
  end, though pooled blocks may remain resident. Stores
  into longer-lived containers route to their own arena, so only true transients
  land in the per-statement one.

- **Move-on-last-use.** `b := a` normally deep-copies. When `a` is read exactly
  once in the whole function and not inside a loop — so that read is its single
  dynamic last use — `b` takes over `a`'s buffer instead, a move rather than a
  copy. Output-invisible; it reduces allocator churn.

- **`sink` — the move you ask for.** Where move-on-last-use is an inference the
  compiler makes silently, a `sink` parameter states it in the signature: the
  callee **owns and consumes** the argument, and the caller may not use that
  variable again. A fresh literal or a call's result is adopted with no copy.
  The value is that it is *checked* — a second use is a compile error rather than
  a copy you never see. The same rules that make it checkable also make it
  strict: a sink argument must be the variable's only mention in the function,
  and no named variable can be adopted inside a loop, because a copy hidden in a
  loop is the one place the move-vs-copy cost is invisible and unbounded. So a
  consuming chain is written point-free — each call's result handed straight to
  the next — rather than accumulated into a named variable.
  ([reference](reference/functions.md), and `tools/tycho-tmpl/` as a worked
  example.)

- **Liveness-driven in-place reuse.** A loop-carried value reassigned each step
  (`a = step(a)`) is the arena's classic worst case: an arena cannot free a
  single object mid-scope, so every dead intermediate would pile up until the
  function returns (peak = total allocation). At supported sites, the compiler
  checks that the old buffer is dead and uniquely owned after the new value is
  computed. It can then hand that buffer back to the arena's free-list for a
  subsequent compatible allocation — the win
  reference counting (Perceus/FBIP) is built for, with no runtime refcount. The
  load-bearing soundness condition is to recycle only a variable read **at least
  twice**, because a read-once variable may have been moved out by
  move-on-last-use and no longer own its buffer.

- **Element-overwrite recycle.** `arr[k] = v` recycles the *evicted* element back
  into the array's arena. The old element is dead and uniquely owned the instant
  the new value is computed (reads deep-copy out, so nothing else references the
  slot). This closes the sliding-window-eviction case — a ring buffer where only
  the last *W* entries are live — and relies on a segregated size-class free-list
  so the eviction window's dead chunks are reused in O(1).

These techniques bound allocation growth in supported loop-carried reassignment
and sliding-window patterns; they do not eliminate all rebuild copies or lower
the block pool's high-water mark. Pointer-shaped, structurally-shared data is
another cost: tries measured ~1.55× C in RAM after the compact indexed-dict map
layout, down from ~3×. See [the thesis](thesis.md) and the
[value-semantics limits note](internals/value-semantics-limits.md).

## Verification

The memory model is checked by complementary mechanisms. These are finite
checks on executed paths, not a proof of universal memory or semantic safety:

- **Sanitizers.** `make test` runs the emitted C for every fixture under
  AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer. This is the
  memory-error oracle on executed paths. An incorrect alias can still point into
  a valid arena block and escape detection, so sanitizers alone do not establish
  value independence.
- **Golden and differential output checks.** Fixtures compare expected output
  and native/sanitized output, including snapshots and mutation independence.
  Agreement covers those cases, not every possible program.
- **Leak fuzzing.** `fuzz/run_leak.py` feeds generated valid, terminating programs
  through the compiler, builds them under ASan+LeakSanitizer and asserts nothing
  is leaked at exit. This checks lost allocations or cleanup, not whether dead
  chunks were reused before exit; an arena can retain them and still free cleanly.
- **Compile-time refusal.** The aliasing cases under `tests/reject/` must fail to
  compile, so the rule is checked from both sides.
- **Allocation-growth and RSS guards** check bounded growth for particular
  workloads. Scope release, reuse and OS-resident memory are different measures.

## Across threads

The model extends to concurrency without new rules (see
[concurrency](reference/concurrency.md)): a spawned task is an ordinary call whose
`_parent` is a private root arena (arguments deep-copy in before the thread
starts; the result copies out at `wait`, then the whole tree releases), the block
pool is thread-local, and channel payloads live in per-cell arenas recycled on
ring reuse. Thread-local block reuse needs no shared pool lock; fresh allocation
can still enter the platform allocator. Unlike ordinary read-only call borrows,
thread-boundary copies isolate Tycho value storage; they are not a claim that
all platform resources or runtime synchronization are race-free.

---

## Appendix: what each mechanism buys, type by type

For contributors. Because placing one type on arenas while others stay on
`malloc` is sound (see *Why arena migration is sound incrementally*), you can
reason about the code generator one type family at a time. This
appendix records what each mechanism buys for each family. RSS figures are for
the workload that exercises the named type. Soundness throughout is checked by
the gates in [controls.md](controls.md) and [architecture.md](architecture.md).

**Compound types.** Arrays (~31×), maps (~3.7×), and structs, tuples, and boxes
(~1.67×) all use the same coupled pattern: allocators take `Arena*`, copies
become recursive and arena-aware, and store sites force the value into the
container's arena. Every heap type is then arena-managed at function scope.

**Per-block arenas.** A child arena per `if`/loop block, freed per iteration, lets
a loop-local container free per-iteration instead of at function return
(loop-local array build 245 MB → 10 MB). Routing is lexical (the current block's
env extent), not dataflow.

**Element-level ownership.** Moving container *elements* into their container's
arena: map keys; array string elements (~274×); nested-array elements (~77×);
struct/tuple array elements (~140×); option/result array elements with scalar
payload (~45×); and `inout` container home-arena threading (~582×) plus its
extension to `inout` heap-struct fields. The hardest case — heap-payload
option/result elements such as `[Option(str)]` — is handled by making options
first-class deep-copied value types (245 → 11 MB at 4M iterations, flat).

**Recursive enums and transient placement.** Enum nodes are arena value types
with a recursive deep-copy at every escape point; transient placement buys the
tree-workload win (binary-trees 2374 MB → 38 MB, tree-rewrite 825 MB → 9 MB);
per-variable block depth (a stack of block-entry indices rather than a single
marker) closes the array-pipeline gap (358 MB → 5 MB); and move-on-last-use
mirrors the C compiler's deep-copy elision.

**Per-statement transient reclaim.** Each expression statement's discarded value
is wrapped in a per-statement arena, so a depth-19 stretch tree from a discarded
`print(... check(make(19)) ...)` frees at statement end rather than at function
return — binary-trees 25 → 13.3 MB, below Koka's reference-counted 14.8 MB, the
last cross-language workload where a reference-counting rival led on memory.
