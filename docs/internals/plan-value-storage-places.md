# Plan: ownership-free storage optimization + values vs places

Status: ACTIVE. Direction, not a promise of dates. No surface change without owner sign-off (`surface.lock`, `ROADMAP.md`).

Principle (preserved absolutely):

> Every value behaves as though it is independent. The compiler is free to share storage, reuse allocations, and move data, provided the program cannot observe a difference.

Invariants: assignments keep independent value semantics; no user-visible aliases to mutable storage; no expressible dangling references; no mandatory GC or per-object refcount; no new lifetime annotations; deterministic cleanup at or before scope exit. Peak memory / reclamation timing are implementation properties, not promises. Refused by decision: stored references (`docs/rfc/limited-references-spike.md`), which dismantle the deep-copy thread boundary and no-escape arena model.

## Phase 1 — generalized storage reuse (Medium / Low). Do first.

Generalize `x = x + piece` / `x += piece` uniqueness+liveness beyond `Decl`-local accumulators: cover the `inout` socket site plus transient rebuilds, as IR liveness not more spine patterns. Fall back to `str_concat` whenever the proof is insufficient.
Gate: single-build bump 518 → ~32 KiB band on an 8 KB line received in 64 B chunks; `tests/accumulator_scope.sh`, `make bench`, `tools/tycho-db/run.sh` green.

**Shipped: named bindings.** Append state lives with the binding and is
forwarded through a hidden pointer on generated `inout string`/`bytes` calls,
including recursive/nested helpers, bindings introduced by tuples, value-if and
match patterns, and instantiated generics. Growth uses the binding's owning
arena; state tracks buffer identity and capacity, length comes from the string
header. Rebinding cannot reuse stale capacity. Fresh state copies incoming
storage once to protect borrowed snapshots; value parameters start fresh. 128
separate calls bump 32 KiB (the pure-concat control: ~518–700 KiB). This
changes only the private generated C convention: no stored reference,
refcount, COW flag, string-layout or FFI change. Pinned by
`tests/inout_string.ty` (semantics) and `tests/inout_append.sh` (allocation
bounds, with a pure-concat control that must exceed them).

**Open:** general IR liveness, and state persistence for projections.
`&obj.field` / `&items[i]` have no persistent binding metadata and pass NULL,
so separate calls on them copy each time. Caching there without tracking
replacement, container relocation and recycling would be unsound.

## Phase 2 — field-sensitive optimization (High / Medium).

Avoid copying whole aggregates when only part survives: move/reuse individual field buffers of dead sources, skip untouched fields on nested update/return construction. Internal only, no partial-move surface.
Gate: targeted fixtures (nested update copies field count, not aggregate size); `make ci` green.

**Shipped for these shapes:** uniquely read, non-overlapping fields reuse
through nested concrete, generic and package-qualified constructors. A fresh
constructor declaration followed by proven straight-line field replacements and
a direct field return places only the final version in `_parent`. Three-clause
loops (zero origin, unit step, a literal or proven-unchanged integer bound,
non-borrowing body) and `if/else` whose both flat arms replace the selected
field likewise place only the final replacement. Everything else keeps
copying: skipped-update paths, nested flow, early exits, unproven or borrowed
bounds, counter mutation, whole-record rebinding, mutable borrows, other
control flow, and calls with unproven result-arena behavior. Pinned by
`tests/field_rebuild.sh` and `tests/return_projection.sh`. Copy and
occupied-offset reductions are not universal capacity or RSS gains: a small
isolated call may need one more local arena block.

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

No Stage B work until all five are green and pinned. Existing partial coverage (`tests/value_semantics.ty`, `tests/borrow_alias.ty`, `tests/elem_recycle.ty`, `tests/projections.ty`, `tests/place_store_order.sh`) does not substitute for this named set.


### Store-owner discipline (audited 2026-10-18)

Every heap store lands its bytes in the owning arena and never aliases dead or
call-temporary storage: struct fields, tuple elements and map put/insert paths
materialize the RHS in the owner arena, copy iff the RHS aliases, and retain the
old value to scope exit by arena design rather than recycling it. `delete`
through a subscript is refused (delete is map-only). Pinned by
`tests/store_discipline.ty` and the `tests/subscript.ty` legs (projection
growth, writeback, place-to-place, compound assign, map-rooted subscript).

`compiler/types/tcheck.ty@_root_ident` (Field/Index only) gates push, pop,
reserve and index-assign, so all four refuse Call/CallVal roots (pinned by four
`tests/reject/*_subscript_*` fixtures). One deliberate conservatism: the
emitter's `_place` does not see through subscript calls, so it copies where it
could alias. That is the safe direction (a missed move, never a dangling
share); changing it is optimization work with its own gate.

