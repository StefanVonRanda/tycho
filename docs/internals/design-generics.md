# Generics design notes (maintainers)

Why the behavior in [reference/generics.md](../reference/generics.md)
is sound. Users don't need this page.

## Strategy

Binding-based monomorphization, no AST cloning. At a call site or type
use, bind `$` parameters by structural match, intern
`(generic_def, [concrete args])` in a side table (same pattern as
`opt_of` / `arr_of` / `mapc_of`), and emit one substituted concrete
copy on first use. Mangled names carry the args
(`first__int`, `Pair__int__string`). After substitution the body flows
through resolve + codegen unchanged.

## Why it fits the memory model

Monomorphization runs before escape analysis. Each instantiation is
already concrete value-semantic code, so the same per-signature escape
decision applies to `first__int` as to hand-written
`fn first(xs: [int])`. No pointers, no aliasing, no whole-program
analysis introduced.

Costs that grow: code size (one copy per distinct type tuple used),
compile time (substitute + re-resolve per instantiation), error
locality (body errors surface at the instantiation).

## Determinism

Same program yields the same set and order of definitions on every
run: instantiation key is `(definition, ordered concrete args)` in
canonical spelling; emission order is first-interned order during an
in-order walk; mangled names are a pure function of the key.
