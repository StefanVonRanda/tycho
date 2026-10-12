# Function design notes (maintainers)

Why the user-facing behavior in [reference/functions.md](../reference/functions.md)
is sound. Users don't need this page.

## Capture by value

A closure captures its environment by value (deep copy) when created, so the
closure is independent of later changes to the originals. On return the
captured environment moves into the caller's scope — the same cross-scope move
as any other escaping value, via the closure's generated environment-copy
routine. No lifetime annotations are needed, and a stored closure (in an
array, struct field, or map value) carries its environment the same way. The
higher-order patterns this covers (`map`/`filter`/`reduce`, predicates,
comparators, factories, dispatch tables) have examples in
[`corelib/iter`](../../corelib/iter/iter.ty).

## Parameter modes

- `inout` is call-scoped exclusive mutation (equivalent to `x = f(x)`); passing
  one variable to two `inout` parameters of a call is overlapping mutable
  access and is rejected.
- `sink` is owned-and-consuming: a fresh literal or a last-use local is
  adopted with no copy, and any later use is a compile error rather than a
  silent copy. A sink argument must be the variable's only mention in the
  function, and no named variable is adopted inside a loop — hence consuming
  builders written as nested calls. `tools/tycho-tmpl/` is the worked example.

## Methods

UFCS (`x.foo(a)` for `foo(x, a)`) resolves on the receiver's compile-time
type with no dynamic dispatch. A built-in in receiver position rewrites to the
plain call under identical argument rules. A same-named fn-typed field wins
over a free function.
