# Performance: self-hosted hierc0 vs the C compiler

Measured after the Stage 4 self-host fixpoint (`make fixpoint`). Best-of-N wall
time; peak RSS via `getrusage(RUSAGE_CHILDREN)`. One machine, `cc -O2`. These
are indicative, not a rigorous benchmark suite.

> Cross-LANGUAGE numbers live in `bench/`: `bench/prongB/` (vs C/Go/Rust/Koka),
> `bench/dbquery/` (real SQLite via FFI), `bench/conc/` (concurrency vs
> C/Go/Rust). This file tracks the self-hosted compiler only.

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

> **CURRENT self-hosted compiler speed (2026-06): ~20 ms** (B = hierc0 compiling
> its own ~3.5k-line source; ~3.1× faster than the campaign start). The numbers in
> this block (down to ~64 ms) are intermediate; the RESOLUTION/CORRECTION blocks
> further down trace the lexer-O(n²), string-index-O(n²), `Ctx`/`Decls`-split, and
> `compute_movables`/`sig_ret` map fixes that reached ~20 ms. After the arena
> migration, hierc0 reproduces the full arena model + move-on-last-use —
> codegen-feature parity with `hierc`. Two later codegen-QUALITY fixes then improved the
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
> best-in-class on binary-trees. The former string-pipeline gap is now closed by
> the additive `char` type: `'x'` literals, `char ± int → char`, and `string +
> char` compiling to a one-byte in-place append (`hi_append_char`, no per-digit
> string allocation) — the same byte-write C/Rust/Go do. With `s = s + ('0' + d)`,
> string-pipeline drops **34 → 1 ms (~21× on hierc, ~10× on hierc0)** at unchanged
> memory, tying C at 1 MB / 1 ms. The C compiler stays the reference until hierc0
> outperforms it.

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
model.

