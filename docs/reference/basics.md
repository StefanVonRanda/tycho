# Basics

> **Memory:** each `if`/`else` block and each loop iteration frees its
> temporary values when it exits.

The procedural core of the language: procedures, variables, expressions,
and control flow. One loop keyword, one procedure form — no `while`,
no `do`/`until`, no ternary.

Where two spellings could do the same job, Tycho keeps one. Some pages
list several similar types (for example, the sequence types); those differ
in memory layout, not style — picking the wrong one is a performance
or lifetime bug, not a taste choice.

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

Parameters come in three kinds. By default a parameter is a **copy** — for
large values (arrays, maps, structs holding them) the callee instead gets a
read-only look that costs nothing. An `inout` parameter is mutated in place:
the callee writes back into the caller's variable, marked with `&` at the
call site.

```tycho
fn incr(n: inout int):
    n = n + 1

fn main():
    x := 41
    incr(&x)             # x is 42 afterwards
```

This is copy-in/copy-out (equivalent to `x = incr(x)`), so it preserves value semantics: the
`&` argument must name a mutable place — a variable, or a field of one (`&b.asks`), and the same variable cannot be passed to two `inout`
parameters of one call (that would be overlapping mutable access). `inout` covers `int`, `bool`,
value structs, and the heap aggregates (`[int]`, `[string]`, heap-bearing structs, maps) —
including `push`/growth and element/field mutation through the borrow. `inout string` works too:
the string stays immutable, but reassignment through the borrow (`s = s + "."`) reaches the
caller, with the new bytes living as long as the caller's variable.

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

This is the copy-avoiding convention: `sink` takes a value the
caller no longer needs — a fresh literal, or a local on its last use —
with **no copy**. It copies only when value semantics require it — if the variable is read
again afterwards, used inside a loop, or captured by a closure. Reusing a value *after*
handing it to a `sink` is a compile error, not a silent copy. A value returned or stored past the
call must still be copied to live longer — that follows from how scopes own memory.

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

**Reserved words.** No name — variable, parameter, function or struct field — may be a keyword,
and the type names are keywords: `int`, `float`, `bool`, `string`, `bytes`, `ptr`, `f32`, and
the sized integers `u8`…`u64`, `i8`…`i64`. So `bytes: int` is not a legal field and `len := …`
is (builtin names are not reserved). The full list of 43 is
[appendix B](../spec/appendix-b-keywords.md#b1-reserved-words).

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
  (recursing structurally through composites); ordering works on two values of one
  numeric type (`int`, `float`, or a sized one like `u8`/`i32`/`f32`), two `char`s, or two
  `string`s (strings compare lexicographically, by byte). `bool` is not ordered.
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

There is no counting form and no `range`: to count, say the
direction in the condition (`i > b` with `i -= s`) instead of in the sign of a step. Note the
trade: a post clause is arbitrary code, so `for i := 0; i < n;
i += 0:` is an infinite loop that nothing diagnoses. To count in parallel, use
`parallel for i in 0..<N:`, which steps by 1 and has no zero-step case.

## Unused names

A local declared with `:=` or with an explicit type and never read again is a
compile **error** — `'x' declared and not used`. A name whose first character is
`_` is exempt, which is how you keep a binding whose only job is to own a value
until the scope ends. `_` is an ordinary *typed* variable here, not a discard, so
a scope that drops both an int and a string needs two names (`_n`, `_s`) rather
than `_` twice.

Parameters, loop variables, and match/select bindings are never reported.

A function nothing ever calls is a **warning**, not an error.
