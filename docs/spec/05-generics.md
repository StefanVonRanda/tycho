# 7. Generics

Tycho's generics are **binding-based monomorphization** over a **closed set of
constraints**. A generic function, struct, or enum is a template; each distinct
concrete instantiation produces one specialized instance. Type parameters never
reach code generation. The constraint mechanism is deliberately *not*
user-extensible — user-defined constraints (traits/typeclasses) are a decided
non-goal ([§1.1](00-conventions.md)); generics grow only by widening the
built-in predicate set.

> Provenance: `instantiate_generic` `src/tychoc.c:6195-6239`; constraints
> `constraint_ok` `:6185-6193`, enforcement `:6218-6237`; parse `parse_fn`
> `:3020-3064`; type grammar for `$T`/`[$N]T` `:1571-1580`,`:1632-1655`.

## 7.1 Type parameters

A type parameter is introduced by the `$` sigil in a type position: `$T` in a
parameter, field, or variant-payload type binds `T` as a parameter of the
enclosing function, struct, or enum. A signature mentioning any `$T` (or a `$N`
size parameter, §7.4) is **generic**. At most 16 type parameters and 16 size
parameters may be introduced per generic.

At a call, each parameter is bound by **structurally matching** the parameter's
type pattern against the concrete argument type. Matching handles a bare `$T`,
and `$T` nested inside `[$T]`, `Option($T)`, `Result($T, $E)`, a function type,
and recursive self-references; every occurrence of a given `$T` MUST bind to the
same concrete type. A generic is instantiated once per distinct binding of its
parameters.

Explicit type arguments may be supplied with the `name$(T, …)` form (§7.6),
which binds the parameters in declaration order; this is required when a
parameter is not inferable from the arguments (for example a payload-less
generic enum variant).

## 7.2 Constraints (`where`)

```
WhereClause ::= "where" Constraint ( "," Constraint )*
Constraint  ::= Predicate "(" IDENT ")" | IDENT ":" Type ( "|" Type )*
Predicate   ::= "numeric" | "comparable" | "has_str" | "hashable" | "defaultable"
```

The predicate set is **closed** — exactly these five names, each a capability the
language already enforces elsewhere. Their meaning, checked against the concrete
type bound to the parameter at instantiation:

| Predicate | Satisfied by (underlying scalar unless noted) |
|---|---|
| `numeric(T)` | `int`, `float` |
| `comparable(T)` | `int`, `char`, `float`, `string` |
| `has_str(T)` | `int`, `bool`, `float`, `string` |
| `hashable(T)` | any legal map key type ([§5.3.5](03-types.md#535-maps-k-v)) |
| `defaultable(T)` | **exactly** `int`, `float`, `bool`, `string` |

`numeric`, `comparable`, `has_str`, and `hashable` see through a newtype (they
test the underlying capability). `defaultable` does **not**: it is satisfied
only by the four bare scalar types, so `defaultable` fails for a newtype even
over a defaultable base — and therefore `zero$(X)` (§7.5) fails for such a
newtype `X`. An unknown predicate is rejected at parse time.

The **type-set** form `T: A | B | …` (up to 16 types) constrains `T` so that its
underlying scalar equals the underlying scalar of one of the listed types. A
`where` clause requires a generic function and allows at most 8 constraints.

Constraints are enforced **at instantiation**, up front, yielding a clear
signature error (`'f' instantiated with T = string, which does not satisfy
numeric(T)`) rather than a deep error inside the substituted body.

Two predicates that might be expected do **not** exist, by design: `ordered(T)`
would duplicate `comparable` (which already gates `< > <= >=`), and no
`equatable(T)` is needed because `==`/`!=` already works on any two values of
the same type ([§5.5](03-types.md#55-equality-and-ordering)).

## 7.3 Generic structs and enums

`struct Name($T, …)` and `enum Name($T, …)` are generic aggregates. Applying one
to concrete type arguments in a type position (`Box(int)`) yields a concrete
instance; applying it to exactly its own type parameters inside its own
definition (`LL($T)` within `struct LL($T)`) is a recursive self-reference,
deferred until the instance is built. A type argument MUST be either fully
concrete or a whole own-parameter reference; it may not *partially* mention a
type parameter. Generic aggregates monomorphize per distinct instantiation and
compose with generic functions, containers, channels, and tasks.

## 7.4 Const generics (`[N]T`, `[$N]T`)

A fixed-size array type `[N]T` has its length `N` fixed at compile time by an
integer literal or an `int` `const` (§5.3.2). The generic form **`[$N]T`**
introduces a **size parameter** `N`: its length is inferred from the (fixed-size
array) argument at each call, and one instance is monomorphized per distinct
length. Inside the body, `N` is an `int` constant. `[$N]$T` infers both the size
and the element type. A `$N` array is rejected in every stored position (struct
field, enum payload, newtype) and as a return-only parameter with nothing to
infer it from — it MUST be inferable from an argument. The argument MUST be a
fixed-size array (a dynamic `[T]` has no compile-time length).

## 7.5 `zero$(T)` and `name$(T, …)`

`zero$(T)` yields the **zero value** of a defaultable type `T` — `0` for `int`,
`0.0` for `float`, `false` for `bool`, `""` for `string` — lowered to the scalar
zero literal. It accepts exactly the four defaultable scalar types (§7.2) and
fails closed for any other type, including a newtype. It lets a generic
accumulator seed from the zero (`acc := zero$(T)`) and so operate on an empty
input.

`name$(T1, …)` is the **explicit type-argument** call form (§7.1); `zero$` is the
one built-in that consumes it specially. There is **no** `empty$(T)` builtin: an
`empty()` returning `[$T]` is an ordinary user-written generic, and `empty$(int)`
is just `name$(…)` applied to it.

> Editor's note (punch-list #27): `docs/generics.md` discusses `empty$` in a way
> that reads as a builtin; it is not. Appendix H logs the reconciliation.

## 7.6 UFCS and generic method-style calls

A generic free function is callable in method position: `x.f(a)` means `f(x, a)`,
dispatched by matching the receiver against the template's first-parameter
pattern and then instantiating. Explicit type arguments on a non-generic call
are an error. Method-call resolution for generics runs before the ordinary
signature lookup ([§15](11-functions.md), forthcoming).

## 7.7 Variadic generics (`...$T`)

A final variadic parameter `...$T` has type `[T]`; a call packs its trailing
arguments into that array, inferring `T` from them, and an existing `[T]` may be
forwarded with the spread form `xs...`. An empty variadic supplied to a generic
element type is an error (nothing to infer `T` from). A variadic parameter
cannot also be `inout` or `sink` (§15, forthcoming).
