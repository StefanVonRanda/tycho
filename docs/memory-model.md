# Memory model in the self-hosted compiler

Tycho's memory model — value semantics over implicit per-scope arenas — is
described in [thesis.md](thesis.md), [arrays-structs.md](arrays-structs.md), and
the README's [The thesis](../README.md#the-thesis) section. This document is
about the *self-hosted* transpiler, `tychoc0`: how it generates code on that same
model, and the reclamation tricks that let its output match the C reference
transpiler, `tychoc`.

`tychoc0` emits the same value-semantic, implicit-arena C that `tychoc` does. I
don't know of any memory gap between the two transpilers, and they have full feature
parity. The sections below describe the model as it stands; a closing appendix
sketches, for contributors, how I brought the self-hosted code generator onto
the model one type family at a time.

This is an experimental, proof-of-concept transpiler. The memory model is the
central idea it exists to demonstrate, and the cross-language benchmarks below
are encouraging, but the implementation is young and should be judged that way.

## What `tychoc0` emits

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
transpiler: **down** as a function argument (a pointer into a parent arena, valid
for the call, no copy) and **up** as a return value (allocated in the caller's
arena). Assigning to a variable that lives in an outer scope works the same
way — the value is built in *that variable's* arena, so it can never dangle.

Each `if`/`else` block and each loop iteration gets its own child arena, reset or
freed at the block's end, so per-iteration temporaries keep loop memory bounded —
a million iterations run in constant memory.

### Escape is lexical, not dataflow

The reason all of this needs no whole-program may-alias analysis is value
semantics. With no aliasing, no closures over references, and no free pointers
(only explicit `mut`), every way a value can outlive its scope is
*syntactically visible*, and its destination is *lexically known at the write
site*: `outer = e` targets `outer`'s home block, `push(outer, v)` targets
`outer`'s home, `return e` targets `_parent`, an argument or `mut` targets the
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

Per-scope freeing alone isn't enough to match hand-written C or beat a GC; four
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
  owner and is dead the instant `new` is computed. The transpiler hands that buffer
  back to the arena's free-list and the next allocation reuses it — the win
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

Together these close the arena's two known weak spots (loop-carried reassignment
and sliding-window eviction), matching C and landing ahead of Go (GC) and Koka
(reference counting) on these allocation-churn workloads. (The model's honest loss
is elsewhere — pointer-shaped, structurally-shared data like tries runs ~3× C in
RAM; I cover that residue in [the thesis](thesis.md) and the
[value-semantics limits note](internals/value-semantics-limits.md).)

## Verification

The memory model is checked by four mechanisms that, between them, catch the
ways an over-aggressive move or recycle could go wrong:

- **Byte-identical self-build.** `make fixpoint` confirms the self-hosted transpiler
  is a fixed point — it transpiles its own source and reproduces its own emitted C
  byte for byte, and the resulting program matches the C transpiler's output. This
  is the soundness oracle: a move or recycle that aliased would diverge the output.
- **Sanitizers.** `make test` runs the self-emitted C under AddressSanitizer,
  UndefinedBehaviorSanitizer, and LeakSanitizer.
- **Per-type RSS benchmarks**, wired as perf guards, confirm each type's bytes are
  actually freed.
- **Differential fuzzing** compares the two transpilers' output across generated
  programs, with distinct-value regression tests so value-masking can't hide a
  corruption the differential alone would miss.

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

## Appendix: what each mechanism buys, type by type

For contributors. Because placing one type on arenas while others stay on
`malloc` is sound (see *Why arena migration is sound incrementally*), you can
reason about the self-hosted code generator one type family at a time. This
appendix records what each mechanism buys for each family. RSS figures are for
the workload that exercises the named type. Soundness throughout is checked by
the byte-identical self-build and the sanitizers above.

> **Benchmark setup.** Figures here were measured on a single machine — AMD Ryzen 7 7735HS (16 hardware threads), Linux — except where a different machine is noted. Toolchain versions and per-suite detail are in the matching `bench/*/RESULTS.md`. `tychoc` is the C-hosted compiler, `tychoc0` the self-hosted one.

**Strings.** `tychoc0`'s own code generation is one giant string build, so strings
matter most. The self-append `out = out + x` naively compiles to a fresh leaked
buffer per step (O(n²)); growing one buffer in place instead brings a
30 000-iteration build from ~4638 MB / 1937 ms to ~10 MB / 3 ms (~464×), using
only `malloc`/`realloc`. The threading spine — the irreducible step where every
function gains `Arena *_parent` and `_scope`, every call threads it, and returns
build into `_parent` — brings transient, local, and returned strings onto the
arena; stored strings move onto the arena with the compound types below. The
growth buffer behind `acc = acc + x` stays on `malloc`/`realloc` by necessity —
an arena cannot `realloc` in place — but it is freed, not leaked.

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
payload (~45×); and `mut` container home-arena threading (~582×) plus its
extension to `mut` heap-struct fields. The hardest case — heap-payload
option/result elements such as `[Option(str)]` — is handled by making options
first-class deep-copied value types (245 → 11 MB at 4M iterations, flat).

**Recursive enums and transient placement.** Enum nodes are arena value types
with a recursive deep-copy at every escape point; transient placement buys the
tree-workload win (binary-trees 2374 MB → 38 MB, tree-rewrite 825 MB → 9 MB);
per-variable block depth (a stack of block-entry indices rather than a single
marker) closes the array-pipeline gap (358 MB → 5 MB); and move-on-last-use
mirrors the C compiler's deep-copy elision.

**Liveness-driven reuse.** The arena free-list and `arena_recycle` turn the
loop-carried reassignment worst case into a win — `iter-transform` 3.5 GB → 4 MB
(`tychoc`) / 6 MB (`tychoc0`). The read-≥-twice condition described above is what
keeps a moved-out buffer from being recycled.

**Element-overwrite recycle.** Recycling the evicted element on `arr[k] = v`,
backed by a per-size-class free-list, closes the sliding-window case — `window`
47.3 MB → 4.2 MB (`tychoc`) / 3.6 MB (`tychoc0`), and faster.

**Per-statement transient reclaim.** Each expression statement's discarded value
is wrapped in a per-statement arena, so a depth-19 stretch tree from a discarded
`print(... check(make(19)) ...)` frees at statement end rather than at function
return — binary-trees 25 → 13.3 MB, below Koka's reference-counted 14.8 MB, the
last cross-language workload where a reference-counting rival led on memory.
