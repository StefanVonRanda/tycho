# Where Hier sits, and ideas from neighbouring languages

Research notes (2026-05) surveying Odin, Jai, Swift, Hylo, Koka/Perceus, and
Vale for features and ideas that fit Hier's identity: AOT, statically typed,
**value semantics with no reference type**, **implicit hierarchical arenas**, no
GC / no borrow checker. The filter for "does this fit Hier?" is: it must not
reintroduce pointers or visible memory management, and it should compose with
deep-copy-on-cross-arena-move.

## The design-space map (where Hier actually is)

Hier's two pillars — value semantics + implicit arenas — put it next to a small
cluster of languages, and the differences are the interesting part:

- **Hylo** (formerly Val) — *the closest living relative*. Also "all types are
  value types," also no pointers/references in user code, also `inout` as an
  exclusive copy-in/copy-out borrow (Hier already cites this). Hylo's
  distinctive tool is **subscripts that *project* (yield) rather than return** —
  the callee temporarily grants the caller read or read/write access to an
  interior value without ever exposing a pointer. That is *exactly* the
  mechanism Hier is missing for "mutate through an array element" (`arr[i].f =
  v`). Hylo gets efficiency from projections; Hier gets it from arenas + the
  deep-copy seam. ([Hylo intro](https://hylo-lang.org/introduction/),
  [spec](https://hylo-lang.org/docs/reference/specification/))
- **Koka / Perceus** — *the same idea via a different mechanism*. Perceus is
  "garbage-free reference counting with reuse"; its **FBIP — "functional but
  in-place"** — lets you write imperative-looking in-place algorithms in a pure
  value-semantics style, because reuse analysis proves a value is uniquely owned
  and reuses its memory on destructure→reconstruct. **Hier's in-place append /
  map accumulator / return-slot move ARE FBIP** — but Hier proves uniqueness
  from *value semantics + lexical arenas* instead of *runtime reference counts*.
  This is the sharpest framing of Hier's novelty.
  ([Perceus paper](https://www.microsoft.com/en-us/research/uploads/prod/2020/11/perceus-tr-v1.pdf),
  [Koka book](https://koka-lang.github.io/koka/doc/book.html))
- **Vale** — memory safety via **generational references + region borrowing**
  (no GC, no borrow checker, ~2–11% overhead). Different safety strategy
  (runtime gen-checks) but the **regions** idea rhymes with Hier's per-scope
  arenas: "tell the compiler a region won't change during a scope and it
  optimizes." ([generational refs](https://verdagon.dev/blog/generational-references),
  [regions](https://verdagon.dev/blog/zero-cost-memory-safety-regions-overview))
- **Odin / Jai** — *the opposite philosophy on memory* (explicit allocators via
  an implicit **`context`** that can be scope-overridden), which Hier
  deliberately rejects. But their *non-memory* features are squarely useful:
  `distinct` newtypes, **SOA** layouts, slices, multiple return values,
  `or_return` error flow, `defer`.
  ([Odin overview](https://odin-lang.org/docs/overview/),
  [Odin context](https://www.gingerbill.org/article/2025/12/15/odins-most-misunderstood-feature-context/),
  [Jai primer](https://github.com/BSVino/JaiPrimer/blob/master/JaiPrimer.md))
- **Swift** — the reference design for **enums with associated values +
  exhaustive pattern matching** (with `where`/`guard case`), which is the
  natural generalization of Hier's `Option(T)` + `match`.
  ([Swift pattern matching](https://alisoftware.github.io/swift/pattern-matching/2016/03/27/pattern-matching-1/))

## Ranked ideas for Hier

### Tier 1 — biggest expressiveness win, on-thesis, unlocks dogfooding

1. **General sum types: `enum` / variants + exhaustive `match`.** `Option(T)`
   is the special case; generalize it to user types, e.g.
   `enum Expr: Num(float), Add(Expr, Expr), Neg(Expr)`. The recursion rules are
   the ones already solved for recursive structs/options (indirection via arrays
   / `Option`, infinite-by-value detection). `match` already exists — extend it
   from two fixed arms to N named cases with binding + exhaustiveness. **This is
   the single most valuable next feature.** (Swift/Rust.)

2. **Dogfood: write a small interpreter *in Hier*.** Not a language feature —
   the *proof experiment*. The thesis targets "compilers and compiler passes";
   a calculator / expression evaluator (which needs #1) is the smallest program
   that exercises the model on its own claimed workload, surfaces the real
   value-semantics copy cost, and drives the missing-feature list. Turns
   "validated on a 26-program suite" into "proven on a real program."

### Tier 2 — removes current real limitations

3. **Projections / yielding element access (Hylo-style).** ✅ *Implemented.*
   A composite-array element is now a mutable place: `arr[i].f = v`,
   `push(arr[i].xs, v)`, `m[i][j] = v`, and `&arr[i].x` (inout) all yield the
   element's slot in the backing buffer (a bounds-checked `hier_arr_C<id>_ptr`),
   with no pointer exposed in Hier and value semantics preserved. The lvalue is
   only granted for composite (ARRC) elements rooted in a mutable variable/field
   — a read-only borrow or scalar-array byte is still rejected.

4. **Error handling.** ✅ *Implemented* (`Result(T, E)` + `or_return`).
   A built-in generic `Result(T, E)` — `Ok(value)` / `Err(error)` — consumed by
   the same exhaustive `match` as `Option`/enums, interned per (T,E) pair like
   `Option`. Both halves may be heap types; value semantics + return-promotion
   hold. The ergonomic half landed too: `v := expr or_return` unwraps an `Ok` or
   propagates the `Err` from the enclosing `Result(_, E)`-returning function
   (postfix operator, lowered to a statement-expression that promotes the err
   payload into the caller's arena before freeing the live scopes). Still open:
   `or_return` for `Option` (None-propagation) — currently `Result`-only.

5. **Multiple return values** (Odin/Go/Jai). ✅ *Implemented, as first-class
   tuples.* `(T1, ..., Tn)` is a real anonymous-product value type (interned per
   element-list like Option/Result): `return a, b` builds one, `x, y := f()`
   destructures it (decl-only), and a tuple is also storable, indexable (`t.0`),
   literal-constructible (`(1, 2)`), passable, comparable, and deep-copied by
   value. Went beyond the Go/Odin "boundary-only" framing because first-class
   tuples compose with value semantics for free. Still open: `a, b = f()`
   re-assignment and mutable elements (`t.0 = v`).

### Tier 3 — planned in the design doc / data-oriented perf

6. **Slices** — non-owning array views (already listed in `arrays-structs.md`),
   so passing a sub-array doesn't copy. Needs a non-storable borrow rule.
7. **`distinct` newtypes** — zero-cost type safety (`type Meters = float`),
   already listed as planned; borrowed from Odin/Tycho.
8. **Generalized reuse analysis (Perceus/FBIP).** Promote the in-place
   optimizations from the three hand-recognized accumulator shapes to *any*
   uniquely-owned destructure→reconstruct. Deep, on-thesis, and the most
   intellectually aligned with the FBIP framing above. Hard.
9. **SOA arrays** (Odin/Jai) — `#soa [N]Struct` cache-friendly layout; fits the
   value+arena model and the performance narrative.

### Tier 4 — large or philosophy-divergent (note, don't rush)

- **Compile-time execution** (Jai/Zig) — powerful, large, arguably out of scope.
- **Generics** — the thesis deliberately avoids a generics engine; the
  monomorphization Hier already does for arrays/options/maps is a limited,
  closed form. A *constrained* generics story could reuse that machinery, but it
  cuts against the stated "small, fixed type surface" design.
- **Modules / multi-file** — real ergonomics, no thesis content.

## Suggested next step

**Sum types + match, then write the interpreter.** It is the highest
expressiveness leverage, it is the experiment that most directly tests the
thesis, and the FBIP/Hylo framing above gives the project a sharper story about
*why* the implicit-arena model is a distinct and defensible point in the design
space.
