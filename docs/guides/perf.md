# Performance: the self-hosted transpiler vs the C one

> **[!CAUTION]** This document measures the **transpiler's own** compile-time performance
> (`tychoc0` vs `tychoc`), tracking optimization history and codegen quality. It is
> **not** about the language's performance or about the arena model's claims — those are
> in [the thesis](../thesis.md) and the cross-language benchmark suite
> (`bench/prongB/`, `bench/conc/`). The thesis numbers are what matters for evaluating
> the model; this page is a contributors' log.

Tycho has two transpilers for the same language: `tychoc`, which I wrote by hand
in C, and `tychoc0`, written in Tycho and able to transpile itself. This document
is about how the self-hosted one performs — how fast `tychoc0` transpiles
its own source, and how the value-semantic, implicit-arena memory model
behaves on a real, allocation-heavy, deeply-recursive workload.

> **Benchmark setup.** Figures here were measured on a single machine — AMD Ryzen 7 7735HS (16 hardware threads), Linux — except where another machine is noted. Toolchain versions and per-suite detail are in the matching `bench/*/RESULTS.md`. `tychoc` is the C-hosted transpiler, `tychoc0` the self-hosted one; each figure names which.

I check every change here against the byte-identical self-build
(`make fixpoint`) and the sanitizer and fuzzer suite (`make test` under
`-fsanitize=address,undefined`, `make bootstrap`, and the differential
fuzzer).

## The three transpilers in play

- **`tychoc`** — the C transpiler I wrote by hand (`src/tychoc.c`): full language,
  type checking, and tuned arena + FBIP codegen. This is the default,
  production transpiler.
- **A** — `tychoc0` built *by* `tychoc`. Because `tychoc` emits arena codegen, A
  runs with the arena memory model.
- **B** — `tychoc0` built *by* `tychoc0` — the self-hosted transpiler.

A and B are the *same tychoc0 source*, so comparing A against B isolates exactly
the codegen / memory-model difference between `tychoc`'s output and `tychoc0`'s
own output.

## Summary

The self-hosted `tychoc0` transpiles its own source in **~31 ms — about 2.4× the C
transpiler's 13 ms** (on one machine; absolute numbers vary widely by
machine, so the ratio is the claim) — and emits the same implicit-arena C the
C transpiler does. With that codegen, `tychoc0` beats `tychoc` on both
memory and time on 3 of 4 workloads in the cross-language benchmark suite
(`bench/prongB/`, [RESULTS.md](../../bench/prongB/RESULTS.md)) and leads the suite
on binary-trees here.

The earliest `tychoc0` used a naive codegen — `malloc`, no frees,
value-copy concatenation — and I measured the first two sections below
against it. I keep them as a *historical baseline*: they show what the
arena model bought, not how the current transpiler behaves. Today `tychoc0`'s
emitted C uses the same implicit-arena model as the C transpiler (see
[docs/memory-model.md](memory-model.md)), so the "arena vs naive" gap those
sections document is closed. Concretely, the `accumulate_big` row below (naive:
257 ms / 598 MB) is now **<1 ms / ~1.6 MB** — flat and bounded; the O(n²) blowup
and the leak are gone. Read sections (1)–(2) as "naive vs
arena," and the later sections as the current transpiler.

## What the self-compile number does (and does not) measure

The ~31 ms figure is `tychoc0`'s *transpile* step alone: reading `tychoc0.ty`
and emitting C. That step is genuinely fast, but it is **not** the time to
build the transpiler. A full `make bootstrap` / `make fixpoint` takes about a
minute of wall clock, and almost all of that belongs to the *host* C compiler,
not to Tycho. A representative breakdown on the primary machine (`cc -O2`;
`tychoc0.ty` ≈ 11.9k lines → emitted C):

| step | wall |
|------|------|
| tychoc0 transpiles `tychoc0.ty` → C | ~0.03 s |
| `cc -O2` compiles that emitted C (once per self-host stage; ×3 in fixpoint) | ~10.7 s each |
| `make bootstrap` end-to-end | ~58 s |

