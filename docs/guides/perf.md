# Performance: the compiler and the C it emits

> **[!CAUTION]** A contributors' log about `src/tychoc.c`, `runtime/tycho_rt.c`
> and their output — how the arena model is kept cheap and what each mechanism
> measured when it landed. It is **not** the language's headline performance
> story: that is [the thesis](../thesis.md) and the cross-language suite
> (`bench/prongB/`, `bench/conc/`).

> **This page was reframed on 2026-08-14.** It used to be a head-to-head between
> `tychoc` and the self-hosted `tychoc0` — a comparison retired when `tychoc0`
> was frozen (2026-07-29) and cut from every gate. The mechanisms it documented
> are kept; the versus framing and the timings taken against the self-compile are
> gone, since that workload can no longer be run. What is left says what each
> mechanism DOES and why, with the profiling notes that still hold. The one
> reproducible number on this page is the transpile time below.

> **This page carries one number**, the transpile time below, measured on
> 2026-08-14 on an AMD Ryzen 7 7735HS (16 hardware threads), Debian x86-64.
> Everything else describes what a mechanism does and why. That is deliberate:
> the figures that used to be here were taken against a workload that can no
> longer be run, and a number nobody can reproduce is worse than no number.
> For figures you can reproduce, `make bench` runs the guard suite (17
> benchmarks) and each `bench/*/RESULTS.md` carries its own date and hardware.

## Transpile time

`bench/transpile/run.sh` measures how long `tychoc` takes to turn one `.ty` file
into C. It generates a syntax-stable input, verifies the compile **succeeds**
before timing it (a compile that dies is faster than one that works, so an
unchecked input reports the failure path as a speed-up), discards a warm-up run,
and reports the minimum of N with median and max beside it.

Measured 2026-08-14, minimum of 10 timed runs over a 4.2k-line input:
**11 ms at `84e83132` (2026-08-04), 11 ms at `77bd8260` (2026-08-11), 11 ms at
`HEAD`** — flat, no regression across that window.

## What the arena codegen buys, by workload pattern

The baseline for comparison is an early **naive codegen** — `malloc`, no frees,
value-copy concatenation — which is what the arena model replaced. It cannot be
run today; what is worth recording is the shape of the gap, and where it does and
does not appear.

## Interpretation

- **Straight-line compute is identical.** With no repeated growth or mutation
  of heap values, the naive baseline and the arena codegen run at the same speed
  (`memo`, `optimize`).
- **The arena model wins exactly where it was designed to**, by orders of
  magnitude in both time and memory: the `acc = acc + s` loop, which the compiler
  rewrites to an in-place O(n) append in a bounded buffer where the naive `sc()`
  re-copies the whole growing string each step (O(n²)) and leaks every
  intermediate.
- **Arena has a per-scope tax.** On code with many small allocations across
  deeply nested scopes plus recursion, the naive leak-everything approach was
  materially faster before the tuning below: every block scope and call does
  `arena_child` / `arena_reset` / `arena_free`, which under the original
  allocator meant a `malloc`+`free` of an arena block per scope or loop
  iteration, and a model that never frees has no such churn — at the cost of
  unbounded memory. The arena advantage is large but concentrated in the
  grow-in-place pattern, not uniform.

That per-scope block churn is the arena concern I address with the tuning below.

## How I keep the per-scope arena tax down (runtime/tycho_rt.c)

`TYCHO_BLOCK_DEFAULT` is 64 KB, and a fresh arena is created per block scope,
call, and loop iteration. A naive `arena_alloc`/`arena_free` does a
`malloc`/`free` of a 64 KB block for *every* scope that allocates even a few
bytes; on deeply-recursive code that is a flood of `malloc(64K)`/`free` pairs
(plus page faults), and it dominates. Two runtime
mechanisms keep that churn off the hot path:

- **Global block free-list (pool).** `arena_reset`/`arena_free` hand their
  blocks to a process-global free-list instead of `free`ing; `arena_alloc`
  takes from it first. Block churn is O(1) pointer ops with no `malloc`/`free`
  and no page re-faulting. Peak live memory is unchanged (the pool holds at
  most what a scope just released; reclaimed by the OS at exit).
- **Block-retaining `arena_reset`.** A loop's scratch arena keeps its head
  block and just rewinds it (`off = 0`), releasing only overflow blocks. The
  common one-block-per-iteration loop does zero pool traffic per iteration.

