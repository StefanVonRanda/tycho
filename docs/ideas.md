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
  interior value without ever exposing a pointer. Hier has since shipped
  "mutate through an array element" (`arr[i].f = v`, and `m[k]` map places)
  *without* projections — the compiler treats the element as a place and
  mutates it in the arena directly (see `tests/soa.hi`). Hylo gets efficiency
  from projections; Hier gets it from arenas + the
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

### Tier 1 — ✅ DONE: the expressiveness win that unlocked dogfooding

1. **General sum types: `enum` / variants + exhaustive `match`.** ✅
   *Implemented.* `Option(T)` was the special case; user types are now the
   general one, e.g. `enum Expr: Num(float), Add(Expr, Expr), Neg(Expr)`. The
   recursion rules reuse the ones already solved for recursive structs/options
   (indirection via arrays / `Option`, infinite-by-value detection); `match`
   extends from two fixed arms to N named cases with binding + exhaustiveness.
   Recursive enums (ASTs) and enum array elements work; self-hosting in both
   compilers (`tests/enums.hi`, `tests/enum_calc.hi`,
   `tests/recursive_enum_array.hi`). This was the single most valuable feature —
   and it is what made the self-hosting compiler below possible. (Swift/Rust.)

2. **Dogfood: write a real compiler *in Hier*.** ✅ *Realized — beyond the
   original ask.* The plan was a small interpreter (a calculator needing #1) as
   the smallest program exercising the model on its claimed workload. Instead the
   project went all the way: `compiler/hierc0.hi` is a **self-hosting** compiler
   written in Hier (`make fixpoint` green), and its codegen was then migrated onto
   the implicit-arena model (MM-0 … MM-7f, [memory-model.md](memory-model.md)) —
   the model proving itself on a real, large, allocation-heavy program, not a
   micro-benchmark. This turned "validated on a 57-program suite" into "proven by
   building and running the compiler itself." A standalone interpreter is now
   optional (a smaller demo of the same point).

### Tier 2 — ✅ DONE: removed real limitations (projections, error handling, tuples)

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
   payload into the caller's arena before freeing the live scopes). `or_return`
   works on `Option` too: `v := opt or_return` unwraps `Some(v)`, else
   short-circuits the enclosing `Option`-returning function with `None`
   (`tests/or_return_option.hi`). Self-hosting in both compilers.

5. **Multiple return values** (Odin/Go/Jai). ✅ *Implemented, as first-class
   tuples.* `(T1, ..., Tn)` is a real anonymous-product value type (interned per
   element-list like Option/Result): `return a, b` builds one, `x, y := f()`
   destructures it (decl-only), and a tuple is also storable, indexable (`t.0`),
   literal-constructible (`(1, 2)`), passable, comparable, and deep-copied by
   value. Went beyond the Go/Odin "boundary-only" framing because first-class
   tuples compose with value semantics for free. `a, b = f()` reassignment (to
   existing vars) and mutable elements (`t.0 = v`) are supported too, both
   self-hosting (`tests/tuple_assign.hi`).

### Tier 3 — planned in the design doc / data-oriented perf

6. **Slices** — ✅ *Implemented.* `xs[a:b]` (and `xs[a:]`/`xs[:b]`/`xs[:]`) is a
   bounds-checked sub-range descriptor `{ data + a, b - a, 0 }`. The
   non-storable-borrow rule falls out of the existing value semantics for free:
   the view aliases the source buffer ONLY as a read-only argument (the same
   zero-copy borrow an array param already is), and `is_place(E_SLICE)` makes
   any bind/return/push deep-copy it into an owning array — so it can never be
   stored while aliasing. No borrow checker; the one added rule rejects a slice
   of `xs` alongside an `inout` of `xs` in one call. Works on every array type
   and composes. **Strings slice too:** `s[a:b]` (and `s[a:]`/`s[:b]`/`s[:]`) is
   a fresh substring — sugar over `substr` — and self-hosts (`tests/string_slice.hi`).
7. **`distinct` newtypes** — ✅ *Implemented.* `type Meters = float` is a named
   type interned in its own band; `c_type`/`type_is_heap`/`copy_into`/`gen_eq`
   all delegate to the underlying, so it is genuinely zero-cost (a `Meters` *is*
   a `double` in the C). `Meters(x)` wraps (identity at runtime), `to_int`/
   `to_float` unwrap. Arithmetic/ordering/`str` are allowed only between two of
   the SAME newtype — units can't be mixed with each other or the base. Now also
   over **`string` and `bool`** underlying: `type UserId = string` /
   `type Active = bool`, unwrapped with `to_str` / `to_bool`, with `==` and
   (string) ordering seeing through to the base — self-hosting
   (`tests/newtype_strbool.hi`). Aggregate underlying still deferred.
8. **Generalized reuse analysis (Perceus/FBIP).** ✅ *First step done:
   move-on-last-use.* `b := a` / `b = a` elides the deep copy and hands off
   `a`'s buffer when `a` is a uniquely-owned local read exactly once (last use on
   every path), not inside a loop, in the destination's arena (`cv_arena` is NULL
   for params, so borrows are never moved). Like the accumulator reuse, it is
   FBIP proven from value semantics + lexical arenas rather than reference
   counts — and it generalizes the principle beyond the three hand-coded shapes
   (the `bench/move` guard: ~126 MB moved vs ~187 MB copied). ✅ *Second step
   done: match-arm payload borrow.* Destructuring a heap enum payload
   (`Add(l, r)` over an `Expr`) used to deep-copy each field's whole subtree
   into the arm; now the binding BORROWS the scrutinee's payload (shares the
   pointer, like an array param borrows its caller's buffer), since the
   scrutinee outlives the match and enum values are immutable. A binding that
   is mutated in the arm (`push`/element-set/`&`-inout on a `[int]` payload)
   keeps its owning copy, so the write can't reach through. This is the
   Perceus tree-rewrite case: it drops a `match`→reconstruct pass (the
   `examples/optimize.hi` optimizer, `tests/match_reuse.hi`) from O(n²) to
   O(n) copying. ✅ *Third step done: construction-arg moves.* Building a
   heap aggregate (enum payload, Option/Result body, tuple, struct, or array
   literal) from a uniquely-owned dead local now hands off its buffer instead
   of deep-copying it — `t := (a, b)` with `a`/`b` dead stores their buffers
   directly (`can_move_from`, the same predicate as `b := a`), and a fresh
   temporary arg is stored without a copy too (it already owns its bytes in
   the target arena). A reused/aliased source still deep-copies, so value
   semantics holds (`tests/ctor_move.hi`; `bench/ctor_move` guards ~126 MB
   moved vs ~187 MB copied). ✅ *Fourth step done: loop-carried self-rebuild
   move.* A self-rebuild `t = Pair(t, Leaf(..))` reads the old `t` once and
   immediately overwrites it, so its buffer is dead at the rebind even inside
   a loop — the constructor analog of the `acc = acc + e` / `m = map_set(m,
   ...)` loop accumulators. The single occurrence of the target is handed off
   instead of copied, turning the O(n²) comb *build* into O(n) (`bench/
   comb_build` measures ~2 MB vs ~368 MB at n=4000). The gate requires the
   name to occur exactly once in the RHS and to be a same-arena local, so a
   mid-build snapshot still freezes correctly (`tests/loop_rebuild.hi`).
   ✅ *Fifth step done: the payload borrow now covers `Option`/`Result` match
   arms too.* A read-only `Some(xs)`/`Ok(xs)`/`Err(e)` binding borrows the
   scrutinee's value (`h_xs = _m.okv`, no deep copy); a binding mutated in the
   arm keeps its owning copy (`tests/optres_borrow.hi`). Tier 3 #8 is now
   fully realized — reuse is proven from value semantics + lexical arenas
   across binds, destructures, constructions, and loop-carried rebuilds, with
   no reference counts.
