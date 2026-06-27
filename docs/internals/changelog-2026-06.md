# Changelog — tychoc0 dogfood & gap-closing campaign (June 2026)

> **Historical record**, not a TODO and not current-state reference. Every entry below is
> CLOSED. For the CURRENT project state — architecture, the gate map, shipped capabilities,
> decided non-goals, and what's actually open — see [`STATUS.md`](../../STATUS.md) at the repo
> root. This file is kept for its "what was wrong / how it was fixed / how it was verified"
> detail, which is useful if a related area ever regresses.
>
> **Campaign outcome:** the integration-dogfood campaign hardened the self-hosted `tychoc0`
> to parity with the reference `tychoc`. It fixed the gaps below, plus an earlier cluster
> (map×closures `1e546a3`; generic-struct container-field `c702dd1`, nested-provenance
> `ca8ea45`, instance-type cluster `861486f`), and ran a 3-round feature accept/reject diff
> (~94 probes) whose final round came back clean. All gates green throughout (`make ci`). The
> reusable differential-probing method is documented at the bottom — the durable takeaway.

---

## GAP 1 — tychoc0 lacks ARRAY + substr bounds checks (memory safety) — CLOSED 2026-06-27

> **CLOSED.** Added `hi_bchk(i,n)` preamble helper; array read `a[i]` lowers to an
> eval-once checked stmt-expr (`({T _a=base; _a.data[hi_bchk(i,_a.len)];})`), array
> write/place to a checked pointer stmt-expr (`(*({T* _a=&(place); &_a->data[hi_bchk(
> i,_a->len)];}))`, base address taken once so a side-effecting place like `m[k][j]`
> runs once). `substr` now clamps `[a,b]` to `[0,len]` matching `tycho_rt.c:806`.
> Side effect: `a[i]` via gen_expr is now an rvalue, so `&place` (EAddr) routes
> through gen_place for ANY place (`is_place` check, subsumes the old map-index
> special case), and the MM-9 str-array-set path (SFieldAssign) switched from
> gen_expr to gen_place. Verified: 9/9 OOB/clamp differential probes MATCH tychoc;
> ASan/UBSan clean in-bounds; `make test` 239/0, `make fixpoint` (B==C + differential
> incl. projections.ty), `make corelib` 3-way all green. NOTE found in passing:
> tychoc itself miscompiles `&a[i]` for a SCALAR-int element (emits malformed
> `tycho_arr_C-1020_ptr`) — a separate latent *tychoc* bug, not Gap 1.

<details><summary>Original gap notes (for history)</summary>

**Symptom (differential, tychoc vs tychoc0):**
- `a[5]` on a len-3 array: tychoc aborts (`tycho: index 5 out of bounds (len 3)`);
  tychoc0 returns garbage (reads adjacent arena memory), rc=0.
- `a[-1]`: same — tychoc aborts, tychoc0 reads garbage.
- `substr("hi", 0, 10)`: tychoc clamps to `"hi"`; tychoc0 over-reads the buffer.
- No ASan hit on the array OOB because the read lands inside the one big arena block
  (it's reading other live data, not past the malloc'd region) — still a real OOB /
  info-leak, just not heap-overflow-detectable.

**Why fixpoint missed it:** tychoc0.ty never indexes out of bounds, so the differential
never triggers the divergence.

**Current tychoc0 state (inconsistent):**
- STRING index `s[i]` is ALREADY checked — `hi_sidx` at `compiler/tychoc0.ty:7394`
  (`if (i < 0 || i >= n) { ...out of bounds...; exit(1); }`). Good model to mirror.
- ARRAY index is UNCHECKED — emitted as raw `.data[i]`:
  - read: `compiler/tychoc0.ty:4298`  (`gen_expr(base) + ".data[" + gen_expr(idx) + "]"`)
  - write/place: `compiler/tychoc0.ty:4927` (`gen_place(b) + ".data[" + gen_expr(i) + "]"`)
- `substr` helper UNCHECKED — `compiler/tychoc0.ty:7418`
  (`static char* substr(Arena* ar, const char* s, long a, long b) { long n = b - a;
  char* r = hs(ar, n); memcpy(r, s + a, (size_t)n); return r; }`) — no clamp/validate of a,b.

**Reference contract to match (tychoc / runtime):** `runtime/tycho_rt.c` emits
`tycho: index %ld out of bounds (len %ld)` for arrays (lines ~1037/1045/1134/1142/1220/1228)
and `tycho: string index %ld out of bounds (len %ld)` (~788/800). Match these messages so
behavior is identical on OOB.

