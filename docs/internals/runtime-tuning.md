# Runtime tuning: keeping the per-scope arena tax down

Maintainer notes behind [docs/performance.md](../performance.md): the
mechanisms that keep the arena model cheap, and the profiling lessons that
decided them.

## The per-scope block churn problem

`TYCHO_BLOCK_DEFAULT` is 64 KB, and a fresh arena is created per block scope,
call, and loop iteration. A naive `arena_alloc`/`arena_free` would
`malloc`/`free` a 64 KB block for *every* scope that allocates even a few
bytes, and on deeply-recursive code that dominates. Two runtime mechanisms keep
that churn off the hot path:

- **Block pool.** `arena_reset`/`arena_free` hand their blocks to a
  thread-local free-list (`runtime/tycho_rt.c@g_block_pool`) instead of
  `free`ing; `arena_alloc` takes from it first. Block churn is O(1) pointer ops
  with no `malloc`/`free` and no page re-faulting. Peak live memory is
  unchanged.
- **Block-retaining `arena_reset`.** A loop's scratch arena keeps its head
  block and rewinds it (`off = 0`), releasing only overflow blocks. The common
  one-block-per-iteration loop does zero pool traffic per iteration.

The pool is the load-bearing one; it is why `arena_child`/`arena_free` do not
appear as a cost in any current profile. What remains is the value-semantics
deep copies, which the codegen below addresses.

## Codegen-level arena handling

**Child arenas are elided for if/match blocks.** The enclosing `scope` always
outlives the block, so block transients fall back to it, and escaping values
promote to `_parent` independent of any `_bN`. Wall-clock and RSS barely move
(the pool already makes `arena_child` nearly free), but the emitted C is
smaller.

**Read-only heap struct params are borrowed, not deep-copied.** This is the real
lever. A heap-bearing by-value struct param is deep-copied into the callee
`_scope` on entry only if the body mutates it (`block_mutates`); otherwise it
borrows the caller's value (the caller outlives the call, unmutated aliasing is
unobservable, and `return param` still deep-copies via the return path). On a
compiler-shaped workload this removed a symbol-table clone per call and cut
string-copy and allocation counts by two orders of magnitude, with peak memory
flat. The same borrow-iff-not-mutated predicate applies to match-arm payloads.

## Profiling lessons

**Do not trust gprof self-times for functions with huge call counts; trust `-O2`
wall-clock deltas.** gprof once blamed `is_variant` (the enum-variant scan) as a
major bottleneck. Replacing it with an O(1) map removed it from the profile and
changed `-O2` wall-clock by nothing: the `-pg` build's `mcount` overhead is
charged to tiny, enormously-called functions. gprof *call counts* are not
distorted, and they show the real `-O2` cost is memory traffic — many small
`arena_alloc`s and `tycho_str_copy`s.

Where `perf` and valgrind are unavailable, use the statistical sampler in
`tools/prof/` (`ITIMER_PROF`+`SIGPROF`, saved-frame-pointer caller attribution,
no `mcount` artifact). It is what found two O(n²) bugs gprof hid: a lexer
recomputing `len(src)` per token, and per-access `strlen` in string indexing
(strings now carry their length, `runtime/tycho_rt.c@tycho_str_len`).

## Push-loop fusion — register-resident array building

A loop that only pushes to a local array would otherwise pay, **per element**,
for the descriptor (`data`/`len`/`cap`) round-tripping through memory: the C
compiler must assume `&arr` aliases the arena pointer passed to `push`, so it
cannot keep the cursor in registers. `reserve`, `restrict`, and a cheaper
empty-arena `reset` all failed to move it; the cost is the descriptor traffic.

**Fusion** (`compiler/emit/emit_scope.ty@_fuse_open`): codegen caches
`data`/`len`/`cap` in C locals across the loop (hot path `_fd[_fl++] = v`),
calls the element type's `_grow` hook only on overflow, and writes the
descriptor back at loop exit. `break` falls through to the flush; `continue`
keeps the cursor in registers; `return` flushes first; nested loops pushing the
same array reuse the outer cursor.

**Sound by construction** — fuse ONLY when the array is read solely as a push
target (`_creads_b == _pushn_b`), is a plain non-`inout`, non-bounded local not
defined or shadowed in the body, and (for a `while`) the condition does not
read it. Any miss falls back to standard codegen.

Every element type fuses: the grow hook regrows the *spine* and recycles the old
one, and the fused store deep-copies the element into the array's owning arena
exactly as the non-fused push does. The win is a large multiple for scalars;
for heap elements the per-element deep copy dominates, so it saves a call and a
descriptor write-back per push. Covered by `tests/push_fusion.ty`,
`tests/str_fuse.ty` and `tests/comp_fuse.ty`.
