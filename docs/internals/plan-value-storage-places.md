# Plan: ownership-free storage optimization + values vs places

Status: ACTIVE 2026-10-13. Direction, not a promise of dates. No surface change without owner sign-off (`surface.lock`, `ROADMAP.md:40-50`).

Principle (preserved absolutely):

> Every value behaves as though it is independent. The compiler is free to share storage, reuse allocations, and move data, provided the program cannot observe a difference.

Invariants: assignments keep independent value semantics; no user-visible aliases to mutable storage; no expressible dangling references; no mandatory GC or per-object refcount; no new lifetime annotations; deterministic cleanup at or before scope exit. Peak memory / reclamation timing are implementation properties, not promises. Refused by decision: stored references (`docs/rfc/limited-references-spike.md`), which dismantle the deep-copy thread boundary and no-escape arena model.

## Baseline before the append fix (measured, both compilers)

Before the fix, `recv_line` in `tools/tycho-db/srv/srv.ty` rebuilt its `inout`
string buffer for every received chunk. `send_block` in the same package and
`read_request_resume` in `corelib/httpd/httpd.ty` already used local append reuse;
the emitters explicitly excluded `inout` string accumulators.

One 8 KB line in 64 B chunks (128 appends), `tychoc` and `tychoc1` agree:

- payload copied: 528,384 B naive vs ~24,000 B geometric (22x).
- bump: 518.0 KiB / 130 allocs vs 32.0 KiB / 10 allocs (16.2x, prediction exact).
- wall, 2000 reps via `clock()`: ~10 ms vs ~0.5 ms (~20x, 3 runs).

Verdict: copying dominates. Compiler-side reuse is warranted; `Place` stays gated on reuse failing. Probe programs under `/tmp/opencode/` (not checked in); no repo change made for the measurement.

Pre-fix compiler shape (historical baseline): whole-value moves only — move-on-last-use `src/tychoc.c:10871`, `sink` adopt `src/tychoc.c:10892`, construction `arg_into` `src/tychoc.c:11244`, self-rebuild `src/tychoc.c:11348`, return-slot `src/tychoc.c:10976`, match-payload borrow `src/tychoc.c:10934`; syntactic in-place (`is_self_append` `src/tychoc.c:11068`, map `src/tychoc.c:11093`, array `do_recycle` `src/tychoc.c:13290`); lexical arenas + loop `_scr` reset + `arena_recycle` freelist (`runtime/tycho_rt.c:706`). No field/partial move (refused `src/tychoc.c:11258`), no COW, no mid-scope subarenas.

## Immediate actions from the language investigation

1. **Implemented for named bindings, including separate helper calls — fix the
   `inout string` append cliff.** Both emitters keep append state with the binding
   and forward it through a hidden pointer on generated `inout string`/`bytes`
   calls. Growth still uses the binding's owning arena. State tracks buffer
   identity and capacity; length comes from the string header. Rebinding cannot
   reuse stale capacity, and nested calls share the same state. Fresh state copies
   incoming storage once to protect borrowed snapshots; value parameters start
   fresh state. No language surface, string layout or C FFI change; the private
   generated C call convention gains the pointer. `tests/inout_string.ty` covers
   semantics; `tests/inout_append.sh` bounds allocation growth under both compilers
   and requires a pure-concat control to exceed the bound. Projections without
   persistent binding metadata retain fresh per-call adoption. Phase 1 below owns
   the wider remaining design.
2. **Implemented — qualify memory, latency and correctness claims.** The existing
   owners (`STATUS.md`, `docs/memory-model.md`,
   `docs/internals/value-semantics-limits.md`, `bench/latency/RESULTS.md`) now
   distinguish arena reuse from OS release, single-block rewind from general
   teardown, no collector from no jitter, and finite test evidence from proof of
   correctness. Latency results retain their historical measurements but no longer
   infer service tail latency from aggregate GC counters. The limits note also
   records selective copy warnings and pool-relative handle validation; generation
   checks do not detect mixing pools. The related regions RFC's O(1) cleanup claim
   is corrected as well.
3. **Implemented — reconcile runtime-surface verification.** The channel-capacity
   and map-capacity diagnostics are intentional overflow guards, already exercised
   under both compilers by `tests/audit_runtime_guards.sh`. Both now have reasoned
   entries in `tests/rtparity/run.py`; `make rtparity` and the self-hosted oracle
   run pass. This fixes the oracle, not runtime behavior.

## Phase 1 — generalized storage reuse (Medium / Low). Do first.

Generalize `x = x + piece` / `x += piece` uniqueness+liveness beyond `Decl`-local accumulators: cover the `inout` socket site plus transient rebuilds, as IR liveness not more spine patterns. Fall back to `str_concat` whenever the proof is insufficient.
Gate: single-build bump 518 → ~32 KiB band on the probe; `tests/accumulator_scope.sh`, `make bench`, `tools/tycho-db/run.sh` green under both compilers.

First increment: the existing append mechanism is shared by locals and `inout`
parameters, rather than adding another expression-pattern optimization. At 128
64-byte chunks, both compilers bump 32 KiB; at 1024 chunks, 256 KiB, versus the
pure-concat control's 518 KiB already at 128 chunks. Bounds are allocation-based,
not timing thresholds. On 2026-10-15, `make test` and the self-hosted fixture
suite each passed 1,149 checks; `make fixpoint-check`, `make bench` and
`make db-check` passed (the last two with both compilers). Corelib's 46 checks,
concurrency, link/citation/script checks and the focused ILP32 fixture also pass.
The runtime-surface oracle had the two omissions subsequently reconciled in
action 3; those checks did not establish full-CI success.