So "tycho compiles itself in milliseconds" is true of the **tycho→C pass**; the
`cc` back-end owns the bootstrap wall clock (hundreds to one). It is not
"instant" end to end. Absolute transpile ms are machine- and source-size-specific
(`tychoc0.ty` has grown since I first measured these, and the profiler-box
trace in [Where the remaining time goes](#where-the-remaining-time-goes) lands at
~20 ms on a different machine); the **~2.4× ratio vs the C transpiler is the stable
claim**, reproducing across both. Re-measure locally for a current absolute number.

## (1) Transpiler speed — turning an earlier tychoc0.ty into C

| compiler | ms |
|---|---|
| `tychoc` (hand-written C) | ~10 |
| **B** = tychoc0, naive codegen | ~40 |
| **A** = tychoc0, arena codegen | ~49 (was ~520 before the arena tuning + codegen work below) |

## (2) Generated-code runtime/memory, by workload pattern

| program | `tychoc` (arena) | tychoc0 (naive) | ratio |
|---|---|---|---|
| `memo` (memoized fib) | 0.48 ms | 0.45 ms | ~1× |
| `optimize` (tree-rewrite pass) | 0.44 ms | 0.41 ms | ~1× |
| `accumulate_big` (string built in a loop) | 1.1 ms / 11 MB | 257 ms / 598 MB | **239× slower, 56× more memory** |

## Interpretation

- **Straight-line compute is identical.** With no repeated growth or mutation
  of heap values, naive and arena codegen run at the same speed (`memo`,
  `optimize`).
- **The arena model wins exactly where I designed it to.** `accumulate_big` is the
  `acc = acc + s` loop: `tychoc` rewrites it to an in-place O(n) append in a
  bounded buffer; the naive `sc()` re-copies the whole growing string each step
  → O(n²) time, and every intermediate is leaked (≈625 MB allocated, never
  freed). The 239× / 56× gap is the memory-model payoff, made concrete.
- **Arena has a per-scope tax.** On the transpiler's *own* workload (many small
  allocations across deeply nested scopes plus recursion), the naive baseline
  measured the arena version at 13× slower (520 ms vs 40 ms). Every block scope
  and call does `arena_child` / `arena_reset` / `arena_free`, which under the
  original allocator meant a `malloc`+`free` of an arena block per scope or loop
  iteration. The naive transpiler does zero frees, so it has no such churn — at
  the cost of unbounded memory. So the arena advantage is large but concentrated
  in the grow-in-place pattern, not uniform.

That per-scope block churn is the arena concern I address with the tuning below.

## How I keep the per-scope arena tax down (runtime/tycho_rt.c)

`TYCHO_BLOCK_DEFAULT` is 64 KB, and a fresh arena is created per block scope,
call, and loop iteration. A naive `arena_alloc`/`arena_free` does a
`malloc`/`free` of a 64 KB block for *every* scope that allocates even a few
bytes; on the deeply-recursive self-compile that is a flood of
`malloc(64K)`/`free` pairs (plus page faults), the dominant cost. Two runtime
mechanisms keep that churn off the hot path:

- **Global block free-list (pool).** `arena_reset`/`arena_free` hand their
  blocks to a process-global free-list instead of `free`ing; `arena_alloc`
  takes from it first. Block churn is O(1) pointer ops with no `malloc`/`free`
  and no page re-faulting. Peak live memory is unchanged (the pool holds at
  most what a scope just released; reclaimed by the OS at exit).
- **Block-retaining `arena_reset`.** A loop's scratch arena keeps its head
  block and just rewinds it (`off = 0`), releasing only overflow blocks. The
  common one-block-per-iteration loop does zero pool traffic per iteration.

Here's the effect on the self-compile (A = tychoc0 under arena codegen):

| arena strategy | ms |
|---|---|
| naive (malloc/free per scope) | ~520 |
| + block pool | ~231 (**2.2× faster**) |
| + retain-reset | ~231 (neutral here — the pool already makes reset cheap; retain-reset pays off on loop-scratch-heavy code) |

