# Limited references for shared/cyclic structures — research spike

> Status: **research spike, not a proposal to build.** Reaches a decision for the
> maintainer. Nothing here changes the compiler. Grounded in primary Hylo sources
> (cited inline); the prior survey is `docs/internals/hylo-mvs-research.md`.

## The question

`docs/internals/hylo-mvs-research.md` left one genuinely-open foundation item: would a
*limited reference* (Hylo's `remote-parts`) let Tycho express graphs / cyclic structures
**by reference** instead of the index-into-pool idiom — *without* breaking value semantics?
Tycho today rejects recursive structs and has no shared mutable reference, so a graph must
be a flat `[Node]` linked by integer indices (`docs/internals/value-semantics-limits.md`,
`bench/dijkstra` ≈ 1.3× C). This spike asks whether limited references change that.

The honest answer below: **no — not without sacrificing the two invariants that *are* the
project.** But the analysis isolates one genuinely-compatible, smaller increment.

## What Hylo actually provides (primary sources)

Three distinct mechanisms, not one. They are easy to conflate.

1. **Second-class references = parameter-passing conventions.** `let` / `inout` / `sink` /
   `set` are *modes*, not types. Per Hylo discussion #754, "references are not first-class
   but rather a parameter passing mode … because they can't be stored in structs or returned
   from functions, lifetime analysis is not needed. Borrow checking is just a disjointness
   check at function call sites." **Tycho already has this**: `let` = immutable-borrow params
   (`src/tychoc.c:3216`, read-only enforced at `:4108`), `inout` = exclusive mutable borrow,
   `sink` = consuming (shipped, `docs/internals/sink-prototype.md`). `set` ≈ the FFI out-param.

2. **Projections via subscripts.** A subscript does not *return* a value; it `yield`s a
   *projection* of its arguments, under a **law of exclusivity** (Hylo spec, Projections §8):
   > "If a projection `p` projects an object `o` immutably, `o` is immutable for the duration
   > of `p`'s lifetime. If a projection `p` projects an object `o` mutably, `o` is
   > inaccessible for the duration of `p`'s lifetime."
   A projection is **scoped and transient** — it lives for an expression/binding, never
   escapes. **Tycho already has the built-in case**: `&m[k]` / array-element places yield a
   slot lvalue, not a copy (`docs/map-mutation.md`). What Tycho lacks is letting *users*
   define their own projecting subscripts.

3. **Remote parts = *stored projection types*.** This is the only new idea for the graph
   problem. Hylo spec gives a type form `[let T]` / `[inout T]` / `[yielded T]` — a struct
   field whose type is a *stored* projection (a captured reference into another value). This
   is what could make `Node.neighbor` a reference rather than owned storage. **But** the
   maintainer's rule (discussion #754, kyouko-taiga): a value holding a remote part follows
   "the rules for bare local 2nd-class references: they aren't allowed to escape their local
   scope." And (spec): "A captured projection is released when the lifetime of the capturing
   object ends." So **a value with a remote part is itself demoted to second-class** —
   non-escaping, non-storable beyond the projected object's lifetime.

   It is also **unproven even in Hylo.** The maintainer on the type-checking/optimization of
   remote-part-bearing code (disc #754): "existing techniques do not take existential alias
   types into consideration … it is not obvious how one can build such an optimizer …
   reasoning about the algorithm … is too hard." Remote parts are an open research feature,
   not a shipped, validated one.

## Tycho's three load-bearing invariants

Any reference feature must survive all three, because they are the project's thesis, not
incidental implementation choices:

- **I1 — implicit scope arenas.** A value's lifetime = its lexical scope; reclamation is one
  O(1) arena reset. The model deliberately has **no escape analysis** beyond move-on-last-use
  and the per-scope arena (`docs/memory-model.md`). It works precisely because no value
  outlives its arena except by an explicit copy to a longer-lived one (`_parent`).
- **I2 — deep-copy *is* the thread boundary.** `spawn` / `parallel for` / channel send copy
  the value across the boundary; that copy is *why* concurrency is race-free with no locks
  and no lifetime rules (`docs/concurrency.md`, `[[hier-concurrency-design]]`: "the deep-copy
  call convention IS a sound thread boundary").
- **I3 — no reference counting.** COW/RC is explicitly rejected: atomic refcount traffic on
  shared values is exactly the cost the arena + lock-free design exists to avoid
  (`hylo-mvs-research.md` §COW).

## Compatibility test

| Hylo mechanism | I1 arenas | I2 deep-copy threads | I3 no-RC | In Tycho today? |
|---|---|---|---|---|
| Second-class refs (`let`/`inout`/`sink`) | ✓ scoped, no escape | ✓ never crosses (borrow stays caller-side) | ✓ | **yes** |
| Built-in projection (`&m[k]`) | ✓ | ✓ (place, not a stored ref) | ✓ | **yes** |
| **User-defined projections** (yielding subscripts) | ✓ scoped like `&m[k]` | ✓ transient, never stored/sent | ✓ | no — *buildable* |
| **Remote parts** (stored projection field) | ✗ field references another arena → needs cross-arena lifetime tracking I1 forbids | ✗ a stored ref **cannot be deep-copied**: follow it and aliasing breaks; copy the pointer and it dangles across the thread/arena boundary | ✓ (no RC) but moot | no — *incompatible* |

The decisive row is **remote parts × I2**. A graph edge stored as a reference is, by
definition, an alias into storage the holder does not own. Tycho's thread boundary is a
*deep copy*: to send a node to another thread you copy it. A remote part has no sound copy
semantics at that boundary — there is nothing to deep-copy a reference *into*. This is not a
tooling gap; it is the same wall Hylo hits (a remote-part value is forbidden from escaping
its scope, i.e. forbidden from exactly the boundary crossing `spawn` is built on).

## So: does any of this give graphs-by-reference?

**No.**
- Second-class refs and projections are **transient** — they exist for a call or a binding,
  then end. You cannot *store* a graph's edges in them. They make *traversing* an
  index-pool graph more ergonomic, not *representing* one differently.
- Remote parts are the only construct that *stores* a reference — and they fail I1/I2 and are
  unproven in Hylo. Adopting them would mean giving up the lock-free, arena, copy-as-boundary
  design — i.e. building a different language.

Even Hylo, with the full static machinery, **falls back to indices** for real graph storage
(`hylo-mvs-research.md` §wall). The index-pool is the MVS answer, not a Tycho cop-out.

## The one compatible increment: user-defined projections

If we want to *act on* item #1, the tractable slice is **user-defined yielding subscripts** —
generalizing the built-in `&m[k]` so a library can expose a zero-copy view into part of a
value:

```
# hypothetical surface — NOT implemented
subscript edge(g: Graph, i: int) -> inout Node:
    yield &g.nodes[i]          # a scoped, exclusive projection into the pool

g.edge(3).weight = 10          # mutate in place through the projection, no copy
```

Why it is compatible: the projection is **scoped and non-escaping** — identical lifetime
discipline to `&m[k]`, which already ships soundly. It never becomes a stored field, never
crosses the deep-copy boundary, needs no RC, needs no new lifetime analysis (the law of
exclusivity is a local disjointness check, exactly as Hylo notes). It is the lvalue/place
machinery Tycho already has, exposed to user code.

What it buys: ergonomic, copy-free mutation *through* an index pool (`g.edge(i).w = …`
instead of `g.nodes[i].w = …` spelled out, and reusable projecting accessors over composite
storage). What it does **not** buy: storing the graph by reference. It is ergonomics on top
of the index idiom, the same role Hylo's projections play.

This is item 3 of `hylo-mvs-research.md` ("nice-to-have, lower priority"), and the spike
confirms that ranking: it is the *only* part of the limited-reference space that fits the
model, and it is a convenience, not a storage breakthrough.

## Decision (for the maintainer)

1. **Do not build remote parts / stored references.** They are incompatible with the
   deep-copy thread boundary (I2) and the no-escape arena model (I1), and are unproven even
   in Hylo. This closes foundation item #1: limited references do **not** unlock
   graphs-by-reference within Tycho's thesis. The index-pool stays the answer — documented,
   measured (`bench/dijkstra`), and the same choice Hylo makes.
2. **Optionally build user-defined projections** if/when an ergonomics need appears — the
   only arena- and thread-compatible slice, a generalization of the shipped `&m[k]`. Scope it
   then; it is not foundational and not urgent.
3. **No change to the concurrency or memory model.** The spike's main result is a *negative*
   one worth recording: the storage-shape limit is fundamental to MVS, and the feature that
   would lift it would dismantle the invariants that make Tycho Tycho.

Net: the foundation backlog item is now *resolved as a decision*, not left open. The copy-cost
axis was closed by `sink` (shipped); the storage-shape axis is closed here as "index-pool is
the answer; references can't store a graph without spending the model."
