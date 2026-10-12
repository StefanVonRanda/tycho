# Enum design notes (maintainers)

Why the user-facing behavior in [reference/enums-options.md](../reference/enums-options.md)
is sound. Users don't need this page.

## Payloads

Each variant's payload is arena-allocated and the enum value itself is a small
tagged descriptor. Copying an enum deep-copies the whole payload; `==`
compares structurally. `Option`/`Result` stay inside the model — no null
pointer, no exceptions.

## Why recursive enums stay finite

A recursive enum (`Expr = Num | Add(Expr, Expr)`) is the hardest shape. Because
the payload lives in the arena rather than inline, the descriptor is finite —
no infinitely-large type — and copying the tree runs the recursive copy at
every level. Generic enums monomorphize the same way generic structs do,
including recursive payloads that name the enum itself. The layout and the
copy behavior are covered by [the memory model](../memory-model.md).

## Lowering

- A value `if`/`match` lowers to the declare-then-assign-in-each-arm statement
  form: same exhaustiveness, same single-type rule, no new semantics.
- `or_return` binds tighter than any arithmetic and applies wherever the
  unwrapped value is wanted. The short-circuited `Err` payload is copied into
  the caller's scope so it outlives the return; the `Option` form returns
  `None` the same way.
- `or_else x: h` binds the error to `x` and returns the one-expression `h`
  (to end of line or closing bracket) from the enclosing function.
