# Plan: arbitrary map value types

Today maps are 4 hardcoded types: `[string:int]` `[string:float]` `[int:int]`
`[int:float]` (`T_MAP_SI/SF/II/IF`). Goal: any value type — `[string:string]`,
`[string:Struct]`, `[int:[T]]`, `[string:Option(T)]`, etc. (procedural language;
no generics needed — monomorphize per `(key,value)` used, exactly like the
composite-array scheme already does for `[Struct]`/`[[T]]`).

## Architecture (verified by reading the source)

- **hierc** (`src/hierc.c`): compound types are interned in side tables with a
  base range and generated monomorphic runtime — `g_arrtypes`+`arrc_of`+`T_ARRC_BASE`
  for `[Struct]`, likewise `g_opttypes`/`g_restypes`/`g_tuptypes`. Composite type
  runtime is emitted by `emit_aggregate()` (src/hierc.c:4314). The 4 hardcoded maps
  come from the embedded `hier_rt_embed.h` (`HierMapSI` etc., ~106 lines).
- **hierc0** (`compiler/hierc0.hi`): maps are ALREADY generated, parameterized by
  `(k,v)` — `gen_map_type(k,v)` / `gen_map_fns(k,v)` (lines ~3861/3875), `mstruct`=
  `Map_<k>_<v>`, `mfam`=`map_<k>_<v>`. So hierc0 is closer; the gap is heap-value
  deep-copy on put + mangling arbitrary `v`.
- Fixpoint is `B==C` (hierc0 self-consistency) + **differential on program OUTPUT**
  (not emitted-C). So hierc and hierc0 may implement composite maps differently as
  long as observable behavior matches — a real simplification.

## The one hard part: heap value lifetimes (RULE 5)

For a heap value type V (string/struct/array/option), every value crossing the
map boundary must be deep-copied into the right arena, or it dangles:
- **`map_set`/literal/put**: deep-copy V into the MAP's owning arena (mirror
  `copy_into` for array elements — `hier_arr_str_push` already does this for str).
- **`map_get(m, k, dflt)`**: returns V. The stored V lives in the map's arena; the
  result must be deep-copied into the CALLER's arena (the map may be a borrow that
  outlives nothing, or freed before the result is read). The `dflt` is also heap →
  deep-copy it too. THIS is the UAF-prone site; needs LSan + a borrow-vs-owned test.
- **`map_copy`** (pure `map_set` accumulator does copy-then-put): deep-copy each V.
- **`keys(m)`** unchanged (returns `[K]`).

## Steps (each a green, committable checkpoint)

1. **hierc type system**: `T_MAPC_BASE` range + `g_maptypes[]{key,val}` + `mapc_of(k,v)`;
   extend `is_map`/`map_key`/`map_val`/`c_type`/`type_name`; route `map_of(k,v)` to
   `mapc_of` when v isn't int/float. (No emit yet → dead, existing tests byte-identical.)
2. **hierc runtime emit**: an `IS_MAPC` case in `emit_aggregate` emitting the map
   struct + with_cap/find/slot/put(+deepcopy)/get(+deepcopy)/has/del/copy/set/keys,
   parameterized by `c_type(key)`/`c_type(val)` and the per-V copy/compare. Mirror
   the embedded `HierMapSI` but with `copy_into(val, arena, ...)` at put/get.
3. **hierc resolve+codegen**: `map_get/set/has/del` for composite maps; `map_set`
   deep-copies value; `map_get` deep-copies stored value + default into the result
   arena. Map literal `["a": Point(..)]` deep-copies values.
4. **Verify hierc alone**: `[string:string]`, `[string:Struct]`, `[int:[int]]`
   programs compile+run; LSan clean (the lifetime check); existing make test +
   fixpoint still green (hardcoded maps untouched).