Second increment: named bindings retain capacity across separate helper calls,
including recursive/nested forwarding and bindings introduced by tuples,
value-if and match patterns, plus instantiated generic helpers. A newly
mutable-borrowed string is treated as mutable
by snapshot-copy decisions even if its caller contains no direct append. At 128
separate calls, the pre-fix ~700 KiB bump becomes 32 KiB; 1024 calls use 256 KiB,
matching helper-local loops under both compilers. Bytes with embedded NULs meet
the same linear bounds. Snapshot, aggregate-copy, rebinding, recursive-call and
value-parameter checks are golden-locked in `tests/inout_string.ty`. The cross-call
increment passed `make test` and the self-hosted fixture suite (1,149 checks each),
`make fixpoint-check`, corelib (46 checks), concurrency (48 checks), and database
integration with both compilers. Its two runtime-surface oracle omissions were
subsequently reconciled in action 3; no full-CI success is claimed.

This changes only the private generated C convention: the existing owner-arena
argument is accompanied by a binding-state pointer. There is no stored reference,
refcount, COW flag or string-layout expansion. `&obj.field` / `&items[i]` lack
persistent binding metadata and pass NULL; the callee starts fresh state and
forwards it to nested helpers for that invocation. Separate calls on those
projections can still copy on each invocation. Caching there without tracking
replacement, container relocation and recycling would be unsound. General IR
liveness and projection-state persistence remain open.

## Phase 2 — field-sensitive optimization (High / Medium).

Avoid copying whole aggregates when only part survives: move/reuse individual field buffers of dead sources, skip untouched fields on nested update/return construction. Internal only, no partial-move surface (`src/tychoc.c:11258` stays).
Gate: targeted fixtures (nested update copies field count, not aggregate size); `make ci` green.

## Phase 3 — inferred temporary regions (High / Medium).