Generated-code benchmarks are unchanged or slightly better (`accumulate_big`
1.07 → 0.59 ms; `memo`/`optimize` ~equal; peak RSS steady at ~11 MB).

**Residual gap (after the pool).** Arena codegen was still ~6× slower than the
naive-leak baseline on the transpiler workload (231 ms vs ~39 ms). That remainder
is almost entirely the value-semantics deep-copies the arena
model performs and the leak-everything model skips — *not* arena bookkeeping
(the pool already made `arena_child`/`free` O(1)). See the codegen work below.

## Codegen-level arena handling (src/tychoc.c)

Two codegen properties keep the arena model's overhead low.

**Child arenas are elided for if/match blocks.** if/else/match-arm blocks would
otherwise create a child arena (`_b%d = arena_child(scope)`) freed at block end.
The enclosing `scope` always outlives the block, so block transients fall back
to it with no early-free, and escaping values promote to `_parent` independent
of any `_bN`. Eliding them drops `arena_child` in A's emission of tychoc0.ty from
**722 → 219** (the 503 `_b` arenas). Wall-clock and RSS are unchanged — the pool
already makes those ops nearly free — but the emitted C is smaller, with fewer
runtime ops per program, which isolates the real cost.

**Read-only heap struct params are borrowed, not deep-copied.** This is the real
lever. A heap-bearing by-value struct param would otherwise be unconditionally
deep-copied into the callee `_scope` on entry (for independence under mutation).
Gating that copy on `block_mutates(body, param)` — copy only if the body mutates
the param, else borrow the caller's value (the caller outlives the call;
unmutated aliasing is unobservable; `return param` still deep-copies via the
return path) — removes the dominant cost: the `Ctx` symbol table cloned on every
call.

| metric (A self-compiling tychoc0.ty) | before step 2 | after |
|---|---|---|
| `tycho_copy_S_Ctx` calls | 72,686 | **0** |
| `tycho_str_copy` calls | 27.8 M | 186 k (149× fewer) |
| `arena_alloc` calls | 31 M | 387 k (80× fewer) |
| wall-clock | 232 ms | **49 ms (4.7×)** |
| peak RSS | 11.8 MB | 11.5 MB |

**Net result.** The arena model's overhead vs naive-leak on the transpiler
workload drops from **~6× to ~1.26×** (49 ms vs ~39 ms) — while holding peak
memory at **11.5 MB** against the naive version's unbounded growth. Combined
with the grow-in-place win (`accumulate_big` 239× / 56×), the value-semantic
implicit-arena model matches a leak-everything allocator on a real,
allocation-heavy, deeply-recursive workload to within ~26%, with bounded
memory.

The borrow-iff-not-mutated rule is the same predicate proven on match-arm
payloads, checked through the byte-identical self-build.

## Where the remaining time goes

A merged gprof profile (30 runs) of A self-compiling shows the entire arena
memory model is now **~6%** of run time: `arena_child` ~2%, `arena_alloc` ~2%,
`tycho_str_copy` ~2%. The remaining cost is *algorithmic, in tychoc0's own
source, and identical in A and B* — not the memory model.

One caution about that profile, because it is easy to read wrong. gprof self-times
once suggested `is_variant` (the enum-variant scan) was 33% of self-compile and
a bounded-array index path another 29%. **Both are wrong — `is_variant` is not a
real bottleneck.** Two independent checks prove it: (1) replacing all four
variant lookups with an O(1) `{string:int}` map — which a fresh gprof confirms
*removes* the 25% `is_variant` line entirely — changed `-O2` wall-clock by
**0%** (126.3 → 126.9 ms, best of 15); (2) the cause is a gprof `mcount`
artifact: `is_variant` is a tiny branch-predicted loop called **1.3M times**,
and gprof's per-call instrumentation overhead — present only in the `-pg`
build, not under `-O2` — is charged to it, manufacturing a fake 25–33%. The
same artifact inflates every call-heavy tiny function in that profile. **Do not
trust gprof self-times for functions with huge call counts; trust `-O2`
wall-clock deltas.** The variant-map change was reverted: no real win, and it
would add the first map to the self-host source.