5. **hierc0 parity**: generalize `gen_map_fns(k,v)` to heap V (deep-copy put/get),
   `map_val`/`mstruct`/`mfam` for arbitrary V, `note_*` so the V's own runtime
   (e.g. a `[int]` value's array family) is emitted. Differential-test vs hierc.
6. **Tests + fuzzer**: `tests/map_values.hi` (golden, incl. a value-semantics
   self-check: mutate a copy, assert original map unchanged); extend `fuzz/gen.py`
   to generate string/struct-valued maps (the value-semantics oracle catches a
   bad deep-copy that the differential alone can't).

## Step 1 DONE (commit 43dc86e)

hierc interning scaffolding landed dormant: `T_MAPC_BASE 32768`, `g_maptypes[]`,
`mapc_of` (marked `__attribute__((unused))` until step 3 removes it), and IS_MAPC
in `is_map`/`map_key`/`map_val`/`c_type` (→ `HierMapC%d`)/`type_name` (→ `[K: V]`).
`map_of` still returns T_VOID for non-int/float values (dormant).

## Step 2 map (traced, ready to execute) — STRING KEYS ONLY first

Scope step 2/3 to `[string: V]` (arbitrary V); `[int: V]` is a SEPARATE scheme
(int maps use an occupancy array `occ[]`, not the NULL/tombstone sentinel — defer).

Emit a `HierMapC<id>` runtime by parameterizing `runtime/hier_rt.c`'s `HierMapSI`
(lines 725-847: struct + with_cap/find/slot/put/del/copy/set/del_pure/get/has/keys)
over the value type. Only the VALUE changes (keys stay `char**`, FNV, strcmp,
`hier_str_copy`); replace `long vals`/`long v` with `c_type(V)` and:
- `put`: `m->vals[s] = <copy_into(V, "a", "v")>` (deep-copy the value into the map
  arena — for V=string `hier_str_copy`, V=struct the struct copy, etc.).
- `copy`: already deep-copies via put, so values re-home correctly.
- `get`: returns the stored value BY VALUE (a borrow into the map arena); the
  CALLER's codegen `copy_into`s it at the bind site, same as any heap return.

Emission is multi-pass (mirror the composite-array passes): typedef pass near
hierc.c:4451, forward-decls near 4544, bodies near 4595 — add a `g_maptypes` loop
to each. Add an `IS_MAPC` case to `copy_into` (hierc.c:2960) → `hier_map_C%d_copy`.

## Step 3 map (the UAF-prone core)

- Flip `map_of`: `(string key, V not int/float)` → `mapc_of(T_STRING, V)`;
  `(int key, V not int/float)` → still T_VOID (deferred). Drop mapc_of's `unused`.
- resolve `map_get/set/has/del` already use `map_key`/`map_val` — they generalize
  for free EXCEPT the codegen, which dispatches on `map_fn(t)` (only si/sf/ii/if).
  Add an IS_MAPC branch in each map-builtin codegen emitting `hier_map_C<id>_*`.
- `map_set(m,k,v)` value arg: `copy_into(V, mapowner, v)` before put.
- `map_get(m,k,dflt)`: emits `hier_map_C<id>_get(m,k,dflt)`; the RESULT is a borrow
  into m's arena → the bind/use site must `copy_into` it (verify this is the
  default path for a heap call-result; if not, force it). The `dflt` arg is also
  heap → `copy_into` it into the result owner. THIS is the lifetime seam: test
  `v := map_get(m,"x",d); m = map_set(m,"x",other); use(v)` stays correct under
  LSan (v must be an independent copy, not aliasing m's mutated slot).

## Steps 2-3 DONE (89b4693, ef6c749) — hierc [string: V] works

Emit + codegen + map_of routing all landed and LSan-verified in hierc:
[string:string] and [string:Struct] full CRUD + literals + keys, the get-returns-
borrow lifetime fixed via copy_into(map_val) on map_get, existing maps byte-
identical (make test + fixpoint green). map_rt(t,op) dispatches hardcoded vs
hier_mapc<id>. T_MAPC_BASE=32768; IS_SOA was bounded `< T_MAPC_BASE` (it was
open-ended and swallowed map types). int-keyed composite values + composite `==`
deferred.

## Step 5 — hierc0 parity (PROPER PLAN, checkpointed)

### Scope discovery (verified, simplifies the work)

hierc0 has **no map-value type gate**: `is_map(ty)` (1495) only checks the type
string starts with `{`; `parse_type` for `[]K: V` (848-853) and `type_of` for a
map literal (1811) build `{K:V}` from ANY key/value types with no restriction. So
hierc0 ALREADY type-checks `[string:str]` / `[string:Struct]` — it just *generates*
broken C (the generator hardcodes `cv = long/double`). **Step 5 is therefore only
the generator + the map_get lifetime; NO type-checker change.** (The earlier note
about relaxing a checker was wrong — there's nothing to relax.)

Tools already present: `cty(dc,ctx,v)` (value C type; one trailing space — strip),
`cp_field(dc,ctx,v,ar,src)` (deep-copy ANY value into `ar`; passthrough for
scalars/non-heap structs), `mangle(v)` (type→identifier; `mangle("int")=="int"`,
`mangle("float")=="float"` ⇒ existing maps stay byte-identical), `map_key`/`map_val`.

### Checkpoint 5a — generalize the generator (BYTE-IDENTICAL refactor, green)

This is a safe, committable checkpoint: it changes HOW the generator is written
but not WHAT it emits for the only maps that exist today (int/float-valued), so
`make fixpoint` B==C and the differential stay green. It does not yet activate
anything new (no composite map is exercised until a test uses one in 5b).

- `mstruct`/`mfam` (1515-1519): `"Map_"/"map_" + map_key(ty) + "_" + mangle(map_val(ty))`.
- `gen_map_type` (3861) + `gen_map_fns` (3875): signature → `(dc, ctx, ty)`;
  inside, `k := map_key(ty)`, `v := map_val(ty)`, `cv := cty(dc,ctx,v)` minus the
  trailing space, and `M`/`f`/`s` built with `mangle(v)`.
  - in `put`: `m->vals[t] = val;` → `m->vals[t] = <cp_field(dc,ctx,v,"ar","val")>;`
    (scalar ⇒ `cp_field` returns `val` ⇒ byte-identical).
  - in `_eq`: append the value compare `|| b.vals[t] != a.vals[i]` ONLY when v is
    scalar (`int`/`float`/`char`); for heap v omit it (struct/array `!=` is a C
    compile error, and composite `==` is dead — hierc rejects it). For existing
    int/float maps v is scalar ⇒ the compare is present ⇒ byte-identical.
  - apply to BOTH the int-key and string-key branches uniformly (composite values
    only ever reach the string-key branch, but the int branch stays byte-identical
    since its v is always scalar).
- call sites: `gen_map_type` (4348) + `gen_map_fns` (4409) → pass `(dc, ctx, ets[i])`.
- **Gate:** build A→B, `make fixpoint` (B==C), `make test` differential — all
  green, and the emitted runtime for existing maps is byte-identical (diff a
  before/after `--emit-c` of a map test to confirm). Commit 5a.

### Checkpoint 5b — activate + the lifetime fix (the verifying step)

- map_get codegen (~2222): wrap the result in `cp_field` —
  `cp_field(dc, ctx, map_val(mt), ctx.owner, "<mfam(mt)_get(...)>")`. Scalar ⇒
  passthrough (int/float byte-identical); heap V ⇒ deep-copy the borrow into the
  caller's arena (the lifetime/UAF fix, mirror of hierc's copy_into-on-get).
  `map_set` value already flows through the runtime `put` (with_owner "0"), which
  now deep-copies via `cp_field` — no codegen change. Verify the map LITERAL
  codegen path also routes through `mfam`/put (it does via EMapLit) — no special case.
- **Gate (the real verification):** B compiles a `[string:str]` and a
  `[string:Struct]` program and its output matches hierc's; the lifetime test
  (`v := map_get(m,"a",d); loop map_set to force rehashes; assert v unchanged`)
  is correct and ASan/LSan clean under hierc0's emitted C; `make fixpoint` green.

### Checkpoint 5c — tests + fuzzer (this is step 6)

- Add `tests/map_values.hi` (+ `.out`): CRUD on `[string:string]` and
  `[string:Struct]`, `keys`, the lifetime self-check, and a value-semantics
  self-check (copy the map, mutate the copy, assert the original unchanged). Now
  in the differential (both compilers handle it) ⇒ `make test` covers it.
- Extend `fuzz/gen.py`: let `map_accum`/map-literal kinds pick a string or struct
  value (not just int/float). The value-semantics oracle catches a bad deep-copy
  the hierc-vs-hierc0 differential alone cannot. Run the campaign FAIL=0.

### Notes / order
Do 5a → commit (green) → 5b → commit (green, with the test) → 5c → commit. If 5a's
diff isn't byte-identical, STOP and reconcile before 5b — a non-identical 5a means
a generator detail (cty spacing, mangle, eq) regressed an existing map.

## Decisions to keep

- Keep the 4 hardcoded int/float maps as-is (byte-identical, zero churn); composite
  maps are the NEW path for other value types. (Unifying everything onto the
  composite scheme is a later cleanup, not needed for the feature.)
- Bool values: fold to int (no separate map). Nested maps as VALUES (`[string:[string:int]]`)
  fall out of the general scheme but defer until 1–6 are solid.

## Status — composite maps COMPLETE (both compilers); NO deferred value type left

Verified working + tested (`tests/map_values.hi` golden + ASan/LSan, `compiler/tests/`
differential, `make fixpoint` B==C):
- `[string: V]` AND `[int: V]` (the occupancy-array scheme, `0` is a real key) for ANY
  value type — scalar / string / struct / **array (`[int: [int]]`)** / **nested map
  (`[string: [string:int]]`)** — full CRUD, literals, `keys`, `map_del`, default on miss.
- Heap-value lifetimes: `map_get` returns an INDEPENDENT deep copy (survives later
  rehashes); copying a map deep-copies values (mutating the copy can't touch the original).
- Composite `==`: deep value equality over string- and struct-valued maps, insertion-order
  independent. (So the earlier "int-keyed composite values + composite `==` deferred" note
  is RESOLVED — both work.)
- `map_has` returns a real `bool` in BOTH compilers (`str(map_has(..))` → `true`/`false`);
  hierc0 previously typed it (and `write_file`/`to_bool`) as `int`, a `str()` divergence
  exposed by the bool-type work — now fixed.

### Fixed this pass — nested-map VALUES in hierc0
`[string: [string:int]]` compiled in hierc but emitted broken C in hierc0: the outer
map struct (`Map_str_map_str_int`, member `Map_str_int* vals`) was typedef'd BEFORE the
inner `Map_str_int` (undefined name). Root cause: `note_arr_types` pushed a map onto the
emit list BEFORE recursing into its value type, so a map-valued map ordered outer-before-
inner. Fix (compiler/hierc0.hi): recurse into `map_val`/the key array FIRST, push the map
LAST — dependency order, so an inner map (and its array families) is always emitted before
any map that holds it. Safe for fixpoint: it only reorders type decls (program output and
B==C self-emission unchanged). Array values were already fine (their family was noted).

### Genuinely separate (not a map gap)
- A bare `[]` empty-array literal as a `map_get` default needs a value type from context
  (`d := []int; map_get(m, k, d)`); that's the general empty-array type-inference rule,
  not map-specific. `m[k]` stays a mutation-only place (read via `map_get`).
