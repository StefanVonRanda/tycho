# Performance: self-hosted hierc0 vs the C compiler

Measured after the Stage 4 self-host fixpoint (`make fixpoint`). Best-of-N wall
time; peak RSS via `getrusage(RUSAGE_CHILDREN)`. One machine, `cc -O2`. These
are indicative, not a rigorous benchmark suite.

> **STATUS (2026-06-02): sections (1)–(2) below are now a HISTORICAL baseline.**
> They were measured when hierc0's codegen was still naive (`malloc`, no frees,
> value-copy concat) — i.e. **B = naive**. Since then the memory-model migration
> ([docs/memory-model.md](memory-model.md), MM-0 … MM-6c) has moved hierc0's
> emitted C onto the same implicit-arena model the C compiler uses (MM-0 … MM-7e),
> so **B is now arena-coded, not naive** — the A-vs-B "arena vs naive" gap these
> sections document is **closed**. Concretely, the headline `accumulate_big` row
> below (B naive: 257 ms / 598 MB) is now **B arena: ~0 ms / ~1.6 MB**, identical
> to A — the O(n²)/leak is gone. The sections are kept because they quantify *what
> the arena model bought* (the motivation for the whole MM campaign); read them as
> "naive vs arena," not as the current B. The prong-B section further down (tuning
> of the C compiler `src/hierc.c`) is current.

> **CURRENT self-hosted compiler speed (2026-06).** After the arena migration,
> hierc0 reproduces the full arena model + move-on-last-use — codegen-feature
> parity with `hierc`. Two later codegen-QUALITY fixes then improved the
> self-hosted compiler's own headline workload (**B = hierc0 compiled by hierc0**,
> compiling `hierc0.hi`): a **block free-list pool** in the emitted arena runtime
> cut time **106 → 64 ms (1.66×)** (no malloc/free churn per scope), and a
> **compact tagged-union enum layout** cut peak RSS **18.2 → 9.9 MB (1.84×)** (the
> flat `Expr`/`Stmt` nodes — 25/33 fields — shrink to their active variant). Both
> are output-invisible (fixpoint B≡C + 58 tests + the fuzzer). A third fix closed
> the last generated-code gap: profiling binary-trees showed hierc0 doing 3× the
> allocations (102.7M vs hierc's 34.0M) from two leaf-path bugs — a redundant
> deep-copy on `return Leaf` and no shared singleton for nullary variants. Fixing
> both dropped binary-trees **38 → 13 MB, 289 → 124 ms** (now hierc's exact alloc
> count). hierc0 now **beats hierc on both memory and time on 3 of 4 prong-B
> workloads** ([../bench/prongB/RESULTS.md](../bench/prongB/RESULTS.md)) and is
> best-in-class on binary-trees; the one place it trails is string-pipeline memory,
> which pays for Hier having no `char` type (each `str(d)` allocates a one-char
> string). The C compiler stays the reference until hierc0 outperforms it.

Three compilers are in play:

- **`hierc`** — the hand-written C compiler (`src/hierc.c`): full language,
  type checking, optimized **arena + FBIP** codegen.
- **A** — hierc0 built *by* `hierc` (so A runs with arena codegen).
- **B** — hierc0 built *by* hierc0 (the self-hosted compiler; runs with hierc0's
  own **naive** codegen — `malloc`, no frees, value-copy concat).

A and B are the *same hierc0 source*, so A-vs-B isolates exactly the
codegen/memory-model difference.

## (1) Compiler speed — compiling hierc0.hi (~2970 lines) to C

| compiler | ms |
|---|---|
| `hierc` (hand-written C) | ~10 |
| **B** = hierc0, naive codegen | ~40 |
| **A** = hierc0, arena codegen | ~49 (was ~520 before the arena tuning + prong-B codegen work below) |

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

**Residual gap (after the pool).** Arena codegen was still ~6× slower than
naive-leak on the compiler workload (231 ms vs ~39 ms). That remainder turned
out to be almost entirely the value-semantics deep-copies the arena model
performs and the leak-everything model skips — *not* arena bookkeeping (the pool
already made `arena_child`/`free` O(1)). See the codegen work below.

## Codegen-level arena work (prong-B, src/hierc.c)

Two codegen changes, in order. Each was verified by `make test` (57 programs,
byte-identical output vs the reference compiler, under
`-fsanitize=address,undefined`), `make bootstrap`, `make fixpoint` (B≡C), and
`make bench`.

**Step 1 — elide child arenas for if/match blocks.** if/else/match-arm blocks
created a child arena (`_b%d = arena_child(scope)`) freed at block end. But the
enclosing `scope` always outlives the block, so block transients can fall back
to it with no early-free, and escaping values already promote to `_parent`
independent of any `_bN`. Removing them dropped `arena_child` in A's emission of
hierc0.hi from **722 → 219** (the 503 `_b` arenas). *Wall-clock and RSS were
unchanged* — the pool had already made those ops nearly free. The value: smaller
emitted C / fewer runtime ops per program, and it isolated the real cost.

**Step 2 — borrow read-only heap struct params instead of deep-copying.** This
was the real lever. Heap-bearing by-value struct params were unconditionally
deep-copied into the callee `_scope` on entry (for independence under
mutation). Gating that copy on `block_mutates(body, param)` — copy only if the
body mutates the param, else borrow the caller's value (caller outlives the
call; unmutated aliasing is unobservable; `return param` still deep-copies via
the return path) — removed the dominant cost: the `Ctx` symbol table cloned on
every call.

| metric (A self-compiling hierc0.hi) | before step 2 | after |
|---|---|---|
| `hier_copy_S_Ctx` calls | 72,686 | **0** |
| `hier_str_copy` calls | 27.8 M | 186 k (149× fewer) |
| `arena_alloc` calls | 31 M | 387 k (80× fewer) |
| wall-clock | 232 ms | **49 ms (4.7×)** |
| peak RSS | 11.8 MB | 11.5 MB |

**Net result.** The arena model's overhead vs naive-leak on the compiler
workload drops from **~6× to ~1.26×** (49 ms vs ~39 ms) — while holding peak
memory at **11.5 MB** against the naive version's unbounded growth. Combined
with the grow-in-place win (`accumulate_big` 239× / 56×), the value-semantic
implicit-arena model now matches a leak-everything allocator on a real,
allocation-heavy, deeply-recursive workload to within ~26%, with bounded memory.

The borrow-iff-not-mutated rule (step 2) is the same predicate already proven on
match-arm payloads through Stages 2–4 and the self-host fixpoint.

**Prong-B is complete — stop here.** A merged gprof profile (30 runs) of A
self-compiling shows the entire arena memory model is now **~6%** of run time:
`arena_child` ~2%, `arena_alloc` ~2%, `hier_str_copy` ~2%. The remaining cost is
*algorithmic, in hierc0's own source and identical in A and B* — not the memory
model: `is_variant` (a doubly-nested linear scan over all enum variants, called
per identifier in codegen) is **33%**, and the bounded-array index gets it drives
are another **29%**. The two further prong-B ideas once noted here —
stack-allocating small transients and compact node representations — target
`arena_alloc` and the deep-copies, i.e. ~2% each; the profile shows eliminating
them entirely would save ~1–2 ms of 49 ms, so they were dropped as high-risk,
near-zero-reward. Any future self-compile speedup belongs in hierc0's algorithms
(e.g. a variant→enum lookup built once), which is orthogonal to the arena thesis.