The genuine `-O2` cost is **memory traffic**. The gprof *call counts* (which are
not distorted) show ~1.7M `arena_alloc`, ~1.3M `tycho_str_copy`, and ~2.1M
bounded-array gets per self-compile — the volume of small string allocations the
string-building codegen does, plus value-semantic copies.

### The real hotspots

Because `perf` is blocked on the measurement machine (`perf_event_paranoid=3`,
with a "no new privileges" flag that stops `sudo` even with a password) and
valgrind is not installable, I use a dependency-free statistical CPU-time sampler
instead (`tools/prof/`, using `ITIMER_PROF`+`SIGPROF`, with no `mcount`
artifact). It surfaces a hotspot the gprof profile hid entirely: **`scan_token`
recomputed `len(src)` — a full `strlen` of the whole source — once per token**,
so lexing was O(tokens × len) = **O(n²)**. The fix is purely algorithmic and
touches no bounds-checking: thread the already-known length (`lex` computes
`n := len(src)` once) into `scan_token` instead of recomputing it. The
self-hosted self-compile (B) dropped **62 → 33 ms (~1.9×)**.

This also revealed B was always ~2× faster than the A binary this doc had been
timing: `tychoc0`'s codegen emits a direct O(1) `s[i]` where `tychoc` emitted a
`strlen`-bounds-checked `tycho_str_get` that was O(n) *per access* → O(n²) in a
loop, for any `tychoc`-compiled program indexing a large string.

That `tychoc`-side O(n²) is now also fixed with a length-carrying check.
`tychoc`'s codegen gains a per-proc pass: a string variable that is indexed
(`s[i]`) and never reassigned (`block_mutates`==0, so for a string its length is
invariant) gets one hoisted `_slen_h_<v> = strlen(v)` sidecar at scope entry,
and its index sites use a new `tycho_str_get_n(s, i, len)` — the **same bounds
check, now O(1)** instead of re-`strlen`-ing per access. Full safety is kept
(verified: out-of-bounds and negative indices still `exit(1)` with the bounds
error). The `tychoc`-built A self-compile dropped **126 → 75 ms (~1.7×)**, with
`tests/str_index.ty` guarding it with hand-verifiable output.

### The "diffuse floor" turned out to be one thing: Ctx reconstruction

After the two O(n²) fixes, the cost looked like diffuse `memcpy`/`malloc` from
value-semantic copies — but that was a profiler blind spot, not the truth.
Improving the sampler's caller attribution (a saved-RBP-chain walk, so libc
leaves like `malloc` are blamed on the Tycho function that called them) dropped
the unattributed "?" from ~75% to ~13% and revealed the real floor:
**`with_owner` (25.7%) + `enter_block` (11.2%) = ~37%** was `Ctx`
*reconstruction*. `with_owner` is called only to change the `owner` string, but
the returned `Ctx` escapes, so value semantics deep-copied **every** field —
including the large, parse-invariant `sigs`/`structs`/`enums` — on every
owner/depth change. `tychoc` never had this because it threads `arena` as a plain
parameter.


The fix splits the immutable parse data into a `Decls` struct, built once and
threaded read-only (`dc`), never reconstructed; `Ctx` keeps only the 7 mutable
per-scope/per-fn fields, so `with_owner`/`enter_block` rebuild a tiny struct.
`dc` is threaded through all 67 `ctx`-taking functions (which pushed
`gen_match_optres` to 9 params, so `tychoc`'s fixed `Sig` param cap was raised
8→16; `tychoc0` uses dynamic arrays). Result: **B self-hosted self-compile 33.5 →
22.7 ms (~1.48×)**, with `with_owner`/`enter_block` gone from the profile. The
new top is genuine codegen logic (`type_of` ~13%, `gen_expr` ~11%,
`compute_movables` ~7%, `sig_ret` ~5%) — a smaller, more diffuse next layer.