## Phase 5 — values vs places experiment (gated).

The trie combines two limitations, not one:

| Limitation | Root cause | Solution |
|---|---|---|
| Nested update copies too much | No general reuse / place-aware update (`bench/trie/trie.ty:8-24` recurses by `&t.kids[c]`) | Phases 1–2 + Stage B below |
| Parents cannot share a node | Independent values provide no shared identity | Identity within an owning aggregate |

Solving only the first makes recursive value trees cheap without making shared graphs easier; identity syntax alone prettifies the same pool (`bench/trie/trie_pool.ty:3-27`: cursors, root `0`, load-bearing `reserve` `bench/trie/RESULTS.md:28-30`, ABA via `core:pool` `corelib/pool/pool.ty:22-25`). Runner-up `tools/tycho-ed/buf/buf.ty:278-280` → `[int]` gap is performance-only (RSS identical 95.7 MB; `git show ba952872:docs/internals/editor-churn-2026-08-12.md`).

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

From [demofox 2026-10-03](https://blog.demofox.org/2026/10/03/the-double-headed-stack-allocator/) and [VMA linear algorithm](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/custom_memory_pools.html#linear_algorithm). One fixed buffer, two bump cursors growing toward each other; VMA generalizes it as free-at-once / LIFO stack / double-stack / ring-buffer modes of one linear pool, with double-stack and ring valid only single-block (`maxBlockCount` 1).

Decision: no language surface. Tycho has N nested lexical scopes on a growing block chain, not two sibling LIFO owners sharing one fixed buffer; a user-visible double-headed arena would reintroduce manual budget sizing and free ordering. Applicable scenarios are tested now, not deferred to post-1.0. Targeted runtime-internal optimizations may proceed on measured evidence; no general allocator replacement is approved. Candidate applications:

- Offset-from-the-top packing: payloads from one end, index/offset table from the other, in a single `arena_alloc` — a container/packet layout optimization in the indexed-dict family, not a new arena kind.
- Bounded single-block arena semantics: two cursors meeting is a clean OOM abort. Does not compose with the growing chained arena + thread-local block pool (`runtime/tycho_rt.c`, `docs/memory-model.md`).
- Threading: shared atomic bump cursors thrash; per-thread arenas + thread-local pool stay, as already designed. Affine `task`/handle/channel destructors also stay out — double-headed use is POD/transient bytes only.


### Rewrite readiness: scope, baseline and safety contract

This work does not authorize a language feature or replace the demand gate.
Experiments stay runtime-internal and preserve growing arenas, N nested lexical
owners, caller-directed return and `inout` allocation, and thread-local reuse.
No bounded-budget caller has been demonstrated.

**Real workload:** `make allocator-baseline` builds uninstrumented and profiled
copies of the self-hosted compiler and emits `compiler/main.ty`; every run must
emit byte-identical C. Instrumentation patches only an emitted-C copy,
calibrates against known totals, and must reject deliberately defeated
accounting. Results go to `build/allocator-baseline/`. Measured 2026-10-15
(Linux x86-64, `-O2`): 210–218 ms, ~67 MiB peak RSS, active capacity peak
58.8 MB with 17.2 MiB of tail slack, 99.9% block-pool reuse. Slack excludes
headers, recycle holes and pooled blocks, so this shows packing is worth
investigating, not how much is recoverable.

**Acceptance:** reduce active tail slack/peak capacity or fresh block
reservation on this workload, with unchanged emitted C and no increased
copy/allocation growth. Report timing/RSS distributions separately, not as a
noisy timing gate. The current allocator stays the comparison.

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

The contract gate defeats ownership checking and rewind in separate runtime
copies and requires each to fail for its own reason (it caught an ILP32
HBlock-header misalignment, since fixed).

Before sharing physical backing, preserve **logical** ownership: `arena_owns`
recognizes a whole block, so linking one block into two arenas would let one
recycle the other's live bytes. Ranges/ownership, recycle bookkeeping, reset
ordering, finalizers, alignment, overflow and growth must be specified and
checked before any such sharing.

### Isolated shared-backing experiment — not promoted (2026-10-15)

`make allocator-packing` (`bench/allocator/compare.py`, `bench/allocator/packed.c`)
packs 4 KiB logical views into 64 KiB slabs, patching only throwaway emitted C.
Each arena owns only its views (`arena_owns` checks the view); backing-block
references are atomic because a task root can be released on the waiting
thread. Contracts (`bench/allocator/packed_contract.c`) pass native,
sanitizers and ILP32.

**Decision: keep the current allocator.** Logical peak capacity matched the
plain 4 KiB-block control exactly (56.1 MB for both), so packing added nothing,
and it reserved more (77.3 vs 71.3 MiB) and ran slower (GCC 235 vs 233 ms,
default 219). A `TYCHO_BLOCK` sweep (4–32 KiB) lowered RSS nowhere. Do not
rescue it without new measured demand.

### True dual-headed scenarios — a targeted opportunity (2026-10-15)

`make allocator-dual` tests a bounded two-ended buffer (`bench/allocator/dual.h`):
lower-end retained output, upper-end scratch or index, owner-bound strictly-LIFO
upper marks, lower-end reset that preserves the upper end, and failed
allocations that leave both cursors unchanged. It runs seven scenarios × two
schedules × four budgets against separate growing arenas, a single arena,
equal fixed partitions and 4 KiB-block arenas; all 280 configurations pass
native, sanitizers and ILP32 under GCC and Clang, and three defeated guards
(collision, order, rewind) must fail for their named reason. POD only: no
cross-thread transfer or finalizer integration.

**It wins only on anti-correlated demand.** Scratch shrinking 48 → 4 KiB while
output grows 4 → 48 KiB, 64 KiB budget, 32 held results: 2.10 MB reserved
against 4.20 MB for default arenas and 3.16 MB for 4 KiB blocks; no fixed split
fits both 48 KiB peaks. Elsewhere small native arenas win or tie (held-result
index: 0.40 MB vs 1.05 MB dual), and an oversized or idle dual buffer erases
the gain. Timing is mixed.

**Decision:** a local optimization candidate for bounded scratch/output phases
with anti-correlated demand, nothing more. Prefer small arenas or presizing
where they win. Before changing a production caller, measure its combined bound
and lifetime pattern against both native controls. An input cap alone
(`MAX_LINE`, `MAX_REQUEST`) does not bound result/scratch storage. No blanket
compiler or global-arena switch.

### Production integration: directory listing (2026-10-15)

`tycho_list_dir` in `runtime/tycho_rt.c` is the one integration. Enumeration is
single-pass with an unknown count (a second `readdir` pass would not preserve
the stream if the directory changes). Length-headered names grow upward in the
current arena's unused tail and a temporary pointer index grows downward, its
window expanding geometrically. Publication reverses/compacts the index and
commits **only used bytes** through ordinary `arena_alloc`; if that reuses a
different recycled chunk, names and index are copied and pointers relocated.
No arena allocation may interleave with the builder before publication. Both
groups have one owner, so whole-block `arena_owns` stays correct: no shared
backing, new Arena field, refcount or ABI change.

If the tail fills, the packed prefix is published and the *same colliding
entry* plus the rest go through normal array growth: no size limit, dropped
entry or new failure mode. Dot-entry filtering, order, partial-read behavior and
handle closure are unchanged.

700 64-byte names, 32 listings held: 2.10 MB reserved against 4.20 MB for every
native alternative (original, fresh-name, 512-pointer stack index); bump bytes
7.9 MB vs 16.4 MB original and 9.3 MB fresh-name, so not all of the saving is
the packing. Larger directories spill and match the fresh-name control.

**Pinned:** `make dir-packing` (`bench/allocator/directory.py`, in `make test`)
checks tiny blocks, collision/overlapping compaction, long/Unicode/hidden
names, fallback, exact order, snapshots, and recycled publication followed by
overwriting the abandoned staging region, native and sanitizers; mutants that
remove index reversal or pointer relocation must fail for their own reason.
`tests/iobuiltins.ty` checks snapshots, caller returns and a spawned worker
returning names. `block_get` guards header-size overflow. Only this POD caller
opts in.

### Caller-level follow-up: measured gate, no second integration (2026-10-15)

`make allocator-callers` runs real applications (self-host compile, docs-site
generation, `tycho-snap` over corelib and a 700-file wide fixture) with
byte-identical outputs across the original builder, fresh-name construction, a
stack index, the integrated builder and a whole-application 4 KiB-block control.
The directory saving survives locally (wide directory: 65,536 B active capacity
vs 131,072 B original) but **no application peak moves**: self-host
58,767,848 B, docs 80,638,192 B, snapshots 10,370,992 / 5,350,672 B, identical
before and after. Smaller blocks are not a general default either: the
compiler's RSS rises.

`bench/allocator/readers.ty` (`--readers-only`) traces the production HTTP and
database readers over a socketpair. HTTP returns raw bytes alongside the parsed
request (near the cap 1,048,061 raw + 1,048,000 body bytes coexist), so dual
cursors need the same payload capacity as presized single-headed storage, and
neither group can be rewound before its result escapes. Database framing has
distinct buffer, scratch and returned-line owners that cannot share one owner
without violating routing.

**Decision:** no second production dual-head caller qualifies. Do not invent
shared ownership, discard raw/snapshot values, or add a reader API to fit an
allocator. Results: `build/allocator-callers/results.json`.

### Peak-driven follow-up: hard framing cap and ordinary byte builders (2026-10-15)

**Database framing:** `recv_line` (`tools/tycho-db/srv/srv.ty`) validates the
first line before splitting or rebinding the buffer. `MAX_LINE` is 8192 payload
bytes, excluding LF and one optional final CR; a pipelined tail is not part of
the line, and each buffered line is checked separately. Each read is bounded by
remaining headroom, so network accumulation is at most 8194 bytes. Oversize
returns `TooLong`; close/error behavior is unchanged. Tests force 64/4096-byte
fragments over exact and one-byte-over LF/CRLF boundaries, empty lines/EOF, a
pending CR, multibyte input and an over-cap pipeline; removing the pre-split
check must admit the one-byte-over line.

**Peak attribution:** `make allocator-peaks` snapshots simultaneous owner rows
at the first global maximum of active backing capacity, with a native stack.
Rows are occupied block offsets (including dead/recycled chunks), not live
values, and must sum to the global total; a two-owner calibration and defeated
accounting guard the profiler. Diagnostic and emitted-C-only.

Two ordinary caller changes address the docs-site and ZIP peaks:

- **Docs:** append `char_at` byte values instead of materializing one-byte
  substrings (twelve sites), and bind escaped per-page title/text before
  appending to the long-lived search index so intermediates stay in the loop
  owner. `to_char(s[i])` does not help: its lowering builds a temporary
  one-byte string to validate.
- **ZIP:** build the output as `bytes`, not an eight-byte `[int]` per byte;
  keep compressed sizes rather than copies of compressed data for the central
  directory; compute each CRC once. Format and validation are unchanged.

| Application (GCC, capacity B) | Before | Simple presizing | After |
|---|---:|---:|---:|
| Docs site | 80,970,632 | 78,414,744 | 15,481,208 |
| Corelib snapshot | 10,371,904 | 7,779,088 | 2,367,936 |
| Wide snapshot | 5,350,672 | 2,774,728 | 1,439,656 |

Most of the ZIP gain is the byte builder, not the metadata change. Site
manifests and archive hashes match every control. Results:
`build/allocator-peaks/results.json`. Scope: no new allocator, arena layout,
ABI, language feature, refcount/GC, mandatory presizing or global block-size
change.

### Compiler callers: remove the adapter and recognize the builder (2026-10-15)

`make allocator-compiler` builds throwaway compiler variants over a frozen
input, checks emitted C and diagnostics for six inputs first, then measures
owner offsets (diagnostic builds) and RSS/timing (plain builds). Two causes
qualified:

- **Driver adapter:** `emit_to` built five parallel arrays, including a deep
  copy of every AST unit (7.65 MB of owner offsets). The emitter now takes the
  loader-owned `types.Program` as an ordinary value input
  (`compiler/driver/driver.ty@emit_to`), trading an acyclic `emit -> types`
  dependency for removing the adapter and its parallel-array invariant.
- **Statement builder:** two unrelated match branches both named `out9`, and
  the conservative name-based accumulator scan disabled reuse in the
  append-lowering formatter. That builder is now `append_code`
  (`compiler/emit/emit_stmt.ty@append_code`): 19.4 MB → 1.36 MB owner offsets.
  Other shadowed-name false negatives stay conservative.

| Compiler variant | Peak active arena capacity B |
|---|---:|
| Before | 59,671,224 |
| Presize the five adapter vectors | 59,671,224 |
| Before, 4 KiB native blocks | 56,218,024 |
| Named append builder only | 54,297,272 |
| Read loaded Program directly only | 49,068,512 |
| **Both** | **43,694,560** |

About 27% less capacity and 25% less median RSS (70,028 → 52,348 KiB GCC;
69,456 → 51,860 KiB Clang), GCC and Clang agreeing exactly; no latency gain.
Staging fragments through `arrays.join` ties the capacity but adds copies and a
dependency. The largest remaining owner rows at the new peak are
`emit__program_in`, `driver__compile` and `emit___fuse_names_e`; further
allocator work stays gated on measurement.

`make compiler-storage` (`tests/compiler_storage.sh`, in `make test`)
deep-snapshots a loaded Program, emits it twice, and checks input equality and
byte-identical output for an append/snapshot fixture and the compiler itself.
Profiling needs POSIX `execinfo`.

## Non-goals

General reference type, user borrowing/annotations, per-object refcount/GC, per-container specials, compile-time execution. One general place rule or nothing.
