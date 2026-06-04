# Plan: in-place map-value mutation (`m[k]` as a place)

Composite map values shipped (docs/map-values.md), but a map value is value-by-copy
only — to grow `[string: [int]]` you `map_get → push → map_set`, copying the whole
value each time (O(n) per append). #2 adds **`m[k]` as an lvalue/place** so a value
can be mutated *in place*: `push(m[k], v)`, `m[k].field = x`, `m[k] op= v`,
`m[k] = v`, `m[k][i] = x`. The payoff: `[string: [int]]` becomes a real inverted
index (no side array — the wall invindex hit, closed the natural way).

## Design decisions (made up front — the crux of #2)

1. **`m[k]` is a PLACE only (lvalue), not an rvalue.** Reads stay `map_get(m, k,
   default)` (which needs the explicit default). Using `m[k]` in value position is
   a compile error ("read with map_get(m, k, default)"). This avoids an
   auto-insert side effect hiding in a read. (Alternative — operator[]-style read
   that auto-inserts — is rejected for least surprise.)
2. **Absent key ⇒ auto-insert the value's zero, then project** (C++ `operator[]`).
   `push(m[k], v)` on a fresh map creates an empty `[int]` then pushes. The zero is
   C `(V){0}` — an empty array/string descriptor (len 0, NULL data) or a zeroed
   struct/scalar, all valid empties. So `push(m["new"], 1)` just works.
3. **`m[k]` is a TRANSIENT place, never a bindable reference.** Hier has no `&` to
   a value, so `m[k]` can only appear as (or inside) a mutation target in one
   statement; it can't outlive the statement, so it can't outlive a rehash.

## Soundness (why it can't dangle — RULE 5)

`m[k]` lowers to `*hier_mapc<id>_slotptr(owner, &m, k)`, where `slotptr` does a
find-or-insert and returns `&m->vals[slot]`. The find-or-insert may rehash, but it
does so INSIDE the call, BEFORE returning the pointer — so the returned pointer is
post-rehash and valid. The enclosing mutation (push/field-set/compound) runs after
with NO intervening map operation, so the slot can't move under it. `push(m[k], v)`
grows the *value* array (in the map's arena), not the map *table* — no map rehash
during the push. In-place mutation is sound because value semantics makes the slot
the UNIQUE owner of its value (no aliasing). The one trap: `m[k] op= v` must
single-eval the slotptr (compute it once, use for read+write) and hoist a
call-bearing key to a temp — reuse the compound-index double-eval machinery
(src/hierc.c hoist_index_calls; hierc0's pending threading).

## Steps (checkpointed)

1. **Runtime slotptr (both compilers).** Emit `hier_mapc<id>_slotptr(Arena*,
   HierMapC<id>*, K) -> V*`: find-or-insert k (on insert, set occ/key and
   `m->vals[s] = (V){0}`), return `&m->vals[s]`. String- and int-key branches
   (mirror put's find-or-insert). hierc: the composite-map emitter. hierc0:
   gen_map_fns. Dormant until step 2 routes m[k] to it. Gate: fixpoint green
   (no m[k] place exists yet → byte-identical).
2. **hierc: `m[k]` as a place.** Resolve `E_INDEX(base, k)` where base is a map →
   value type (k must match map_key); REJECT it in rvalue position. `gen_lvalue`
   for a map-index emits `(*hier_mapc<id>_slotptr(owner, &m, k))`. The existing
   place users (S_ASSIGN/S_INDEXSET/S_FIELDSET/push/compound) call gen_lvalue, so
   they light up for free. Verify: `push(m[k], v)`, `m[k].f = x`, `m[k] += 1`,
   `m[k] = v` on `[string:[int]]`/`[string:Struct]`/`[int:int]`; ASan/LSan
   (auto-insert + grow-in-place leak-free); a key-with-a-call single-eval test.
3. **hierc0 parity.** Same in the self-hosted compiler; differential + fixpoint;
   add tests/map_mutation.hi to the suite.
4. **Fuzzer.** Let gen.py emit `push(m[k], v)` / `m[k] op= e` on map vars (the
   value-semantics oracle catches an in-place mutation that wrongly aliases).
5. **Payoff — rewrite + re-benchmark invindex.** `bench/invindex` with
   `[string: [int]]` (term → posting list as a map value, `push(post[term], doc)`).
   Compare to the current `[string:int]` + side-array form AND to C/Go. The
   question: does the natural data structure now compete, and how does the
   arena's grow-in-map behave at scale (it's still the build-and-hold many-growing-
   arrays shape from hier-invindex-reserve — `reserve` may not reach into a map
   value, so watch the memory).

## Open question to resolve in step 2
Does `m[k].field` (a deeper place) compose cleanly through `gen_lvalue` (slotptr
deref, then `.field`)? It should — `gen_lvalue` is recursive over the place chain —
but verify the slotptr is single-eval'd when the chain re-reads it (compound).
