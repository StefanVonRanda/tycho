# Subscripts (user-defined projections)

> **Thesis context:** A subscript is the *one* limited-reference construct that fits the
> arena + deep-copy model (see the [limited-references spike](../rfc/limited-references-spike.md)).
> It generalizes the built-in `&m[k]` place: a **scoped, transient projection** into part of
> a value — never a stored reference, never crossing the deep-copy thread boundary, needing
> no lifetime analysis. It is the lvalue/place machinery Tycho already has, exposed to user
> code.

A `subscript` names a reusable, zero-copy **place** into one of its arguments. It does not
*return* a value — it `yield`s a projection:

```
struct Node:
    weight: int
struct Graph:
    nodes: [Node]

subscript edge(g: Graph, i: int) -> inout Node:
    yield &g.nodes[i]

fn main():
    g := Graph([Node(1), Node(2)])
    g.edge(1).weight = 10        # write in place through the projection — no copy
    w := g.edge(0).weight        # read through it
    bump(&g.edge(0).weight)      # a field of the projection as an `inout` argument
```

`g.edge(i)` is not a function call. At compile time it is replaced by the yielded place with
the arguments substituted (`g.nodes[i]`), then the surrounding read or write flows through
the ordinary place machinery. There is **no runtime object** — a subscript is a compile-time
place-macro, the exact analog of the built-in `&m[k]`.

## Form

```
subscript <name>(<recv>: T, <params>...) -> inout U:
    yield &<place>
```

- The body is a single `yield &<place>`, where `<place>` is a field/index spine
  (`g.nodes[i]`, `r.cells[i]`, `p.tags[0].x`, …).
- `-> inout U` is the type of the projected place; it must match the yielded place's type.
- Called as a method on the first parameter: `recv.name(args)`. The receiver's type selects
  the subscript.

## Rules (checked at compile time)

- **Rooted in a parameter.** The yielded place must be rooted in one of the subscript's
  parameters — it projects *into* an argument, so it cannot dangle. A place rooted in a
  fresh local is rejected.
- **Each parameter used at most once** in the yielded place, so an argument is never
  double-evaluated when substituted. (Multi-use with argument hoisting is a future extension.)
- **Value semantics is unchanged.** A projection is a place into a value you already own or
  borrow; the usual mutability rules apply (writing through a projection into a by-value
  parameter mutates that parameter's private copy, exactly like `g.nodes[i].w = …` spelled
  out). A projection is scoped and transient — it is never stored in a field and never sent
  across a `spawn`/channel boundary. That is what keeps it compatible with the arena model.

## What it is not

Subscripts do **not** let you store a graph by reference. They make *traversing* an
index-pool structure ergonomic (`g.edge(i).w = …` instead of `g.nodes[i].w = …`, and reusable
accessors over composite storage); they are convenience over the index-pool idiom, not a new
storage shape. Storing a reference (a "remote part") is a decided non-goal — it has no sound
deep-copy at the thread boundary. See the [limited-references spike](../rfc/limited-references-spike.md).
