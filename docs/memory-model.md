# hierc0 memory-model codegen — closing the perf gap to the C compiler

## Where we are

hierc0 self-hosts (Stage 4 fixpoint, `make fixpoint` green) but emits
**naive malloc/leak C**: every allocation helper in `preamble()` and the
generated families uses bare `malloc` with no arena and no free —
`sc`/`i2s`/`f2s`/`substr`/`hi_input`/`hi_chr`/`hi_read_all`/`hbox`,
`Arr_<T>_from`, `Map_<…>_cap`, the per-struct deep-copy mallocs. The program
leaks every heap byte it ever allocates. Correct output, unbounded RSS.

The C compiler (`src/hierc.c`) instead threads an arena hierarchy and frees
per scope. This doc stages hierc0 toward that model. **The fixpoint does NOT
require matching the C compiler's emitted C** — only B≡C (hierc0 reproduces
itself) and B matching the C compiler's program *output*. So hierc0 may use a
*coarser* arena discipline (per-function, not per-block) and still pass; we
refine toward per-block later.

## Ground truth — the C compiler's arena model

From emitted C (verified):

```c
int main(void) {
    Arena _root = arena_new(0);   /* root arena */
    h_main(&_root);
    arena_free(&_root);
}
long h_sc(Arena *_parent, long h_a, long h_b) {
    Arena _scope = arena_child(_parent);
    { long _ret = (h_a + h_b); arena_free(&_scope); return _ret; }
}
```

Rules:
1. `main()` wrapper creates `_root = arena_new(0)`, calls `h_main(&_root)`, frees `_root`.
2. Every fn gains a leading `Arena *_parent`; opens `Arena _scope = arena_child(_parent)`.
3. Heap allocations take an arena arg (`hier_str_concat(Arena*, …)` etc.).
4. **Return-slot**: a returned heap value must be built/copied into `_parent`
   (which outlives `_scope`); the body wraps every return as
   `{ T _ret = <expr in _parent>; arena_free(&_scope); return _ret; }`.
   Void/scalar returns just free `_scope` first.

Runtime API (`runtime/hier_rt.c`, shared, arena-based): `arena_new`,
`arena_child`, `arena_alloc`, `arena_reset`, `arena_free`, and arena-taking
`hier_str_*` etc. hierc0 can either embed the shared runtime or keep its own
helpers but give them an `Arena*` first param. (Decision in MM-1.)

## Soundness of a MIXED model (why type-by-type works)

`arena_free(&_scope)` only reclaims memory that was `arena_alloc`'d into
`_scope`. Heap that is still `malloc`'d (a not-yet-migrated type) is untouched
by the free — it just leaks, exactly as today. So migrating ONE type family
onto arenas while the rest stay malloc is sound: no use-after-free, the
migrated type stops leaking, the rest leak as before. This is what makes the
campaign incremental.

The only cross-cutting, irreducible piece is the **threading spine** (MM-1):
once any user fn takes `Arena *_parent`, every caller must pass one and every
allocation in that fn must route an arena — you cannot half-thread it.

## REVISED first increment (do this BEFORE the arena spine)

Deeper analysis (2026-06-01) shows the arena spine is NOT a sound *small*
first step: freeing `_scope` is only safe if nothing live points into it, but
a `_scope`-allocated string stored into a still-malloc'd container (array
field, struct field) that escapes would dangle — so "strings on arena"
entangles with escape analysis from day one. Not a clean increment.

The genuinely self-contained, high-leverage first win is the **string-append
accumulator** — the C compiler has it (`is_self_append` → `hier_str_append`),
hierc0 LACKS it. Today hierc0 emits `out = out + x` as `out = sc(out, x)`:
O(n²) time and a fresh leaked buffer every step. This is hierc0's OWN dominant
cost (its codegen is one giant `out = out + …` string build). Fixing it is
pure malloc/realloc — **no arena threading needed** — yet it converts O(n²)→
O(n) and collapses N leaked buffers into one growing buffer. Memory + perf win.

**Soundness prerequisite (verified by reading `gen_rhs`):** hierc0 deep-copies
arrays/structs/maps/tuples/soa on a place-bind (`b := x`) but does NOT copy
strings (`b := s` → `char* b = s`, pointer share — safe today only because
strings are immutable). In-place append MUTATES the buffer, so a snapshot
`b := acc` must first deep-copy. Therefore this increment MUST also add a
string case to `gen_rhs`: `is str and is_place(e)` → `scopy(code)`. (Identical
output — strings immutable — so fixpoint stays green; it just unblocks
in-place mutation. Same soundness pattern as the map accumulator, Stage 3I.)

