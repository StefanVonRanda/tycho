# Subscripts (user-defined projections)

> **Memory:** a subscript projects into a value you already have. Nothing is stored and nothing outlives its scope.

A `subscript` names a reusable read/write view into part of a value. It does not
*return* a value — it `yield`s a **place**, something you can read or assign
through (a variable, a field, an index):

```tycho
struct Node:
    weight: int
struct Graph:
    nodes: [Node]

fn bump(n: inout int):
    n = n + 1

subscript edge(g: Graph, i: int) -> inout Node:
    yield &g.nodes[i]

fn main():
    g := Graph([Node(1), Node(2)])
    g.edge(1).weight = 10        # write in place through the projection — no copy
    _w := g.edge(0).weight        # read through it
    bump(&g.edge(0).weight)      # a field of the projection as an `inout` argument
```

`g.edge(i)` is not a function call. At compile time it is replaced by the yielded place with
the arguments substituted (`g.nodes[i]`), and the surrounding read or write uses that place
directly. There is **no runtime object** — a subscript behaves exactly like the built-in
`&m[k]`.

Array-typed projections have one restriction that struct-typed ones do not:
`push` through the projection and element index-assign through it are refused
(`push's first argument must be an array variable or field`; `can only
index-assign an array or map variable or field`). Only the built-in array and
map places grow in place or assign by index. To grow through a projection,
pass `&g.items(i)` as an `inout` argument to a helper that pushes; to replace
a whole projected value, assign the place directly (`g.item(i) = v`), which
deep-copies into the owner's arena.

## Form

```text
subscript <name>(<recv>: T, <params>...) -> inout U:
    yield &<place>
```

- The body is a single `yield &<place>`, where `<place>` is a field/index spine
  (`g.nodes[i]`, `r.cells[i]`, `p.tags[0].x`, …).
- `-> inout U` is the type of the projected place; it must match the yielded place's type.
- Called as a method on the first parameter (the receiver): `recv.name(args)`. The
  receiver's type selects the subscript.

## Rules (checked at compile time)

- **Rooted in a parameter.** The yielded place must be rooted in one of the subscript's
  parameters — it projects *into* an argument, so it cannot dangle. A place rooted in a
  fresh local is rejected.
- **Each parameter used at most once** in the yielded place, so an argument is never
  evaluated twice when substituted. (Using one argument twice, with the repeated part
  hoisted, is a possible future extension.) The receiver counts: `yield &g.cells[r * g.w + c]`
  reads two fields of `g` and is refused, so 2-D indexing into a *flat* array is not
  expressible this way. Store the rows nested and yield `&g.rows[r][c]`, which mentions
  the receiver once.
- **Concrete receiver.** The first parameter's type must be a concrete type:
  `subscript at(p: Pool($T), …)` is rejected at the declaration, whether or not it is ever
  called. The receiver selects the subscript by exact type, so a generic receiver could
  never match the concrete type a call site presents. Declare it on the concrete
  instance (`subscript at(p: Pool(int), i: int) -> inout int`), or expose a generic
  **function** instead.
- **Value semantics is unchanged.** A projection is a place into a value you already own or
  borrow; the usual mutability rules apply (writing through a projection into a by-value
  parameter mutates that parameter's private copy, exactly like `g.nodes[i].w = …` spelled
  out). A projection is scoped and transient — it is never stored in a field and never sent
  to another task. That is what keeps it compatible with value semantics.

## What it is not

Subscripts do **not** let you store a graph by reference. They make *traversing* an
index-pool structure ergonomic (`g.edge(i).w = …` instead of `g.nodes[i].w = …`, and reusable
accessors over composite storage); they are convenience over the index-pool idiom, not a new
storage shape.

---

How this is implemented — compile-time substitution and why each rule exists — is in
[design-subscripts](../internals/design-subscripts.md), for maintainers.
