# `m[k]` as a place: in-place map-value mutation

Once a map can hold any value type ([map-values.md](map-values.md)), the next
question is how to *grow* one of those values without copying it. This note
covers `m[k]` — indexing a map both as a write target and as a read — and why it
stays sound with no borrow checker. The user-facing map surface is in the
README's [Maps](../README.md#maps-string-v-int-v) section.

## Why in-place mutation

With value semantics, changing a stored composite value would otherwise mean
reading it out, mutating the copy, and reinserting it — and the reinsert copies
the whole value every time. For a `[string: [int]]` inverted index that is O(n)
per append, quadratic over a build. Instead, `m[k]` names the value's storage
slot directly, so the mutation happens *in place*:

```
push(idx[term], doc)                  # grow the value array in its own slot
```

That turns `[string: [int]]` into a real inverted index with no side array.

## The two roles of `m[k]`

`m[k]` means different things on the two sides of an assignment, and the
distinction is deliberate.

**As a place** (the target of a write, compound, or projection) it resolves to
the value's slot in the map and mutates it in place:

```
m[k] = v             # plain store of a (heap-copied) value
m[k] += 1            # compound op on the slot
push(m[k], v)        # grow an array-valued slot
m[k][i] = x          # write into a nested array value
m[k].field = x       # write a field of a struct value
```

If the key is absent, the slot is **auto-inserted with the value type's zero**
and then projected into — the same convenience as C++'s `operator[]`. The zero
is whatever an empty of that type is: `0` / `0.0` / `""` / `false` for a scalar,
an empty array, or a zeroed struct. So `push(m["new"], 1)` on a fresh map
creates the empty `[int]` and then pushes; `m["new"] += 1` starts from `0`.

**As a read** (an rvalue, for a scalar value type) it is a **pure** lookup that
returns the value's zero on a missing key and **never inserts** — there is no
hidden write lurking in a read. This is what makes the counter idiom read
naturally:

```
cnt := []string: int
for w in words:
    cnt[w] = cnt[w] + 1     # read cnt[w] (0 if absent), store back
miss := cnt["absent"]       # 0, and "absent" is NOT added to the map
```

A composite (array/struct/map) value is not returned by a bare read — use
`map_get(m, k, default)` for those, so the copy-out is explicit. (The compound
forms above, e.g. `cnt[w] += 1`, are the more direct way to write the counter.)

## Why it can't dangle

`m[k]` as a place is a **transient**: Hier has no way to take a reference to a
value, so `m[k]` can only appear *as* (or *inside*) a single mutation target,
and it cannot be stored, bound, or carried past that one statement. That single
property is what makes it sound:

- The find-or-insert that locates the slot may rehash the table, but it does so
  *before* yielding the slot. The mutation that follows runs with no intervening
  map operation, so the slot cannot move under it.
- `push(m[k], v)` grows the *value's* buffer, not the map's table, so it
  triggers no rehash of the map.
- Value semantics already proves the slot is the unique owner of its value
  (nothing else aliases it), so mutating it in place is invisible to everyone
  else — `cp := m; cp[k] = x` leaves `m` untouched, because `cp` took its own
  deep copy.

A key expression that has side effects is evaluated exactly once per statement,
even for a compound op that reads and writes the same slot:

```
m[next_key()] += 1     # next_key() runs once, not twice
```

`reserve(m[k], n)` reaches a map's array-value slot too, so a posting list can
be pre-sized.

## Example

`tests/map_mutation.hi` exercises every form — a `[string: [int]]` index built
with `push(m[k], v)`, a nested `m[k][i] = x`, an `[int: int]` counter with
`m[k] += 1` from a zero start, a struct value via `m[k].field = x`, single-eval
of a call-bearing key, and the value-semantics check that a copied map's
in-place mutation leaves the original alone. `tests/map_index_read.hi` covers
the pure read across scalar value types and both key kinds.

## Implementation notes

For contributors. `m[k]` as a place lowers to a slot-pointer accessor —
`*hier_mapc<id>_slotptr(owner, &m, k)` — that does the find-or-insert (setting
the zero value on insert) and returns the slot address. The existing place
machinery (assignment, index-set, field-set, `push`/`reserve`/`pop`) reuses that
accessor through the recursive place-lowering pass, so the deeper chains
(`m[k].field`, `m[k][i]`) compose for free. A compound op single-evaluates the
slot pointer and hoists a call-bearing key to a temporary, reusing the same
double-eval guard the compound array-index path already had. A scalar rvalue
read lowers to a pure `map_get` with the type's zero as the default.
