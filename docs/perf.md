# Performance: self-hosted hierc0 vs the C compiler

Measured after the Stage 4 self-host fixpoint (`make fixpoint`). Best-of-N wall
time; peak RSS via `getrusage(RUSAGE_CHILDREN)`. One machine, `cc -O2`. These
are indicative, not a rigorous benchmark suite.

Three compilers are in play:

- **`hierc`** — the hand-written C compiler (`src/hierc.c`): full language,
  type checking, optimized **arena + FBIP** codegen.
- **A** — hierc0 built *by* `hierc` (so A runs with arena codegen).
- **B** — hierc0 built *by* hierc0 (the self-hosted compiler; runs with hierc0's
  own **naive** codegen — `malloc`, no frees, value-copy concat).

A and B are the *same hierc0 source*, so A-vs-B isolates exactly the
codegen/memory-model difference.

## (1) Compiler speed — compiling hierc0.hi (~2250 lines) to C

| compiler | ms |
|---|---|
| `hierc` (hand-written C) | ~10 |
| **B** = hierc0, naive codegen | ~40 |
| **A** = hierc0, arena codegen | ~520 |

## (2) Generated-code runtime/memory, by workload pattern

| program | `hierc` (arena) | hierc0 (naive) | ratio |
|---|---|---|---|
| `memo` (memoized fib) | 0.48 ms | 0.45 ms | ~1× |
| `optimize` (tree-rewrite pass) | 0.44 ms | 0.41 ms | ~1× |
| `accumulate_big` (string built in a loop) | 1.1 ms / 11 MB | 257 ms / 598 MB | **239× slower, 56× more memory** |

## Interpretation

- **Straight-line compute is identical.** With no repeated growth/mutation of
  heap values, naive and arena codegen run at the same speed (`memo`,
  `optimize`).
- **The arena model wins exactly where designed.** `accumulate_big` is the
  `acc = acc + s` loop: `hierc` rewrites it to an in-place O(n) append in a
  bounded buffer; hierc0's naive `sc()` re-copies the whole growing string each
  step → O(n²) time, and every intermediate is leaked (≈625 MB allocated, never
  freed). 239× / 56× is the memory-model payoff, concretely.
- **But arena has a per-scope tax.** On the compiler's *own* workload (many
  small allocations across deeply nested scopes + recursion), the **arena
  version is 13× slower than naive** (520 ms vs 40 ms). Every block scope and
  call does `arena_child` / `arena_reset` / `arena_free`, which under the
  original allocator means a `malloc`+`free` of an arena block per scope/loop
  iteration. The naive compiler does zero frees, so it has no such churn (at the
  cost of unbounded memory). The arena advantage is **large but concentrated in
  the grow-in-place pattern**, not uniform.

This per-scope block churn is the "prong-B" arena concern noted in
docs/bootstrap.md; see the arena tuning below.

## Arena-overhead tuning (runtime/hier_rt.c)

**Diagnosis.** `HIER_BLOCK_DEFAULT` is 64 KB, and a fresh arena is created per
block scope, call, and loop iteration. The original `arena_alloc`/`arena_free`
did a `malloc`/`free` of a 64 KB block for *every* scope that allocated even a
few bytes. On the deeply-recursive self-compile that meant a flood of
`malloc(64K)`/`free` pairs (plus page faults) — the dominant cost.

**Fix 1 — global block free-list (pool).** `arena_reset`/`arena_free` now hand
their blocks to a process-global free-list instead of `free`ing; `arena_alloc`
takes from it first. Block churn becomes O(1) pointer ops with no `malloc`/
`free` and no page re-faulting. Peak live memory is unchanged (the pool holds
at most what a scope just released; reclaimed by the OS at exit).

**Fix 2 — block-retaining `arena_reset`.** A loop's scratch arena now keeps its
head block and just rewinds it (`off = 0`), releasing only overflow blocks. The
common one-block-per-iteration loop does zero pool traffic per iteration.

**Result** (self-compiling hierc0.hi, A = hierc0 under arena codegen):

| arena strategy | ms |
|---|---|
| original (malloc/free per scope) | ~520 |
| + block pool | ~231 (**2.2× faster**) |
| + retain-reset | ~231 (neutral here — the pool already made reset cheap; retain-reset pays off on loop-scratch-heavy code) |

Generated-code benchmarks are unchanged or slightly better (`accumulate_big`
1.07 → 0.59 ms; `memo`/`optimize` ~equal; peak RSS steady at ~11 MB). `make
test` / `bootstrap` / `fixpoint` stay green.

**Residual gap.** Arena codegen is still ~6× slower than naive-leak on the
compiler workload (231 ms vs ~39 ms). That remainder is the *inherent* cost of
bounded memory: per-call arena management plus the value-semantics return/bind
deep-copies the arena model performs and the leak-everything model skips.
Closing it further is deeper codegen work — e.g. not creating a child arena for
scopes that allocate nothing or whose values don't escape, stack-allocating
small transients, and more compact node representations (the rest of prong-B).
The pool/retain-reset changes are pure runtime and risk-free (behaviour
identical, verified by the full suite); the codegen-level work is the next, more
involved step.
