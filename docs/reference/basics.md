# Basics

The procedural core of the language: how you define a procedure, declare and assign
variables, write expressions, and control flow. Tycho is small — one way to do each thing.

## Procedures

```
fn add(a: int, b: int) -> int:
    return a + b

fn main():               # entry point: exactly `fn main():`, no return value
    print(str(add(2, 3)) + "\n")
```

A `fn` with no `-> type` returns nothing. Blocks are indentation-based (tabs or spaces — just
don't mix the two within one line's indentation), every block header ends with `:`, and `#`
starts a comment — like Python's layout.

By default a parameter is a **copy** (or, for the heap aggregates, a read-only **borrow**). An
`mut` parameter is mutated in place: the callee writes back into the caller's variable, marked
with `&` at the call site.

```
fn incr(n: mut int):
    n = n + 1

fn main():
    x := 41
    incr(&x)             # x is 42 afterwards
```

This is copy-in/copy-out (equivalent to `x = incr(x)`), so it preserves value semantics: the
`&` argument must name a mutable variable, and the same variable cannot be passed to two `mut`
parameters of one call (that would be overlapping mutable access). `mut` covers `int`, `bool`,
value structs, and the heap aggregates (`[int]`, `[string]`, heap-bearing structs, maps) —
including `push`/growth and element/field mutation through the borrow. `mut string` works too:
the string stays immutable, but reassignment through the borrow (`s = s + "."`) reaches the
caller, with the new bytes built in the caller's arena.

## Declarations and assignment

```
x := 41          # declare + infer the type from the initializer
y : int = 1      # declare with an explicit type
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

There is exactly one loop keyword, `for`, in three shapes — it does everything a `while` would.

```
if cond:
    ...
elif other:                 # zero or more elif branches
    ...
else:
    ...

for cond:                   # condition form: repeat while cond is true
    ...
for i in range(n):          # counting form: i goes 0 .. n-1
    ...
for i in range(a, b):       # a .. b-1   (range(a, b, step); step may be negative)
    ...
for x in xs:                # foreach: each element of an array, or each byte of a string
    ...

break                       # exit the nearest enclosing loop
continue                    # skip to its next iteration
```

In the counting form the loop variable is an `int` scoped to the loop; the foreach form binds
each element of an array (`[T]`) or each byte of a string, evaluating the collection once. The
condition form takes any `bool`. `break` and `continue` work in every shape and are an error
outside a loop.
