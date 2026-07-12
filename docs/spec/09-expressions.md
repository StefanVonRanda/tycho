# 13. Expressions

An expression computes a value. Its grammar, precedence, and associativity are
in [§4.4–§4.5](02-grammar.md#44-expressions); this chapter defines the meaning
of each operator, the **evaluation order**, and the expression-valued control
forms.

> Provenance: binary-op resolver `src/tychoc.c:5088-5225`; short-circuit
> lowering `:2291-2313`; value-control `parse_value_ctrl`/`ctrl_rewrite_tails`;
> closures `docs/reference/functions.md:80-117`. Evaluation-order rules marked
> "probed" were resolved by running both compilers (spec-plan.md §6a).

## 13.1 Place expressions

A **place** (lvalue) designates storage that can be read and assigned. The places
are: a variable; an array or map index `a[i]` / `m[k]`; a struct field `p.f`; a
tuple element `t.0`; and a user-defined subscript call ([§18](12-aggregates.md),
forthcoming). A place is required as the target of an assignment, a compound
assignment, and the operand of `&` (`inout` argument). Reading a place yields a
value (a deep copy where the value is heap-bearing and must be independent);
writing a place stores into that storage.

## 13.2 Operators

**Arithmetic** (`+ - * / %`, unary `-`). Both operands MUST have the same type,
which is `int`, `float`, a sized numeric (`u32`/`u64`/`f32`), `char` (for `+`/`-`
with the byte-domain rule below), or a newtype over a numeric base; the result
keeps that type. `int`/`u32`/`u64` division or modulo by a zero *value* aborts
([§30](17-runtime.md), forthcoming); by a zero *literal* it is a compile error.
`float`/`f32` division never traps and follows IEEE-754 (`x/0.0` → `±inf` or
`NaN`). Integer overflow wraps (two's-complement for `int`, modulo `2^32`/`2^64`
for `u32`/`u64`). `char ± int` has type `char` but its value is the ordinary
integer result and is **not** reduced to `0..255` (probed: `'a' + 300` yields a
`char` holding `397`).

**Comparison** (`== != < > <= >=`) and `in`. Both operands MUST share a type.
`==`/`!=` apply to any type except `void` and are structural except for function
values; ordering applies only to the ordered set
([§5.5](03-types.md#55-equality-and-ordering)). `x in m` tests map membership;
`x in xs` is not provided (membership on arrays is a corelib function).

**Logical** (`and`, `or`, `not`) operate on `bool` and yield `bool`. `and` and
`or` **short-circuit**: the right operand is evaluated only if the result is not
determined by the left (`a and b` evaluates `b` only when `a` is `true`; `a or
b` evaluates `b` only when `a` is `false`). Precedence, tightest first among the
logical group: `not`, then `and`, then `or`, all looser than any comparison.

**Bitwise and shift** (`& | ^ ~ << >>`). Operands MUST be the same integer type.
`>>` is a logical shift. Per the Go-style precedence (§4.5) `& << >>` bind at the
multiplicative level and `| ^` at the additive level, so every bitwise operator
binds tighter than a comparison: `a & b == c` is `(a & b) == c`. A shift count
outside `0 .. width−1` is **unspecified** (probed: currently inherits the C
target's undefined behavior; a program MUST NOT rely on it).

## 13.3 Unary operators

`-e` negates; `~e` is bitwise NOT; `not e` is boolean negation; `&p` forms an
`inout` argument from a place (§11). Unary operators bind tighter than every
binary operator except the postfix group.

## 13.4 Evaluation order

The following are normative (those marked *probed* were confirmed identical on
both compilers):

- **Short-circuit** `and`/`or` as in §13.2.
- **`match` subject — evaluated exactly once** (*probed*), before any arm is
  tested.
- **`for x in xs` — the collection is evaluated exactly once**, before the loop.
- **Compound assignment** — a **side-effecting call inside the place is evaluated
  once** (*probed*); a pure index sub-expression may be evaluated twice.
- **Argument evaluation order is *unspecified*** (*probed*). Tycho does not
  sequence the arguments of a call, the operands of a binary operator, or the
  sub-expressions of a place relative to one another; their order of side effects
  is inherited from the target and MUST NOT be relied on. A program that needs a
  particular order MUST introduce explicit intermediate bindings.

> Editor's note (punch-list #9, design option): a future version MAY pin
> left-to-right evaluation by emitting sequenced temporaries. Until it does, the
> order is unspecified as stated. Appendix F lists this in the
> unspecified-behavior register.

## 13.5 Expression-valued `if` and `match`

An `if` or `match` may **produce a value** when it appears in tail position — as
the whole right-hand side of a `:=`, a typed `x : T =`, an `x =` or
place-assignment, or a `return`. In that position:

- each branch or arm is a **single expression** whose value is the branch's
  result;
- a value `if` MUST have an `else` (every path yields a value); `elif` chains are
  allowed;
- a value `match` MUST be exhaustive;
- all branches MUST have the same type (with numeric-literal adaptation and
  bare-sum-constructor fixing, [§6.5](04-inference.md#65-branch-unification-for-value-if--match)),
  and that type is the type of the whole expression.

The value lands in the destination's storage exactly as a returned value does; it
introduces no new aliasing. Multi-statement branches, a branch that diverges
(e.g. `return`s) instead of yielding, and use as a nested sub-expression
(`1 + if …`) are not part of the value form; the statement forms
([§14](10-statements.md)) cover those.

## 13.6 Closures and function values

A `fn(params) -> R: expr` lambda and a named function both produce first-class
**function values**. A closure **captures by deep copy at creation**: the values
of the free variables it references are copied into the closure's environment
when the closure is formed, so later mutation of the originals is not observed by
the closure (value semantics). A returned closure's captured environment is
deep-copied into the caller's storage on return, like any escaping value. A
function that has an `inout` parameter cannot be used as a first-class value
(§15). Function values compare by identity
([§5.5](03-types.md#55-equality-and-ordering)).

## 13.7 Other expression forms

- **f-strings** desugar to `str`-concatenation
  ([§3.9.5](01-lexical.md#395-f-string-interpolated-literals)); a hole MUST have
  a type `str` accepts ([§8.2](06-conversions.md#82-explicit-conversion-builtins)).
- **`or_return`** is a postfix operator that unwraps an `Option`/`Result` or
  short-circuits out of the enclosing function ([§19](12-aggregates.md),
  forthcoming); it binds tighter than any binary operator.
- **`spawn`**, **`channel(…)`**, and the concurrency operators are specified in
  [§20](13-concurrency.md); each has grammar restrictions noted in §4.4.
