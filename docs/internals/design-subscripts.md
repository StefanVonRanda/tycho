# Subscript design notes (maintainers)

Why the user-facing behavior in [reference/subscripts.md](../reference/subscripts.md)
is sound. Users don't need this page.

## Lowering

A subscript is a compile-time place-macro: at a call site `g.edge(i)` is replaced
by the yielded place with the arguments substituted (`g.nodes[i]`), and the
surrounding read or write flows through the ordinary lvalue machinery. There is
no runtime object. It generalizes the built-in `&m[k]` place to user code: a
scoped, transient projection into part of a value — never a stored reference,
never crossing the deep-copy thread boundary, needing no lifetime analysis.
That is what fits it inside the arena + deep-copy model.

## Why each rule exists

- **Rooted in a parameter.** The projection must reach into an argument, so it
  cannot dangle. A place rooted in a fresh local is rejected. This is the same
  scoping argument as the limited-references spike
  ([limited-references](../rfc/limited-references-spike.md)): the one
  limited-reference construct that stays sound is the one that cannot escape.
- **Each parameter used at most once.** Substitution duplicates the argument
  expression at each mention, so a double mention would double-evaluate it.
  Multi-use with argument hoisting is the natural future extension. The receiver
  counts, which is why 2-D indexing into a flat array
  (`yield &g.cells[r * g.w + c]`) is refused; storing rows nested
  (`&g.rows[r][c]`) mentions the receiver once. `tools/tycho-grid/` works
  exactly that way.
- **Concrete receiver.** Selection is by the receiver's exact monomorphized
  type, so a `Pool($T)` receiver could never match the `Pool(int)` a call site
  presents. Rejected at the declaration. The remedy is a generic function —
  which is what Go and Odin do, neither having user-defined indexing at all.
- **Scoped and transient.** Never stored in a field, never sent across a
  `spawn`/channel boundary. Storing a reference (a "remote part") is a
  non-goal: it has no sound deep-copy at the thread boundary.

## Value semantics

Writing through a projection into a by-value parameter mutates that parameter's
private copy, exactly like the spelled-out place. The usual mutability rules
apply unchanged; the projection adds no new storage shape, only ergonomic
traversal of the index-pool idiom.

## Decided 2026-10-18 (update probe)

- **No `inout` callbacks.** A function with an `inout` parameter is not a
  value and lambdas cannot declare one, so the `update(g, id, fn(p))` shape
  is a copy-back transformer plus a writeback through the projection. Kept
  deliberately: the idiom is proven, and real `inout` captures would need
  escape analysis the tree does not have. Revisit on a real program that
  needs it, not on principle.
- **Index-assign and the mutating builtins refuse subscript calls.**
  `push(g.fr(0), v)` and `g.fr(0)[0] = v` die with the variable-or-field
  rule even though the projection is a place. This is parity with the
  reference compiler, kept by default: the owner states no preference for
  or against subscript targets here, so the normative behavior stands.
  Details in FRICTION 206.
