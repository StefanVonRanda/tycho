# hierc0 memory-model codegen — closing the perf gap to the C compiler

> **STATUS (complete): the migration documented here is DONE — MM-0 … MM-7f.**
> hierc0 now emits the same value-semantic implicit-arena C the reference compiler
> does, with **no known memory gap** and full feature parity; the last residual
> (heap-payload option arrays, `[Option(str)]`) was closed in MM-7f. The
> "Starting point" section just below describes where this doc *began* — the naive
> malloc/leak codegen at the fixpoint — not the current state.

## Starting point (historical)

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

**✅ DONE (string accumulator).** Implemented in `compiler/hierc0.hi`:
`scopy`/`hi_append` runtime, `gen_rhs` string copy-on-place-bind (snapshot
safety), a `sacc_scan` pass (threaded via `Ctx.saccums`), heap-ify+sidecars at
SDecl/STypedDecl, in-place `hi_append` / sidecar-resync at SAssign. Two bugs
found+fixed via the fixpoint+ASan (both env-desync in the scan, not the
accumulator: SDecl pushed the name before computing `type_of` → parallel
arrays desynced → `type_of` substr crash; SMatch typed arm binds `""` instead
of the real payload types → same crash). Verified: `make fixpoint` B≡C green,
`make test` 57/57 (`tests/string_accum.hi` covers snapshot independence +
resync), and a 30 000-iter string build dropped from **4638 MB / 1937 ms to
10 MB / 3 ms** (≈464× memory, ≈645× time). Arena spine (below) is next.

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
   B from the C compiler), `make test` 57/57, and `bench/peakrss` time+RSS on
   a string-build workload (e.g. `examples/accumulate.hi` / `accumulate_big`)
   showing the O(n²)→O(n) drop. Watch `s = s + f(s)` (RHS reads s): sequential
   appends differ from full-concat pre-eval — no current fixture does it, but
   if fixpoint diverges, pre-evaluate pieces into temps first.

Then resume the arena spine (MM-1 below) for the heap types append can't cover.

## Stages

### Arena spine — sub-step log

- **✅ S1 (commit cc9aa27): threading scaffolding, sound, frees nothing.**
  Arena runtime in the preamble; every fn takes `Arena* _parent` + opens
  `Arena _scope = arena_child(_parent)`; main builds `_root`; user calls thread
  `&_scope`; return-slot `{ T _ret = e; arena_free(&_scope); return _ret; }`
  (T from `Ctx.rettype`). Allocators still malloc → `arena_free` reclaims
  nothing → no UAF. fixpoint B≡C + 57/57. This is the *threading* spine; the
  sub-steps below fill the arenas.

- **S2 (next, the large body): redirect allocations onto arenas.** Confirmed
  there is NO sound partial here — a `_scope` string stored into a still-malloc'd
  container that outlives the scope dangles, so strings and their containers must
  migrate together. Required, all at once:
  1. Thread an `owner` arena param through `gen_expr`/`gen_rhs` (default
     `&_scope`; mechanical but touches every call site).
  2. `owner_arena_of(var)`: param → `_parent` (caller-owned/borrowed), local →
     `&_scope` (one scope per fn until MM-5 adds per-block).
  3. Every allocator (`sc`/`i2s`/`f2s`/`substr`/`scopy`/`hi_*`/`hbox`,
     `Arr_*_from/copy/push`, `Map_*`, struct copy, tuple) takes `Arena*` +
     `arena_alloc`.
  4. **Arena-aware recursive copies**: container copy must re-allocate NESTED
     heap (strings, inner arrays/maps/structs) into the target arena — today's
     shallow pointer-copy is UAF once memory is freed.
  5. Store sites (`push`, `map_set`, index/field assign) build the value into
     `owner_arena_of(container)`; returns build into `_parent`.
  Verify each increment with fixpoint + ASan on the self-emitted C (the oracle
  that caught the S1-era and accumulator bugs). Skip the move-on-last-use /
  return-elision *optimizations* (always-copy is sound, just slower) — those are
  MM-6.

- **✅ S2.1 (commit 8407340): transient arena for print args.** owner-plumbing
  established: string allocators take a leading `Arena*` (NULL→malloc via
  `amem`, the safe fallback for un-migrated contexts); `Ctx.owner` (default `"0"`)
  + `with_owner()`; `print(e)` builds temporaries in a per-statement `_t` arena
  freed after `fputs`. Sound because print args don't escape. Internal runtime
  callers that stay malloc (hi_split substrs, struct-copy/map-key `sc`) pass `0`;
  the accumulator's `scopy` stays `0` (realloc-grown buffer must be malloc).
  fixpoint B≡C + 57/57 + ASan clean; print-heavy 2M-iter loop 421MB→10MB (~42×).

