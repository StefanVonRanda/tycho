# Generics

> **Memory:** each instantiation is ordinary concrete code, so the same
> per-scope freeing applies. Generics add no pointers and no aliasing.

A type parameter is written `$T`. Generic functions, structs, and enums are **monomorphized** —
the transpiler stamps out a concrete copy per type the code actually uses, so a generic costs nothing at runtime. There are no type
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

A `where` clause constrains the parameter to a fixed predicate set, so an
operation the body relies on is checked at the call. The set is **closed at five** —
there is no way to add one:

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

Generic enums work like generic structs, including **recursive payloads that
name the enum itself** — directly (`Node(Tree($T), $T, Tree($T))`) and through a container
(`Node([Tree($T)])`).

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

## How inference works

`$T` is introduced at its first appearance and inferred from the argument
by structural match — the parameter pattern and the argument type are walked
together, and where the pattern says `$T`, that concrete type is recorded:

```text
pattern:  [ $T ]          arg type: [int]            =>  T := int
pattern:  Option($T)      arg type: Option(string)   =>  T := string
pattern:  [$K: $V]        arg type: [string: [int]]  =>  K := string, V := [int]
```

- Each parameter is bound by the first argument that pins it. A later
  argument that disagrees is a call-site type error.
- Inference runs left to right, with no backtracking.
- A parameter that never appears in an argument type cannot be inferred —
  supply it explicitly (`empty$(int)`).
- The body is substituted and checked as ordinary concrete code. A body
  error points at the instantiation that triggered it; a `where` clause
  turns it into a signature error instead.

```tycho
fn sum(xs: [$T]) -> T where numeric(T):
    total := zero$(T)
    for x in xs:
        total = total + x
    return total
```

A `where` on a non-generic function, an unknown predicate, or a name that
is not a type parameter is rejected. A constraint may also be a type set —
`where T: int | float` — checked at instantiation like the predicates.

A generic free function works as a method through UFCS: `xs.first()` is
`first(xs)`.

## Non-goals

Not supported, to keep the surface small:

- No runtime generics, boxing, or type erasure — always monomorphized.
- No variance, higher-kinded types, or associated types.
- No user-defined type classes or traits.
- No generic globals.
- No specialization of one generic for particular types.
- No implicit numeric coercion through `$T` — `$T` bound to `Meters`
  stays `Meters`.

---

How this is implemented — binding, interning, determinism — is in
[design-generics](../internals/design-generics.md), for maintainers.
