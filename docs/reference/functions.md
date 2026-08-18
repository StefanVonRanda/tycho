# Functions and closures

> **Memory:** A closure captures its environment by value (deep copy) and re-homes it
> into the caller's arena on return — the same cross-arena move as any other value.
> Function values may therefore outlive the scope that created them.

A function in Tycho can be a first-class value — bound, passed, stored, returned, and called
indirectly. A closure captures by value, like every other value in the language, which is what
lets closures escape with no lifetime annotations. And any function can be called in method
position with `x.f(a)`, without classes.

## Parameter passing modes

A parameter is passed in one of three modes ([spec §15.2](../spec/11-functions.md#152-parameter-passing-modes)):

- **Default (by value).** A scalar is copied; a heap-bearing value (a `string`, array, map or
  heap-bearing struct) is a **read-only borrow** — the callee may read it but not mutate it,
  and mutating it is a compile error. The caller's value is unchanged either way, so this is
  value semantics with the copy elided where nobody can tell.
- **`inout`** — an exclusive, call-scoped mutable borrow, written `&x` at the call site and
  equivalent to `x = f(x)`. The same variable may not be passed to two `inout` parameters of
  one call.
- **`sink`** — the callee **owns and consumes** the argument. After passing a variable to a
  `sink` parameter the caller MUST NOT use it again; that is a compile error, not a silent
  copy. A fresh literal or a call's result is adopted with no copy.

`sink` is stricter than it first reads, and the shape most people try does not work: a sink
argument must be the variable's **only** mention in the function, and no named variable can be
adopted inside a loop. So a consuming builder is written point-free —
`render(join(of(collect()), add(empty(), tag)))` — rather than as `d = add(d, x)` in a loop.
`tools/tycho-tmpl/` is a worked example, and the reasoning is in
[spec §15.2](../spec/11-functions.md#152-parameter-passing-modes).

`sink` and `inout` are mutually exclusive, and neither may combine with a variadic parameter.

## Variadic parameters

A final parameter written `xs: ...T` is **variadic**: inside the body `xs` is a `[T]`, and a
call packs its trailing arguments into that array. It is sugar over an array — the packed
`[T]` is a value, deep-copied at the boundary like any array, so it needs no new machinery.

```tycho
fn sum(xs: ...int) -> int:
    acc := 0
    for x in xs:
        acc = acc + x
    return acc

sum(1, 2, 3)          # packs [1, 2, 3] -> 6
sum()                 # []int -> 0
nums := [4, 5, 6]
sum(nums...)          # spread: pass an existing array -> 15
```

- Only the **last** parameter may be variadic, and it may follow fixed parameters
  (`fn tagged(tag: string, xs: ...int)`).
- `f(a, b, c)` **packs**; `f(arr...)` **spreads** an existing `[T]` (so a variadic function
  can forward its own args: `sum(xs...)`). A spread argument must be the sole variadic
  argument; a spread is only valid into a variadic parameter.
- The **generic form** `xs: ...$T` infers `T` from the arguments — `count(1, 2, 3)` binds
  `T = int`, `count("a", "b")` binds `T = string`. An empty call has nothing to infer from, so
  `count()` is a compile error — but you can name the type instead: `count$(int)()` supplies
  the empty `[]int`.
- Packing is a property of the **call**, not of how the callee was named: `vp.sum(1, 2, 3)`
  through a package qualifier packs identically, and so does a call carrying explicit type
  arguments.

## First-class function values

A top-level function used as a value has type `fn(P1, ..., Pn) -> R` (drop the `-> R` for a
`void` return):

```tycho
fn dbl(x: int) -> int:
    return x * 2

fn apply(g: fn(int) -> int, x: int) -> int:   # higher-order: takes a function
    return g(x)

fn main():
    f := dbl                       # f : fn(int) -> int
    print(str(f(5)))               # 10  (indirect call)
    print(str(apply(dbl, 21)))     # 42
```

A function value is either a **reference** to a named function — it captures nothing, so it is
just a code pointer, zero-cost and immortal — or a **closure** (below). This is what gives you
`map`/`filter`/`reduce`-style helpers over concrete function arguments without generics. Two
things cannot become values: builtins (`len`, `push`, …) and functions with `inout` parameters.
Everything else is first-class — storable in a struct field, array, map value, or tuple.

## Closures (lambdas)

A **lambda** is an anonymous function written inline; its body is a single expression (an
implicit return). Parameter and return types are elidable wherever an expected `fn` type
supplies them — `apply(fn(x): x * 2, 21)`:

```tycho
fn apply(f: fn(int) -> int, x: int) -> int:
    return f(x)

fn main():
    n := 10
    addn := fn(x: int) -> int: x + n     # a closure: captures n
    print(str(apply(addn, 5)))           # 15
```

Closures **capture by value**: the captured variable is deep-copied into the closure when it is
created, so the closure is independent of any later change to the original. This is what keeps
the value-semantic model intact — a closure is a plain value, with no shared references.

```tycho
a := [10, 20]
get_len := fn() -> int: len(a)
push(a, 30)                  # mutate the original after capture
print(str(get_len()))        # 2 -- the closure kept its own copy, not 3
```

A closure can also **escape** — be returned from the function that created it:

```tycho
fn make_adder(n: int) -> fn(int) -> int:
    return fn(x: int) -> int: x + n      # captures n, then escapes

fn main():
    add5 := make_adder(5)
    print(str(add5(100)))                # 105
```

This stays sound with **no lifetime annotations**: on return, the closure's captured
environment is deep-copied (re-homed) into the caller's arena, exactly like every other heap
value that escapes. The closure carries its own env-copy routine, so the move is automatic.
Function values are full members of the data model — a closure can live in a container and be
called once stored, and a returned closure can be applied inline:

```tycho
ops := [make_adder(1), make_adder(100)]   # an array of closures
print(str(ops[1](5)))                     # 105
print(str(make_adder(7)(3)))              # 10  (apply a returned closure inline)
```

When a container of closures escapes its scope, each closure's captured environment re-homes
along with it — the same deep copy as any heap value. The common higher-order patterns
(`map`/`filter`/`reduce`, predicates, comparators, factory functions, dispatch tables) are all
covered; see [`corelib/iter`](../../corelib/iter/iter.ty).

## Methods (UFCS)

`x.foo(a)` is sugar for `foo(x, a)` — a "method" is just a free function whose first parameter
is the receiver. There are no classes, no inheritance, and no `self`: dispatch is static (on
the receiver's compile-time type), the receiver is passed by value like any argument, and any
type can be a receiver — a struct, an `int`, anything. Calls chain, including on call results:

```tycho
struct Vec:
    x: int
    y: int

fn add(p: Vec, q: Vec) -> Vec:
    return Vec(p.x + q.x, p.y + q.y)

fn norm1(p: Vec) -> int:
    return p.x + p.y

fn doubled(k: int) -> int:
    return k * 2

a := Vec(1, 2)
b := Vec(3, 4)

_m := a.add(b).norm1()   # == norm1(add(a, b))
n := 21
_d := n.doubled()        # == doubled(n) -- an int receiver
```

This extends to the built-ins that take a receiver-shaped first argument, so the operations you
reach for most read the same way your own functions do:

```tycho
s := "a,b"
xs := [1, 2]

_parts := s.split(",")   # == split(s, ",")
xs.push(9)              # == push(xs, 9)
_k := xs.len()           # == len(xs)
_f := n.to_float()       # == to_float(n)
```

`recv.name(args)` on a built-in lowers to `name(recv, args)` and is checked by the exact same
argument rules as the plain call — a wrong receiver type is rejected, not silently coerced. The
receiver-first built-ins include `split`, `find`, `len`, `push`, `pop`, `reserve`, `keys`, `str`,
`substr`, `chr`, the `to_*` conversions, `is_null`, `sqrt`/`pow`/`floor`/`fabs`, and the `map_*`
forms. Constructors (`Some`/`Ok`/`Err`/`None`) and niladic built-ins (`args`, `now`, `ncpu`, …)
have no receiver and stay plain calls.

One disambiguation rule: if the receiver's struct has a *fn-typed field* with the same name as
a free function, the field wins — `h.cb(5)` calls the stored function value, not a free `cb`.
