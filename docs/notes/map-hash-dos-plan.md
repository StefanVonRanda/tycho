# Map hash-flooding hardening — implementation plan

> Internal working note (not part of the public docs surface). Source: a 2026
> podcast review surfaced this; verified against the code. Design validated on one
> map family, then reverted — this plan is the dedicated implementation pass.

## 1. The finding (verified, current)

`runtime/tycho_rt.c` `tycho_si_hash` is an **unseeded** FNV-1a (basis
`1469598103934665603`, mult `1099511628211`); the int map hash `tycho_ik_hash` is
an **unseeded** SplitMix64 finalizer. A program that keys a map on **untrusted
input** is exposed to an algorithmic-complexity DoS: an attacker crafts colliding
keys, every probe walks a long chain, and lookups degrade to O(n²). This is the
class Python/Ruby/etc. fixed in 2011 with seeded SipHash.

Reproduction shape: read attacker-controlled strings, put them in a `[string: V]`
map, observe lookup time blow up.

(The review's other technical claims were **outdated**: `corelib/crypto/` ships
OpenSSL-backed crypto via FFI; generics/monomorphization are shipped. "No
user-defined traits/typeclasses" is true and **permanent by design** — the
language is frozen on this; we are *securing what exists*, not adding to it.)

## 2. Goal & approach (two phases)

Hardened, **deterministic**, byte-safe maps:

1. **Phase 1 — insertion-ordered iteration.** Make `keys()` / `for k in m` iterate
   in **insertion order** instead of bucket order. This makes a random hash seed
   **output-invisible** (observable map order no longer depends on the hash), so
   fixpoint `cA==cB` survives differently-seeded runs and goldens stay
   deterministic.
2. **Phase 2 — seeded keyed hash.** Replace FNV/SplitMix with a hash keyed by a
   **random per-process seed** (SipHash-1-3 for strings; seeded SplitMix64 for
   ints), seeded once at program start. Safe *only after* Phase 1.

Land Phase 1 fully green **before** starting Phase 2.

## 3. Phase 1 — validated design: the `ord` insertion-order vector

Add to every map struct a vector of the **live keys in insertion order**:

- struct gains `KEY *ord; long nord; long ocap;` (`char*` for string families,
  `long` for int families).
- `with_cap`: `ord = NULL; nord = 0; ocap = 0;`.
- `put`: on a **new** key only (not on update), append the **stored** key
  (pointer/value) to `ord` via a geometric-growth helper. In the **rehash** block,
  **carry `ord` unchanged** (`n.ord=m->ord; n.nord=m->nord; n.ocap=m->ocap;` before
  `*m=n`) — `ord` holds arena-stable key pointers, independent of slot reshuffle,
  so it needs **no rebuild** on rehash. `slotptr` has the identical rehash/new-key
  paths.
- `del`: **shift-remove** the key from `ord` (by pointer identity for strings, by
  value for ints), preserving order, **before** tombstoning the slot.
- `copy`: iterate `src.ord` (insertion order) and `put` each (look its value up via
  `find`), so the copy preserves order.
- `keys()`: walk `ord` (O(n), insertion order) instead of `for i in 0..cap if live`.

**Correctness:** delete+reinsert → reinsert appends to the END (Python-dict
semantics); update keeps original position. No sort, no per-entry sequence number.
The lookup/probe path is **untouched** (lowest risk).

### 3a. Reference implementation (the SI family — string→int — in `tycho_rt.c`)

This was implemented and verified, then reverted. Apply verbatim, then replicate
for SF/II/IF and the tychoc0-generated families.

Struct + helpers (after the `TychoMapSI` typedef / `tycho_map_live`):

```c
typedef struct { char **keys; long *vals; long len; long cap; long used; char **ord; long nord; long ocap; } TychoMapSI;

static void tycho_ord_push_s(Arena *a, char ***ord, long *nord, long *ocap, char *k) {
    if (*nord == *ocap) {
        long nc = *ocap ? *ocap * 2 : 8;
        char **nb = (char **)arena_alloc(a, (size_t)nc * sizeof(char *));
        for (long i = 0; i < *nord; i++) nb[i] = (*ord)[i];
        *ord = nb; *ocap = nc;
    }
    (*ord)[(*nord)++] = k;
}
static void tycho_ord_del_s(char **ord, long *nord, const char *k) {   /* remove by pointer identity, keep order */
    for (long i = 0; i < *nord; i++)
        if (ord[i] == k) { for (long j = i + 1; j < *nord; j++) ord[j - 1] = ord[j]; (*nord)--; return; }
}
```

`with_cap`: add `m.ord = NULL; m.nord = 0; m.ocap = 0;` next to `m.used = 0;`.

`put` and `slotptr` rehash block — before `*m = n;` add:
```c
        n.ord = m->ord; n.nord = m->nord; n.ocap = m->ocap;   /* carry insertion order */
```
`put`/`slotptr` new-key branch — after `m->keys[s] = tycho_str_copy(a, k); m->len++;` add:
```c
        tycho_ord_push_s(a, &m->ord, &m->nord, &m->ocap, m->keys[s]);
```
`del` — before `m->keys[s] = TYCHO_MAP_TOMB;` add:
```c
    tycho_ord_del_s(m->ord, &m->nord, m->keys[s]);
```
`copy`:
```c
    for (long j = 0; j < src.nord; j++) { long s = tycho_map_si_find(src, src.ord[j]); tycho_map_si_put(a, &r, src.ord[j], src.vals[s]); }
```
`keys()`:
```c
    TychoArrStr r = tycho_arr_str_with_cap(a, m.nord);
    for (long j = 0; j < m.nord; j++) tycho_arr_str_push(a, &r, m.ord[j]);
    return r;
```

