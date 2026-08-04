# What comes next

> 2026-08-04: the arena-observability phase is complete (325bc6b). New
> owner-directed phase: the generic hash the intern phase deferred. Verified
> before admission: intern with struct keys ALREADY canonicalizes — the map
> hashes composite keys natively (`map_of` accepts any hashable composite,
> `gen_hash` emits the per-type hash) — so the gap is not intern itself but the
> *public* facility: no program can hash a struct/tuple/array today; `core:hash`
> hashers take strings only.

## Phase 1 — `hash(x)` builtin + intern generic keys

1. **`hash(x)` builtin** (`src/tychoc.c`): resolve inline next to `str` (arity 1,
   arg type must be a legal map-key type — the `where hashable(T)` set: int,
   string, newtypes over them, fieldless enums, composites of hashable leaves;
   bare float/bool/char/bytes rejected, matching `map_of`), returns `int`
   (the map's full 64-bit hash as a signed int). Codegen via `gen_hash`
   (`src/tychoc.c:9178`) — the same per-type emitter the map keys use, so
   equal-by-`==` values hash equal by construction. **Seeded per process like
   map keys** — documented as in-process use (tables/dedup), not checksums
   (that is `fnv1a_32`/`djb2`/`sdbm`/`crc32`'s role); a deterministic variant
   would mean duplicating three generated hash families plus new runtime
   hashers — wide surface, no in-process caller. Add `hash` to
   `is_pure_builtin` (a discarded `hash(x)` warns like `len(x)`).
   Emission: `tycho_hash_S_*/T*/arr_C*` are gated on `struct_keyused` (map-key
   use only) — a `hash()` on a type never used as a map key needs its hash
   function emitted, so add a parallel `hash_keyused` tracker (arg types
   recorded at resolve, reachability via the existing `struct_in_key`) and OR
   it into the five prototype/body gates.
2. **intern generic keys**: works today (verified — `Point(1,2)` keys
   canonicalize); extend `corelib/test/intern/main.ty` with struct and tuple
   keys, update the package header and the corelib guide (drop the
   "string/[int] keys first" scoping).
3. **Docs**: `hash(x)` entry in `docs/spec/16-builtins.md` (29.x); `core:hash`
   header gains the generic sibling; guide `hash` + `intern` entries updated.
4. **Tests**: generic-hash assertions in `corelib/test/hash/main.ty`
   (equal→equal across string/struct/tuple/array, inequality on distinct ints —
   deterministic, SplitMix is a permutation; on full 64-bit values, so
   collision-free per run), a reject fixture for a non-hashable arg
   (`tests/reject/`), the intern struct/tuple keys.

Gate: `make test` (~8 min — compiler change; existing emission is unchanged
unless `hash()` is used, `hash_keyused` is empty then, so fixtures stay
byte-identical) + `make corelib` (hash + intern tests) + `make goldens-check`
(re-recorded/added goldens) + `make selfhost-check` (compiler changed) + doc
gates. The 21+ `src/tychoc.c` citations shift again (5th time) — re-point
mechanically.
Expected: 589 fixtures green plus the new reject fixture; hash + intern goldens
locked; fixed point holds.

## Not in scope

- A deterministic (cross-run) generic hash: needs the duplicate hash families
  — follow-on if a caller needs cross-run composite checksums.
- Hash of bare float/bool/char/bytes (not map-key types; a float hashes as a
  composite leaf).
- The build-tool candidate remains shelved.

> Phase 1 evidence — 2026-08-04: all gates green. `make test` 590/0 (589 +
> `reject/hash_float`), `make corelib` all green (hash + intern tests updated),
> `make goldens-check` ok, `make selfhost-check` green (existing emission is
> unchanged — `hash_keyused` is empty unless a program calls `hash()` — so the
> fixed point holds), doc gates ok.
>
> The key finding from admission: intern with generic keys ALREADY worked — the
> map hashes composite keys natively — so the phase's real deliverable is the
> public `hash(x)` builtin, and the intern half is the extended test + docs.
> `hash(x)` resolve `src/tychoc.c:5947-5960` (arity 1, arg must satisfy the
> map-key rule, records composite args), codegen `:9661-9663` via `gen_hash`
> (`:9208` — the map's own per-type emitter), `is_pure_builtin` gets `hash`
> (a discarded call warns). Type-emission gate `hash_keyused`
> (`:1450-1454`) OR'd into the ten hash-function gates, so a `hash()` on a
> struct that is never a map key still emits `tycho_hash_S_*`/`T*`/`arr_C*`.
>
> Seeded-vs-deterministic decided SEEDED (the map's own contract): deterministic
> would mean duplicating three generated hash families + new runtime hashers,
> for a facility whose documented role is in-process tables/dedup — the
> deterministic string hashers remain the checksum path. Tests assert only
> properties that hold regardless of seed: equal→equal (string/struct/tuple/
> array/nested), inequality on distinct small ints (deterministic — SplitMix is
> a permutation on the full 64-bit), the reject case
> (`tests/reject/hash_float.ty`). Docs: 16-builtins §29.7 gains `hash(x)` with
> the map-key contract; core:hash + intern headers and the guide updated —
> intern now documents any map-key type. 80 citations re-pointed (+10/+30/+34
> by region — the `hash_keyused` insert at 1442 shifted refs below the usual
> regions for the first time).