- **S2.2 (next, intricate/high-risk): returns→`_parent` + exhaustive store-copy.**
  FINDING: no more cleanly-safe flips remain — print was the only context whose
  values provably don't escape. Returns escape: `return P("a"+s, n)` builds a
  string into `_parent` that the caller can store long-term → UAF. So returns-flip
  is sound ONLY together with forcing EVERY container-store to copy strings into
  the container's arena (= `0`/malloc while containers are still malloc), at all
  of: `push` value, `map_set`/map-literal value, `gen_store_args`
  (struct/enum/tuple fields — needs per-field TYPE to know which args are str),
  `hbox` payloads (Some/Ok/Err), `SFieldAssign`/index-assign rhs. Rule: at a
  string store, emit `scopy(0, <value built with owner 0>)` → stored strings are
  malloc/immortal regardless of the ambient owner, making returns→`_parent` safe.
  One missed site = UAF in the self-hosting compiler, so this needs exhaustive
  per-site coverage + ASan on the self-compile + an adversarial probe
  (return-string-then-store-into-param-array). Modest benefit (frees returned
  non-accumulator string churn; accumulators already minimize the dominant cost),
  so weigh against the container migrations (MM-2..4) which are the bigger wins
  and the same coupled-difficulty class.

- **✅ S2.2 (a850736): sound returns + store-copy foundation.** ECall passes
  `ctx.owner` as the callee's parent (a returned tail-call lands in `_parent`,
  not the freed `_scope` — fixed a real self-compile UAF); callee guards a NULL
  parent (`_scope = _parent ? arena_child(_parent) : arena_new(0)`); construction
  store-sites force owner `0` so a container built inside a return never holds a
  freed-scope pointer; returns build in `_parent`. Memory-neutral; enables S2.3.

- **✅ S2.3 (29c881a): locals-flip — STRINGS FULLY ON ARENA.** Default owner
  `"0"`→`"&_scope"`: local/transient string allocations live in the scope arena,
  freed at function return. Store-copy completed at `push` (array+soa) and
  `SFieldAssign`. self-compile ASan-clean + fixpoint B≡C + 57/57. Batch workload
  733MB→10MB (~73×). **String migration done** (transient/local/returned on
  arena; stored strings malloc-copied; accumulator buffers stay malloc by
  necessity). Wins so far: accumulator 464×, print 42×, locals 73×.

  Remaining: MM-2/3/4 migrate the COMPOUND types (arrays/maps/structs/tuples/
  boxes) onto arenas — same coupled pattern (allocators take `Arena*`, copies
  become recursive arena-aware, store-sites already force owner 0), now on a
  proven foundation. Then MM-5 per-block arenas, MM-6 the move/borrow opts.

### MM-2 (next: arrays on arena) — design + coupling finding

Arrays are MORE coupled than strings. Plan (option A — buffers on arena,
elements stay malloc via the existing store-copy, so copies stay shallow):
- Array runtime (`gen_arr_fns`): `_from`/`_copy`/`_slice` take a leading
  `Arena*` and `amem(ar, …)` the buffer; `_push` takes `Arena*` and ARENA-GROWS
  (realloc is impossible on arena memory) — on `len==cap`, `amem(ar, 2*cap)` +
  memcpy, leaving the old buffer dead (reclaimed at arena_free; doubling ⇒ O(2n)).
  `_new` stays empty/no-buffer. `_eq` unchanged.
