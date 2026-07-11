# Generics (monomorphized)

> Generics in Tycho are monomorphized parametric `fn`s, `struct`s, and
> `enum`s: generic *functions*, generic-struct *construction* and
> *type-position* annotations, struct *dependency-ordering* (`Box(Point)`),
> *structured type-param patterns* (`fn first(xs: [$T]) -> Option(T)`),
> *map patterns* (`fn lookup(m: [$K: $V], k: $K, d: $V) -> $V`), generic
> *enums* (`enum Tree($T)`, including recursive payloads —
> `Node(Tree($T), $T, Tree($T))`), *`where` constraints*
> (`fn sum(xs: [$T]) -> T where numeric(T)`), and *explicit call-site type
> args* (`empty$(int)` for the non-inferable `empty() -> [$T]`). The case for
> them — and why they don't compromise the memory model — is in
> [§7](#7-why-generics-suit-this-memory-model).

## 1. The shape in one screen

Tycho already has *built-in* parametric types — `Option(T)`, `Result(T, E)`,
`[T]`, `[K: V]` — that the transpiler stamps out one concrete copy per concrete
inner type. Generics open that same mechanism to user `fn`s and `struct`s. They
are **monomorphized**: every instantiation is ordinary concrete code, chosen at
compile time. There's no runtime polymorphism, no boxing, no type erasure, and
— critically — no new pointer or aliasing.

```
# A generic function: `$T` at its first appearance introduces the type
# parameter T, inferred from the argument. Later uses write plain `T`.
fn first(xs: [$T]) -> Option(T):
    if len(xs) > 0:
        return Some(xs[0])
    return None

fn main():
    match first([3, 1, 2]):                     # T = int
        Some(x):
            println(str(x))                # 3
        None:
            println("empty")
    match first(["a", "b"]):                    # T = string
        Some(s):
            println(s)                     # a
        None:
            println("empty")
```

```
# A generic struct: construction infers the type arguments from the field values
# (like a generic function call), monomorphizing one concrete struct per tuple.
struct Pair($A, $B):
    first: A
    second: B

fn main():
    p := Pair(7, "hi")                          # infers Pair($A=int, $B=string)
    println(str(p.first) + " " + p.second)
```

`first([3, 1, 2])` compiles to a concrete `first` specialized for `[int]` — the
same code you would get by hand-writing `fn first(xs: [int]) -> Option(int)`.
`first(["a", "b"])` stamps out a second copy for `[string]`. Each distinct
instantiation is emitted exactly once, the way `Option(int)` and
`Option(string)` already are.

## 2. Type parameters: `$T`

A **type parameter** is written `$T` at its *first* occurrence in a parameter's
type. It introduces `T` into scope for the rest of the signature and the body;
every later mention is plain `T`. This is Odin's convention, and it reads as
"this argument fixes T."

- `fn id(x: $T) -> T` — T is whatever `x` is.
- `fn first(xs: [$T]) -> Option(T)` — T is the array's element type.
- `fn pair(a: $A, b: $B) -> Pair(A, B)` — two parameters, inferred independently.
- `fn keys_of(m: [$K: $V]) -> [K]` — K, V from a map's key/value types.

`$T` may appear nested inside a structured type (`[$T]`, `Option($T)`,
`($A, $B)`, `[$K: $V]`); the inference in [§3](#3-inference-argument-directed-structural-matching)
peels the argument's type apart to bind each parameter. A parameter that never
appears in any argument type cannot be inferred — see
[§6](#6-when-t-cant-be-inferred).

Naming: `$T`, `$K`, `$V`, `$A`… any identifier. By convention single uppercase
letters, to read distinctly from concrete types.

## 3. Inference: argument-directed structural matching

Tycho's generic inference has no unification variables and no constraint
solver: it is a one-directional **structural match** of each
concrete argument type against its parameter's type *pattern*, binding the
`$`-parameters as it goes. There is no two-way unification and no global
constraint set — only "walk the pattern and the concrete type in lockstep, and
where the pattern says `$T`, record the concrete type sitting there."

```
pattern:  [ $T ]          arg type: [int]            =>  T := int
pattern:  Option($T)      arg type: Option(string)   =>  T := string
pattern:  [$K: $V]        arg type: [string: [int]]  =>  K := string, V := [int]
pattern:  ($A, $B)        arg type: (int, Meters)    =>  A := int, B := Meters
```

Rules:

- Each `$`-parameter is bound by the **first** argument that pins it. If a later
  argument's type disagrees with an already-bound parameter, that is a
  call-site type error (`first arg fixes T = int, but T = string here`).
- Inference is **left-to-right over the parameter list**, reusing the same
  forward, in-order discipline as the rest of type-inference grounding — no
  backtracking.
- It composes with the existing literal/`[]`/`None` grounding: a bare `[]`
  argument is grounded *by the parameter's pattern* (`fn push_all(xs: inout [$T],
  …)` called with `[]` grounds the array from the other arguments, exactly as
  `push` already grounds a pending `[]`).
- The result type and body are then **substituted** ($T → the bound concrete
  type) and type-checked as ordinary concrete code. Errors in the body surface
  with the instantiation that triggered them (see
  [§5](#5-constraints-checked-at-instantiation)).

This is strictly weaker than HM — it can't infer a parameter that doesn't
appear structurally in an argument — which is the point: it stays a linear
forward scan, decidable and cheap, with no solver.

## 4. Monomorphization reuses the machinery that already exists

Monomorphization is built on the same registry the transpiler already runs for
its built-in parametric types. The transpiler already:

- **interns** each concrete parametric type once — `opt_of(inner)`,
  `res_of(ok, err)`, `arr_of(elem)`, `mapc_of(k, v)` in `src/tychoc.c` assign a
  stable id per concrete inner type and never duplicate it;
- **emits** one monomorphic C struct + its helpers (constructor, deep-copy,
  `==`, match) per interned type, in a deterministic id order
  (`TychoOpt<id>`, `TychoRes<id>`, `TychoArrC<id>`, `TychoMapC<id>` and the
  `tycho_eq_*`/`*_copy` bodies);
- **mangles** a concrete type into a C identifier (`Arr_<elem>_push`,
  `tycho_map_<fn>_has`, `tycho_eq_S_<struct>`), and
- **dispatches** every operation (`push`, `Some(x)`, `m[k]`, `==`) to the right
  monomorphic helper from the operand's concrete type.

The self-hosted `compiler/tychoc0.ty` mirrors all of this on string-typed type
names (`mangle`, `mangle_type`, the `Arr_*`/`Map_*` families).

Generics add **one pass** on top: at a call site (function) or a type use
(struct), bind the `$`-parameters ([§3](#3-inference-argument-directed-structural-matching)),
**intern the instantiation** `(generic_def, [concrete type args])` in a new
side-table exactly like `opt_of` interns `(inner)`, and emit one substituted,
concrete copy of the body the first time that key is seen. The mangled name
carries the type args (`first__int`, `Pair__int__string`). After substitution
the body is *concrete code* and flows through the existing resolve + codegen
unchanged. No fundamentally new mechanism — a generalization of one that is
already load-bearing for `Option`/`Result`/`[T]`/maps.

## 5. Constraints: checked at instantiation

A generic body uses operations on `T` (`xs[0] + 1`, `a == b`, `str(x)`). Rather
than build a trait/type-class system up front, Tycho checks these the way C++ templates
(pre-concepts) and Odin (sans `where`) do: **substitute, then type-check the
concrete body.** If `T = string` reaches `xs[0] + 1`, instantiation fails with
the ordinary "cannot add string and int" error, attributed to the call site that
asked for `T = string`.

This keeps the rule simple. It does have a real downside — an error inside a deep generic
helper points at the instantiation, not always the line you expected — which the
`where` clause below addresses with optional explicit constraints:

```
# a `where` clause turns an instantiation-time body error
# into a checked-up-front signature error with a clear message.
fn sum(xs: [$T]) -> T where numeric(T):
    …
```

The `where` predicates are a *fixed, compiler-known* set (not user-defined type
classes — consistent with Tycho's small-surface stance), each defined as exactly
the capability the resolver already enforces, over the newtype-resolved base:

- `numeric(T)` — `int` or `float` (supports `+ - * /`).
- `comparable(T)` — `int`, `char`, `float`, or `string` (supports `< > <= >=`).
- `has_str(T)` — `int`, `bool`, `float`, or `string` (passable to `str()`).

They are checked at instantiation against each inferred concrete type; a
violation is a clear signature error (`'sum' instantiated with T = string, which
does not satisfy numeric(T)`) instead of a deep "cannot add string and int" in
the substituted body. A `where` on a non-generic function, an unknown predicate,
or a name that is not a type parameter is rejected at parse.

**Type sets.** Beyond the fixed predicates, a constraint
may be a *user-listed type set* — `where T: int | float | Meters` — bounding `T`
to one of the named types (Go's `~int | ~float64` idea, minus the runtime
machinery). Membership is checked against the **newtype-resolved base**, so it
composes with `distinct` the same way the predicates do, and a type set may mix
with predicates in one clause (`where numeric(T), U: int | string`). A violation
is the same instantiation-time signature error (`… T = float, which is not in the
type set { int | float }`). This is the *expressiveness* lever type sets
buy, kept fully monomorphizable and free of type classes — no dictionaries, no
boxing; it is a compile-time membership test.

## 6. When `$T` can't be inferred

Some signatures name a type parameter that no argument pins — `fn
empty() -> [$T]`, `fn zero($T) -> T`. Two options, in order of what I'd reach for first:

1. **Forbid them by default.** Require every `$`-parameter to appear in some
   argument type. `fn empty()` is rare; users write `xs := []int` or pass a
   witness. This keeps inference purely argument-directed.
2. **Explicit type arguments.** Odin passes an explicit `$T: typeid`;
   Tycho already spelled explicit type args for built-in generic *types*
   (`Option(int)`, `Pair(int, string)`), and now for function calls:
   `name$(T1, ...)`, optionally followed by a value-arg list. `empty$(int)` /
   `empty$(string)` bind the type parameters in declaration order; a return-only
   `$T` is folded into the instance name so the two stay distinct, and the body's
   own `[]T` / `[]K: V` literal substitutes per instance.

## 7. Why generics suit this memory model

Two properties make generics a natural fit for a monomorphizing,
value-semantic language:

1. **The monomorphization registry already exists**
   ([§4](#4-monomorphization-reuses-the-machinery-that-already-exists)). Tycho
   monomorphizes `Option`/`Result`/`[T]`/maps today, with interning, per-type
   emission, mangling, and concrete dispatch. Generics generalize that registry
   to user definitions instead of inventing a new one.

2. **Generics do not touch the memory model.** Correctness rests on "no
   reference type + copy on cross-arena move," and generics preserve that.
   Monomorphization runs *before* the signature-directed escape analysis
   of [arrays-structs.md §8](arrays-structs.md): each instantiation is already
   concrete, value-semantic code, so the *same* local, signature-only escape
   decision applies to `first__int` as to any hand-written `fn first(xs:
   [int])`. A generic introduces no pointer, no aliasing, and no whole-program
   analysis — `$T` is a value type bound to a concrete value type. The "every
   lifetime question is locally decidable from signatures" property
   ([§9 verdict](arrays-structs.md)) is preserved verbatim, because after
   substitution there is nothing generic left to analyze.

A couple of things do genuinely grow, and I'd rather say so plainly:

- **Code size.** Each instantiation is duplicated code — already true for
  `Option(int)` vs `Option(string)`; generics let users multiply it. Dedup by
  instantiation key bounds it to "one copy per distinct type tuple actually
  used."
- **Compile time.** A substitution + re-resolve per new instantiation.
- **Error locality.** Body errors surface at the instantiation
  ([§5](#5-constraints-checked-at-instantiation)).

The bet is the same one Go made at 1.18 and Odin makes by design: the handful
of parametric shapes people actually need (containers, a couple of helpers) are
worth a monomorphization pass that the transpiler *already runs* for its built-ins.

## 8. Capabilities

- **Generic functions** (both transpilers). `fn f(x: $T) -> T`,
  parameters inferred from argument types
  ([§3](#3-inference-argument-directed-structural-matching)), every
  `$`-parameter required to appear in an argument
  ([§6](#6-when-t-cant-be-inferred) option 1). Monomorphized via an instantiation
  registry that feeds the existing per-type emission. UFCS gets generic "methods"
  for free, since `x.f(a)` is already sugar for `f(x, a)`. Constraints checked at
  instantiation ([§5](#5-constraints-checked-at-instantiation)). A generic
  call's result may be fed into another generic — nested (`f(g(x))`) or through a
  bound variable — and a generic may take a function-typed parameter
  (`fn($T) -> $T`), so higher-order helpers like `map`/`filter` are expressible.
- **Generic structs (construction)** (both transpilers).
  `struct Box($T)` / `struct Pair($A, $B)` are templates; each construction
  *infers* the type arguments from the field values (`Box(5)` → `Box__int`,
  `Pair(7, "x")` → `Pair__int__string`) and monomorphizes one concrete struct
  with substituted field types. Field access and inferred locals work on the
  instance. Construction reuses all downstream machinery (field access, copy/eq,
  codegen) unchanged. Generic functions compose with generic structs (a `$T`
  binds to `Box__int`).
- **Generic structs (type-position)** (both transpilers).
  `Box(int)` as an explicit-type-args annotation in a parameter, return, field, or
  typed-declaration position — the same surface as the built-in `Option(int)` /
  `Result(int, string)`. A generic struct can be named in a parameter, return,
  field, or typed-declaration position and crosses non-generic function
  boundaries.
- **Struct dependency-ordering.** A generic struct parameterized by
  another *concrete struct* (`Box(Point)`, where the instance embeds `Point` by
  value) works in both transpilers. A struct is emitted after the structs it
  embeds by value (names inside `[...]` are pointers, not dependencies), ties
  breaking by input order.
- **Structured type-param patterns.**
  `fn first(xs: [$T]) -> Option(T)` — `$T` is inferred from *inside* a container
  argument (`[$T]` matched structurally against `[int]`) and the return/param
  types (`Option(T)`, `[T]`, `Result($T,$E)`) are substituted and
  monomorphized.
- **Map patterns.**
  `fn lookup(m: [$K: $V], k: $K, d: $V) -> $V` — both the key and value type are
  inferred from inside the map argument, substituted, and monomorphized per
  concrete map.
- **`where` constraints.**
  A `where pred(T), …` clause over the fixed predicate set `numeric` /
  `comparable` / `has_str` ([§5](#5-constraints-checked-at-instantiation)),
  parsed after the return type and checked at instantiation against each inferred
  concrete type (newtype base resolved).
- **Explicit type args.**
  `name$(T1, ...)` supplies the type params when no argument pins them
  ([§6](#6-when-t-cant-be-inferred) option 2). Type args are bound in declaration
  order, return-only params are folded into the instance name, and the body's
  typaram literals (`[]T`, `[]K: V`) are substituted per instance.
- **UFCS × generics.** A generic free function is callable as a
  method: `xs.first()` == `first(xs)`, `n.dbl().dbl()`. UFCS dispatch, when no
  concrete method matches, tries the generic templates — a candidate is one whose
  first-parameter *pattern* accepts the receiver — then prepends the receiver and
  instantiates like any generic call. Pure composition of two existing features;
  no new concept, no memory-model impact. Chaining resolves because the generic
  method's return type is substituted from the receiver.
- **Type sets.** A `where` constraint can be a
  user-listed type set, `where T: int | float` (see [§5](#5-constraints-checked-at-instantiation)),
  mixable with the fixed predicates. Membership uses the newtype-resolved base;
  it's a compile-time check, still fully monomorphized (no dictionaries, no
  boxing).
- **Generic enums** (both transpilers). A user sum type takes
  `$T`: `enum Box($T): Has($T); Empty`. Monomorphized like a generic struct —
  one concrete `enum` per type argument, payloads substituted. Inference is from
  the constructor's payload values (`Has(42)` ⇒ `T = int`); a nullary variant
  fixes no `$T`, so it takes an explicit arg (`Empty$(int)`), the same surface as
  `empty$(int)`.
- **Recursive generic enums** (both transpilers). A variant may name the
  enum itself — `enum Tree($T): Leaf($T); Node(Tree($T), Tree($T))`. A
  self-reference resolves to the *template* type and is concretized on demand;
  the instantiator dedups before substituting, so the cycle terminates, and the
  payload stays finite because enum payloads are arena-allocated. `T` is inferred
  even when **no payload is a bare `$T`** — `Node(left, right)` recovers `T` from
  the instance type of its arguments, because each enum instance records the
  concrete type args it was built with. Recursive generic structs
  (`struct LL($T): tail: [LL($T)]`) and nested generic structs
  (`Pair(Box(int))`) resolve in both transpilers the same way.

Runnable examples of each capability live in the `tests/generic_*` suite (and
the rejection cases under `tests/reject/`).

## 9. Two-compiler determinism

The fixpoint differential requires `tychoc` and `tychoc0` to emit byte-identical C
for `tychoc0.ty`, and to agree on every fixture. So generics have to
instantiate **deterministically and identically** in both:

- the instantiation key is the `(definition, ordered concrete type args)` tuple,
  using the same canonical type spelling both transpilers already share for
  interning (`Option(int)`, `[string: [int]]`, …);
- emission order is the order instantiations are *first interned* during the
  same in-order walk both transpilers already perform — so the same program yields
  the same set and order of monomorphic definitions;
- mangled names are a pure function of the key.

If those hold, a generic program is, post-monomorphization, an ordinary
concrete program that both transpilers already handle in lockstep.

## 10. Non-goals

Stuff I deliberately left out, to keep the surface small and the model intact:

- **No runtime generics / type erasure / boxing** — always monomorphized.
- **No variance, no higher-kinded types, no associated types.**
- **No user-defined type classes / traits** — constraints, if added, are a fixed
  compiler-known predicate set ([§5](#5-constraints-checked-at-instantiation)).
- **No generic globals** (Tycho has no mutable globals anyway).
- **No specialization/overloading** of a generic for particular types — one
  generic body, monomorphized uniformly.
- **No implicit numeric/`distinct` coercion through `$T`** — a `$T` bound to
  `Meters` stays `Meters`, matching newtype rules.
