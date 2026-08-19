# Generics

> **Memory:** Monomorphization runs before the escape analysis, so each instantiation
> is concrete, value-semantic code and the same locally-decidable lifetime rules apply.
> Generics introduce no pointers, no aliasing and no whole-program dependence.

A type parameter is written `$T`. Generic functions, structs, and enums are **monomorphized** —
the transpiler stamps out a concrete copy per type the code actually uses, reusing the same
container machinery as everything else, so a generic costs nothing at runtime. There are no type
classes or trait bounds; the small `where` predicate set is the only way to constrain a parameter.

## Generic functions

A function parameterized by `$T` (introduce the parameter with `$` once; refer to it as `T`
after):

```tycho
fn id(x: $T) -> T:
    return x

fn first(xs: [$T]) -> Option(T):
    if len(xs) > 0:
        return Some(xs[0])
    return None
```

A `where` clause constrains the parameter to a fixed, transpiler-known predicate set, so an
operation the body relies on is checked at the call. The set is **closed at five** — there is
no way to add one, which is the deliberate anti-traits stance:

| predicate | satisfied by |
|---|---|
| `numeric(T)` | `int`, `float` |
| `comparable(T)` | `int`, `char`, `float`, `string` |
| `has_str(T)` | `int`, `bool`, `float`, `string` |
| `hashable(T)` | any legal map key type — and it admits `K` as a map key *inside* the body |
| `defaultable(T)` | exactly `int`, `float`, `bool`, `string` |

Several constraints are separated by **commas**, not `and`:
`where defaultable(T), numeric(T)`.

The first four see through a newtype (they test the underlying capability); `defaultable` does
not, so `zero$(X)` fails for a newtype even over a defaultable base.

```tycho
fn maxv(a: $T, b: $T) -> T where comparable(T):
    if a > b:
        return a
    return b
```

## Explicit type arguments and `zero$(T)`

Type arguments are normally inferred from the arguments. Where they cannot be — a payload-less
generic enum variant, or an empty generic variadic — supply them with `name$(T, …)`, which
binds the declaration's parameters in order:

```tycho
fn count(xs: ...$T) -> int:
    return len(xs)

fn main():
    println(str(count(1, 2, 3)))    # inferred: T = int
    println(str(count$(int)()))     # named: nothing to infer from
```

The name may be package-qualified (`vp.pair$(int, string)(1, "a")`). `zero$(T)` is the one builtin that consumes this form: it yields the
zero value of a `defaultable` type, which is what lets a fold seed an accumulator without the
caller supplying one (`acc := zero$(T)`).

## Generic structs and enums

```tycho
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

**A type argument list is all-parameters or all-concrete.** Applying a generic to
its own parameters, in order, is a recursive reference and is deferred until the
instance is built:

```tycho
struct Node($T):
    v: $T
    kids: [Node($T)]
```

Anything else must be entirely concrete — `Index(string, [float])`. A list that
mixes the two is refused even when each argument alone would be legal, so
`Index($K, [float])` does not work in a parameter position and neither does
`Index($K, [$V])`. Write the helper against the concrete instantiation, or make
it generic over the whole struct (`ix: inout Index($K, $V)`).


## Composition

A generic type composes with the rest of the language uniformly — a `Box(int)` may be a map
value, a channel element, a `Task` result, a struct field, or another generic's argument
(`Box(Box(int))`), and a generic function may return a generic value. Each combination is just
another monomorphized instance.

---

*The monomorphization strategy* (binding-based, no AST cloning) is documented in
[the generics design note](../guides/generics.md).