9. **SOA arrays** (Odin/Jai) — ✅ *Foundational core implemented.* `soa [Point]`
   is a struct-of-arrays: one growable arena buffer per struct field + a shared
   len/cap, instead of an array of records — cache-friendly when a loop touches
   one field across all elements. (`#soa` is impossible — `#` is the comment
   char; the type is `soa [Struct]`, an empty value `soa []Struct`, matching the
   `[int]`/`[]int` convention.) Core ops: empty literal, `push` (grows each
   field buffer in the arena + scatters, deep-copying heap fields), `len`, field
   read `a[i].f`, field write `a[i].f = v` — all bounds-checked like the AoS
   arrays. Plus whole-element gather `p := a[i]` (assembles an independent struct
   value) and pass/return-by-value (deep-copied per field — value semantics
   hold), structural `==`/`!=` (length + every field elementwise), and slices
   `a[lo:hi]` (a bounds-checked view that offsets each field pointer, copied on
   store like array slices). A soa param is a read-only zero-copy borrow (it
   shallow-shares the caller's buffers, like array/map params; in-place mutation
   is rejected — copy first with `local := ps`). A soa can also be a struct
   field — copying the struct deep-copies its soa (value semantics through the
   nesting); the soa typedef is forward-emitted so a struct may embed it by
   value. (`tests/soa.hi`.) **SOA fully self-hosts**: hierc0's own naive codegen
   implements the entire feature (empty, push, len, field read/write, gather,
   pass/return-by-value, `==`, slices, nesting), so the full `tests/soa.hi`
   compiles byte-identically under hierc0 and runs in the fixpoint differential
   with no skip (`tests/soa_basic.hi` is the minimal core subset).