- Need `owner_arena_of(place, names, types)`: an inout-param array → `"_parent"`
  (its buffer is the caller's), else local → `"&_scope"`. `push` grows in
  `owner_arena_of(args[0]'s root var)`, NOT `ctx.owner`; from/copy/slice/literal
  use `ctx.owner`. A root-var helper is needed for nested places (`s.items`).
- COUPLING: `Arr_*_copy(ar, …)` is called inside `StructName_copy`,
  map copy, and `Soa_*_copy` (struct/map/soa fields that are arrays). Giving
  `_copy` an arena param forces those copy fns to take `Arena*` too — so MM-2
  drags in struct/map/soa copy (part of MM-4) for their copy paths. `hi_split`
  (returns `Arr_str`) also gains an `Arena*` and threads it to its internal
  `Arr_str_new/push`; `split` emission passes `ctx.owner`.
- Soundness: container ELEMENTS are already malloc (store-copy forces owner 0),
  so array copies stay SHALLOW (element pointers) — no recursive deep-copy
  needed. Only the buffer moves to the arena. (Frees the buffer per scope;
  heap elements still leak — a later refinement could push elements into the
  array's arena + deep-copy, but that needs the recursive copy.)
- Verify: self-compile ASan (hierc0 is array-heavy — token/node lists) +
  fixpoint B≡C + 57/57 + an array-build-and-discard RSS delta.
This is the project's biggest/riskiest increment; do it as its own focused pass.

### Core spine COMPLETE (MM-1..MM-4) — all heap types arena-managed at function scope

Status: every heap type is now allocated in an arena (the value's owner) and
freed at function return; values that escape (return/store) are copied to a
longer-lived arena. Sound model: default owner `&_scope`, returns `_parent`,
container stores `0`/malloc, print `_t`. owner_arena_of: local container
`&_scope`, inout container `0`/malloc (its home arena isn't threaded — see
below). Wins (each verified self-compile ASan-clean + fixpoint B≡C + test 57/57):
MM-0 accumulator 464x, S2.1 print 42x, S2.3 locals 73x, MM-2 arrays 31x,
MM-3 maps 3.7x, MM-4 structs/tuples/boxes 1.67x. Enum nodes stay malloc (the
recursive AST). Commits: ef68daa, cc9aa27, 8407340, a850736, 29c881a, 5438dc3,
dfbf2d2, 605085c.

### MM-5 (per-block arenas) — design
Open `Arena _bN = arena_child(<enclosing>)` per if/while/for block (per-ITERATION
for loops); in-block fresh allocations → `&_bN`; free `_bN` at block exit. Frees
per-iteration transients eagerly instead of holding them to function return.
- NOT escape analysis. Because Hier is value-semantic with no aliasing, no
  closures, and no free pointers (only explicit `inout`), every cross-scope move
  is syntactically visible and its destination is LEXICALLY known at the write
  site: `outer = e` → outer's home block; `push(outer,v)` → outer's home;
  `return e` → `_parent`; arg/`inout` → caller. So it's lexical home-scope
  routing, not dataflow tracing.
- Implemented (lighter, sound): SDecl values + pure transient subexpressions use
  `ctx.owner` = the current block arena (freed at block exit / per loop
  iteration). Every WRITE that could target an enclosing-scope binding routes
  away from the block arena to a longer-lived one: plain SAssign → `&_scope`
  (function scope, outlives all blocks); push/map-accumulator → `owner_arena_of`
  (function-scope local / malloc inout); SFieldAssign/stores → `0`/malloc;
  return → `_parent` after freeing every enclosing block arena. So a value can
  never be referenced after its block frees. A `return` inside depth-d blocks
  frees `_bd..._b1` then `_scope`.
- ✅ MM-6a (full lexical per-block, commit b9f66c7): instead of a per-var home
  stack, one int `Ctx.block_base` = env length at block entry. owner_arena_of
  routes a var declared in the CURRENT block (env index >= block_base) to the
  block arena, an outer var to `&_scope`, inout to `0` — used by push, the
  map-accumulator, and plain SAssign. So a loop-local container/reassignment
  built in the SAME block frees per-iteration (loop-local array build:
  245MB->10MB, O(n)->O(1)). Lexical, no dataflow. Remaining coarseness: a
  container declared in an OUTER block but pushed in a NESTED block routes to
  `&_scope` (block_base only marks the current block, not each var's exact
  depth) — sound, just not freed until function return. A true per-var depth
  map would close that; rare in practice.

### MM-6 (optimizations) — banked refinements
- ✅ MM-6b (f1ff194): MAP KEYS now in the map's arena (sc(ar,k) in _put; struct/
  tuple map fields deep-copy; struct_is_heap counts maps). Map memory fully O(1).
- ✅ MM-6c (commit c84eb4c): ARRAY STRING ELEMENTS now live in the array's arena
  and free with it. `elem_deepcopy(ctx,et)` (true for str — scopy is in the
  preamble, so no forward-decl ordering hazard) gates: (1) `Arr_str_from` (hence
  `_copy`/`_slice`) deep-copies each element `scopy(ar,s[i])` instead of memcpy'ing
  the pointer, so a copied/returned/sliced [str] fully owns its strings; (2) `push`
  builds the str value in `owner_arena_of(arr)` (co-located with the buffer) instead
  of owner 0; (3) array-literal str elements build in `ctx.owner` (no leaked temp);
  (4) `hi_split` substrings + `map_*_keys` results `scopy` into the result arena
  (the latter also closes a latent return-dangle where keys aliased the map's keys).
  push C-fn still grows the buffer shallowly (same identity/arena) — it does NOT
  route through `_from`. Sound because Hier is value-semantic/non-aliasing: every
  escape (return/store/bind) routes through `_copy`/`scopy` which re-homes the
  strings into the destination arena, and a loop-local array is unreachable once
  its element-arena frees. Verified: fixpoint B≡C + 57/57 ASan/UBSan-clean +
  loop-local [str] build 2M iters **368MB → 1.3MB (~274×)** under hierc0
  (bench/strarr_build.hi, now a perf guard). NOTE: "map string VALUES" in the
  earlier residual list is moot — gen_map_type only monomorphizes int/float value
  types, so no str-valued maps exist.
- ✅ MM-6d (commit pending): NESTED-ARRAY ELEMENTS (`[[T]]`) now live in the
  array's arena. `elem_deepcopy` also returns true for `is_array(et)`, and a new
  `elem_copy_expr(ctx, et, ar, src)` emits the per-element copy (str -> scopy /
  array -> `Arr_<inner>_copy(ar, src)`); `_from`'s deep-copy loop calls it. The
  emission-ORDERING blocker is solved by having `gen_arr_type` (step 2, all
  typedefs) emit `_from`/`_copy` FORWARD PROTOTYPES, so an outer `Arr_*_from`
  body (step 6) can call the inner `Arr_*_copy` regardless of body order — and
  the typedef ordering that already makes nested-array typedefs compile orders
  the protos identically (so it fails CLOSED: a wrong order is a C compile error
  the fixpoint catches, not a UAF). Recurses to any depth (`[[[T]]]`) and through
  `[[str]]` (inner _from scopy's the strings). Store sites (push value owner,
  array-literal element owner) already gate on `elem_deepcopy`, so they extended
  to arrays for free. `[Struct]`/`[(tuple)]` ELEMENTS inside a nested array stay
  shallow+immortal (Arr_Struct_from still memcpy's struct values; their heap
  fields are owner-0/malloc) — sound, leaks only those fields. Verified: fixpoint
  B≡C + 57/57 ASan/UBSan-clean (`compiler/hierc0.hi` + `tests/projections.hi`
  exercise `[[…]]`) + loop-local [[int]] build 1M iters **108MB -> 1.4MB (~77×)**
  (bench/nestarr_build.hi, now a perf guard).
- ✅ MM-6e (commit pending): STRUCT / TUPLE ARRAY ELEMENTS now live in the array's
  arena — and, because the leak source for these was *construction*, this also
  closed the banked "struct/tuple construction → malloc" item. Two coupled parts:
  (1) `elem_deepcopy` extends to heap structs (`is_struct and struct_is_heap` —
  only heap structs have a `_copy`) and tuples; `elem_copy_expr` gains a tuple
  (`Tup_*_copy`) and heap-struct (`StructName_copy`) arm; `_from` deep-copies via
  them; the printer emits `Struct_copy`/`Tup_copy` FORWARD PROTOTYPES (a new
  step 5b, before the array fns) so an `Arr_Struct_from` body can call them
  regardless of definition order (fails CLOSED, like MM-6d). (2) struct
  construction (`(S){...}`) and tuple literals now build their fields in the
  AMBIENT owner (`gen_store_args_owner(.., ctx.owner)` / `gen_rhs(.., ctx)`)
  instead of forced `0` — so a constructed element's heap fields land in the
  array's arena and free with it. SOUND because the contexts that need
  immortality set the ambient owner to `0` *before* building their args (enum
  ctor via `gen_store_args`, `Some/Ok/Err` via `with_owner 0`, `SFieldAssign`),
  so a struct/tuple nested in an enum / option-box / field-store still inherits
  `0` (the recursive AST and option payloads stay malloc); every other escape
  (return → `_parent`, push → the container's arena, outer assign →
  `owner_arena_of`, bind → `gen_rhs` deep-copy) is at-least-as-live as the
  construction. Verified: fixpoint B≡C (6384 → 6450 lines C) + 57/57 ASan/UBSan
  (hierc0.hi is saturated with `[Token]`/`[Stmt]`/`[Func]` etc., so the
  self-compile exercises both parts heavily) + loop-local `[Item]` (string field)
  build 1M iters **184MB → 1.3MB (~140×)** (bench/structarr_build.hi, now a perf
  guard).
- ✅ MM-6f (commit pending): OPTION / RESULT ARRAY ELEMENTS — the box now lives in
  the array's arena. `elem_deepcopy` extends to `is_option`/`is_result`;
  `elem_copy_expr` re-boxes via the preamble helpers — `o.tag ? hsome(hbox(ar,
  sizeof(T), o.val)) : hnone()` (result: `hok`/`herr` on `.ok`/`.err`) — so `_from`
  gives a copied array an independent box, and `push`/literal (already gated by
  `elem_deepcopy`) build the box in the array's arena instead of owner-0 malloc.
  No forward protos needed (hbox/hsome/… are in the preamble). SCOPE (a deliberate
  call): the box is re-homed and the PAYLOAD is shared shallowly — sound because
  construction is unchanged (`Some/Ok/Err` payload stays owner-0/immortal, and the
  whole option model still treats options as immutable/share-safe, so no copy path
  had to change). This FULLY closes `[Option(scalar)]` / `[Result(scalar,…)]` (no
  payload heap); for heap payloads (`[Option(str)]`) the box is freed but the
  payload still leaks at construction. Verified: fixpoint B≡C (6450 → 6468 lines C)
  + 57/57 ASan/UBSan (`tests/option_arrays.hi` is in `compiler/tests/`, so the
  fixpoint differential covers `[Option]`) + loop-local `[Option(int)]` build 1M
  iters **62MB → 1.4MB (~45×)** (bench/optarr_build.hi, now a perf guard).
- ✅ MM-7f: HEAP-PAYLOAD option/result elements (`[Option(str)]`,
  `[Result([int],_)]`) — the last residual, now CLOSED. Previously `Some/Ok/Err`
  built their payload at owner-0 (malloc/immortal) and every copy path re-boxed
  the payload POINTER shallowly, so a heap payload (string/array/…) leaked at
  construction. The fix makes options first-class deep-copied value types,
  mirroring hierc's `hier_optN_copy` (`if (v.has) v.val = hier_str_copy(a, v.val)`)
  in three coordinated changes: (1) a `box_payload(ctx, ty, ar, valptr)` helper
  deep-copies a HEAP payload into `ar` (scalar payloads keep the shallow re-box);
  (2) `elem_copy_expr` + `cp_field` use it, so every container/field copy
  (array-element, struct/tuple/enum field) deep-copies the payload; (3) `Some`/
  `Ok`/`Err` construction builds the payload in the box's arena (`ctx.owner`)
  instead of owner-0. (1)+(2) and (3) are complementary: arena-owned payloads need
  the deep-copy on cross-arena moves, or they'd UAF. Verified: fixpoint B≡C +
  60/60 ASan/UBSan + a loop-local `[Option(str)]` build (`bench/optstr_build.hi`):
  hierc0 **245 → 11 MB at 4M iters (now flat, == hierc)**, LSan zero-leak; and a
  **1200-seed differential campaign** with the fuzzer extended to generate
  `Option(string)`/`[Option(string)]` heap-payload options (the UAF check for the
  arena-owned-payload change) — `ok=1200 skip=0 FAIL=0`. hierc0 now has **no known
  memory gap vs hierc** — every element type, common and rare, is closed.
- ✅ MM-6g (75d85e4): INOUT CONTAINER HOME-ARENA THREADING. An
  `inout [T]` / `inout {…}` / `inout soa` param now carries a hidden
  `Arena* _ina_<name>` (its array/map/soa lives in the CALLER's arena), and
  `owner_arena_of` returns `_ina_<name>` for it instead of `"0"` — so a `push`/
  grow/alloc into an inout container lands in the caller's arena and frees with
  it, instead of malloc-leaking. Mechanism: `gen_params` appends the hidden param
  after each inout container param (`inout_container_param`); the call site
  (`gen_call_args` + `arg_ina`) interleaves the matching arena after each
  `&container` arg — the place's caller-side home via `owner_arena_of` (a local →
  its block/scope arena; a chained inout → the caller's own `_ina_`, threading the
  true home up the chain). Param-side and call-side use the IDENTICAL predicate,
  so a mismatch is a C arity error the fixpoint catches (fail-closed). Inout
  scalars get no `_ina_` (stay malloc); inout heap STRUCTS were a separate
  residual, closed in MM-7e. SOUND: the threaded arena is the container's
  true home, which outlives the call. Verified: fixpoint B≡C (6468 → 6577 lines C;
  hierc0 threads `inout [Token]`/`[string]`/`[Expr]` through lexing/parsing/codegen,
  so the self-compile exercises it pervasively) + 57/57 ASan/UBSan + a callee
  filling a caller-owned per-iteration `inout [int]` 200k×200: **798MB → 1.4MB
  (~582×)** (bench/inout_fill.hi, now a perf guard).
- ✅ MM-7e (commit pending): INOUT HEAP-STRUCT field-container threading — extends
  MM-6g from arrays/maps/soa to heap structs. `inout_container_param` (gained a
  `ctx` arg) now also returns true for `is_struct and struct_is_heap`, so an
  `inout Struct` param carries a hidden `Arena* _ina_<name>` and `owner_arena_of`
  routes its FIELD-containers to it: `push(s.items, v)` on an `inout Bag` grows
  `s.items` in the caller's arena (`_ina_s`) and frees with the caller's struct,
  instead of malloc-leaking. `arg_ina` (the call site) mirrors the same extended
  predicate so arity stays in sync (fail-closed). Verified: fixpoint B≡C (7318
  lines C, unchanged — hierc0 itself passes Ctx by value, no inout structs; the
  fixpoint differential exercises it via `examples/inout.hi`/`context.hi` +
  `tests/value_semantics.hi`, all in `compiler/tests/`) + 57/57 ASan/UBSan + a
  callee filling an `inout Bag`'s `[int]` field 200k×200: **798MB → 1.4MB (~582×)**
  (bench/instruct_fill.hi, now a perf guard). The one-time remaining residual —
  HEAP-PAYLOAD option arrays (`[Option(str)]`) — was later CLOSED in **MM-7f**
  (see above): a `[Option(string)]` repro showed it was reachable (hierc0 leaked
  ~60MB/1M iters where hierc stayed flat), so the option payload was made a
  first-class deep-copied value (mirroring hierc), verified by fixpoint + LSan +
  a 1200-seed option-heap fuzz campaign. hierc0 now has no known memory gap.

### MM-7 — enums on arena (the recursive AST) + transient placement

The prong-B head-to-head (bench/prongB) exposed that hierc0 was 50–170× worse than
the C compiler on recursive-enum tree workloads (binary-trees 2 GB, tree-rewrite
1.2 GB), because two things were never migrated: enum NODES stayed malloc-immortal,
and transient values inherit the enclosing statement's owner (function scope).

- ✅ MM-7a (ff96105): ENUM NODES ARE NOW ARENA VALUE TYPES. `mk_<Variant>`
  takes a leading `Arena*` and `amem`s the node (both call sites pass `ctx.owner`;
  payload args inherit the ambient arena via `gen_store_args_owner`, so nested
  children co-locate — a `return Node(make(d-1),make(d-1))` builds the whole tree
  in `_parent`). A generated recursive `<Enum>_copy(Arena*, <Enum>*)` (using the
  new `cp_field` full-type dispatch) deep-copies a node tree into a target arena;
  it's wired into every ESCAPE point — `gen_rhs` place-bind (return/store/outer-
  assign), `gen_struct_copy`/`gen_tuple_copy` members, `elem_copy_expr` (`[Enum]`
  elements) — and forward-declared (step 5c) so the mutually-recursive copy fns
  resolve in any order. Match-arm payload bindings KEEP borrowing (`cp_typed`
  unchanged), so the O(n) tree-walk opt (bench/treewalk) is preserved — deep-copy
  happens only when an enum crosses an arena boundary. SOUND by the same invariant
  as MM-6e (struct/tuple): every cross-arena move is a copy point; construction in
  the ambient owner is at-least-as-live. Verified: fixpoint B≡C (6577 → 6853 lines
  C — hierc0 IS enums: Expr/Stmt/Token built/copied/returned/stored everywhere, the
  strongest possible oracle) + 57/57 ASan/UBSan + treewalk still O(n). FOUNDATIONAL
  / memory-neutral ALONE: the tree-workload win needs MM-7b — the transient
  `make(d)` tree still inherits `ctx.owner = &_scope` (the accumulator `sum`'s home)
  in `sum = sum + check(make(d))`, so trees pile up in function scope until return.
- ✅ MM-7b (d4d13df): TRANSIENT PLACEMENT — landed the tree-workload win.
  When an assignment/decl LHS is SCALAR (`is_scalar_ty`: int/float/bool) and the
  RHS contains a call (`expr_has_call` — so the hot `i = i + 1` counters are NOT
  wrapped), the whole RHS value is transient (no heap escapes a scalar result), so
  evaluate it in a fresh per-statement `_t` arena (`scalar_transient`, the `print`
  pattern) and free it after the scalar is stored — the `make(d)` tree builds in
  `_t`, freed per statement. SOUND because stores inside the RHS still route to
  their own arena (`owner_arena_of`), so only true transients land in `_t`; and
  MM-7a made enums deep-copied on escape, so a transient `_t` enum is safe.
  Wired into SAssign / SDecl / STypedDecl. Verified: fixpoint B≡C (6853 → 6959
  lines C) + 57/57 ASan/UBSan + make bench within bounds (treewalk still O(n),
  transient/heap_transient hold). prong-B under hierc0: **binary-trees 2374 MB →
  38 MB (~62×), tree-rewrite 825 MB → 9 MB (~88×)** — now competitive with the C
  compiler / C / Rust / Koka (was 50–170× worse). Did NOT fix arr_pipeline — that
  was MM-7c.
- ✅ MM-7c (commit pending): PER-VAR BLOCK DEPTH — closed the last hierc0-vs-hierc
  gap (arr_pipeline). The MM-6a coarseness was a SINGLE `block_base` in `Ctx`, so a
  var declared in an ENCLOSING block (not the current one, not function-top) routed
  coarsely to `&_scope` instead of its block arena — e.g. `ys := []int` in a `for j`
  body, grown by `push` in the nested `for i` loop, accumulated all 200 buffers in
  function scope (358 MB). Fix: replace `block_base: int` with a STACK
  `bbases: [int]` (env length at entry to each nested block, pushed by
  `enter_block`); `var_block_depth(ctx, vi)` = the deepest block whose entry index
  ≤ `vi` (bbases is increasing), and `owner_arena_of` returns that block's arena
  `&_b<d>` (in scope at any deeper point, so no dangle) instead of `&_scope`. So
  `ys` grows in the `for j` block arena and frees per outer-loop iteration. SOUND:
  a var's use-depth ≥ its decl-depth (lexical scoping), so `&_b<d>` is always live;
  value semantics truncates the env (and the bbases copy) on block exit. Verified:
  fixpoint B≡C (6959 → 6991 lines C; hierc0 has deeply nested loops/ifs/matches
  everywhere) + 57/57 ASan/UBSan + make bench within bounds. arr_pipeline under
  hierc0: **358 MB → 5 MB (~72×)**, and faster (177 → 32 ms). hierc0 is now
  competitive across ALL four prong-B workloads — no known memory gap to `hierc`.
- ✅ MM-7d (commit pending): MOVE-ON-LAST-USE (deep-copy elision). `b := a` over a
  uniquely-owned local normally deep-copies; if `a` is read exactly once in the
  whole function and that read is not in a loop (so it is its single dynamic last
  use), `b` takes over `a`'s buffer — a move, no copy. Mirrors the C compiler's
  `can_move_from`. A `gen_func` pre-pass (`rd_*` read-scan + `compute_movables`)
  precomputes `Ctx.movables` (read-once, not-in-loop names, loop-context baked in);
  `can_move`, folded into `gen_rhs`, additionally requires at the site: heap type,
  not a param (params borrow the caller's buffer), and the source's home arena ==
  the destination owner (lifetime match). Output-INVISIBLE when sound, so the
  fixpoint differential + `tests/value_semantics.hi` are the soundness oracle (an
  over-aggressive move would alias and diverge output). One bug found+fixed: bare
  enum variants (`Leaf`) are `EVar` but not locals — `can_move` now requires the
  name be in the env before any var lookup. Verified: fixpoint B≡C (6991 → 7318
  lines C) + 57/57 (value_semantics green) + make bench within bounds + emitted-C
  shows `Arr_int dup = big;` (was `..._copy(...)`). HONEST IMPACT: correct and it
  fires, but negligible peak-RSS change (self-compile 16.5 → 16.7 MB) — the
  per-block arena reset + transient placement already reclaim most short-lived
  copies, so eliding them frees little; the value is reduced allocator churn and
  codegen-feature parity with `hierc`, not a leak fix.

### MM-8 — liveness-driven in-place REUSE (the arena's worst case, closed)

The result that turns the arena's one clean defeat into a win. The boundary, mapped
by the `iter-transform` prong-B workload: an arena cannot free a single object
mid-scope, so a loop-carried value reassigned each step (`a = step(a)`) accumulates
every dead intermediate until the function returns — peak = TOTAL allocation, not
the live set (3.5 GB vs ~3 MB for C). Exactly the case reference counting (Koka's
Perceus) was built for: it sees the value is uniquely owned and reuses the storage
in place (FBIP).

**Key insight: Hier already proves unique ownership STATICALLY, for free.** Value
semantics means no aliasing — every bind deep-copies or moves — so at `a = new`,
`a`'s OLD buffer has exactly one owner and is dead the instant `new` is computed. No
runtime refcount (unlike Perceus). The compiler hands the dead buffer back to its
arena and the next allocation reuses it.

- **Runtime** (commit bbea2c9 hierc / 7807850 hierc0): each `Arena` gains an object
  free-list. `arena_recycle(a, ptr, n)` pushes a dead buffer; `arena_alloc` serves a
  best-fit chunk in `[n, 2n]` before bumping; `arena_reset`/`arena_free` drop the
  list (its chunks live in blocks about to be pooled). SOUND because `arena_alloc`
  only bumps FORWARD — a recycled chunk (below the bump pointer) can never overlap a
  fresh bump. The array `push` also recycles each geometric-growth buffer it
  outgrows.
- **Codegen** (`S_ASSIGN`): a qualifying reassignment emits, in this **load-bearing
  order**, `tmp = RHS; if (old.data && old.data != tmp.data) arena_recycle(old); a =
  tmp;`. The RHS is evaluated FIRST (it may read the old buffer — a call's arg, a
  slice's range); recycling before would alias the live buffer.

**Soundness condition — the var must never be a MOVE SOURCE**, learned the hard
way. The first gate keyed on "inside a loop", an *unsound* proxy: `b := a` (outside
a loop) moves `a`'s buffer to `b`, then `a = mk()` (inside one) would recycle that
still-live buffer and corrupt `b`. **Two latent corruption bugs shipped and were
caught** (only manifesting with distinct values + size-matching reuse, which the
value-masked fuzz initially missed; pinned by `tests/recycle_alias.hi`). Fix:
recycle only a var read **≥ 2 times** (hierc) / **not in `ctx.movables`** (hierc0) —
move-on-last-use moves a var read exactly once, so read ≥ 2 ⇒ never moved ⇒
uniquely owns its buffer.

**Gate coverage (widened in two careful steps, each fuzz-clean):**
- *Element types* (aa30d66): ANY array. We recycle the SPINE buffer (`data[]`);
  flat-struct arrays (`[P]`) get FULL reclaim (2.5 GB → 6.6 MB), heap-element arrays
  (`[string]`) get the spine only (partial — the elements are separate allocations,
  also dead but not recycled). Required completing `push`-recycle across all `hierc`
  array families.
- *RHS forms* (05f22f4): ANY RHS — slice (`a = a[1:]`: 1.55 GB → 2 MB), copybind,
  literal, identity — not just calls. A place RHS is deep-copied to a fresh buffer;
  a call/literal is inherently fresh; the `.data != .data` guard backs distinctness.
  (Array `+`-concat doesn't exist in Hier.) Drove a `hierc0` cleanup (3379c53): a
  slice already owns (`Arr_*_slice`/`substr` allocate fresh), so `gen_rhs` no longer
  double-copies it. SOA slices are VIEWS (cap 0) — excluded, still `Soa_copy`'d.

**Result:** `iter-transform` 3.5 GB → 4 MB (`hierc`) / 6 MB (`hierc0`), flat in m,
matching C and beating Go/Koka — on the former worst case. Verified: fixpoint B≡C +
`make test` distinct-value regression tests + ASan/LSan + the hierc-vs-hierc0
differential fuzz (`arr_rebuild`/`arr_realloc`/`arr_slice` patterns). NOTE the
differential is a *weak* net here — value-masking can hide corruption — so the
dedicated distinct-value tests are the real guard. The one residual is inherent:
recycling a single buffer can't reclaim heap-element arrays' separately-allocated
elements (spine only). FBIP-grade reuse from *static* value semantics + lexical
arenas, no reference counts — Perceus on its home turf without paying for it.

### MM-9 — element-overwrite recycle (the sliding-window eviction case, closed)

MM-8 recycled a buffer on whole-variable reassign (`a = step(a)`); its residual,
noted at the time, was heap-element arrays — it recycles the spine but not the
separately-allocated elements. The `window` workload is exactly that gap: a ring
buffer of strings, `ring[k] = rec` each step, where only the last W are live. The
spine is stable, but each overwrite dropped the evicted string into the arena with
no way back — peak tracked the whole stream, not the window (47 MB vs C 3.3, ~14×;
`bench/window`). The arena's one clean memory loss.

**The fix — recycle the evicted element on `arr[k] = v`.** The old element is dead
and uniquely owned the instant the new value is computed (value semantics: reads
deep-copy out, so nothing else can reference the slot's bytes). Hand it back to the
array's arena; the next store reuses it. Static unique ownership again — no refcount.
- **Codegen.** Build the new value FIRST (the RHS may read the slot — `s[k]=s[k]+x`),
  store it, then recycle the old buffer. Guards: `old != new` (never recycle the
  buffer just stored) and `arena_owns(a, old)` (recycle ONLY a buffer this arena
  allocated — never an interned string literal, which is malloc'd/immortal/possibly
  shared, nor a cross-arena pointer). hierc: `hier_arr_str_set` in `runtime/hier_rt.c`.
  hierc0: the `SFieldAssign` emission (`is_str_arr_index`), which additionally moves
  the element from malloc (owner 0) into the array's arena so it is recyclable at
  all — bringing the two compilers to the same model.
- **Segregated free-list (the load-bearing half).** A single capped (HIER_FREECAP=32)
  best-fit free-list was the bottleneck: an eviction window needs the free-list to
  hold ~W dead chunks, but a linear best-fit scan cannot be uncapped. Instrumented,
  the cap-32 list DROPPED ~1.04M of 2M dead chunks → element recycle alone only got
  47→27 MB. Replaced with a per-8-byte-size-class free-list (`Arena.bkt[16]`, sizes
  8..120 B): O(1) push/pop, no cap, no scan. Larger chunks keep the capped best-fit
  `freelist`, so MM-8's large-buffer (array spine) reuse is untouched. With buckets:
  drop=0, peak 4.2 MB.

**Result:** `window` string case 47.3 MB → 4.2 MB (hierc) / 3.6 MB (hierc0), ~14× C
→ ~1.3× C, beating Go — and *faster* (fewer bumps). Verified: checksum matches C/Go
on both compilers; ASan/UBSan clean; `make test` 97/0; `make fixpoint` B==C (MM-9 is
output-invisible); differential fuzz clean; `tests/elem_recycle.hi` is the
distinct-value alias regression (the MM-8 recycle_alias lesson: value-masking fuzz
can hide element corruption, so a dedicated distinct-value test is the real guard).

- **MM-1 — threading spine + strings on arena (the irreducible first slice).**
  - `main` wrapper + `_root`; every fn gains `Arena *_parent` + `_scope`.
  - Every user-call site passes `&_scope` as the first arg.
  - String helpers (`sc`/`i2s`/`f2s`/`substr`/`hi_*`) gain an `Arena*` first
    param and `arena_alloc` instead of `malloc`; every string-producing call
    threads `&_scope`.
  - Return-slot for string returns (copy into `_parent`); `_scope` freed on
    every exit path. Arrays/maps/structs/tuples/boxes STILL malloc (leak) —
    sound per the mixed-model argument.
  - **Verify:** `make fixpoint` (B≡C + matches C compiler), `make test` 57/57,
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
- `make test` 57/57 (ASan/UBSan clean — the self-emitted C runs under sanitizers).
- Memory delta: ASan leak report and/or `bench/peakrss` on a workload that
  exercises the migrated type — each stage must show that type's bytes now
  freed. No silent "looks done" — quote the before/after.

## Risk

High blast radius on the self-hosting compiler: MM-1 touches every emitted
signature, call site, and return. Mitigation: land MM-1 behind a full fixpoint
run before commit; if the self-emission breaks, ASan on the self-emitted C is
the debugger (it caught the Stage-4 return-promotion bugs). Keep each stage its
own commit with the verification quoted in the message.
