# Thinking in Tycho

Tycho has no references, no stored borrows, and no interface values. This page
is what to reach for instead — one idiom per shape, each the recommended way
rather than a workaround. Every idiom below is also the faster and safer
spelling: the performance note with each one says by how much.

## Graphs: hold nodes in a pool, link by index

When nodes point at each other, hold every node in one array and make an edge
an integer index into it. Sharing is two indices naming one slot; cycles are
index cycles, which value semantics permits.

```tycho
struct Node:
    kids: [int: int]           # next byte -> node INDEX, not a Node by value
    word: bool

fn insert(pool: inout [Node], s: string):
    cur := 0
    for i := 0; i < len(s); i += 1:
        c := s[i]
        if not (c in pool[cur].kids):
            push(pool, Node([]int: int, false))
            pool[cur].kids[c] = len(pool) - 1
        cur = pool[cur].kids[c]
    pool[cur].word = true

fn main():
    pool := [Node([]int: int, false)]
    insert(&pool, "ab")
    insert(&pool, "ac")
    println(str(len(pool)) + " nodes, root fanout " + str(len(pool[0].kids)))
```

```output
4 nodes, root fanout 1
```

Why this way: one contiguous array, 8-byte indices instead of scattered
pointers, traversal that walks cache lines. The same layout data-oriented C
engines choose on purpose — value semantics make it the default. Measured: a
300k-node graph as index adjacency is ~1.3× C's memory (vs ~1.55× by value),
and the shape expresses sharing — DAGs, cycles — that by-value storage cannot
represent at all. When nodes must be *deleted*, reach for `core:pool`, the
packaged generational pool: per-slot generations reject stale indices instead
of silently aliasing a reused slot. The full figures are in
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).

## Observers: send messages, don't hold pointers

When one value must react to another, don't store a pointer back at the
subject — send the event over a channel to a task that owns the observer
state. The edge is a message, not an alias, so nothing can dangle and nothing
races.

```tycho
fn watcher(events: chan string):
    for:
        select:
            recv(events, e):
                println("saw " + e)
            closed:
                break

fn main():
    events := chan string(16)
    t := spawn watcher(events)
    send(events, "open")
    send(events, "close")
    close(events)
    wait(t)
```

Why this way: each task owns its arena and every crossing value is a copy, so
separate tasks cannot race on Tycho values — no locks, no Sendable machinery.
See [concurrency](reference/concurrency.md).

## Heterogeneity: close the set with an enum

When several shapes flow through one place and the set is known, make it a
closed `enum` and take it apart with exhaustive `match`. No vtable, no
indirection — and the compiler checks that every variant is handled.

```tycho
enum Shape:
    Circle(float)
    Rect(float, float)

fn area(s: Shape) -> float:
    return match s:
        Circle(r): 3.14159 * r * r
        Rect(w, h): w * h

fn main():
    println(str(area(Circle(2.0))))
```

Why this way: a generic body already covers the static half (one function over
many types, monomorphized per instantiation, no indirection). The enum covers
the mixed half wherever the variants are known. Only the *open* set —
plugins, unknown implementors — stays outside the language, because an open
set needs a stored reference by definition. See
[enums](reference/enums-options.md) and [generics](reference/generics.md).

## Long-lived state: mutate, don't rebuild

When a value outlives every scope that would reclaim it — an editor buffer, a
session cache, server state — change it in place rather than rebuilding it per
operation. Rebuilding pays a full copy per edit; a mutation pays for the edit.

```tycho
fn bump(hits: inout [string: int], k: string):
    hits[k] += 1

fn main():
    hits := []string: int
    bump(&hits, "open")
    bump(&hits, "open")
    println(str(hits["open"]))   # 2
```

Why this way: `inout` is an exclusive, call-scoped borrow — equivalent to
`x = f(x)`, checked by the compiler — so the callee genuinely shares the
caller's aggregate across frames. Measured: a memoized `fib(40)` collapses
from 0.60s to under a millisecond, which is the proof the table is shared,
not copied per call. For hot text, hold the buffer as `[int]` and convert at
the boundary; a keystroke then writes bytes instead of copying a line. The
worked editor case is in [thesis §4c](thesis.md). See
[maps](reference/maps.md) (`m[k] += 1`, `push(m[k], v)` update in place).

## Transients: scope the work, free the proof

When a long-lived scope builds throwaway values — parsing a document, loading
an input — do that work in an inner function so its arena frees on return.
The caller keeps the result; the transient never outlives the call.

Why this way: arenas reclaim at scope exit, so a scope that holds its
transients holds its high-water mark. Splitting the load from the loop turns
bulk-free into per-item free with no code beyond a function boundary. See
[from `malloc` to arenas](from-c-to-arenas.md) and the
[limits note](internals/value-semantics-limits.md).

## Views: name a projection with `subscript`

When pool indexing gets noisy, name the projection. A `subscript` is a
zero-copy view into a value you already hold — usable for reads, writes, and
`inout` — that cannot be stored or carried past its statement, which is what
keeps it sound.

```tycho
struct Node:
    weight: int
struct Graph:
    nodes: [Node]

subscript edge(g: Graph, i: int) -> inout Node:
    yield &g.nodes[i]

fn main():
    g := Graph([Node(1), Node(2)])
    g.edge(1).weight = 10
    println(str(g.edge(1).weight))   # 10
```

Why this way: it reads like a pointer and stores like an index — the
ergonomics of references over the representation that fits the model. See
[subscripts](reference/subscripts.md).