### Tier 4 — large or philosophy-divergent (note, don't rush)

- **Compile-time execution** (Jai/Zig) — powerful, large, arguably out of scope.
- **Generics** — **decided against (firm).** Hier stays monomorphic; the
  closed-form monomorphization it already does for arrays/options/maps is the
  extent of it. A generics engine cuts against the "small, fixed type surface"
  design and adds nothing to the arena thesis. Not a roadmap item.
- **Modules / multi-file** — **promoted out of Tier 4: this is the agreed next
  major language feature.** Odin-style packages — a package is a directory of
  files sharing one namespace, `import "path"`, referenced `pkg.symbol`,
  whole-program transpile with package-prefixed C symbols, and **no privacy**
  (every package symbol is always visible). Removes the single-source-file limit
  and lets the self-hosted compiler split into packages.

## Where this stands now

The original "suggested next step" — **sum types + match, then dogfood** — is
**done and then some**: enums + `match` shipped (Tier 1), and the dogfood became
a full **self-hosting compiler** (`compiler/hierc0.hi`, `make fixpoint`) rather
than a toy interpreter. Tiers 2–3 (projections, error handling, tuples, slices,
newtypes, the FBIP reuse family, SOA) are all shipped too. The FBIP/Hylo framing
above is now the project's sharpest story precisely *because* it was proven on
the compiler itself.

The memory-model codegen migration ([memory-model.md](memory-model.md),
MM-0 … MM-10) that was the live frontier is now **complete** — hierc0 reproduces
the full arena model with **no known memory gap** and full feature + memory parity
with the C compiler (the last residual, heap-payload option arrays, closed in
MM-7f). Since then: a `char` type, comprehensive differential + ASan/UBSan fuzzing
([fuzz/](../fuzz/)), a dependency-free sampling profiler ([tools/prof/](../tools/prof/)),
a ~3.1× self-compile speedup, and a real-workload demo (`examples/json.hi`). The
**Odin-style packages & modules** that the frontier then moved to have shipped
too — as have closures, UFCS methods, C FFI ([ffi.md](ffi.md)), and a corelib —
so the open items left in *this* doc are the deferred streaming-codegen rewrite
(scoped in [perf.md](perf.md), to *outperform* the C compiler) and the remaining
Tier-4 ideas: compile-time execution and a demo interpreter. Generics are
decided-against.
