# Basics

> **Memory:** Every `if`/`else` block and every loop iteration gets its own arena,
> freed at block exit. This page shows the scope boundaries the compiler sees.

The procedural core of the language: how you define a procedure, declare and assign
variables, write expressions, and control flow. This part is deliberately narrow — one
loop keyword, one procedure form, no `while`, no `do`/`until`, no ternary.

**"One way to do each thing" is true here and not language-wide**, so read it as a claim
about this page rather than about Tycho. A sequence, for instance, has five spellings:
growable [`[T]`](../spec/03-types.md#531-arrays-t), fixed
[`[N]T`](../spec/03-types.md#532-fixed-size-arrays-nt), size-generic `[$N]T`,
inline variable-count [`bounded[N]T`](../spec/03-types.md#5310-boundednt), and
[`soa`](../spec/03-types.md#537-soa); a parameter has
[three passing modes](../spec/11-functions.md#152-parameter-passing-modes). Those are
distinctions of **representation**, not taste — each names a different memory layout or
ownership rule, and picking the wrong one is a performance or lifetime bug rather than a
style preference. What the language avoids is two spellings for the *same* thing.

## Procedures

```tycho
fn add(a: int, b: int) -> int:
    return a + b

fn main():               # entry point: exactly `fn main():`, no return value
    println(str(add(2, 3)))
```

A `fn` with no `-> type` returns nothing. Blocks are indentation-based (tabs or spaces — just
don't mix the two within one line's indentation), every block header ends with `:`, and `#`
starts a comment — like Python's layout.

By default a parameter is a **copy** (or, for the heap aggregates, a read-only **borrow**). An
`inout` parameter is mutated in place: the callee writes back into the caller's variable, marked
with `&` at the call site.

```tycho
fn incr(n: inout int):
    n = n + 1

fn main():
    x := 41
    incr(&x)             # x is 42 afterwards
```

This is copy-in/copy-out (equivalent to `x = incr(x)`), so it preserves value semantics: the
`&` argument must name a mutable variable, and the same variable cannot be passed to two `inout`
parameters of one call (that would be overlapping mutable access). `inout` covers `int`, `bool`,
value structs, and the heap aggregates (`[int]`, `[string]`, heap-bearing structs, maps) —
including `push`/growth and element/field mutation through the borrow. `inout string` works too:
the string stays immutable, but reassignment through the borrow (`s = s + "."`) reaches the
caller, with the new bytes built in the caller's arena. (The copy-in/copy-out wording is the
semantic contract; the codegen passes a pointer plus the owner arena, so no aggregate is copied
in or out — see `docs/internals/design-aggregate-ref.md`.)

A third convention, `sink`, marks a parameter the callee **owns and consumes** — owned, so it
may mutate the buffer (a plain borrow is read-only); consuming, so the caller gives it up:

```tycho
fn scale2(xs: sink [int]) -> int:
    s := 0
    for i := 0; i < len(xs); i += 1:
        xs[i] = xs[i] * 2          # legal: a sink parameter is mutable
        s = s + xs[i]
    return s

fn main():
    print(str(scale2([5, 5, 5])))  # 30 — the fresh literal is adopted and mutated, ZERO copies
    a := [1, 2, 3]
    scale2(a)                      # a is dead after this, so it is adopted too — still no copy
```

This is the copy-eliding convention, and the elision is the point: `sink` adopts a value the
caller no longer needs — a fresh literal, or a local on its last use — straight into the call
with **no copy at all** (verified in the generated C: no `tycho_arr_int_copy` is emitted). It
falls back to a copy exactly when value semantics demand independence — if the variable is read
again afterwards, used inside a loop, or captured by a closure. Reusing an owned value *after*
handing it to a `sink` is a compile error, not a silent copy, so the move is a
checked guarantee. The one copy `sink` can't elide is escape: a value returned or stored past the
call must still be copied to a longer-lived arena — a property of the arena model.

## Declarations and assignment

```tycho
x := 41          # declare + infer the type from the initializer
_y : int = 1      # declare with an explicit type
x = x + 1        # assign (the variable must already exist)
x += 1           # compound assignment: `x op= e` desugars to `x = x op e`
```

Compound assignment works for every binary operator (`+= -= *= /= %= &= |= ^= <<= >>=`) on any
assignable place — a variable, an array element (`a[i] += 1`), a struct field (`p.x *= 2`), or a
map entry (`m[k] += 1`).

## Expressions

- **Integers:** `+ - * / %` (`/` truncates toward zero, `%` is the remainder), unary `-`.
  Bitwise and shift: `& | ^ << >>` and unary `~`. These follow **Go precedence** — `% << >> &`
  bind at the multiplicative level and `| ^` at the additive level, so every bitwise operator
  binds *tighter* than a comparison: `a & b == c` parses as `(a & b) == c`, with no C surprise.
  Signed overflow is defined as two's-complement wraparound; `/0` and `%0` abort.
- **Floats:** `+ - * /` (`/` is true division), unary `-`. A float literal is `digits.digits`,
  a leading-dot `.digits`, or any of these with an `e`/`E` exponent (`3.14`, `.5`, `1e10`,
  `1.5e-3`). `int` and `float` never mix implicitly — convert with `to_float` / `to_int`.
- **Strings:** `+` concatenates. `s[i]` reads the byte at index `i` as an `int` (`0`–`255`),
  bounds-checked; strings are immutable, so `s[i] = v` is a compile error.
- **Comparisons:** `== != < > <= >=` produce `bool`. `==`/`!=` work on any matching pair
  (recursing structurally through composites); ordering works on two `int`s or two `string`s
  (strings compare lexicographically, by byte).
- **Logic:** `and`, `or`, `not` on `bool`, producing `bool`. `and`/`or` short-circuit.
  Precedence, tightest first: comparisons, then `not`, `and`, `or` — so `a < b and not done` is
  `(a < b) and (not done)`.
- **Calls:** `f(a, b)`.

## Control flow

There is exactly one loop keyword, `for`, in four shapes — it does everything a `while` would.

```tycho
cond := true
other := false
n := 3
a := 3
b := 0
s := 1
xs := [10, 20]

if cond:
    println("then")
elif other:                 # zero or more elif branches
    println("elif")
else:
    println("else")

for cond:                   # condition form: repeat while cond is true
    cond = false
for:                        # infinite form: until a break or a return
    break
for i := 0; i < n; i += 1:  # three-clause form: i goes 0 .. n-1
    println(str(i))
for i := a; i > b; i -= s:  # counting down — the direction is in the condition
    println(str(i))
for x in xs:                # foreach: each element of an array, or each byte of a string
    println(str(x))

```

Inside a loop body:

| | |
|---|---|
| `break` | exit the nearest enclosing loop |
| `continue` | skip to its next iteration (runs the post clause) |
| `pass` | do nothing — the body for a block that has no work |

In the three-clause form all three clauses are required (`for:` is the only degenerate form);
the init is a declaration or an assignment, the post is an assignment to a variable, and a
variable the init declares is scoped to the loop. `continue` **runs the post clause**, so a
loop that advances only there still terminates. The foreach form binds each element of an
array (`[T]`) or each byte of a string, evaluating the collection once. The condition form
takes any `bool`. `break` and `continue` work in every shape and are an error outside a loop.

No block may be empty, and **`pass` is how you write one that does nothing** — a `match` arm
whose case is deliberately ignored, an `if` branch kept for its comment, a loop whose work is
all in its head. Reach for it before writing an empty function to call:

```tycho
fn drain(r: Result(int, string)) -> int:
    match r:
        Ok(n): return n
        Err(e): pass          # this case is deliberately ignored
    return 0
```

`pass` is contextual, not reserved: it is a statement only in statement position, so a variable
named `pass` still works. It is not an expression — it produces no value and cannot be assigned
([spec §14.1.1](../spec/10-statements.md#1411-pass)).

There is no counting form and no `range`: `for i in range(a, b, step):` was removed on
2026-07-29, and a `for` head that names `range` is refused with its replacements. Say the
direction in the condition (`i > b` with `i -= s`) instead of in the sign of a step. **One
guarantee was lost with it, deliberately:** `range()` rejected a literal `0` step at compile
time and aborted on a runtime `0`; a post clause is arbitrary code, so `for i := 0; i < n;
i += 0:` is an infinite loop that nothing diagnoses. To count in parallel, use
`parallel for i in 0..<N:`, which steps by 1 implicitly and so has no zero-step case at all.

## Unused names

A local declared with `:=` or with an explicit type and never read again is a
compile **error** — `'x' declared and not used`. A name whose first character is
`_` is exempt, which is how you keep a binding whose only job is to own a value
until the scope ends. `_` is an ordinary *typed* variable here, not a discard, so
a scope that drops both an int and a string needs two names (`_n`, `_s`) rather
than `_` twice.

Parameters, loop variables, and match/select bindings are never reported.

Fences on this page and in the specification use `_name` where a value is bound
only to show the syntax and reading it would add nothing. The guides do the
opposite and use what they bind.

A function nothing ever calls is a **warning**, not an error. It is reported
after code generation, because a callee reached only from a generic body is not
resolved until that body is instantiated.
