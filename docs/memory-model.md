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
int main(int argc, char **argv) {
    Arena _root = arena_new(0);   /* root arena */
    h_main(&_root);
    arena_free(&_root);           /* everything reachable frees here */
}
tycho_int h_greet_len(Arena *_parent, char *h_name) {
    Arena _scope = arena_child(_parent); _scope.name = "greet_len";
    char *h_msg = tycho_str_concat(&_scope, TYCHO_LIT("hello "), h_name);
    { tycho_int _ret = tycho_str_len(h_msg); arena_free(&_scope); return _ret; }
}
```

The rules:

1. `main`'s wrapper creates `_root`, calls into the program, and frees `_root`.
2. Every function gains a leading `Arena *_parent`, and opens
   `Arena _scope = arena_child(_parent)` when its body allocates something that
   does not escape (a function that allocates nothing, or only its result, opens
   none).
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

## Reclamation techniques

Per-scope release alone isn't enough to match hand-written C or beat a GC.
Several compiler-directed techniques reduce copying and allocation; none uses
reference counts.

- **Transient placement.** A statement whose result is scalar or discarded builds
  its heap temporaries in a per-statement arena that is freed immediately. So a
  discarded `print(... check(make(19)) ...)` does not leave its tree resident
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
the block pool's high-water mark. See [the thesis](thesis.md) and the
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
