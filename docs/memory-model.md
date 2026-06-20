# Memory model in the self-hosted compiler

The arena model itself — value semantics over implicit per-scope arenas — is laid
out in [thesis.md](thesis.md), [arrays-structs.md](arrays-structs.md), and the
README's [Memory model](../README.md#memory-model) section. This document is about
the *self-hosted* compiler: how `hierc0`'s code generation was brought onto that
same model, one type family at a time, and the reclamation techniques that make
the result competitive with the C reference compiler.

**Status: complete.** `hierc0` now emits the same value-semantic implicit-arena C
that `hierc` does, with **no known memory gap** and full feature parity. The
staged path that got there (MM-0 … MM-10) is recorded in the appendix; this top
half describes the model as it stands.

## What `hierc0` emits

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

Data moves between arenas in exactly two directions, both arranged by the
compiler: **down** as a function argument (a pointer into a parent arena, valid
for the call, no copy) and **up** as a return value (allocated in the caller's
arena). Assigning to a variable that lives in an outer scope is handled the same
way — the value is built in *that variable's* arena, so it can never dangle.

Each `if`/`else` block and each loop iteration gets its own child arena, reset or
freed at the block's end, so per-iteration temporaries keep loop memory bounded —
a million iterations run in constant memory.

### Escape is lexical, not dataflow

The reason all of this needs no whole-program may-alias analysis is value
semantics. With no aliasing, no closures over references, and no free pointers
(only explicit `inout`), every way a value can outlive its scope is
*syntactically visible*, and its destination is *lexically known at the write
site*: `outer = e` targets `outer`'s home block, `push(outer, v)` targets
`outer`'s home, `return e` targets `_parent`, an argument or `inout` targets the
caller. So the compiler decides where each allocation lives by reading
signatures and scopes — a local routing decision, not a dataflow trace.

### Why the migration could be incremental

`arena_free` reclaims only memory that was `arena_alloc`'d into that arena; heap
that is still `malloc`'d (a not-yet-migrated type) is untouched and simply leaks,
exactly as before. So migrating one type family onto arenas while the rest stay
on `malloc` is sound — no use-after-free, the migrated type stops leaking, the
rest leak as they did. That property is what let the codegen move type-by-type
behind a green fixpoint at every step. The one irreducible, all-at-once piece was
the threading spine: once any function takes `Arena *_parent`, every caller must
pass one.

## Reclamation techniques

Per-scope freeing alone is not enough to match a C compiler or beat a GC; four
further techniques close the gap, and all of them are **static** — there are no
reference counts anywhere.

- **Transient placement.** A statement whose result is scalar or discarded builds
  its heap temporaries in a per-statement arena that is freed immediately. So a
  discarded `print(... check(make(19)) ...)` no longer leaves its tree resident
  for the rest of the function — the transient is gone at statement end. Stores
  into longer-lived containers route to their own arena, so only true transients
  land in the per-statement one.

- **Move-on-last-use.** `b := a` normally deep-copies. When `a` is read exactly
  once in the whole function and not inside a loop — so that read is its single
  dynamic last use — `b` takes over `a`'s buffer instead, a move rather than a
  copy. Output-invisible; it reduces allocator churn.

- **Liveness-driven in-place reuse.** A loop-carried value reassigned each step
  (`a = step(a)`) is the arena's classic worst case: an arena cannot free a
  single object mid-scope, so every dead intermediate would pile up until the
  function returns (peak = total allocation). The fix uses what value semantics
  already proves *statically*: at `a = new`, `a`'s old buffer has exactly one
  owner and is dead the instant `new` is computed. The compiler hands that buffer
  back to the arena's free-list and the next allocation reuses it — the win
  reference counting (Perceus/FBIP) is built for, with no runtime refcount. The
  load-bearing soundness condition, learned from two caught corruption bugs: only
  recycle a variable read **at least twice**, because a read-once variable may
  have been moved out by move-on-last-use and no longer own its buffer.

- **Element-overwrite recycle.** `arr[k] = v` recycles the *evicted* element back
  into the array's arena. The old element is dead and uniquely owned the instant
  the new value is computed (reads deep-copy out, so nothing else references the
  slot). This closes the sliding-window-eviction case — a ring buffer where only
  the last *W* entries are live — and relies on a segregated size-class free-list
  so the eviction window's dead chunks are reused in O(1).

Together these turn the arena's two known defeats (loop-carried reassignment and
sliding-window eviction) into wins, matching C and beating Go (GC) and Koka
(reference counting) across the cross-language suite.

## Verification

Every stage was held to the same bar, with before/after numbers quoted rather
than a "looks done":

- `make fixpoint` — the self-build stays a fixed point (B≡C) and matches the C
  compiler's program output. This is the soundness oracle: an over-aggressive
  move or recycle that aliased would diverge the output.
- `make test` green with the self-emitted C running under AddressSanitizer /
  UndefinedBehaviorSanitizer / LeakSanitizer.
- Per-type RSS benchmarks, now wired as perf guards, each showing that type's
  bytes are now freed.