The pool is the load-bearing one: it turns per-scope block churn into O(1)
pointer traffic, and it is why `arena_child`/`arena_free` do not appear as a cost
in any current profile. Retain-reset is neutral on code that allocates once per
scope and pays off on loop-scratch-heavy code, which is what it was added for.
Peak live memory is unchanged by either.

**What the pool does NOT fix.** With block churn off the hot path, what remained
was the value-semantics deep copies the arena model performs and a
leak-everything allocator skips — not arena bookkeeping. That is what the codegen
work below addresses.

## Codegen-level arena handling (src/tychoc.c)

Two codegen properties keep the arena model's overhead low.

**Child arenas are elided for if/match blocks.** if/else/match-arm blocks would
otherwise create a child arena (`_b%d = arena_child(scope)`) freed at block end.
The enclosing `scope` always outlives the block, so block transients fall back
to it with no early-free, and escaping values promote to `_parent` independent
of any `_bN`. Eliding them removes the great majority of `arena_child` calls from
a typical emission. Wall-clock and RSS are unchanged — the pool already makes
those ops nearly free — but the emitted C is smaller, with fewer runtime ops per
program, which isolates the real cost.

**Read-only heap struct params are borrowed, not deep-copied.** This is the real
lever. A heap-bearing by-value struct param would otherwise be unconditionally
deep-copied into the callee `_scope` on entry (for independence under mutation).
Gating that copy on `block_mutates(body, param)` — copy only if the body mutates
the param, else borrow the caller's value (the caller outlives the call;
unmutated aliasing is unobservable; `return param` still deep-copies via the
return path) — removes the dominant cost: the `Ctx` symbol table cloned on every
call.

On a compiler-shaped workload this eliminated the symbol-table clone entirely and
cut string-copy and allocation counts by two orders of magnitude, with peak
memory flat. It is the single largest win in this file.

**Net effect.** With the copy gated, the value-semantic implicit-arena model
lands close to a leak-everything allocator on an allocation-heavy,
deeply-recursive workload — while holding peak memory bounded where the
leak-everything version grows without limit. That, plus the grow-in-place win
above, is the case for the model.

The borrow-iff-not-mutated rule is the same predicate used on match-arm
payloads.

## Where the remaining time goes

Profiling put the arena memory model itself — `arena_child`, `arena_alloc`,
`tycho_str_copy` — at a small share of run time. What dominates is algorithmic
work in the compiler's own source, not the memory model.

**One caution, because it is easy to read a profile wrong.** gprof self-times
once pointed at `is_variant` (the enum-variant scan) as a major bottleneck. It
is not one. Two independent checks settled it: replacing all four variant
lookups with an O(1) `{string:int}` map removed the `is_variant` line from a
fresh gprof and changed `-O2` wall-clock by nothing at all; and the cause is a
gprof `mcount` artifact — `is_variant` is a tiny branch-predicted loop called
enormously often, and gprof's per-call instrumentation overhead, present only in
the `-pg` build, is charged to it. The same artifact inflates every call-heavy
tiny function in such a profile. **Do not trust gprof self-times for functions
with huge call counts; trust `-O2` wall-clock deltas.** The variant-map change
was reverted — no real win, and it would have added a map to source that had
none.

The genuine `-O2` cost is **memory traffic**. gprof *call counts* are not
distorted the way self-times are, and they show the shape: many small
`arena_alloc`s and `tycho_str_copy`s, which is the volume of short-lived string
allocation the string-building codegen does, plus value-semantic copies.

### The real hotspots

Because `perf` is blocked on the measurement machine (`perf_event_paranoid=3`,
with a "no new privileges" flag that stops `sudo` even with a password) and
valgrind is not installable, I use a dependency-free statistical CPU-time sampler
instead (`tools/prof/`, using `ITIMER_PROF`+`SIGPROF`, with no `mcount`
artifact). It surfaces a hotspot the gprof profile hid entirely: **`scan_token`
recomputed `len(src)` — a full `strlen` of the whole source — once per token**,
so lexing was O(tokens × len) = **O(n²)**. The fix is purely algorithmic and
touches no bounds-checking: thread the already-known length (`lex` computes
`n := len(src)` once) into `scan_token` instead of recomputing it, which takes
lexing from O(n²) back to O(n).