**Fix scope (by difficulty):**
1. **substr — SIMPLE.** Add a clamp/bounds check inside the emitted `substr` helper (one
   string literal at 7418). Decide: clamp like tychoc, or abort? Check what tychoc's substr
   does (clamp to len) and match it exactly.
2. **array read (4298) — MODERATE.** Wrap in an eval-once checked access, e.g.
   `({ <cty(arrtype)> _a = <base>; _a.data[hi_bchk(<idx>, _a.len)]; })` where a new preamble
   helper `static inline long hi_bchk(long i, long n){ if(i<0||i>=n){fprintf(stderr,
   "tycho: index %ld out of bounds (len %ld)\n", i, n); exit(1);} return i; }`. Need the
   array's C type (cty of type_of(base)). Nested `a[i][j]` -> nested `({...})`, each with
   its own `_a` (block-scoped, fine).
3. **array WRITE / place (4927) — TRICKIER.** It's an lvalue (`a[i] = v`), so the
   statement-expression trick does NOT work (GCC stmt-expr result is an rvalue, not
   assignable). Options: (a) a pointer-returning checked helper `*hi_aptr(arr.data,
   hi_bchk(i, arr.len))` — but evaluates base twice (bad if base has side effects);
   (b) emit a statement-level guard before the assignment (in the SFieldAssign/SAssign
   codegen, check the index first); (c) check how `gen_place` is consumed and whether a
   helper that takes `&arr` + i and returns `T*` is feasible. The place form is the real
   work — study how tychoc emits checked array writes for the pattern.

**Validation when fixing:** the 3 OOB probes must now match tychoc (abort/clamp), `make
test`, `make fixpoint` (B==C + differential — the added checks change emitted C but B==C
holds since tychoc0 emits them consistently; in-bounds behavior unchanged), `make corelib`,
ASan. Also re-run the bounds-probe batch (see "Probing method").

**Severity note:** tychoc (the primary/reference compiler) IS fully safe; tychoc0 is the
self-hosted bootstrap and its own code never OOBs, so this is defense-in-depth / parity, not
an active hole in shipped tooling. Still worth closing given the project's safety emphasis.

</details>

---

## GAP 2 — generic ENUM instance types not resolved (systemic) — CLOSED 2026-06-27

> **CLOSED.** The fix the handoff feared ("fail-open shallow copy") was avoided by
> healing the two consumers that actually matter rather than whack-a-mole at every
> source. Three changes in `compiler/tychoc0.ty`:
> 1. `resolve_ginst` (10166): also accept enum instances (`... or is_enum(dc,ctx,cand)`).
> 2. `cp_field` (4838): resolve the type INTERNALLY (`resolve_ginst(resolve_nt(...))`)
>    so the deep-copy dispatch (`is_enum`/`is_struct`) selects the real copier — the
>    unresolved app `Box(int)` was the thing that fell through to a fail-open shallow
>    `return src` (aliased the freed channel cell → corrupted tag). Fixing cp_field is
>    exactly why #1 is now SAFE where the bare 1-liner was not.
> 3. `SMatch` (6467): resolve the scrutinee type so `variant_ptypes_in` finds the
>    instance's payload types — an unresolved `Box(Box(int))` missed and fell back to
>    the wrong enum's `$T` → a `long` binding (nested-enum miscompile).
>
> Verified MATCH (tychoc vs tychoc0): generic-enum channel scalar AND heap/string
> payload (the real deep-copy test), nested `Box(Box(int))` through a channel, generic
> enum via Task (spawn/wait), fn-return, and generic-STRUCT channel with a heap field
> (a latent shallow-copy bug this also fixes). Regression test: `tests/generic_enum_channel.ty`
> (+ golden). Gates: `make test` 240/0, `make fixpoint` (B==C + differential),
> `make corelib` 3-way, `make fuzz-quick` 60/60 — all green.
>
> **Separate gap found in passing (NOT Gap 2) — CLOSED 2026-06-27:** `select recv(c, x):
> match x` failed on tychoc0 for ANY enum with `type: unknown variable 'x'` — the select
> recv-binding wasn't registered before its arm body was walked by the passes that type
> it. `collect_stmt`'s SSelect already registered it; `sacc_stmt`'s SSelect (the
> string-accumulator pass, the original failure point) and `mono_stmt`'s SSelect (exposed
> when Gap 3 added `type_of(subj)` to mono's SMatch) did not. Fix: both now register the
> recv binding (type = channel element) before walking the arm body, mirroring
> `collect_stmt`. Verified MATCH: plain enum, generic enum, and a string accumulator grown
> inside a select arm. Regression test `tests/select_enum_match.ty`.

