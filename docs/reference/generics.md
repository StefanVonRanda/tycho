# Generics

> **Thesis context:** Generics test that the arena model survives type parameterization.
> Monomorphization runs before the escape analysis — each instantiation is concrete,
> value-semantic code, so the same locally-decidable lifetime rules apply. If generics
> introduced pointers, aliasing, or whole-program dependence, the arena model would break.
> They don't, so the model holds.

A type parameter is written `$T`. Generic functions, structs, and enums are **monomorphized** —
the transpiler stamps out a concrete copy per type the code actually uses, reusing the same
container machinery as everything else, so a generic costs nothing at runtime. There are no type
classes or trait bounds; the small `where` predicate set is the only way to constrain a parameter.

## Generic functions

A function parameterized by `$T` (introduce the parameter with `$` once; refer to it as `T`
after):

```
fn id(x: $T) -> T:
    return x

fn first(xs: [$T]) -> Option(T):
    if len(xs) > 0:
        return Some(xs[0])
    return None
```

A `where` clause constrains the parameter to a fixed, transpiler-known predicate set —
`numeric(T)`, `comparable(T)`, `has_str(T)` — so an operation the body relies on is checked at
the call:

```
fn maxv(a: $T, b: $T) -> T where comparable(T):
    if a > b:
        return a
    return b
```

## Generic structs and enums

```
struct Box($T):
    v: $T

enum Tree($T):
    Leaf($T)
    Node([Tree($T)])        # recursive, generic payload through a container
```

Construct an instance by supplying values; the type argument is inferred from them — `Box(7)` is
a `Box(int)`, and `Leaf(7)` a `Tree(int)`. A payload-less variant that cannot fix the parameter
from a value takes an **explicit type argument**: `Empty$(int)`.

Generic enums are monomorphized exactly like generic structs, including **recursive payloads that
name the enum itself** — both directly (`Node(Tree($T), $T, Tree($T))`) and through a container
(`Node([Tree($T)])`, the generic-AST case).

## Composition

A generic type composes with the rest of the language uniformly — a `Box(int)` may be a map
value, a channel element, a `Task` result, a struct field, or another generic's argument
(`Box(Box(int))`), and a generic function may return a generic value. Each combination is just
another monomorphized instance.

---

*The monomorphization strategy* (binding-based, no AST cloning) is documented in
[the generics design note](../guides/generics.md).
