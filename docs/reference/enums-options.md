# Enums, options, and `match`

A *sum type* is a value that is exactly one of several alternatives. Tycho has one
general form, the user-defined `enum`, and two built-in special cases that carry the
language's answers to two perennial questions: `Option(T)` (the absence of a value,
without `null`) and `Result(T, E)` (failure, without exceptions). All three are taken
apart the same way — an exhaustive `match` — and all three are ordinary values, deep-copied
on assignment and compared structurally with `==`, like everything else in the language.

## `enum` — user-defined sum types

An `enum` declares a type whose value is exactly one of several named **variants**, each
carrying a payload of zero or more types. This is the tagged union, or algebraic data type:

```
enum Shape:
    Circle(float)            # a variant with a payload
    Rect(float, float)
    Unit                     # a payload-less variant

fn area(s: Shape) -> float:
    match s:                 # exhaustive: every variant needs an arm
        Circle(r):
            return 3.14159 * r * r
        Rect(w, h):
            return w * h
        Unit:
            return 1.0
```

You build a value by naming a variant — `Circle(2.0)`, or a bare `Unit` for a payload-less
one — and take it apart with `match` (below). Variant names are global: you write `Circle`,
not `Shape.Circle`, and no two enums may share a variant name.

An enum value is a small value-semantic descriptor whose payload lives in the arena, so
copying one is a **deep copy of the whole payload** and `==` compares two values
structurally. An enum may appear anywhere a type may — a struct field, an array element
(`[Shape]`), a map value, a function parameter or return.

### Recursive enums are ASTs

A variant may carry the enum itself, which makes the type an abstract syntax tree. Because
the payload is arena-allocated rather than stored inline, a recursive enum is still a finite
value — there is no infinitely-large type — and copying one deep-copies the entire tree:

```
enum Expr:
    Num(float)
    Add(Expr, Expr)          # recursive: a variant carrying Expr
    Neg(Expr)

fn eval(e: Expr) -> float:
    match e:
        Num(v):
            return v
        Add(l, r):
            return eval(l) + eval(r)
        Neg(x):
            return -eval(x)

fn main():
    e := Add(Num(2.0), Neg(Num(4.0)))
    print(str(eval(e)))      # -2.0
```

An enum may also be **generic** — `enum Tree($T)` — and is then monomorphized like a generic
struct, including recursive payloads that name the enum itself. See [Generics](generics.md).

## `match` — the exhaustive eliminator

`match` is how every sum type is consumed. It dispatches on the variant and binds the
payload in one step:

```
match shape:
    Circle(r):               # r is bound to the payload (a float here)
        ...
    Rect(w, h):              # multiple payload fields bind in order
        ...
    Unit:                    # no payload, no bindings
        ...
```

Two rules make `match` safe by construction:

- **It is exhaustive.** Every variant of the matched type must have an arm; a missing case
  is a compile error, so adding a variant forces you to handle it everywhere. A wildcard
  arm `_:` matches any remaining variant when you genuinely want a catch-all.
- **Bindings are local to their arm.** `r` exists only inside the `Circle` arm. There is no
  way to read a payload that does not belong to the current variant.

`match` is a statement. To produce a value, assign or `return` from inside each arm, as the
`eval` example does.

## `Option(T)` — a value, or nothing

`Option(T)` is the built-in enum with variants `Some(x)` and `None`. It is Tycho's answer to
the null-reference problem: a value that may be absent is *typed* as absent, and the absence
cannot be ignored, because reading it requires an exhaustive `match`.

```
fn index_of(xs: [int], target: int) -> Option(int):
    for i in range(len(xs)):
        if xs[i] == target:
            return Some(i)
    return None              # None's type comes from the return type

fn main():
    match index_of([10, 20, 30], 20):
        Some(i):             # i is bound to the value (an int here)
            print("found at " + str(i) + "\n")
        None:
            print("not found\n")
```

`None` has no type of its own, so it is only allowed where the expected type is already
known — a return type, a declaration annotation (`box : Option(string) = None`), an
assignment target, or a call argument. A bare `x := None` is a compile error, because there
is nothing to fix `T`.

`T` may be any type, including another option (`Option(Option(int))`); each instantiation is
a distinct tagged value, deep-copied like any other. An option is a fine struct field
(`age: Option(int)`) or array element (`[Option(int)]` — a list of optionals, where a `None`
element takes its type from the others, so the first element cannot be a bare `None`).

Two options compare with `==` (structurally — `Some(1) == Some(1)` is `true`, `Some(1) ==
None` is `false`), and so do values that merely contain them.

## `Result(T, E)` — success, or an error

`Result(T, E)` is the built-in enum with variants `Ok(value)` and `Err(error)` — Tycho's
answer to error handling without exceptions. A function that can fail returns one, and the
caller must confront both outcomes in a `match`:

```
fn checked_div(a: int, b: int) -> Result(int, string):
    if b == 0:
        return Err("divide by zero")
    return Ok(a / b)

fn main():
    match checked_div(10, 0):
        Ok(v):
            print("= " + str(v) + "\n")
        Err(e):
            print("error: " + e + "\n")
```

Like `None`, a bare `Ok(v)` or `Err(e)` fixes only one of the two type parameters, so the
other comes from context (a return type, an annotation, an assignment target, or an
argument); a bare `x := Ok(1)` is a compile error. Both `T` and `E` may be any type,
including heap ones such as `Result([int], string)`. Two `Result` values compare with `==`,
recursing through `T` and `E`.

## `or_return` — propagating errors without boilerplate

Chaining fallible calls with `match` at every step is noise. `or_return` removes it: writing
`v := expr or_return` unwraps an `Ok` (binding `v` to the value) or, the moment `expr` is an
`Err`, returns that `Err` from the enclosing function — which must itself return a
`Result(_, E)` with the *same* error type `E`:

```
fn add_two(a: string, b: string) -> Result(int, string):
    x := parse_digit(a) or_return    # Ok -> bind x; Err -> return it from add_two
    y := parse_digit(b) or_return
    return Ok(x + y)
```

`or_return` is a postfix operator that binds tighter than any arithmetic, so it is valid
anywhere the unwrapped value is wanted — `foo(parse(s) or_return)`,
`return Ok(parse(s) or_return + 1)`, and inside nested blocks. The short-circuited `Err`'s
payload is promoted into the caller's arena, so it outlives the return.

The same operator works on `Option`: inside a function that returns `Option(T)`,
`v := opt or_return` binds `v` on `Some(v)` and returns `None` on `None`.

---

*Design background:* why arena-allocated payloads keep recursive enums finite and copyable,
and how the tagged value is laid out, is in [the memory model](../memory-model.md).