> **CORRECTION (2026-06, measured).** An earlier version of this paragraph
> claimed `is_variant` (the enum-variant scan) was **33%** of self-compile and a
> "bounded-array index" path another **29%**, citing a gprof profile, and
> suggested "a variant→enum lookup built once" as the win. **Both the claim and
> the profile it rests on are wrong — `is_variant` is *not* a real bottleneck.**
> Proven two ways: (1) replacing all four variant lookups with an O(1)
> `{string:int}` map threaded through `Ctx` — which a fresh `gprof` confirms
> *removes* the 25% `is_variant` line entirely — changed `-O2` wall-clock by
> **0%** (126.3 → 126.9 ms, best of 15); (2) the reason is a **gprof `mcount`
> artifact**: `is_variant` is a tiny branch-predicted loop called **1.3M times**,
> and gprof's per-call instrumentation overhead (only present in the `-pg` build,
> not under `-O2`) is charged to it, manufacturing a fake 25–33%. The same
> artifact inflates every other call-heavy tiny function in that profile
> (`count_str_occ` 397k calls, the `hier_arr_*_get` bounds-checks). **Lesson: do
> not trust gprof self-times for functions with huge call counts; trust `-O2`
> wall-clock deltas.** The variant-map change was reverted (no real win, and it
> adds the first map to the self-host source).
>
> The genuine `-O2` cost is **memory traffic** — accurate gprof *call counts*
> (which are not distorted): ~1.7M `arena_alloc`, ~1.3M `hier_str_copy`, ~2.1M
> bounded-array gets per self-compile — i.e. the volume of small string
> allocations the string-building codegen does, plus value-semantic copies.
>
> **RESOLUTION (2026-06, sampling profiler built).** `perf` stays blocked
> (`perf_event_paranoid=3`, and a "no new privileges" flag stops `sudo` even with
> a password), and valgrind isn't installable — so a dependency-free statistical
> CPU-time sampler was written instead (`tools/prof/`, `ITIMER_PROF`+`SIGPROF`,
> no `mcount` artifact). It found the real hotspot in one shot, which the gprof
> profile had completely hidden: **`scan_token` recomputed `len(src)` — a full
> `strlen` of the whole source — once per token**, so lexing was O(tokens × len)
> = **O(n²)**. The fix is purely algorithmic and touches no bounds-checking:
> thread the already-known length (`lex` computes `n := len(src)` once) into
> `scan_token` instead of recomputing it. **Self-hosted self-compile (B = hierc0
> built by hierc0) dropped 62 → 33 ms (~1.9×)**; fixpoint B≡C + 60 tests green.
> (Aside: this also revealed B was always ~2× faster than the "A = hierc0 built
> by hierc" binary this doc had been timing — hierc0's codegen emits a direct
> O(1) `s[i]` where hierc emitted a `strlen`-bounds-checked `hier_str_get` that
> was O(n) *per access* → O(n²) in a loop, for any *hierc*-compiled program
> indexing a large string.)
>
> **That hierc-side O(n²) is now also fixed (length-carrying check).** hierc's
> codegen gains a per-proc pass: a string variable that is indexed (`s[i]`) and
> never reassigned (`block_mutates`==0, so for a string its length is invariant)
> gets one hoisted `_slen_h_<v> = strlen(v)` sidecar at its scope entry, and its
> index sites use a new `hier_str_get_n(s, i, len)` — the **same bounds check,
> now O(1)** instead of re-`strlen`-ing per access. Full safety kept (verified:
> OOB and negative indices still `exit(1)` with the bounds error). Result: the
> hierc-built A self-compile **126 → 75 ms (~1.7×)**; `make test` 58 byte-
> identical + ASan, fixpoint B≡C, `tests/str_index.hi` guards it with hand-
> verifiable output. After both fixes the self-compile cost looked diffuse
> `memcpy`/`malloc` from value-semantic copies — but that was a profiler blind
> spot, not the truth.
>
> **The "diffuse floor" was actually one thing: `Ctx` reconstruction (now fixed).**
> Improving the sampler's caller attribution (saved-RBP-chain walk, so libc leaves
> like `malloc` are blamed on the hier function that called them) dropped the
> unattributed "?" from ~75% to ~13% and revealed the real floor: **`with_owner`
> (25.7%) + `enter_block` (11.2%) = ~37%** was `Ctx` *reconstruction*. `with_owner`
> is called just to change the `owner` string, but the returned `Ctx` escapes, so
> value semantics deep-copied **every** field — including the large, parse-
> invariant `sigs`/`structs`/`enums` — on every owner/depth change. hierc never
> had this because it threads `arena` as a plain parameter. **Fix:** split the
> immutable parse data into a `Decls` struct built once and threaded read-only
> (`dc`), never reconstructed; `Ctx` keeps only the 7 mutable per-scope/per-fn
> fields, so `with_owner`/`enter_block` rebuild a tiny struct. Threaded `dc`
> through all 67 `ctx`-taking functions (this pushed `gen_match_optres` to 9
> params, so hierc's fixed `Sig` param cap was raised 8→16; hierc0 uses dynamic
> arrays). Result: **B self-hosted self-compile 33.5 → 22.7 ms (~1.48×)**, with
> `with_owner`/`enter_block` gone from the profile; fixpoint B≡C + 58 tests + fuzz
> green. The new top is genuine codegen logic (`type_of` ~13%, `gen_expr` ~11%,
> `compute_movables` ~7%, `sig_ret` ~5%) — a smaller, more-diffuse next layer.
>
> **Two more from that layer, both output-invariant (2026-06).** The Decls split
> also made it safe to add O(1) lookup *maps* to the immutable `Decls` (built once,
> never reconstructed — the per-clone copy cost that doomed an earlier such attempt
> is gone). (1) `compute_movables` (the move-on-last-use pre-pass) was O(reads²) —
> it called `count_str_occ(reads, n)` for every read; a one-pass frequency map +
> loopreads set makes it O(reads). (2) `sig_ret`'s per-call linear `dc.sigs` scan
> became an O(1) `dc.sigmap` lookup. Together: **B 22.7 → 19.8 ms (~13%)**; fixpoint
> B≡C + 58 tests + fuzz green. What remains (`gen_expr`/`type_of` + a large
> unattributed `memcpy`/`malloc` chunk) is the inherent string-building codegen:
> hierc0 returns `sc()`-concatenated owned strings, so output bytes are copied once
> per nesting level — the value-semantic string-copy floor. No logic-level change
> moves it.

### Push-loop fusion — register-resident array building (2026-06, both compilers)

The generated-code counterpart to the wins above. A loop that only pushes to a
local scalar array paid, **per element**, for the array descriptor (`data`/`len`/
`cap`) round-tripping through memory: the C compiler must assume `&arr` aliases
the arena pointer also passed to `push`, so it can't keep the cursor in
registers. Profiling `iter_transform` (a 200M-element push loop) isolated it —
arithmetic + bounds-elided reads were 334 ms, `push` ~915 ms (73%); hand-hoisting
the loop hit 337 ms. `reserve`, `restrict`, and a cheaper empty-arena `reset` all
failed to move it — the cost is the descriptor traffic, not growth or the
capacity branch.

**Fusion:** when a loop's body uses a local scalar array (`[int]`/`[float]`)
ONLY as `push(arr, …)`, codegen caches `data`/`len`/`cap` in C locals across the
loop (hot path `_fd[_fl++] = v`), calls a grow hook (`hier_arr_int/float_grow`)
only on overflow, and writes the descriptor back at loop exit. `break` needs
nothing (the flush sits after the loop, which `break` falls through to);
`continue`'s cursor survives in registers; `return` flushes via the registry
first; nested loops pushing the same array reuse the outer cursor.

**Sound by construction** — fuse ONLY when `count_reads == pushcount` (used solely
as a push target), the array is a plain non-inout scalar local not defined/
shadowed in the body, and (for a `while`) the condition doesn't read it. Any miss
falls back to today's codegen, so a non-fused loop is never wrong. In hierc the
registry is C globals (`g_fuse`); hierc0 has no globals, so it threads through
`Ctx` (fields `fusearr`/`fusesuf`/`fusety`, top-down into the body) with cursor
names keyed on `ctx.depth`.

