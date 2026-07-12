# 12. Declarations, bindings, and scoping

This chapter defines how names are introduced, assigned, resolved, and scoped
within function bodies. The grammar of each form is in
[§4.3.1](02-grammar.md#431-simple-statements); this chapter gives their meaning.

> Provenance: `parse_stmt` `src/tychoc.c:2605-2950`; const folding
> `:2608-2622`,`:3664-3681`; package mangling `pkg_mangle` `:1340-1343`.

## 12.1 Binding forms

| Form | Meaning |
|---|---|
| `x := e` | Declare `x`, inferring its type from `e` ([§6.3](04-inference.md#63-declarations)). `e` MUST NOT be `void`. |
| `x : T = e` | Declare `x` with explicit type `T`; `e` is checked against `T`. |
| `x = e` | Assign to an existing variable `x`; `e` is checked against `x`'s type. |
| `p = e` | Assign through a place `p` (index, field, tuple element, or subscript). |
| `p op= e` | Compound assignment (§12.4). |
| `a, b := f()` | Declare `a`, `b` from a tuple-valued RHS (2–8 targets). |
| `a, b = f()` | Assign to existing `a`, `b` from a tuple-valued RHS. |

A `:=` or `x : T =` **declares** a new variable; a program MUST NOT declare a
name already visible in the same block (a nested block may shadow it, §12.3). An
`=` or place-assignment **requires** the target to already denote a mutable
variable or place; assigning to an undeclared name is an error. A declaration's
value form may be an expression or a tail-position value `if`/`match`
([§13.5](09-expressions.md), [§14.3](10-statements.md)).

## 12.2 Constants

```
const NAME = ConstExpr
```

A `const` binds a name to a compile-time constant, at module top level or inside
a function body. The right-hand side MUST fold to a single literal at compile
time: a literal, or (for the folding forms) integer arithmetic, bitwise, and
unary operations over literals and — at top level — backward references to
earlier top-level constants. A `const` is an immutable named literal, folded at
each use site (it has no run-time storage). Reassigning a `const`, or a
non-constant right-hand side, is an error. A top-level `const` MUST NOT collide
with a struct, enum, newtype, handle, variant, function, or another constant of
the same name. Float arithmetic in a `const` right-hand side, and a *local* `const`
referencing another `const`, are not permitted (only negative float *literals*
fold).

## 12.3 Scope and shadowing

Tycho has **lexical block scope**. A variable is visible from its declaration to
the end of the block that contains it, and each `if`/`elif`/`else` arm, each
`match` arm, each loop body, and each `select` arm is a block. A `match` arm's
payload bindings are visible only within that arm.

A declaration in a nested block MAY **shadow** a name from an enclosing block;
within the nested block the inner binding is the one in scope, and the outer
binding is unaffected after the block ends. A counting-`for` loop variable is
scoped to the loop body. Because Tycho has value semantics, a shadowing
declaration takes an independent copy; mutating the inner name never affects the
shadowed outer value.

## 12.4 Compound assignment

`p op= e` means `p = p op e` for `op` in `+ - * / % & | ^ << >>`, on any place
`p` (variable, index, field, tuple element). The place is evaluated for the read
and the store; a **side-effecting call inside the place is evaluated once** (the
implementation hoists it), while a pure index sub-expression may be evaluated
twice — exactly as writing the assignment out longhand would
([§13.4](09-expressions.md#134-evaluation-order); resolved by probing, both
compilers agree).

## 12.5 Name resolution

Within a body, an identifier resolves to (in order) a local variable or
parameter in scope, a top-level constant, a nullary enum variant, or a
function/type name. A qualified name `pkg.name` resolves through the imported
package's prefix ([§28](15-program.md)). All package-level names
are mangled to one flat namespace by a per-package prefix; this mangling is not
observable in the source language (a program always uses the unqualified or
`pkg.`-qualified spelling).