Two more from that layer, both output-invariant. The Decls split also made it
safe to add O(1) lookup *maps* to the immutable `Decls` (built once, never
reconstructed — the per-clone copy cost that doomed an earlier such attempt is
gone). (1) `compute_movables` (the move-on-last-use pre-pass) was O(reads²) — it
called `count_str_occ(reads, n)` for every read; a one-pass frequency map plus
loopreads set makes it O(reads). (2) `sig_ret`'s per-call linear `dc.sigs` scan
became an O(1) `dc.sigmap` lookup. Together: **B 22.7 → 19.8 ms (~13%)**. What
remains (`gen_expr`/`type_of` plus a large unattributed `memcpy`/`malloc` chunk)
is the
inherent string-building codegen: `tychoc0` returns `sc()`-concatenated owned
strings, so output bytes are copied once per nesting level — the value-semantic
string-copy floor. No logic-level change moves it.

### Codegen quality of tychoc0's own output

These properties of the code `tychoc0` *emits* when transpiling itself (B = tychoc0
compiled by tychoc0, transpiling `tychoc0.ty`) are output-invisible — the self-build
stays byte-identical:

- A **block free-list pool** in the emitted arena runtime cuts time **106 → 64
  ms (1.66×)** by removing malloc/free churn per scope.
- A **compact tagged-union enum layout** cuts peak RSS **18.2 → 9.9 MB
  (1.84×)**: the flat `Expr`/`Stmt` nodes — 25 and 33 fields — shrink to their
  active variant.

