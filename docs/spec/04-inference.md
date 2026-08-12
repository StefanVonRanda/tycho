# 6. Type inference

Tycho types every expression **at its own site**. There is no global type
inference, no Hindley-Milner unification, and no type variables in the checker.
Inference is **bidirectional and local** (the Pierce-Turner style): a site is
either *synthesized* (its type is computed bottom-up from the expression) or
*checked* against an expected type supplied by its context. This locality is a
deliberate design property, not a limitation to be lifted
([§1.1](00-conventions.md); the alternative, global inference, is a decided
non-goal).

> Provenance: synthesis `resolve_expr`, checking `resolve_exp(e, want)`
> (`src/tychoc.c@resolve_expr_inner`, `src/tychoc.c@resolve_exp`); declaration
> handling `src/tychoc.c@S_DECL`.

## 6.1 Synthesis and checking

- **Synthesis.** Given an expression with no expected type, the checker computes
  its type from its form and its already-typed sub-expressions (an integer
  literal synthesizes `int`, `a + b` synthesizes from its operands, a call
  synthesizes its callee's return type, and so on).
- **Checking.** When a context supplies an **expected type** `T`, the checker
  verifies the expression against `T`. Checking is not a separate type
  algorithm; it performs only the things synthesis cannot do on its own
  (§6.2), then falls back to synthesizing and requiring the result to equal `T`.

An expected type flows from exactly these contexts: a typed declaration's
annotation, an assignment's target type, a call argument's parameter type, a
struct/enum/newtype constructor's field/payload type, a `return`'s function
return type, a map/element/field store's slot type, and a tuple or array
literal's element type.

## 6.2 What checking adds beyond synthesis

When checking an expression against an expected type `T`, and only then, the
following groundings occur:

1. **Ground a pending type.** A declaration whose initializer is an
   otherwise-untypeable empty form is *pending* until first use (§6.4); checking
   against `T` grounds it to `T`.
2. **Fill an empty aggregate.** A bare `[]` takes its element types (array or
   soa) or its key/value types (map) from `T`.
3. **Coerce a bracket literal to a fixed-size array.** A `[a, b, c]` literal
   checked against `[N]U` becomes a fixed-size array, with the element count and
   element types verified against `N` and `U`.
4. **Adapt a numeric literal.** An integer or float **literal** (not a typed
   value) adapts to `T` when `T` is a numeric type it can represent (§8).
5. **Elide a lambda's types.** A lambda checked against a function type `T` may
   omit its parameter and return type annotations, taking them from `T`.
6. **Fix a bare sum constructor.** A bare `None`, `Ok(v)`, or `Err(e)` takes its
   missing type parameter from `T` (an `Option`/`Result`). A payload-less `Ok()`
   already knows its half — the ok payload is `void`
   ([§5.3.6](03-types.md#536-enums-option-result)) — so it takes only the error
   type from `T`, and fails if `T`'s ok payload is anything but `void`.
7. **Check a tuple literal element-wise.** A tuple literal checked against a tuple
   type of the same arity checks **each element** against the corresponding element
   type, so an `Ok`/`Err`/`None`/`[]` element grounds inside a tuple exactly as it
   does anywhere else: `return (Err(A), "partial")` from a
   `-> (Result(int, E), string)` function is well typed.

If none of these applies, the expression is synthesized and its synthesized type
MUST equal `T`; otherwise the program is rejected with a type-mismatch
diagnostic naming both the expected and the actual type.

## 6.3 Declarations

- `x := e` (declare-and-infer) **synthesizes** the type of `e` and gives it to
  `x`. The initializer MUST NOT be `void`.
- `x : T = e` (typed declaration) **checks** `e` against `T`. A mismatch — for
  example a `U` value where a newtype over `U` is declared — is rejected. This
  is where newtype distinctness ([§5.4](03-types.md#54-newtypes)) is enforced.
- `x = e` (assignment) checks `e` against the existing type of `x`.
- Function parameter and return types are **always explicit** in the signature;
  they are the fixed points from which inference within a body proceeds.

## 6.4 Pending types

Two initializers cannot be typed in isolation: a bare empty array `[]` and a
bare `None`. When used as a declaration initializer without an annotation, the
variable's type is **pending** and is grounded by the type expected at its first
use within the same block; a pending type that is **never** grounded is rejected
(a program MUST annotate or ground it, e.g. `x : [int] = []`, `x : Option(int) =
None`). The pending mechanism resolves a type with exactly **one** free parameter
that a single later use can pin — `[]` is `[?T]` and `None` is `Option(?T)`.

The `Result` constructors are a **deliberate exception**: `x := Ok(v)` and
`x := Err(e)` are **not** pending. A bare constructor pins only one of `Result`'s
two parameters (`Ok(v)` gives the ok type, `Err(e)` the err type), leaving the
other free, which a single grounding use in the same block does not generally fix;
so they are rejected **immediately** at the declaration and MUST be annotated
(`x : Result(int, string) = Ok(v)`). This is the one place the inference asymmetry
is visible; the compiler's diagnostic names the required annotation. (Pure
precedent — Go, Swift, and Odin all reject a bare `nil`/`None` declaration outright
— would argue for rejecting `None` too, but that would remove the deliberate `[]`
/ `None` pending convenience, so Tycho keeps it and documents the `Result` limit
instead.)

> Provenance: pending deferral `src/tychoc.c:7312-7324`, grounding `pend_ground`
> `:4993-5020`; rejection of ungrounded `None` / immediate rejection of bare
> `Ok`/`Err` `:7332-7337`.

## 6.5 Branch unification for value `if` / `match`

A value-producing `if` or `match` in tail position ([§4.3.2](02-grammar.md),
[§13](09-expressions.md)/[§14](10-statements.md)) requires **all
branches to have the same type**, which becomes the type of the whole
expression. For the `:=` and typed `x : T =` forms, unification is **strict**
equality of the independently-synthesized branch types: there is no
numeric-literal adaptation across branches and no type variable, and a bare
`None`/`Ok`/`Err` branch is rejected (it must be annotated). In the `x =` /
place-assignment / `return` tail positions a destination type flows into each
branch, so adaptation and bare-sum-constructor fixing apply there. A branch
whose type cannot be made equal to the others is rejected.

A branch whose tail is a **diverging** call — `die(msg)` or `exit(code)`
([§29.12.1](16-builtins.md)) — is **exempt**: control never leaves it, so it
carries no type, takes no part in unification, and is not rewritten into an
assignment to the destination. This is what makes the failure arm of a `Result`
readable (`Err(e): die("cannot bind")`) instead of costing a dummy initializer and
a statement `match`. If every branch diverges there is no value at all and the
declaration is rejected.

## 6.6 Non-goals of inference

A conforming implementation MUST NOT perform inference beyond the local,
bidirectional rules above. In particular there is no whole-program or
whole-function unification, no flow across statement boundaries other than the
within-block pending-grounding of §6.4, and no inference of function signatures.
Every expression's type is decidable from that expression, the types already
assigned to its sub-expressions, and at most one expected type supplied by its
immediate context.