Compiler-assigned subarenas for provably dead allocation groups reclaimed before lexical scope exit. Start with all-local, no-escape, straight-line phases. The goal is earlier reuse, not an immediate RSS drop: released blocks can remain pooled (see [build-and-hold limits](value-semantics-limits.md#2-long-lived-scope-holding-transient-garbage-build-and-hold)).
Gate: phased function lowers peak arena-owned allocation under `TYCHO_ARENA_STATS=full` with identical output; report RSS separately; no `if/match` behavior change.

## Phase 4 — arena-local COW (Very high / High). Do last.

Share backing storage only under provable common lifetime. Cross-arena sharing has no owner for `reset/free` and is out of scope.
Gate: sharing fires only arena-local; peak delta recorded as implementation property; sanitizer + `make fuzz` clean.

## Semantic baseline (gate before Stage B)

Five permanent `tests/*.ty` fixtures (golden-locked via `tests/run.sh`, both compilers, native vs sanitizer identical) that every later optimization must preserve:

1. Copy independence: copy an aggregate, mutate either copy, verify the other is unchanged.
2. Nested dynamic selection: mutate through multiple levels of runtime-computed indices/keys.
3. Structural invalidation: resize or structurally modify a container while element access is live.
4. Return after nested modify: return an aggregate after modifying one nested element.
5. Control-flow escape: early returns, error paths (`or_return` / `Result`), and nested `inout` calls.

No Stage B work until all five are green and pinned. Existing partial coverage (`tests/borrow_alias.ty`, `tests/elem_recycle.ty`, `tests/projections.ty`, `tests/place_store_order.sh`) does not substitute for this named set.

## Phase 5 — values vs places experiment (gated).

The trie combines two limitations, not one:

| Limitation | Root cause | Solution |
|---|---|---|
| Nested update copies too much | No general reuse / place-aware update (`bench/trie/trie.ty:8-24` recurses by `&t.kids[c]`) | Phases 1–2 + Stage B below |
| Parents cannot share a node | Independent values provide no shared identity | Identity within an owning aggregate |

Solving only the first makes recursive value trees cheap without making shared graphs easier; identity syntax alone prettifies the same pool (`bench/trie/trie_pool.ty:3-27`: cursors, root `0`, load-bearing `reserve` `bench/trie/RESULTS.md:28-30`, ABA via `core:pool` `corelib/pool/pool.ty:22-25`). Runner-up `tools/tycho-ed/buf/buf.ty:278-280` → `[int]` gap is performance-only (RSS identical 95.7 MB, `docs/internals/editor-churn-2026-08-12.md:80-89`).

Hypothesis under test — aggregate-relative identity: aggregate is an ordinary value and owns all storage; identity names an element within it and owns nothing; place is temporary access during one operation and cannot escape. Destroying the aggregate destroys its storage. No new identity primitive yet: `core:pool` handles already solve packing/generation/stale-die at library level, so first test whether a general place makes those handles convenient.

Stricter place rule: a place is an operation on a location (`docs/spec/09-expressions.md:9-16`), not a value containing one. It must not be stored, returned, captured by an escaping closure, passed to detached concurrency, converted to persistent identity, or held across a structural invalidation. Existing `inout` + `a[i]` / `m[k]` / `p.f` place targets already cover the foundation — no general abstraction until the gap over them is named; prefer the smallest generalization that closes it.

Experiment (one branch, three implementations — recursive trie, pool trie, plus a small DAG):

| Stage | Change | Success criterion |
|---|---|---|
| A | Liveness-based reuse | `inout` string case approaches local-append band (Phase 1 gate) |
| B | Nested dynamic selection on existing places | Recursive-trie update avoids copying unaffected subtrees |
| C | Same place mechanism over pool nodes | Pool ops get simpler with no new identity semantics |

Score expressiveness (machinery the programmer writes), memory (allocation + copies), and compiler complexity (new rules / special cases) independently — 20 fewer application lines for 2,000 new compiler lines is a poor trade. A tree needs no shared identity but a DAG does: recursive-tree wins count as access progress only, and the DAG keeps the structural question from being obscured by tree performance. No further abstractions until A/B/C report evidence; the objective is how much expressiveness a small number of consistent rules yields.

Reserve: structural identity under value semantics (copy duplicates the identity domain). External-identity semantics — coordinates usable against both copies vs domain-bound — are explicitly undecided; postponed until the pool is pushed as far as it goes.

Central question: can relationships between values become as natural as relationships between memory locations, without ever making a location into a value?
Gate: recursive trie competitive without identity machinery, or pool complexity isolated as the genuine missing abstraction; `tools/tycho-ed/run.sh` flat with `make ed-check` leg 6 byte-identical; no `surface.lock` change without a second independent caller. Scoped capabilities deferred: non-copyable contexts are not values.

## Runtime-internal packing: evaluate targeted optimizations now

Recorded 2026-10-15 from [demofox 2026-10-03](https://blog.demofox.org/2026/10/03/the-double-headed-stack-allocator/) and [VMA linear algorithm](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/custom_memory_pools.html#linear_algorithm). One fixed buffer, two bump cursors growing toward each other; VMA generalizes it as free-at-once / LIFO stack / double-stack / ring-buffer modes of one linear pool, with double-stack and ring valid only single-block (`maxBlockCount` 1).

Decision: no language surface. Tycho has N nested lexical scopes on a growing block chain, not two sibling LIFO owners sharing one fixed buffer; a user-visible double-headed arena would reintroduce manual budget sizing and free ordering. Following the owner's 2026-10-15 direction, test applicable scenarios now rather than postpone all investigation until post-1.0. Targeted runtime-internal optimizations may proceed on measured evidence; no general allocator replacement is approved. Candidate applications:

- Offset-from-the-top packing: payloads from one end, index/offset table from the other, in a single `arena_alloc` — a container/packet layout optimization in the indexed-dict family, not a new arena kind.
- Bounded single-block arena semantics: two cursors meeting is a clean OOM abort. Does not compose with the growing chained arena + thread-local block pool (`runtime/tycho_rt.c`, `docs/memory-model.md`).
- Threading: shared atomic bump cursors thrash; per-thread arenas + thread-local pool stay, as already designed. Affine `task`/handle/channel destructors also stay out — double-headed use is POD/transient bytes only.

### Rewrite readiness: scope, baseline and safety contract

The pre-rewrite work does not authorize a language feature or replace the
existing demand gate. Scope any experiment to runtime-internal packing while
preserving growing arenas, N nested lexical owners, caller-directed return and
`inout` allocation, and thread-local reuse. A fixed two-owner buffer is not a
substitute for that contract. No bounded-budget caller has been demonstrated.

Checkpoint before preparation: `/tmp/tycho-allocator-readiness/checkpoint/`
contains HEAD, working-tree/index patches and status; existing work was not
committed or reset. For full-CI evidence, run `make ci` and require `make status`
to recognize the matching tree fingerprint in `build/ci-status`. Focused checks
are not a replacement for that record.

**Reproduce the real workload:** `make allocator-baseline` builds otherwise
identical uninstrumented/profiled copies of the self-hosted compiler, then emits
`compiler/main.ty`. The warmer, three timed runs, runtime-stats run and slack
profile must all emit byte-identical C. Logs and `results.json` are retained in
`build/allocator-baseline/`, with compiler-output hashes and source fingerprint.
Instrumentation patches only an emitted-C copy, calibrates against exact known
capacity/offset totals, and must reject deliberately defeated accounting.

2026-10-15, Linux x86-64, cc `-O2`, runtime default block size: three plain runs
were 210–218 ms and 66,884–67,276 KiB peak RSS. Profiled active capacity peaked at
58,767,848 B; maximum active tail slack was 18,012,888 B (17.2 MiB), at a point
with 53,393,896 B capacity and 35,381,008 B bump offsets. The active block count
peaked at 534. Separately, unmodified runtime stats reported 1.1 GiB cumulative
bump allocation, 66.6 MiB cumulative OS reservation and 99.9% block-pool reuse;
aggregate instrumented teardown was 75.742 ms. These are not simultaneous peaks,
semantic live-byte counts, uninstrumented cleanup latency or service tail bounds.
Tail slack excludes headers, recycle holes and pooled blocks. The evidence makes
packing worth investigating; it does not demonstrate how much is recoverable.

**Acceptance:** reduce active tail slack/peak capacity or fresh block reservation
on this real workload, with unchanged emitted C and no increased copy/allocation
growth. Report plain timing/RSS distributions separately, not a noisy timing gate.
Keep the current allocator as the comparison until that evidence exists.

**Pinned safety coverage:**

| Contract | Existing gate / fixture |
|---|---|
| Logical ownership; no foreign recycling; parent results survive scratch reset/release and pool reuse; 8-byte alignment | `make allocator-contract` (`tests/allocator_contract.c`), native and stats modes, ASan/UBSan/LSan where available; ILP32 runs it in `make ilp32` |
| Snapshots, return copies and caller-owned `inout` growth | `tests/value_semantics.ty`, `tests/inout_string.ty`, `tests/inout_append.sh` |
| Early return and propagated error cleanup | `tests/loop_return.ty`, `tests/or_return_frees.ty`, `tests/or_else_frees.ty` |
| Recursive element/map recycling and projection stores | `tests/elem_recycle.ty`, `tests/projections.ty`, `tests/map_overwrite_scale.sh` |
| Task/channel isolation and finalization on return/break/continue | `make conc`, including `tests/conc/implicit.ty`, `tests/conc/heap.ty`, `tests/conc/chan.ty` |
| Typed-handle finalizers | `make ffi` (`tests/ffi/main.ty`) |
| Allocation-size overflow and capacity diagnostics | `tests/audit_runtime_guards.sh` |

The runtime contract gate defeats ownership checking and rewind in separate
runtime copies and requires each to fail for its own reason. It found an actual
ILP32 defect: a 12-byte HBlock header offset payloads from 8-byte alignment. The
header now has an 8-aligned member, preserving payload alignment on ILP32 without
changing its LP64 size. This is a prerequisite correctness fix, not a rewrite.

Before sharing physical backing, preserve **logical** ownership: `arena_owns`
currently recognizes a whole block. Merely linking that block into two arenas
would let one recycle the other's live bytes. Independent ranges/ownership and
recycle bookkeeping, reset ordering, finalizers, alignment, overflow and growth
behavior must be specified and checked before such sharing is implemented.

### Isolated shared-backing experiment — not promoted (2026-10-15)

`make allocator-packing` reproduces an opt-in experiment in
`bench/allocator/compare.py` and `bench/allocator/packed.c`. It patches only
throwaway emitted-C copies under `build/allocator-packing/`; neither the runtime
nor compiler-copy logic is changed. The baseline's exact calibration and
negative control remain shared through `bench/allocator/profile_runtime.py`.

**Ownership model tested:** a physical slab contains multiple disjoint HBlock
header/payload ranges. Each logical arena owns only its linked views;
`arena_owns` still checks the view, never the whole slab. View capacities are
immutable, and reset/recycling cannot reach a neighbor's range. Pools transfer
whole views. Only the creating thread advances a slab cursor. The cursor and
each view retain backing-block references; those references must be atomic
because a task's root can outlive its worker and be released on the waiting
thread. These are references on physical backing, not refcounts on language
values. This resolves ownership for this prototype, not for arbitrary overlapping
ranges or a future two-cursor implementation.

The candidate packs 4 KiB logical views into 64 KiB backing slabs, with dedicated
backing for oversized requests. It accepts arbitrary arena lifetime order rather
than imposing a two-owner double-stack order. Retain an unmodified 4 KiB-block
control as well as the default 64 KiB allocator: otherwise a block-size effect
could be mistaken for a packing improvement. The existing `TYCHO_BLOCK` knob
also enabled an initial 4/8/16/32 KiB sweep without runtime changes; none lowered
RSS on this workload.

Same self-host compiler workload, Linux x86-64, `-O2`, one warmup and five
interleaved plain runs (alternate variant order), with profiling in separate
runs. Medians from the final experiment:

| C compiler | Allocator | Wall ms | Peak RSS KiB |
|---|---|---:|---:|
| GCC (`cc`) | Default | 219 | 66,788 |
| GCC (`cc`) | 4 KiB control | 233 | 74,684 |
| GCC (`cc`) | Shared slabs, 4 KiB views | 235 | 76,804 |
| Clang | Default | 146 | 66,188 |
| Clang | 4 KiB control | 171 | 73,612 |
| Clang | Shared slabs, 4 KiB views | 182 | 75,912 |

Logical peak capacity was 58,767,848 B by default and 56,145,992 B for **both**
small-view variants. Peak tail slack was about 18.0 MB versus 17.1 MB; packing
added no improvement over the block-size control. The GCC profile reserved
77.3 MiB cumulatively from malloc for the packed variant, versus 71.3 MiB for
the small-block control and 66.6 MiB by default. Unassigned slab tails, headers
and pooled views are not logical tail slack. RSS is measured separately; lower
logical capacity did not imply lower process memory. Timing samples are
descriptive, not a performance gate or service-tail bound.

**Safety evidence:** GCC and Clang each passed native, ASan/UBSan/LSan and ILP32
contracts. The extended contract in `bench/allocator/packed_contract.c` checks
actual shared backing, foreign-recycle rejection, partial pool flush, and both
cross-thread release orders (worker exits first / waiting thread releases first).
Widening ownership to a whole slab and dropping backing releases each fail for
their own reason. Header/slab size overflows fail cleanly. Twelve existing
semantic fixtures, emitted by both Tycho compilers, match their goldens under
native and sanitizer builds, including snapshots, `inout`, early/error exits,
recycling and task/channel finalization. All measured compiler outputs are
byte-identical. These finite checks are not a general correctness proof; slab
allocation also reduces ASan's physical redzone granularity between views.

**Decision:** keep the current allocator. This candidate fails the acceptance
gate: no packing-specific capacity benefit, more reserved/RSS memory, and slower
plain runs. Do not promote it or add more machinery to rescue it without a new
measured demand. This does not disprove other allocator layouts or change the
roadmap's post-1.0 demand gate. Source fingerprint, raw samples, emitted sources,
profile logs and output hashes are in `build/allocator-packing/`; the dated GCC
and Clang evidence is preserved under `/tmp/tycho-packing-prototype/`.

### True dual-headed scenarios — a targeted opportunity (2026-10-15)

The rejected shared-slab candidate above did **not** test opposite bump cursors.
`make allocator-dual` now tests a true bounded two-ended buffer in
`bench/allocator/dual.h`, with lower-end retained output and upper-end scratch
(or a packed index). Upper-end nested marks are owner-bound, unique, and strictly
LIFO. Lower-end bulk reset preserves upper-end allocations. Failed allocations
leave both cursors unchanged, with no silent growth or truncation. No runtime,
compiler, language layout or copy-elision behavior is changed by this experiment.

**Matrix:** seven scenarios (payload/index, scratch/result, nested scratch,
alternating skew, capacity collision, retained-output growth, anti-correlated
scratch/output), two schedules (serial batches / all 32 outputs held until a
phase boundary), four budgets (32/64/72/128 KiB), five controls:

- Current separate growing arenas, default 64 KiB blocks.
- Current single arena: presized index for the index case; scratch retained to
  batch end otherwise, since rewinding would destroy later caller-owned output.
- Equal fixed partitions of the same total budget.
- Actual opposing cursors sharing that budget.
- Current separate arenas with 4 KiB blocks, to detect a simpler tuning win.

The three source inputs are `compiler/main.ty`, `compiler/emit/emit.ty`, and
`tools/tycho-db/srv/srv.ty` (9,400 nonempty lines, lengths capped at 4 KiB). The
first three scenarios derive allocation sizes from those lines. The remaining
four deliberately control group sizes to exercise skew and hard boundaries;
using repository bytes does not make them end-to-end application benchmarks.
Every surviving byte and index is checked, including after other contexts run.
Successful variants must agree on checksums. Unsupported capacities are recorded
as rejections, not fast timings. All fixed-capacity failures still run cleanup.

**Positive case:** anti-correlated scratch shrinks from 48 to 4 KiB while retained
output grows from 4 to 48 KiB. Combined peak is 52 KiB, although each group's
individual peak is 48 KiB. With a 64 KiB budget and 32 held-result contexts:

| Allocator | Reserved bytes | GCC median RSS KiB | Clang median RSS KiB |
|---|---:|---:|---:|
| Default separate arenas | 4,195,840 | 5,816 | 6,072 |
| Separate arenas, 4 KiB blocks | 3,155,712 | 5,348 | 5,552 |
| Dual-headed, 64 KiB each | 2,097,152 | 4,780 | 4,492 |

The dual buffer reserves 50% less than default separate arenas and 33.5% less
than the smaller-block control. Both C compilers also show lower RSS. An equal
32/32 KiB partition fails here; a *static unequal* split cannot accommodate both
48 KiB individual peaks in 64 KiB either. A single arena that retains all scratch
uses 14,685,440 reserved bytes in the held-results schedule. The memory advantage
is capacity sharing across genuinely different lifetimes, not eliminated value
copies. Timing is mixed: the GCC dual median is 139.0 ms versus 148.8 ms for the
small-block control; Clang is 126.7 versus 120.0 ms. No universal speedup is claimed.

**Counterexamples matter:** for the held-result index scenario, native 4 KiB
arenas reserve only 395,520 B versus 1,048,576 B for 32 KiB dual buffers. Small
arenas also win reservation on the repository-derived scratch/nested traces.
For 64 KiB retained output plus 8 KiB scratch, a 72 KiB dual buffer works, but
small native arenas reserve nearly the same amount; that alone does not justify
new machinery. A 64 KiB dual buffer correctly rejects the same trace. The 80 KiB
collision trace is rejected below 80 KiB while the native allocator keeps growing.
Oversizing a dual buffer or using it for mostly idle contexts can erase its gain.

**Proof and limits:** GCC and Clang each passed all 280 matrix configurations in
native, ASan/UBSan/LSan, and native ILP32 builds. Contracts cover alignment,
disjoint ownership, cursor collision, size/mark-counter overflow, stale/foreign/
non-LIFO marks, nested survivors and independent lower-end reset. Three defeated
guards (collision, order, rewind) must fail for their named reason. Plain timing
and RSS use one warmup plus five interleaved rounds with alternating order;
reservation comes from separate stats-enabled runs. Final cleanup samples are
retained, but they are not service-tail latency evidence. This POD-only local
allocator has no cross-thread ownership transfer or handle-finalizer integration;
those remain requirements for any applicable production integration.

**Decision now:** retain dual-headed allocation as a concrete local optimization
candidate for bounded scratch/output phases with anti-correlated demand. Do not
throw it away or defer investigation solely by release date. Prefer existing
small arenas or presizing where those win. Before changing a production caller,
measure that caller's combined bound and lifetime pattern, then compare its
actual output, fallback/error behavior, RSS and latency against both native
controls. Capped request readers (`MAX_LINE` in the database server and
`MAX_REQUEST` in HTTP) are inspection candidates, not proven beneficiaries: an
input cap alone does not bound result/scratch storage or prove anti-correlation.
No blanket compiler/global-arena switch follows from these scenario results.

Reproduction, input hashes, raw samples and all outcomes:
`build/allocator-dual/results.json`; dated GCC/Clang artifacts:
`/tmp/tycho-dual-scenarios/`. This targeted evaluation supersedes the old blanket
post-1.0 investigation deferral, not the safety or measured-benefit gates.

### Production integration: directory listing (2026-10-15)

Implemented in `runtime/tycho_rt.c`'s existing `tycho_list_dir` caller. Directory
enumeration is single-pass and its entry count is unknown; counting with a second
`readdir` pass would not preserve the observed stream if the directory changes.
This is a local construction optimization, not a replacement for lexical arenas.

Fresh length-headered names grow upward in the current arena's unused tail;
a temporary pointer index grows downward. Its window expands geometrically
within that tail, rather than touching the far end of a large block for a tiny
listing. Publication reverses/compacts the index and commits **only used bytes**
through ordinary `arena_alloc`. If that allocation reuses a different recycled
chunk, names and index are copied there and pointers relocated. No arena
allocation may interleave with the private builder before publication; the
producer's packing and growing loops are deliberately separate. Both groups
have one arena owner, so whole-block `arena_owns` remains correct. There is no
shared logical-arena backing, new Arena field, refcount or C ABI change.

If the tail fills, publish the packed prefix and process the *same first
colliding entry* plus every remaining entry through normal array growth. There
is no directory size limit, dropped entry or new failure mode. Fresh libc names
are copied into their final owner once, rather than constructing a temporary
Tycho string and copying it again on push. This is fresh-value construction,
not weakened snapshot semantics or a compiler copy-elision change. The fallback
also adopts only fresh names; it never adopts a user-visible borrowed value.
Empty/missing directories need no array allocation, and dot-entry filtering,
iteration order, partial-read behavior and handle closure remain unchanged.

`make allocator-directory` compares the actual filesystem operation against the
old implementation, a fresh-name native array builder, and a simpler 512-pointer
stack-index builder. Five controlled directory sizes cover empty through 1,600
long names. For **700 64-byte names, 32 simultaneously held listings**, both GCC
and Clang report:

| Implementation | Backing reserved B | Bump bytes over four rounds |
|---|---:|---:|
| Original | 4,195,840 | 16,424,960 |
| Fresh-name native | 4,195,840 | 9,256,960 |
| Native + stack index | 4,195,840 | 8,740,864 |
| Dual-ended arena tail | 2,097,920 | 7,884,800 |

The packed result fits one existing 64 KiB block where both native alternatives
need two; RSS also falls. Separating the fresh-name control makes clear that
not all savings come from dual-ended packing. Larger directories safely spill;
there the reservation matches the fresh-name controls rather than beating them.
CPU timing is mixed and noisy, not a promised speedup. The whole self-host
compiler smoke emits byte-identical C before/after; its broad timing spread
supports no global latency claim. Plain timing/RSS, separate stats runs and raw
samples are retained in `build/dir-packing/` and
`/tmp/tycho-dual-integration/`.

**Pinned verification:** `make dir-packing` is in `make test`. The C contract
checks tiny arena blocks, collision/overlapping compaction, long/Unicode/hidden
names, fallback, exact enumeration order, snapshots, and recycled publication
followed by overwriting the abandoned staging region. Native/stats modes,
ASan/UBSan/LSan and available native ILP32 run under GCC and Clang. Mutants that
remove index reversal or pointer relocation must fail for their own reason.
`tests/iobuiltins.ty` also checks value snapshots, caller returns and a spawned
worker returning names after its own scopes exit, under both compilers. Review
of block acquisition added a header-size overflow guard in `block_get`; direct
SIZE_MAX requests now fail with the existing OOM diagnostic in every C test leg.

Only this concrete POD-producing caller opts in. General scratch arenas,
compiler allocation routing, finalizer-bearing values and cross-thread shared
backing are unchanged. Broader integrations still need their own evidence.

### Caller-level follow-up: measured gate, no second integration (2026-10-15)

`make allocator-callers` now evaluates existing applications, not just a raw
`list_dir` microbenchmark: self-host compilation, documentation-site generation,
and `tycho-snap` over the real corelib tree plus a labelled 700-file wide fixture
with repository-derived contents. GCC and Clang compare the original builder,
fresh-name native construction, a 512-pointer stack index, the integrated
builder, and a **whole-application** 4 KiB native-block control. Outputs are
removed before every run and must be byte-identical, including the complete
site manifest and verified ZIP. Plain timing/RSS have a warmup plus five
interleaved rounds; calibrated ownership counters and per-directory allocation
traces run separately. Defeating block acquisition accounting must fail.

The local saving survives in the real snapshot path: its wide directory adds
131,072 B of active capacity with either original or fresh-name construction,
81,920 B with smaller native blocks, and 65,536 B with dual-ended construction.
But that is **not** the whole application's memory peak:

| Workload (GCC) | Original peak active capacity B | Integrated peak B |
|---|---:|---:|
| Self-host compiler | 58,767,848 | 58,767,848 |
| Documentation site | 80,638,192 | 80,638,192 |
| Corelib snapshot | 10,370,992 | 10,370,992 |
| Wide snapshot | 5,350,672 | 5,350,672 |

Cumulative reservation likewise does not fall in these applications: later
allocation phases and pool reuse absorb the local saving. Clang reaches the
same conclusion. The ordinary compiler/docs/snapshot walks make respectively
35/5/93 listing calls, with maximum directory widths 13/42/93. RSS/timing do not
establish an end-to-end win; GCC compiler samples also suffer large run-to-run
drift. The earlier retained-listing niche remains demonstrated, **not** a claim
that typical application peaks or latency improved. Smaller blocks are not a
general default recommendation either: the compiler's RSS rises despite lower
logical active capacity.

**Readers were traced through the actual production functions and net shim.**
`bench/allocator/readers.ty` calls HTTP framing and database `recv_line` over a
private POSIX socketpair, with real binary-body/line verification, 64/4096-byte
read limits, clean/short/ambiguous inputs, and native 64/4 KiB arena defaults.
Both Tycho compilers and GCC/Clang native and ASan/UBSan/LSan runs agree. The
fixture's sender owns its input copy until joined; it never retains an FFI
borrow or opens a listener. `--readers-only` reruns this focused part.

- HTTP returns raw bytes **alongside** the parsed request. At the near-cap
  point, 1,048,061 raw bytes coexist with 1,048,000 body bytes. Including string
  storage alignment, their minimum payload capacity is 2,096,088 B. True dual
  cursors correctly reject a 1 MiB buffer; exact-budget dual and ordinary
  presized single-headed storage require the same payload capacity. This replay
  is a checked lower-bound layout, not an optimized-reader timing result, and
  excludes parser temporaries. Neither group can be rewound before its result
  escapes without changing the API/value-lifetime contract.
- Database framing has separate buffer-owner, scratch and returned-line owners.
  Its raw prefix coexists with newly split line/rest strings before rebinding;
  packing them under one owner would violate existing routing. Repeated reads
  already reuse append state. At this measurement `MAX_LINE` was a **soft** cap:
  a 10,000-byte newline-terminated line was accepted with 4096-byte reads but
  refused with 64-byte reads. This reproduced the database finding in
  [the audit](audit-2026-09-25.md). The hard-cap follow-up below fixes that issue;
  it does not combine the distinct owners or turn 8 KiB into a total backing
  budget for a buffered pipeline and its returned values.
- A simpler control already helps these fixtures: small native blocks reduce
  whole-fixture peak arena capacity from 393,216 to 32,768 B for the tiny HTTP
  request, and 196,608 to 62,056 B for the two-line DB exchange with 4096-byte
  reads. Those totals include fixture allocations, not just retained results;
  the sender's native input copy is outside arena accounting. This does not
  license a global block-size switch or a tail-latency claim.

**Decision:** no additional production dual-head caller qualifies. Keep the
existing directory integration scoped; do not invent shared ownership, discard
raw/snapshot values, or add a special reader API just to fit an allocator.
Ordinary presizing/smaller-block controls remain preferable where they win.
Reproducible raw reports, hashes, phase traces, layout rejection rows and limits
are under `build/allocator-callers/run-*/results.json`; the latest summary is
`build/allocator-callers/results.json`. No runtime or language change is made
by this follow-up.

### Peak-driven follow-up: hard framing cap and ordinary byte builders (2026-10-15)

The next work fixes the reproduced boundary defect and follows the actual
application peaks, **not** another dual-head integration.

**Database framing:** `recv_line` validates the first line before allocating its
split results or rebinding the buffer. `MAX_LINE` means 8192 payload bytes,
excluding LF and one optional final CR; a pipelined tail is not part of that
line. An incomplete line may retain one pending CR. Each read is bounded by
remaining payload/framing headroom, so ordinary network accumulation is at most
8194 bytes. A caller may supply a larger already-buffered pipeline; each line
is still checked separately. Oversized input returns the existing `TooLong`
error; once detected, the offending buffered line is not split or removed.
Connection-close/error behavior is unchanged; this does not add an in-band error protocol.

Tests force 64/4096-byte fragments with both compilers, native and sanitizers:
exact and one-byte-over LF/CRLF boundaries, empty lines/EOF, a pending CR,
multibyte input counted as bytes, and a prefilled pipeline larger than the cap.
Removing the pre-split check must admit the one-byte-over line, defeating the
regression oracle. The normal database integration runner also tests valid and
oversized statements followed by QUIT over the actual TCP server.

**Peak attribution:** `make allocator-peaks` snapshots simultaneous owner rows
at the *first* global maximum active backing capacity, with a captured native
stack. Rows are occupied block offsets, including dead/recycled chunks, **not
live values**, per-owner reserved capacity, or independently summed function
peaks. They must sum to global occupied offsets; slack is reported separately.
Known two-owner calibration and defeated accounting checks guard the profiler.
The extra instrumentation is single-threaded, diagnostic and emitted-C-only.
Plain timing/RSS use separate binaries and alternating interleaved runs.

In the initial GCC run (source fingerprint
`ad5c08d78ed79da70893776b7e0533f3d350bb02`), the compiler's 58,767,848 B capacity
peak coincides with `emit___stmt` (20,590,680 occupied B), `emit__program_in`
(11,488,400 B), `driver__compile` (8,752,080 B) and `driver__emit_to`
(8,684,224 B). These are simultaneous rows, not their separate high-waters.
At that stage compiler ownership/code generation was unchanged; the compiler
follow-up below addresses that remaining peak without skipping snapshot copies
or introducing a general allocator rewrite.

Two ordinary caller changes address the other observed peaks:

- **Docs:** append existing `char_at` byte values rather than materializing
  one-byte substrings in six rendering/search helpers (twelve sites). This
  preserves every byte, including NUL/non-ASCII, without a per-byte allocation.
  Bind escaped per-page title/text before appending to the long-lived search
  index so their growth intermediates stay in the loop owner, not the index
  owner. `to_char(s[i])` was tried and rejected: current lowering constructs a
  temporary one-byte string to validate the conversion, so it did not save
  capacity. Native `char_at` already owns the required behavior.
- **ZIP:** build the wire output as `bytes`, not an eight-byte `[int]` per wire
  byte. Reuse the existing cross-call append state. Keep compressed sizes, not
  copies of all compressed data, for the central directory; compute each CRC
  once. Classic count/name/size/offset validation, compression choice, ordering,
  format bytes, input independence and published snapshots remain unchanged.

Initial GCC application capacity results (bytes; input hashes and source
fingerprints are pinned in each raw run, so documentation growth can move them):

| Application | Historical | Simple presizing | Integrated byte/lifetime changes |
|---|---:|---:|---:|
| Docs site | 80,970,632 | 78,414,744 | 15,481,208 |
| Corelib snapshot | 10,371,904 | 7,779,088 | 2,367,936 |
| Wide snapshot | 5,350,672 | 2,774,728 | 1,439,656 |

Docs presizing reserves upper bounds in the two original search helpers only;
ZIP presizing reserves the uncompressed archive upper bound in the original
native int vector (with ordinary fallback when the bound is huge). Neither is
an oracle that knows compressed output size. Whole-application 4 KiB native
blocks and ablations are separate controls. Search-only byte characters reduce
docs capacity to 31,078,768 B; all twelve sites without lifetime staging reach
22,477,680 B. Compact ZIP metadata/CRC alone reaches 10,175,296/5,285,136 B for
corelib/wide: most of the remaining gain comes from the byte builder, not that
metadata change. No packing-specific gain is being attributed to these changes.

Full site manifests and archive hashes must match every control. Python verifies
ZIP CRCs; a differential helper probe checks every byte/prefix and an 8 KiB
binary input, native and ASan/UBSan/LSan. Normal docs tests cover Unicode,
quotes/backslashes and markup; normal ZIP tests cover incompressible binary
growth, unchanged inputs, archive snapshots and the empty writer, alongside
existing format limits/malformed archives. Peak RSS falls in the measured runs;
timings remain machine/workload-specific, not a latency guarantee. Reports and
separate plain/profile logs live under `build/allocator-peaks/run-*/`; the
atomic latest summary is `build/allocator-peaks/results.json`.

**Scope:** no new allocator, arena layout, runtime ABI, language feature,
reference count/GC, ownership exception, mandatory presizing or global block-size
change. Pause further dual-head deployment until another caller qualifies.

### Compiler callers: remove the adapter and recognize the builder (2026-10-15)

`make allocator-compiler` isolates the remaining compiler peak. It constructs
throwaway compiler variants, freezes a common compiler input, and checks emitted
C and diagnostics for six inputs before measuring anything. Diagnostic builds
attribute simultaneous owner offsets and selected operation deltas; plain builds
supply RSS/timing, with warmup and five interleaved, order-reversed rounds. Site
accounting has a 24/16-byte calibration and a reason-specific defeated control.
All owner rows must sum, no owner labels may be lost, and every selected site
must execute (except the deliberately removed adapter).

Two causes qualified, independently and together:

- **Driver adapter:** `emit_to` built five parallel arrays, including a deep
  copy of every AST unit. The unit-adapter operations alone grew their owner's
  occupied offsets by 7,653,656 B in the frozen input. Paths/source/prefixes and
  vector growth account for further driver storage. The emitter now takes the
  existing loader-owned `types.Program` as an ordinary non-`inout` value input.
  Its tables still store independent values through the existing operations;
  no mutation, borrowing annotation or copy-policy exception was added. This
  trades an acyclic internal `emit -> types` dependency for removal of the
  adapter and its parallel-array alignment invariant. There is one existing
  production caller. Package order, prefixes, f-string diagnostic file/text,
  and corelib identification are unchanged in the emitted results.
- **Statement builder:** two unrelated match branches used the name `out9`.
  The conservative name-based accumulator scan therefore disabled reuse in the
  append-lowering formatter. Naming that builder `append_code` expresses its
  distinct role and reuses the existing native append implementation. The
  completed branch's cumulative owner-offset increments fall from 19,436,672
  to 1,357,312 B in this input. These are occupied-offset changes, including
  rounding/dead storage, **not** semantic live or payload-copy byte counts. No
  eligibility rule was broadened; other shadowed-name false negatives remain
  conservative. This is not a general binding-analysis rewrite.

Controlled results (capacity bytes; GCC and Clang agree exactly):

| Compiler variant | Peak active arena capacity B |
|---|---:|
| Historical caller shapes | 59,671,224 |
| Presize the five adapter vectors | 59,671,224 |
| Historical callers, 4 KiB native blocks | 56,218,024 |
| Distinctly named append builder only | 54,297,272 |
| Stage fragments and use standard `arrays.join` only | 54,297,272 |
| Read loaded Program directly only | 49,068,512 |
| Program plus standard fragment join | 43,694,560 |
| **Integrated: Program plus named builder** | **43,694,560** |

The integrated reduction is about **27%**. Median plain RSS falls from
70,028 to 52,348 KiB (GCC), and 69,456 to 51,860 KiB (Clang), about **25%**.
The frozen-input baseline is not the earlier 58,767,848 B ordinary-path run;
source paths/layout and pool reuse can change capacity. These comparisons keep
one input fixed within each matrix. Both matrices produce the same compiler C
hash `744f365b9e9465432cf14ec96ae51964ac8072edf432bd8bb05ee42b13254683`.
Performance source fingerprint: `dda8d567070a35ca038707f0991718b01ecfef8e`;
later report metadata/docs do not imply a new performance measurement.

The standard-library fragment staging control ties the capacity result but
adds a fragment vector, stored fragment copies and another dependency; it is
not needed over the existing builder. Adapter presizing does not remove AST
copies. Smaller blocks increase RSS here. Function-body results already use
per-iteration scratch: duplicating that staging is not a new optimization.
No demonstrated latency gain: integrated medians are 235 vs 228 ms (GCC),
149 vs 146 ms (Clang), with overlapping/noisy samples. Memory wins are not a
speed or tail-latency guarantee, nor OS-release or cumulative-reservation claims.

At the new first capacity maximum the largest occupied owner rows are
`emit__program_in` (11,485,480 B), `driver__compile` (8,754,128 B), and
`emit___fuse_names_e` (6,411,544 B). These are simultaneous offsets, not live
values. Further allocator work remains gated, rather than following that stack
with another unmeasured rewrite.

`make compiler-storage` (also in ordinary tests) deep-snapshots a loaded Program,
emits it twice, checks input equality and byte-identical output, under both
compilers, for an append/snapshot fixture and the compiler itself. Frozen-input
C identities, the self-host fixpoint and separate GCC/Clang ASan/UBSan/LSan
compiler runs protect the integration. The opt-in benchmark reconstructs the
historical caller shapes by default; `--before TREE` can pin an actual prior
source tree. Raw artifacts are in `build/compiler-peak/run-*/`; publication of
`build/compiler-peak/results.json` is atomic. Profiling needs POSIX `execinfo`;
no Windows or 32-bit sanitizer/performance result is implied.

**Handoff verification:** GCC/Clang matrices, snapshot/repeated-emission tests,
fixpoint, sanitizers and the 1495-verdict parser checks passed. The last bounded
four-core CI run passed the ordinary, platform, corelib, apps and remaining
functional lanes, plus 200 rejection and 150 leak fuzz cases. It is **not CI
GREEN**: all 200 main differential-fuzz cases timed out compiling, leaving zero
comparisons under the default oversized fuzz worker pool. Further runs were
stopped at the user's request. Bound `FUZZ_JOBS` explicitly on a shared host;
`CI_SERIAL=1` also bounds that pool. No performance guarantee follows from
these resource-constrained verification runs.

## Non-goals

General reference type, user borrowing/annotations, per-object refcount/GC, per-container specials, compile-time execution. One general place rule or nothing.