The last generated-code gap on the cross-language suite came down to a pair of
leaf-path bugs: profiling binary-trees showed `tychoc0` doing 3× the allocations
(102.7M vs `tychoc`'s 34.0M) from a redundant deep-copy on `return Leaf` and no
shared singleton for nullary variants. With both fixed, binary-trees drops **38 →
13 MB, 289 → 124 ms** — `tychoc`'s exact allocation count. With this, `tychoc0`
beats `tychoc` on both memory and time on 3 of 4 workloads in the cross-language
benchmark suite and leads the suite on binary-trees here.

The former string-pipeline gap is closed by the additive `char` type: `'x'`
literals, `char ± int → char`, and `string + char` compiling to a one-byte
in-place append (`hi_append_char`, no per-digit string allocation) — the same
byte-write C/Rust/Go do. With `s = s + ('0' + d)`, string-pipeline drops **34 →
1 ms (~21× on tychoc, ~10× on tychoc0)** at unchanged memory, tying C at 1 MB / 1
ms.

### Push-loop fusion — register-resident array building (both compilers)

The generated-code counterpart to the wins above. A loop that only pushes to a
local scalar array paid, **per element**, for the array descriptor
(`data`/`len`/`cap`) round-tripping through memory: the C compiler must assume
`&arr` aliases the arena pointer also passed to `push`, so it cannot keep the
cursor in registers. Profiling `iter_transform` (a 200M-element push loop)
isolated it — arithmetic plus bounds-elided reads were 334 ms, `push` ~915 ms
(73%); hand-hoisting the loop hit 337 ms. `reserve`, `restrict`, and a cheaper
empty-arena `reset` all failed to move it — the cost is the descriptor traffic,
not growth or the capacity branch.

**Fusion:** when a loop's body uses a local scalar array (`[int]`/`[float]`)
ONLY as `push(arr, …)`, codegen caches `data`/`len`/`cap` in C locals across the
loop (hot path `_fd[_fl++] = v`), calls a grow hook (`tycho_arr_int/float_grow`)
only on overflow, and writes the descriptor back at loop exit. `break` needs
nothing (the flush sits after the loop, which `break` falls through to);
`continue`'s cursor survives in registers; `return` flushes via the registry
first; nested loops pushing the same array reuse the outer cursor.

**Sound by construction** — fuse ONLY when `count_reads == pushcount` (used
solely as a push target), the array is a plain non-inout scalar local not
defined/shadowed in the body, and (for a `while`) the condition does not read
it. Any miss falls back to the standard codegen, so a non-fused loop is never
wrong. In `tychoc` the registry is C globals (`g_fuse`); `tychoc0` has no globals,
so I thread it through `Ctx` (fields `fusearr`/`fusesuf`/`fusety`, top-down into
the body) with cursor names keyed on `ctx.depth`.

**Result:** `iter_transform` 1249 → 416 ms (6.7× → 2.3× C) and 4 → 3 MB;
`arr_pipeline` 53 → 30 ms (2.2× → 1.25× C). It applies generally — every scalar
push loop, including the self-compiler. Fusion correctly compiles the push-heavy
`tychoc0.ty`, which self-reproduces byte-identically and compiles itself faster;
`tests/push_fusion.ty` covers break/continue/return/nested/two-array/while/bail.

**Every element type now fuses** (both compilers): not just scalars, but
`[string]`, structs, tuples, nested arrays, options/results, and enums — any
array whose element family has a `_grow` hook (all of them). The grow hook is
element-generic: regrow the *spine* (the element buffer), recycle the old spine;
the heap each element points to was already deep-copied into the array's arena,
so the shallow per-element spine copy keeps it valid. This is sound because a
push only *appends* — no element is ever overwritten — and the fused store
deep-copies the element into the array's owning arena exactly as the non-fused
push does, preserving value semantics. The win is largest for scalars; for heap
elements the per-element deep-copy dominates, so the descriptor-elision is a
smaller fraction — a consistency/completeness close more than a hot-path
multiplier, though it still cuts a call plus a descriptor write-back per push on
the very common build-a-list loop. Composite fusion fires on `tychoc0.ty`'s own
struct/tuple array push loops (+400 lines C vs scalar-only) and the self-build
stays byte-identical, with `tests/str_fuse.ty` and `tests/comp_fuse.ty`
covering it. Every array element type fuses.

## Where the self-compile gap stands

**Current gap: tychoc0 self-compile ≈ 2.4× the C transpiler** (on the primary
machine, B 31 ms vs `tychoc` 13 ms transpiling `tychoc0.ty`; the profiler-box
trace in [Where the remaining time goes](#where-the-remaining-time-goes) lands at
~20 ms — same ratio, different machine, so compare the *ratio*, not the absolute ms). I'm out of cheap wins: the algorithmic
O(n²)s are gone (`scan_token` strlen, `compute_movables`), the linear scans are
O(1) maps (`sig_ret`/`dc.sigmap`), and the per-scope `Ctx` deep-copy is gone
(the `Decls` split). I tried two more logic micro-opts and reverted both as
wall-clock noise — `sig_ret` user-map-first (it was already O(1)) and a
`type_of` short-circuit (skip recursing operands when the result is always
`int`). They are correct but 0%, because the profiler's by-caller hotspots
(`gen_expr`/`type_of`/`sig_ret`) are those functions *on the stack while their
returned strings are copied*, not their own compute.

**The gap is architectural, not a bug.** About 85% of `tychoc0`'s self-compile
time is string allocation and copying, dominated by `scopy` return-value copies
(~5:1 over `sc` concats). This is the **value-semantic string-copy floor**:
`tychoc0`'s codegen builds output by returning and concatenating owned strings
(each copied once per nesting level, plus a deep-copy on every string-returning
`return`), whereas the C transpiler writes output through `fprintf` with raw
pointers. No logic-level change moves it; I don't think outperforming the C
transpiler with a value-semantic self-hosted one is reachable incrementally.

**The C transpiler `src/tychoc.c` stays the default, production transpiler.**
`tychoc0` is the self-hosting proof — the byte-identical self-build
(`make fixpoint`) is the definitive dogfood of the value-semantic plus arena
model on a real, allocation-heavy self-hosted transpiler (~11.9k lines of Tycho)
— and the counterpart in the
differential oracle. I haven't retired it, and it
is not on the correctness-critical path for production. By the convention I
hold to, retiring the C transpiler would mean `tychoc0` has to *outperform* it,
not merely match; and that gap is a fundamental property of the value-semantic
model rather than a missing optimization.