---

## GAP 3 — recursive generic ENUM with array payload (SHARED) — CLOSED 2026-06-27

> **CLOSED.** `enum Tree($T): Leaf($T) Node([Tree($T)])` constructed `Node([Leaf(10),
> Leaf(20)])` now compiles + runs identically on both compilers. Three distinct bugs:
>
> **tychoc (`src/tychoc.c:8278`):** the enum forward-decl loop skipped generic templates
> (`if generic continue`), but a recursive generic enum with an array payload interns a
> DEAD template array composite `TychoArrC<n> { E_Tree **data; }` (element = the
> uninstantiated template cell) that still gets emitted → cc `unknown type name 'E_Tree'`.
> Fix: forward-typedef generic enum templates too (the struct tag loop above already
> does this for `S_<name>`); the body/payload/copy loops still skip generics, so the
> template stays an incomplete pointer-only type.
>
> **tychoc0, two bugs:**
> 1. `recover_enum_binds` (~10639): binding inference for `Node([Tree($T)])` matched the
>    payload pattern `[Tree($T)]` against arg type `[Tree__int]`, but neither
>    `match_typaram_str` (parened app vs bare instance) nor `recover_enum_binds` (rejects
>    a `[...]` pattern) could bind `$T` → `unknown type '$T'`. Fix: descend through
>    array/map wrappers before the enum-app provenance recovery.
> 2. `mono_stmt`'s SMatch (~11078) walked each arm body WITHOUT registering the arm's
>    payload bindings, so a foreach in a generic-enum arm (`Node(kids): for k in kids`)
>    — which desugars to `_fc := kids`, and mono types that rhs — hit `unknown variable
>    'kids'`. Fix: register each arm's bindings (new `match_arm_ptypes` helper, handles
>    Option/Result/wildcard/user-enum) before `mono_block`, mirroring codegen's SMatch.
>
> Verified MATCH (tychoc vs tychoc0): int fold (42) and string fold (abcd) over a nested
> `Tree`, plus foreach in a generic-enum match arm. Regression test
> `tests/generic_enum_array.ty` (+ golden). Gates: `make test` 241/0, `make fixpoint`
> (B==C + differential), `make corelib` 3-way, `make fuzz-quick` 80/80 — all green.
> Note: the tychoc.c forward-typedef fix is reference-only — tychoc0 doesn't emit the
> dead template composite, so no mirror was needed (verified: its emitted C compiles).

---

## Feature accept/reject diff campaign — 3 gaps CLOSED 2026-06-27

A systematic tychoc-vs-tychoc0 differential (each probe classified as match / both-reject /
tychoc0-missing / fail-open / miscompile) catches accept/reject divergences the output-only
fixpoint is blind to. Run in three rounds (~94 probes total); **all 3 gaps it found are now
fixed**, and round 3 came back clean.

**Round 1 (~42 single-feature probes) — 2 gaps:**

- **Early bare `return` in `main()` — tychoc0 MISCOMPILE.** tychoc0 inlined main's body into
  `int main()`, so an early `return` (anywhere but the last line) emitted `return;` into an
  int-returning function → cc error `'return' with no value`. A whole class of valid programs
  couldn't self-host. Fix (`gen_func` ~6910): emit the body as `static void h_main(Arena*)`
  + a thin `int main` wrapper that calls it, mirroring tychoc. Test `tests/early_return_main.ty`.
- **`str(char)` — tychoc0 FAIL-OPEN.** `str()` fell through to `i2s` for a char arg (and
  `char +/- int` stays a char), silently printing the byte value where tychoc rejects it.
  Fix (`str` codegen ~4558): reject any non-int (after str/float/bool) with tychoc's exact
  message; only int / newtype-of-int reaches `i2s`. Test `tests/reject/str_char.ty`.

**Round 2 (~23 combination probes) — 1 gap:**