**Result:** `iter_transform` 1249→416 ms (6.7×→2.3× C) and 4→3 MB; `arr_pipeline`
53→30 ms (2.2×→1.25× C). General — every scalar push loop, including the
self-compiler. Verified: `make test` all green (ASan/UBSan); `make fixpoint` B≡C in
both compilers (fusion correctly compiles the push-heavy `hierc0.hi`, which
self-reproduces byte-identically and compiles itself faster); `bench-prongB` all
outputs identical, no regression; differential fuzz 300 seeds FAIL=0 (×2, one per
compiler); `tests/push_fusion.hi` covers break/continue/return/nested/two-array/
while/bail.

**Heap-element extension — EVERY element type now fuses** (both compilers): not
just scalars, but `[string]`, structs, tuples, nested arrays, options/results,
enums — any array whose element family has a `_grow` hook (i.e. all of them). The
grow hook is element-generic: regrow the *spine* (the element buffer), recycle the
old spine; the heap each element points to was already deep-copied into the array's
arena, so the shallow per-element spine copy (`memcpy` / `nd[i]=(*data)[i]`) keeps
it valid. The "copy/recycle interplay" that kept this out of scope was a non-issue:
a push only *appends*, so no element is ever overwritten (the MM-9 element-recycle
path is overwrite-only and untouched), and the fused store deep-copies the element
into the array's owning arena exactly as the non-fused push does, preserving value
semantics. It unifies cleanly: hierc's fused store is one
`copy_into(arr_elem, ow, gen_expr(v))` (identity for scalars/pure structs → int/float
emission stays byte-identical; the right deep-copy for str/struct/tuple/ARRC/…) and
one `hier_arr_<arr_fn>_grow` (runtime `int`/`float`/`str`; generated `C<id>`); hierc0
generates `Arr_<mangle(et)>_grow` for every element type and deep-copies via
`gen_rhs(.., with_owner ow)` when `elem_deepcopy(et)`. Cursor type is
`c_type(arr_elem(t))` (uniform). Eligibility is now simply `is_array` (soa + inout
excluded). Win is largest for scalars; for heap elements the per-element deep-copy
(`scopy`/`hier_copy_S_*`/…) dominates, so the descriptor-elision is a smaller
fraction — a completeness/consistency close more than a hot-path multiplier, though
it does cut a call + descriptor write-back per push on the very common build-a-list
loop. Verified: `make fixpoint` B≡C byte-identical (composite fusion fires on
`hierc0.hi`'s own struct/tuple array push loops, +400 lines C vs scalar-only, still
self-reproduces); `make test` 137 green incl `tests/str_fuse.hi` +
`tests/comp_fuse.hi` (pure struct / heap struct / nested array / value-semantics)
under ASan/UBSan; corelib, conc, ffi, bench-guard green; differential fuzz 500
FAIL=0. Nothing left deferred here — every array element type fuses.

## The self-compile gap: status and decision (closed for now)

**Current gap: hierc0 self-compile ≈ 2.4× the C compiler** (one box: B 31 ms vs
hierc 13 ms compiling `hierc0.hi`; absolute numbers vary widely by machine — compare
the *ratio*). The cheap-wins lane is **exhausted**: the algorithmic O(n²)s are gone
(`scan_token` strlen, `compute_movables`), the linear scans are O(1) maps
(`sig_ret`/`dc.sigmap`), and the per-scope `Ctx` deep-copy was removed (the `Decls`
split). Two further logic micro-opts were tried this round and **reverted as
wall-clock noise** — `sig_ret` user-map-first (it was already O(1)) and a `type_of`
comparison/`&&`/`||` short-circuit (skip recursing operands when the result is always
`int`). They're correct but 0%, because the profiler's "by caller" hotspots
(`gen_expr`/`type_of`/`sig_ret`) are those functions *on the stack while their
returned strings are copied*, not their own compute.

**The gap is architectural, not a bug.** ~85% of hierc0's self-compile time is string
allocation/copying, dominated by `scopy` return-value copies (~5:1 over `sc` concats).
This is the **value-semantic string-copy floor**: hierc0's codegen builds output by
returning and concatenating owned strings (each copied once per nesting level, plus a
deep-copy on every string-returning `return`), whereas the C compiler writes output
through `fprintf` with raw pointers. No logic-level change moves it; outperforming the
C compiler with a value-semantic self-hosted one is likely not reachable incrementally.

**Decision (2026): the C compiler `src/hierc.c` stays the DEFAULT / production
compiler.** hierc0 remains the self-hosting proof (`make fixpoint` B≡C, the definitive
dogfood of the value-semantic + arena model on a real 9.3k-line-of-C compiler) and the
differential oracle's counterpart — it is NOT retired and is NOT on the
correctness-critical path for production. Per the standing project constraint, retiring
the C compiler requires hierc0 to *outperform* it, not merely match; that gap is
documented as a fundamental property of the value-semantic model. Perf work on the
self-compile path is closed.