It also exposed a second O(n²), this one in `tychoc`'s own output: the emitted
`strlen`-bounds-checked `tycho_str_get` was O(n) *per access*, so any compiled
program indexing a large string in a loop paid O(n²).

That is fixed with a length-carrying check.
`tychoc`'s codegen gains a per-proc pass: a string variable that is indexed
(`s[i]`) and never reassigned (`block_mutates`==0, so for a string its length is
invariant) gets one hoisted `_slen_h_<v> = strlen(v)` sidecar at scope entry,
and its index sites use a new `tycho_str_get_n(s, i, len)` — the **same bounds
check, now O(1)** instead of re-`strlen`-ing per access. Full safety is kept
(verified: out-of-bounds and negative indices still `exit(1)` with the bounds
error), and `tests/str_index.ty` guards it with hand-verifiable output.

### The "diffuse floor" turned out to be one thing: Ctx reconstruction

After the two O(n²) fixes, the cost looked like diffuse `memcpy`/`malloc` from
value-semantic copies — but that was a profiler blind spot, not the truth.
Improving the sampler's caller attribution (a saved-RBP-chain walk, so libc
leaves like `malloc` are blamed on the Tycho function that called them) cut the
unattributed share dramatically and revealed the real floor: `with_owner` plus
`enter_block` — that is, `Ctx` *reconstruction*. `with_owner` is called only to change the `owner` string, but
the returned `Ctx` escapes, so value semantics deep-copied **every** field —
including the large, parse-invariant `sigs`/`structs`/`enums` — on every
owner/depth change. `tychoc` never had this because it threads `arena` as a plain
parameter.


The fix splits the immutable parse data into a `Decls` struct, built once and
threaded read-only (`dc`), never reconstructed; `Ctx` keeps only the 7 mutable
per-scope/per-fn fields, so `with_owner`/`enter_block` rebuild a tiny struct.
`dc` is threaded through all 67 `ctx`-taking functions (which pushed
`gen_match_optres` to 9 params, so `tychoc`'s fixed `Sig` param cap was raised
8→16). `with_owner`/`enter_block` leave the profile entirely; what surfaces
underneath is genuine codegen logic (`type_of`, `gen_expr`, `compute_movables`,
`sig_ret`) — a smaller, more diffuse next layer.

Two more from that layer, both output-invariant. The Decls split also made it
safe to add O(1) lookup *maps* to the immutable `Decls` (built once, never
reconstructed — the per-clone copy cost that doomed an earlier such attempt is
gone). (1) `compute_movables` (the move-on-last-use pre-pass) was O(reads²) — it
called `count_str_occ(reads, n)` for every read; a one-pass frequency map plus
loopreads set makes it O(reads). (2) `sig_ret`'s per-call linear `dc.sigs` scan
became an O(1) `dc.sigmap` lookup. What remains (`gen_expr`/`type_of` plus a large unattributed `memcpy`/`malloc` chunk)
is the
inherent string-building codegen: concatenated owned strings mean output bytes
are copied once per nesting level — the value-semantic string-copy floor. No
logic-level change moves it.

### Push-loop fusion — register-resident array building

The generated-code counterpart to the wins above. A loop that only pushes to a
local scalar array paid, **per element**, for the array descriptor
(`data`/`len`/`cap`) round-tripping through memory: the C compiler must assume
`&arr` aliases the arena pointer also passed to `push`, so it cannot keep the
cursor in registers. Profiling `iter_transform` (a 200M-element push loop)
isolated it: `push` dominated the loop, and hand-hoisting the descriptor brought
it back down to roughly the cost of the arithmetic alone. `reserve`, `restrict`,
and a cheaper empty-arena `reset` all failed to move it — the cost is the
descriptor traffic, not growth or the capacity branch.

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
wrong. The registry is C globals (`g_fuse`).

**Result:** a large multiple on push-dominated loops, closing most of the gap to
hand-written C, with peak memory slightly down. It applies generally — every
scalar push loop. `tests/push_fusion.ty` covers
break/continue/return/nested/two-array/while/bail.

**Every element type now fuses**: not just scalars, but
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
the very common build-a-list loop. `tests/str_fuse.ty` and `tests/comp_fuse.ty`
cover it. Every array element type fuses.