### 3b. The other C families (`tycho_rt.c`)

Same edits, per family. Int families use `occ[]` (0/1/2, not NULL/TOMB) and a
**`long`-keyed** ord helper (`tycho_ord_push_i` / `tycho_ord_del_i`, remove by
value). `eq` is content-based — leave it (order-independent).

- **SF** (`TychoMapSF`, string→double): identical to SI (string ord). ~line 1474.
- **II** (`TychoMapII`, int→int): int ord; del removes by value `k`. ~line 1623.
- **IF** (`TychoMapIF`, int→double): int ord. ~line 1624/1734.

GOTCHA seen during the aborted attempt: II and IF share several **identical** C
lines (the new-key `put` append, the `m->occ[s]=2; m->len--` del), so a careless
`replace_all` touches both — fine ONLY if both families are fully converted in
lockstep (`with_cap` init + rehash carry must already be in place, or IF maps
push to an uninitialized `ord` → corruption). Convert each family completely
before building.

### 3c. The tychoc0 preamble-generated families (`compiler/tychoc0.ty`)

tychoc0 emits its runtime as preamble C-strings (the `gen_map_*` functions,
`~7079–7145`). Two key families: `Map_str_V` (`{char** keys; CV* vals; long len,
cap;}` — NO `used`, NO tombstones; delete REBUILDS) and `Map_int_V` (`{long* keys;
CV* vals; unsigned char* occ; long len, cap;}`). Mirror the same `ord` mechanism
in the emitted strings:
- struct gets `KEY* ord; long nord; long ocap;`.
- `_cap` (with_cap) inits ord.
- `_put`: rehash carry + new-key append. (tychoc0 `_slot` has no tombstone reuse.)
- `_keys`: walk ord.
- delete is a REBUILD (`_del` makes a fresh map reinserting survivors) — iterate
  the source via `_keys`/ord order so the rebuilt map preserves insertion order;
  no in-place `ord` shift needed there.
Add `ord` push/del helpers to the preamble too (or inline).

The comment at `compiler/tychoc0.ty:7062` ("keys() in bucket order … mirroring
byte-for-byte is what makes keys()-order match") becomes **obsolete** — insertion
order is hash/rep-independent, so the two runtimes match trivially. Update it.

### 3d. keys() lowering (no change needed, just confirm)

`keys(m)` lowers to `<family>_keys(arena, m)` in both: `src/tychoc.c` ~5901
(`map_rt(..., "keys")`); `compiler/tychoc0.ty` ~4315 (`mfam(...) + "_keys"`).
`for k in m` desugars to `keys(m)` then array iterate. So only the `_keys` bodies
change.

## 4. Goldens to regenerate (bucket → insertion order)

`tests/`: `maps.ty`, `int_maps.ty`, `float_maps.ty`, `map_delete.ty`,
`map_param_composite.ty`, `map_values.ty`, `enum_key.ty`, `newtype_key.ty`,
`accum_in_match.ty`, `inference.ty`. `examples/`: `wordcount.ty`, `site/main.ty`,
`fetch/main.ty`. (Re-grep `keys(` and map `for` before finalizing — list may drift.)
Regenerate each `.out` from the rebuilt `tychoc`, then confirm tychoc0 matches.

## 5. Phase 2 — seeded keyed hash (after Phase 1 is green)

- Add SipHash-1-3 (string keys) keyed by a random 128-bit key; for int keys, mix
  the seed into SplitMix64 (`splitmix(k ^ seed)`).
- Seed ONCE at program start from the OS (`getrandom`, fallback `/dev/urandom` or
  time⊕pid), stored in a runtime global. Hook: the `main()` emit —
  `src/tychoc.c` ~8391 and `compiler/tychoc0.ty` ~6436 (neither seeds today). Set
  the seed before the root arena / first map use.
- Mirror in BOTH runtimes (tycho_rt.c + tychoc0 preamble), behaviorally identical.
- Determinism check: fixpoint runs A and B as **separate processes with different
  seeds**; `cA==cB` must STILL hold (proves nothing but `keys()` is order-
  observable). If it ever fails, something depends on internal probe order — find
  and fix it.

## 6. Verification checklist (run after Phase 1, and again after Phase 2)

`make tychoc` → `make fixpoint` (byte-identical) → `make test` (regenerated
goldens) → `make corelib` → `make conc` → `make ffi` → all 4 parity lanes
(`eqparity`/`typeparity`/`unaryparity`/`parforparity`) → `make fuzz N>=80`
(differential — stresses maps). Add a regression that asserts **insertion order**
(`["b":1,"a":2]` → `keys()` == `[b, a]`) and, for Phase 2, that two runs of a
keys()-using program give **identical** output (determinism) while the internal
seed differs.

## 7. Invariants the change must preserve

- **Both runtimes change in lockstep** — keys() order must match between tychoc
  and tychoc0 (parity tests compare output). Insertion order makes this trivial.
- **fixpoint cA==cB** — both are tychoc0-built; deterministic insertion-order
  iteration keeps it byte-identical even with a random seed.
- **Lookup path untouched** in Phase 1 (lowest risk); only iteration changes.
- The map is THE core structure used by the compiler itself — a bug miscompiles
  everything. Convert each family completely, build, and run fixpoint+test before
  moving on. Don't leave a half-converted tree.

See also the memory note `hier-map-hash-dos` and `hier-string-byte-safety`
(the byte-safe `tycho_str_cmp` map compare landed recently in the same code).
