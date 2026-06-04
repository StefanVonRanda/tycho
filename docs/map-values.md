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

## Decisions to keep

- Keep the 4 hardcoded int/float maps as-is (byte-identical, zero churn); composite
  maps are the NEW path for other value types. (Unifying everything onto the
  composite scheme is a later cleanup, not needed for the feature.)
- Bool values: fold to int (no separate map). Nested maps as VALUES (`[string:[string:int]]`)
  fall out of the general scheme but defer until 1–6 are solid.