**Implementation sketch (all in `compiler/hierc0.hi`):**
1. `preamble()`: add `scopy(const char*)` (heap dup) and
   `hi_append(char** s, long* len, long* cap, const char* e)` (realloc-grow,
   doubling, keeps NUL terminator so plain reads still work).
2. `gen_rhs`: `if ty == "str" and is_place(e): return "scopy(" + code + ")"`.
3. New `sacc_scan(body) -> [string]`: recursively collect every local `n`
   with a stmt `SAssign(n, <+chain whose leftmost leaf is EVar(n)>)`. Must
   recurse into SIf/SWhile/SForRange/SMatch bodies (mirror `collect_stmt`).
   Helper `sacc_target(e)`: leftmost-descend an all-`+` EBin chain, return the
   leaf var name or "".
4. `Ctx` gains `saccums: [string]`; `gen_func` computes it via `sacc_scan(f.body)`
   and threads it (like the new `vparams`).
5. SDecl / STypedDecl of an accumulator string: heap-ify the init
   (`char* n = scopy(<init>);`) + sidecars `long _len_n = strlen(n);
   long _cap_n = _len_n + 1;`.
6. SAssign to an accumulator: if RHS is `n + …` → emit one
   `hi_append(&n, &_len_n, &_cap_n, <piece>)` per appended piece (left→right);
   else (plain reassign `n = expr`) RESYNC: `n = scopy(<expr>); _len_n =
   strlen(n); _cap_n = _len_n + 1;` (keeps sidecars consistent across mixed use).
7. Verify: `make fixpoint` (oracle for soundness — a snapshot bug diverges
   B from the C compiler), `make test` 45/45, and `bench/peakrss` time+RSS on
   a string-build workload (e.g. `examples/accumulate.hi` / `accumulate_big`)
   showing the O(n²)→O(n) drop. Watch `s = s + f(s)` (RHS reads s): sequential
   appends differ from full-concat pre-eval — no current fixture does it, but
   if fixpoint diverges, pre-evaluate pieces into temps first.

Then resume the arena spine (MM-1 below) for the heap types append can't cover.

## Stages

- **MM-1 — threading spine + strings on arena (the irreducible first slice).**
  - `main` wrapper + `_root`; every fn gains `Arena *_parent` + `_scope`.
  - Every user-call site passes `&_scope` as the first arg.
  - String helpers (`sc`/`i2s`/`f2s`/`substr`/`hi_*`) gain an `Arena*` first
    param and `arena_alloc` instead of `malloc`; every string-producing call
    threads `&_scope`.
  - Return-slot for string returns (copy into `_parent`); `_scope` freed on
    every exit path. Arrays/maps/structs/tuples/boxes STILL malloc (leak) —
    sound per the mixed-model argument.
  - **Verify:** `make fixpoint` (B≡C + matches C compiler), `make test` 45/45,
    and an ASan-leak / RSS delta on a string-heavy input showing strings freed.

- **MM-2 — arrays on arena.** `Arr_<T>_from`/`_copy`/`_push` take `Arena*`;
  array-producing sites thread it; array return-slot.
- **MM-3 — maps on arena.** `Map_<…>_cap`/grow/`_set` take `Arena*`.
- **MM-4 — structs / tuples / boxes (`hbox`) on arena.** Deep-copy mallocs → arena.
  After MM-4 nothing leaks under per-function scoping.
- **MM-5 — per-block arenas.** Open/free `_scope` per if/loop block (matching
  block-scoped = arena-scoped), so transient block garbage is freed eagerly,
  not held to function exit.
- **MM-6+ — optimizations (the hard, output-invisible codegen).**
  move-on-last-use (FBIP reuse), match-arm borrow, construction moves,
  transient placement, return-slot elision. Each is invisible to program
  output, so guarded purely by fixpoint + RSS/throughput benchmarks.

## Validation per stage

- `make fixpoint` green (B≡C; B matches the C compiler's golden output).
- `make test` 45/45 (ASan/UBSan clean — the self-emitted C runs under sanitizers).
- Memory delta: ASan leak report and/or `bench/peakrss` on a workload that
  exercises the migrated type — each stage must show that type's bytes now
  freed. No silent "looks done" — quote the before/after.

## Risk

High blast radius on the self-hosting compiler: MM-1 touches every emitted
signature, call site, and return. Mitigation: land MM-1 behind a full fixpoint
run before commit; if the self-emission breaks, ASan on the self-emitted C is
the debugger (it caught the Stage-4 return-promotion bugs). Keep each stage its
own commit with the verification quoted in the message.
