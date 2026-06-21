# Generics (Odin-style, monomorphized)

> **Status: Stages 1, 2a, 2b, and most of Stage 3 shipped** — generic *functions*,
> generic-struct *construction* and *type-position* annotations, struct
> *dependency-ordering* (`Box(Point)`), and *structured type-param patterns*
> (`fn first(xs: [$T]) -> Option(T)`), both compilers. The remaining Stage 3
> (`where` constraints / explicit call-site type args) stays design. This is
> the contract the implementation is built against. It reverses an earlier
> "no generics (firm)" decision; the argument for the reversal is in
> [§7](#7-the-reversal-the-registry-already-exists). Each stage ships only when
> both compilers implement it and `make test` / `make fixpoint` / `make fuzz`
> are green — Stage 1 is, via `tests/generics.hi`.

## 1. The shape in one screen

Hier already has *built-in* parametric types — `Option(T)`, `Result(T, E)`,
`[T]`, `[K: V]` — that the compiler stamps out one concrete copy per concrete
inner type. Generics open that same mechanism to user `fn`s and `struct`s. They
are **monomorphized**: every instantiation is ordinary concrete code, chosen at
compile time. There is no runtime polymorphism, no boxing, no type erasure, and
— critically — no new pointer or aliasing.

```
# A generic function: `$T` at its first appearance introduces the type
# parameter T, inferred from the argument. Later uses write plain `T`.
fn first(xs: [$T]) -> Option(T):
    if len(xs) > 0:
        return Some(xs[0])
    return None

fn main():
    print(str(first([3, 1, 2])) + "\n")        # T = int    -> Some(3)
    print(first(["a", "b"]) + "\n")             # T = string -> Some("a")
```

```
# A generic struct: construction infers the type arguments from the field values
# (like a generic function call), monomorphizing one concrete struct per tuple.
struct Pair($A, $B):
    first: A
    second: B

fn main():
    p := Pair(7, "hi")                          # infers Pair($A=int, $B=string)
    print(str(p.first) + " " + p.second + "\n")
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
`($A, $B)`, `[$K: $V]`); the inference in [§3](#3-inference-argument-directed-not-hindley-milner)
peels the argument's type apart to bind each parameter. A parameter that never
appears in any argument type cannot be inferred — see
[§6](#6-when-t-cant-be-inferred).

Naming: `$T`, `$K`, `$V`, `$A`… any identifier. By convention single uppercase
letters, to read distinctly from concrete types.

## 3. Inference: argument-directed, not Hindley-Milner

Hier deliberately has **no Hindley-Milner**, no unification variables, no
constraint solver (see [docs/inference.md](inference.md)). Generic inference
keeps that promise: it is a one-directional **structural match** of each
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
  forward, in-order discipline as B-0..B-3 grounding — no backtracking.
- It composes with the existing literal/`[]`/`None` grounding: a bare `[]`
  argument is grounded *by the parameter's pattern* (`fn push_all(xs: mut [$T],
  …)` called with `[]` grounds the array from the other arguments, exactly as
  `push` already grounds a pending `[]`).
- The result type and body are then **substituted** ($T → the bound concrete
  type) and type-checked as ordinary concrete code. Errors in the body surface
  with the instantiation that triggered them (see
  [§5](#5-constraints-checked-at-instantiation)).

This is strictly weaker than HM — it cannot infer a parameter that does not
appear structurally in an argument — which is the point: it stays a linear
forward scan, decidable and cheap, with no solver.

## 4. Monomorphization reuses the machinery that already exists

The §7 "no generics" verdict (below) rested on one claim: that a generics
engine needs *"the cross-module monomorphization registry that is a major source
of complexity … [and] simply does not exist."* It exists. The compiler already:

- **interns** each concrete parametric type once — `opt_of(inner)`,
  `res_of(ok, err)`, `arr_of(elem)`, `mapc_of(k, v)` in `src/hierc.c` assign a
  stable id per concrete inner type and never duplicate it;
- **emits** one monomorphic C struct + its helpers (constructor, deep-copy,
  `==`, match) per interned type, in a deterministic id order
  (`HierOpt<id>`, `HierRes<id>`, `HierArrC<id>`, `HierMapC<id>` and the
  `hier_eq_*`/`*_copy` bodies);
- **mangles** a concrete type into a C identifier (`Arr_<elem>_push`,
  `hier_map_<fn>_has`, `hier_eq_S_<struct>`), and
- **dispatches** every operation (`push`, `Some(x)`, `m[k]`, `==`) to the right
  monomorphic helper from the operand's concrete type.

The self-hosted `compiler/hierc0.hi` mirrors all of this on string-typed type
names (`mangle`, `mangle_type`, the `Arr_*`/`Map_*` families).

Generics add **one pass** on top: at a call site (function) or a type use
(struct), bind the `$`-parameters ([§3](#3-inference-argument-directed-not-hindley-milner)),
**intern the instantiation** `(generic_def, [concrete type args])` in a new
side-table exactly like `opt_of` interns `(inner)`, and emit one substituted,
concrete copy of the body the first time that key is seen. The mangled name
carries the type args (`first__int`, `Pair__int__string`). After substitution
the body is *concrete code* and flows through the existing resolve + codegen
unchanged. No fundamentally new mechanism — a generalization of one that is
already load-bearing for `Option`/`Result`/`[T]`/maps.

## 5. Constraints: checked at instantiation

A generic body uses operations on `T` (`xs[0] + 1`, `a == b`, `str(x)`). Rather
than a trait/type-class system up front, Hier checks these the way C++ templates
(pre-concepts) and Odin (sans `where`) do: **substitute, then type-check the
concrete body.** If `T = string` reaches `xs[0] + 1`, instantiation fails with
the ordinary "cannot add string and int" error, attributed to the call site that
asked for `T = string`.

This keeps Stage 1 small. It has a known cost — an error inside a deep generic
helper points at the instantiation, not always the line a user expected — which
[§8](#8-stages) addresses by adding optional explicit constraints later:

```
# Stage 3 (proposed): a `where` clause turns an instantiation-time body error
# into a checked-up-front signature error with a clear message.
fn sum(xs: [$T]) -> T where numeric(T):
    …
```

`where` predicates would be a *fixed, compiler-known* set (`numeric`,
`comparable`, `has_str`…), not user-defined type classes — consistent with
Hier's small-surface stance. Stage 1 ships without them.

## 6. When `$T` can't be inferred

Some signatures name a type parameter that no argument pins — `fn
empty() -> [$T]`, `fn zero($T) -> T`. Two options, in order of preference:

1. **Forbid them in Stage 1.** Require every `$`-parameter to appear in some
   argument type. `fn empty()` is rare; users write `xs := []int` or pass a
   witness. This keeps inference purely argument-directed.
2. **Explicit type arguments (later).** Odin passes an explicit `$T: typeid`;
   Hier already spells explicit type args for built-in generic *types*
   (`Option(int)`, and `Pair(int, string)` for generic structs). The same
   surface can carry an explicit call-site type arg if needed
   (`empty(int)()` or a turbofish-free `empty$(int)` — syntax TBD in Stage 3).
   Deferred until a real need appears.

## 7. The reversal: the registry already exists

The pre-generics docs recorded this as *"decided against (firm)"*, on two
grounds (both since reversed — see [arrays-structs.md §7](arrays-structs.md)).
Both are addressed:

1. *"The cross-module monomorphization registry … simply does not exist."* — It
   does ([§4](#4-monomorphization-reuses-the-machinery-that-already-exists)).
   Hier monomorphizes `Option`/`Result`/`[T]`/maps today, with interning,
   per-type emission, mangling, and concrete dispatch. Generics generalize that
   registry to user definitions instead of inventing a new one.

2. *"Dropping generics does not weaken [the memory model]; correctness rests on
   'no reference type + copy on cross-arena move'."* — Generics **do not touch
   this.** Monomorphization runs *before* the signature-directed escape analysis
   of [arrays-structs.md §8](arrays-structs.md): each instantiation is already
   concrete, value-semantic code, so the *same* local, signature-only escape
   decision applies to `first__int` as to any hand-written `fn first(xs:
   [int])`. A generic introduces no pointer, no aliasing, and no whole-program
   analysis — `$T` is a value type bound to a concrete value type. The "every
   lifetime question is locally decidable from signatures" property
   ([§9 verdict](arrays-structs.md)) is preserved verbatim, because after
   substitution there is nothing generic left to analyze.

What genuinely grows is real and worth stating plainly:

- **Code size.** Each instantiation is duplicated code — already true for
  `Option(int)` vs `Option(string)`; generics let users multiply it. Dedup by
  instantiation key bounds it to "one copy per distinct type tuple actually
  used."
- **Compile time.** A substitution + re-resolve per new instantiation.
- **Error locality.** Body errors surface at the instantiation
  ([§5](#5-constraints-checked-at-instantiation)).

The wager is the same one Go made at 1.18 and Odin makes by design: the handful
of parametric shapes people actually need (containers, a couple of helpers) are
worth a monomorphization pass that the compiler *already runs* for its built-ins.

## 8. Stages

Each stage lands in **both** compilers behind `make fixpoint`, its own focused
commit, fully green before the next — same discipline as every other change.

- **Stage 1 — generic functions — SHIPPED** (both compilers). `fn f(x: $T) -> T`,
  parameters inferred from argument types
  ([§3](#3-inference-argument-directed-not-hindley-milner)), every
  `$`-parameter required to appear in an argument
  ([§6](#6-when-t-cant-be-inferred) option 1). Monomorphized via an instantiation
  registry that feeds the existing per-type emission. UFCS gets generic "methods"
  for free, since `x.f(a)` is already sugar for `f(x, a)`. Constraints checked at
  instantiation ([§5](#5-constraints-checked-at-instantiation)). Test:
  `tests/generics.hi`. In hierc: a `T_TYPARAM` interned type, templates kept out
  of the sig table, a call infers + interns one instance Sig + rewrites itself,
  instances resolved+emitted from the shared body sequentially during codegen. In
  hierc0 (string-typed, no globals): a `monomorphize_program` pre-pass walks every
  function body with a type environment, infers each `$T`, interns one instance
  Func sharing the template body (sound because hierc0 recomputes types per
  function), rewrites the calls, and drops the templates; the mangled instance
  name matches hierc byte-for-byte. Known edge (Stage 1c): a generic call nested
  inside a *declaration's* initializer in hierc0 isn't instantiation-typed — the
  test uses the direct forms; fails closed (a compile error, never a miscompile).
- **Stage 2a — generic structs (construction) — SHIPPED** (both compilers).
  `struct Box($T)` / `struct Pair($A, $B)` are templates; each construction
  *infers* the type arguments from the field values (`Box(5)` → `Box__int`,
  `Pair(7, "x")` → `Pair__int__string`) and monomorphizes one concrete struct
  with substituted field types. Field access and inferred locals work on the
  instance. Test: `tests/generic_structs.hi`. In hierc, a generic template is a
  `StructDef` with type-param fields, kept out of codegen; construction interns a
  substituted `StructDef` and reuses all downstream machinery (construction, field
  access, copy/eq, codegen) unchanged. In hierc0, the `monomorphize_program` pass
  interns the concrete `StructDef` (field types string-substituted) and rewrites
  the construction call; the instance name matches hierc. Generic functions
  compose with generic structs (a `$T` binds to `Box__int`).
- **Stage 2b — generic structs (type-position) — SHIPPED** (both compilers).
  `Box(int)` as an explicit-type-args annotation in a parameter, return, field, or
  typed-declaration position — the same surface as the built-in `Option(int)` /
  `Result(int, string)`. In hierc, `parse_type` interns the instance directly at
  the use site. In hierc0 (string types), `parse_type` yields the spelling
  `"Box(int)"` and `monomorphize_program` runs a `resolve_gstruct_type` pass over
  every signature, struct field, and typed-decl annotation, interning the concrete
  `StructDef` and rewriting the string to `"Box__int"`. A generic struct now
  crosses non-generic function boundaries.
- **Stage 3 (struct dep-ordering) — SHIPPED.** A generic struct parameterized by
  another *concrete struct* (`Box(Point)`, where the instance embeds `Point` by
  value) now works in both compilers. hierc already topo-ordered via
  `emit_aggregate`; hierc0 gained a stable struct topological sort
  (`topo_structs`) applied in `monomorphize_program` — it orders a struct after
  the structs it embeds by value (names inside `[...]` are pointers, not deps),
  ties breaking by input order. It runs only for generic programs, so hierc0.hi's
  own emission is untouched and B==C stays byte-identical. Test:
  `tests/generic_struct_deps.hi`.
- **Stage 3 (structured type-param patterns) — SHIPPED.**
  `fn first(xs: [$T]) -> Option(T)` — `$T` is inferred from *inside* a container
  argument (`[$T]` matched structurally against `[int]`) and the return/param
  types (`Option(T)`, `[T]`, `Result($T,$E)`) are substituted and monomorphized.
  In hierc, `instantiate_generic` uses `match_type` + `subst_type` (taught to use
  `is_array`/`arr_of` so the builtin `[int]` matches `[$T]`); the transient
  typaram-bearing composite types a template interns (`Option($T)`, `[$T]`) are
  kept out of every emission loop by a `has_typaram` guard. In hierc0, templates
  are *dropped* before codegen so those types never reach it — only a structural
  string match (`match_typaram_str`) and a recursive bare-`T`→`$T` rewrite are
  needed. Test: `tests/generic_structured.hi`. (Edge: a `[$K: $V]` *map* pattern
  and a `where`-style constraint are still future work.)
- **Stage 3 — constraints, explicit type args (remaining).**
  `where` predicates ([§5](#5-constraints-checked-at-instantiation)), an
  explicit call-site type argument for the non-inferable case
  ([§6](#6-when-t-cant-be-inferred) option 2), and any nesting Stage 1/2 left
  out.

## 9. Two-compiler determinism

The fixpoint differential requires `hierc` and `hierc0` to emit byte-identical C
for `hierc0.hi`, and to agree on every fixture. Generics must therefore
instantiate **deterministically and identically** in both:

- the instantiation key is the `(definition, ordered concrete type args)` tuple,
  using the same canonical type spelling both compilers already share for
  interning (`Option(int)`, `[string: [int]]`, …);
- emission order is the order instantiations are *first interned* during the
  same in-order walk both compilers already perform — so the same program yields
  the same set and order of monomorphic definitions;
- mangled names are a pure function of the key.

If those hold, a generic program is, post-monomorphization, an ordinary
concrete program that both compilers already handle in lockstep.

## 10. Non-goals

Out of scope, to keep the surface small and the model intact:

- **No runtime generics / type erasure / boxing** — always monomorphized.
- **No variance, no higher-kinded types, no associated types.**
- **No user-defined type classes / traits** — constraints, if added, are a fixed
  compiler-known predicate set ([§5](#5-constraints-checked-at-instantiation)).
- **No generic globals** (Hier has no mutable globals anyway).
- **No specialization/overloading** of a generic for particular types — one
  generic body, monomorphized uniformly.
- **No implicit numeric/`distinct` coercion through `$T`** — a `$T` bound to
  `Meters` stays `Meters`, matching newtype rules.

## 11. Docs updated when it landed

The "decided against" passages were rewritten to "monomorphized generics,
reusing the container machinery" — `docs/arrays-structs.md §7/§9`,
`CONTRIBUTING.md` (the "please don't propose generics" line), and the README's
container/generics mentions — and this doc's status header moved from *design*
to *shipped*. Still pending: a short generics section in the learning materials.