- The differential fuzzer, with distinct-value regression tests where
  value-masking could otherwise hide a corruption the differential alone misses.

## Across threads

The model extends to concurrency without new rules (see
[concurrency.md](concurrency.md)): a spawned task is an ordinary call whose
`_parent` is a private root arena (arguments deep-copy in before the thread
starts; the result copies out at `wait`, then the whole tree frees), the block
pool is thread-local so allocation never contends, and channel payloads live in
per-cell arenas recycled on ring reuse. Every cross-thread byte is a deep copy —
the same rule as an ordinary call — which is what makes threads race-free with
zero annotations.

---

## Appendix: migration history (MM-0 … MM-10)

For contributors. `hierc0` reached the Stage-4 fixpoint emitting **naive
malloc/leak C** — correct output, unbounded RSS. This is the staged campaign that
brought it onto the arena model, each step gated by the fixpoint + sanitizers,
each commit carrying its verification. Headline RSS deltas are for the workload
that exercises the migrated type.

**Strings (MM-0 … MM-1).**
- *MM-0 — string accumulator* (`ef68daa`). The self-append `out = out + x`
  compiled to a fresh leaked buffer per step (O(n²)); rewritten to grow one
  buffer in place. A 30 000-iteration build went **4638 MB / 1937 ms → 10 MB /
  3 ms** (~464×). Pure malloc/realloc — no arena threading — and high-leverage
  because `hierc0`'s own codegen is one giant string build.
- *MM-1 — threading spine + strings on arena* (`cc9aa27`, `8407340`, `a850736`,
  `29c881a`). The irreducible step: every function gains `Arena *_parent` and
  `_scope`, every call threads it, returns build into `_parent`. Brought on
  incrementally — print-arg transients (~42×), then sound returns + store-copy,
  then the locals flip (batch workload ~73×). String migration done: transient,
  local, and returned strings on the arena; stored strings malloc-copied;
  accumulator buffers stay malloc by necessity.

**Compound types (MM-2 … MM-4)** (`5438dc3`, `dfbf2d2`, `605085c`). Arrays
(~31×), maps (~3.7×), then structs/tuples/boxes (~1.67×) migrated with the same
coupled pattern (allocators take `Arena*`, copies become recursive and
arena-aware, store sites force the value into the container's arena). After MM-4
every heap type is arena-managed at function scope.

**Per-block arenas (MM-5 / MM-6a)** (`b9f66c7`). A child arena per if/loop block,
freed per iteration, so a loop-local container frees per-iteration instead of at
function return (loop-local array build 245 MB → 10 MB). Routing is lexical (a
`block_base` marking the current block's env extent), not dataflow.

**Element-level ownership (MM-6b … MM-6g, MM-7e/7f).** A long series moving
container *elements* into their container's arena: map keys (`f1ff194`); array
string elements (`c84eb4c`, ~274×); nested-array elements (~77×); struct/tuple
array elements (~140×); option/result array elements (scalar payload, ~45×); and
`inout` container home-arena threading (`75d85e4`, ~582×) plus its extension to
`inout` heap-struct fields. **MM-7f** closed the last residual — heap-payload
option/result elements like `[Option(str)]` — by making options first-class
deep-copied value types (245 → 11 MB at 4M iters, flat, verified by a 1200-seed
heap-payload fuzz campaign).

**Recursive enums + transient placement (MM-7a … MM-7d).** Enum nodes became
arena value types with a recursive deep-copy at every escape point (`ff96105`);
transient placement (`d4d13df`) landed the tree-workload win (binary-trees
2374 MB → 38 MB, tree-rewrite 825 MB → 9 MB); per-var block depth replaced the
single `block_base` with a stack of block-entry indices, closing the
array-pipeline gap (358 MB → 5 MB); and move-on-last-use mirrored the C
compiler's deep-copy elision.

**Liveness-driven reuse (MM-8)** (`bbea2c9` / `7807850`, widened in `aa30d66`,
`05f22f4`). The arena free-list and `arena_recycle`, turning the loop-carried
reassignment worst case into a win — `iter-transform` 3.5 GB → 4 MB (`hierc`) /
6 MB (`hierc0`). Two latent corruption bugs (recycling a moved-out buffer) were
caught and pinned by distinct-value tests; the fix is the read-≥-twice condition.

**Element-overwrite recycle (MM-9).** Recycling the evicted element on
`arr[k] = v`, backed by a per-size-class free-list (`Arena.bkt[16]`), closing the
sliding-window case — `window` 47.3 MB → 4.2 MB (`hierc`) / 3.6 MB (`hierc0`),
and faster.

**Per-statement transient reclaim (MM-10).** Each expression statement's
discarded value is wrapped in a per-statement arena, so a depth-19 stretch tree
from a discarded `print(... check(make(19)) ...)` frees at statement end rather
than at function return — prong-B binary-trees **25 → 13.3 MB**, below Koka's
reference-counted 14.8 MB, the last workload where a refcounting rival led on
memory.