- **Composite-keyed map LITERALS — tychoc0 FAIL-OPEN (fixed by EXTENDING tychoc).** `[Red: 1]`
  (fieldless enum), `[K(1): 10]` (struct), `[(1,2): 100]` (tuple) were rejected by tychoc
  (`map keys must be string or int`) but accepted by tychoc0 — yet the same key kinds already
  worked on BOTH via a declared map + assignment (`m := []Col: int; m[Red] = 1`). Only the
  literal path diverged. Direction chosen: extend tychoc (real capability, machinery exists),
  not restrict tychoc0. Fix (`src/tychoc.c` map-literal typecheck ~3611): drop the redundant
  `kt != STRING && kt != INT` guard so `map_of` is the single key validator on both the
  declared and literal paths (it already routes composite keys to `mapc_of`, returns T_VOID
  only for float/bool/non-hashable). Literal codegen already used the same family fns. Test
  `tests/map_literal_composite_key.ty`. Commit `7d54add`.

**Round 3 (~30 deeper combination + safety-edge probes) — 0 gaps.** Nested data (map-of-array,
struct-with-map-field, 3D arrays), newtype-over-{array,struct,map-key}, soa pop/len/field-mutate,
concurrency (parreduce-product, channel-of-heap-struct, two-recv select, spawn-returns-array),
abort edges (div-zero, nested OOB, pop-empty, substr-OOB all abort consistently), string ops,
generic+closure combos (closures-in-array, returned closures, `or_return` on Result) — all MATCH.

Gates at each landing: `make test` (→245/0), `make fixpoint` (B==C + differential), `make corelib`
3-way, `make eqparity` (504/504 for the map-key change), `make fuzz-quick` 80/80.

**Reusable harnesses** (in the session scratchpad): `featdiff.py` (round 1), `featdiff3.py`
(round 2), `featdiff4.py` (round 3). Re-run after any language change to catch new accept/reject
drift — cheap, and the highest-yield finds are feature COMBINATIONS and safety edges, not single
shapes. Lesson reaffirmed: when a fail-open is found, decide DIRECTION (tighten tychoc0 to match
the reference, or extend tychoc to make the capability real) — both are valid, it's a design call.

---

## Verified-solid this round (no bug)

Fundamentals are clean (byte-identical, correct): substr/split/find/string-index, signed
int div/mod (`-7/2=-3`, `-7%3=-1`), overflow wrap, char ops, array slice/pop, nested array
mutation, string-concat loops, generic structs everywhere (arrays/maps/channels/fn-returns/
nesting/closures), concurrency × composites (incl. closures through channels), recursive-enum
ASTs. `empty-array pop` and `div-by-zero` abort cleanly on both.

Probe-errors / non-bugs noted: `float(3)`/`int(3.9)` (no such cast fns — `float`/`int` are
type names; both reject); `soa [P]` (wrong soa literal syntax); `[fn(...)]` empty-array
literal (it's `[]fn(...)`, not `[fn(...)]`); the partial-mention generic form
`W($A,$B): x: Box($A)` is deliberately REJECTED by tychoc (and tychoc0 just doesn't
front-end-validate it — by design).

---

## Probing method (how to continue the dogfood)

Differential harness pattern (tychoc vs tychoc0, byte-identical = MATCH):

```sh
T=$(mktemp -d); ./tychoc compiler/tychoc0.ty -o "$T/tc0" 2>/dev/null
P() { label="$1"; shift; printf '%s\n' "$@" > "$T/m.ty"
  ./tychoc "$T/m.ty" -o "$T/a" 2>/dev/null && A=$(timeout 10 "$T/a" 2>&1) || A="T-FAIL"
  "$T/tc0" "$T/m.ty">"$T/b.c" 2>/dev/null && cc -O2 -fwrapv -o "$T/b" "$T/b.c" -lpthread -lm 2>/dev/null \
    && B=$(timeout 10 "$T/b" 2>&1) || B="T0-FAIL"
  [ "$A" = "$B" ] && echo "[MATCH] $label" || echo "[DIFF] $label  T=[$A] T0=[$B]"; }
```

Lessons: the highest-yield bugs are **feature COMBINATIONS** (generic × container/channel/
fn-return, map × closures) and **safety edge cases** (OOB, churn) — single-shape tests and
the fixpoint differential miss both. When a tychoc0-only fix is tempting, ALWAYS check it
doesn't turn a clean compile-error into a fail-OPEN miscompile (GAP 2 lesson). `_` match
wildcard DOES work on both compilers (an old note said it didn't).
