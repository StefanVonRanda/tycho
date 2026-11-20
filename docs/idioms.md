# Thinking in Tycho

Tycho has no references, no stored borrows, and no interface values. This page
is what to reach for instead — one idiom per shape, each the recommended way
rather than a workaround, and usually also the faster spelling.

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
engines choose on purpose. Measured: index adjacency on the dijkstra graph
peaks at 0.99× C's memory, and the shape expresses sharing — DAGs, cycles —
that by-value storage cannot represent at all. When nodes must be *deleted*, reach for `core:pool`, the
packaged generational pool: per-slot generations reject stale indices instead
of silently aliasing a reused slot. The full figures are in
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).

## Pools with subscripts: name the slot, type the id

Three helpers take the noise out of an index pool. `arrays.push_index` appends
and hands back the new slot's index. A `subscript` names the slot, so
`g.node(i).weight = 10` replaces `g.nodes[i].weight = 10`. `arrays.OptIndex`
is a "maybe index" stored as one int (`-1` is none), with no `Option` tag.

```tycho
package main
import "core:arrays"

struct Node:
    weight: int
    parent: arrays.OptIndex
struct Graph:
    nodes: [Node]

subscript node(g: Graph, i: int) -> inout Node:
    yield &g.nodes[i]

fn main():
    g := Graph([]Node)
    root := arrays.push_index(&g.nodes, Node(1, arrays.no_index()))
    kid := arrays.push_index(&g.nodes, Node(2, arrays.some_index(root)))
    g.node(kid).weight = 10
    println(str(g.node(kid).weight) + " parent=" + str(arrays.get_or(g.node(kid).parent, -1)))
    println("root has parent: " + str(not arrays.is_none(g.node(root).parent)))
```

```output
10 parent=0
root has parent: false
```

A `subscript` is a zero-copy view — usable for reads, writes and `inout` — that
cannot be stored or carried past its statement, which is what keeps it sound:
it reads like a pointer and stores like an index. See
[subscripts](reference/subscripts.md).

When slots are deleted, `core:pool`'s typed ids carry the element type:
`pool.add_typed` returns an `Id(Node)`, and passing it to a `Pool(Edge)` is a
compile error rather than a silent read of the wrong slot.

## Observers: send messages, don't hold pointers

When one value must react to another, don't store a pointer back at the
subject — send the event over a channel to a task that owns the observer
state. The edge is a message, not an alias, so nothing can dangle and nothing
races.

```tycho
fn watcher(events: Channel(string)) -> int:
    n := 0
    for true:
        match recv(events):
            Some(e):
                println("saw " + e)
                n = n + 1
            None:                   # closed and drained
                break
    return n

fn main():
    events := channel(string, 16)
    t := spawn watcher(events)
    send(events, "open")
    send(events, "close")
    close(events)
    println(str(wait(t)))   # 2
```

```output
saw open
saw close
2
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

## Flat state: intern names, slice children by offset

When one owned state holds many records with repeated strings and child lists,
give the root one text blob and one array per record kind. Store a name as
`(offset, length)` into the blob, and a child list as `(offset, count)` into a
shared array; records become fixed-size and pointer-free.

```tycho
package main

import "core:buffer"

packed struct NameRef:
    off: i32
    len: i32

struct Album:
    title: NameRef
    first: int
    count: int

struct Library:
    text: bytes
    names: [NameRef]
    albums: [Album]
    tracks: [NameRef]

fn intern(text: inout buffer.Buffer, seen: inout [string: NameRef], s: string) -> NameRef:
    if s in seen:
        return seen[s]
    r := NameRef(to_i32(len(text.data)), to_i32(len(s)))
    buffer.write(&text, s)
    seen[s] = r
    return r

fn name(text: bytes, r: NameRef) -> string:
    o := to_int(r.off)
    return to_str(text[o:o + to_int(r.len)])

fn main():
    text := buffer.new()
    seen := []string: NameRef
    lib := Library(to_bytes([]int), [], [], [])
    so_what := intern(&text, &seen, "So What")
    freddie := intern(&text, &seen, "Freddie Freeloader")
    kind := intern(&text, &seen, "Kind of Blue")
    push(lib.names, so_what)
    push(lib.names, freddie)
    push(lib.names, kind)
    push(lib.tracks, so_what)
    push(lib.tracks, freddie)
    push(lib.albums, Album(kind, 0, 2))
    lib.text = buffer.as_bytes(text)
    a := lib.albums[0]
    println(name(lib.text, a.title) + ": " + name(lib.text, lib.tracks[a.first]) +
            ", " + name(lib.text, lib.tracks[a.first + 1]) + " (names=" + str(len(seen)) + ")")
```

```output
Kind of Blue: So What, Freddie Freeloader (names=3)
```

Why this way: one array per kind means the root is the only thing holding a
data pointer; elements are flat records of integers, so a copy copies bytes
and a save can visit records in order. Repeated names cost one blob and one
offset per use instead of a string header per record. Measured on
[`bench/flat-state`](../bench/flat-state/RESULTS.md) (11,673 albums, 186,768
tracks, nine interleaved runs): 41 vs 113 MiB peak RSS, 3.31 vs 4.43 MB on
disk, save 7.7 vs 25.9 ms, load 3.7 vs 12.0 ms, teardown 0.9 vs 18.5 ms.
`to_bytes(xs)` / `from_bytes$([T])(b)` move a whole `[packed T]` in one copy,
which is what keeps save and load from visiting every record. The cost
is build: 10.4 vs 7.2 ms, because interning hashes every name. Query is parity.
Deletion needs a policy — tombstone the pool slot and leave the child range, or
compact both arrays and rewrite offsets.

## Transients: scope the work, free the proof

When a long-lived scope builds throwaway values — parsing a document, loading
an input — do that work in an inner function so its arena frees on return.
The caller keeps the result; the transient never outlives the call.

Why this way: arenas reclaim at scope exit, so a scope that holds its
transients holds its high-water mark. Splitting the load from the loop turns
bulk-free into per-item free with no code beyond a function boundary. See
[from `malloc` to arenas](from-c-to-arenas.md) and the
[limits note](internals/value-semantics-limits.md).

## Updates: transform, then write back

A callback cannot take `inout`: a function with an `inout` parameter is not
a value, and a lambda cannot declare one. So an update passed into a helper
is a pure transformer — it receives a copy, returns the new value, and the
caller writes it back through a `subscript` place. Confinement is preserved
(the callback never touches the aggregate's memory), and the writeback is a
single checked line.

```tycho
struct Person:
    name: string
    age: int
struct Club:
    members: [Person]

subscript member(c: Club, i: int) -> inout Person:
    yield &c.members[i]

fn birthday(p: Person) -> Person:
    p.age = p.age + 1
    return p

fn update(c: inout Club, id: int, f: fn(Person) -> Person) -> bool:
    if id < 0 or id >= len(c.members):
        return false
    c.member(id) = f(c.member(id))
    return true

fn main():
    c := Club([Person("ann", 30)])
    update(&c, 0, birthday)
    println(c.member(0).name + " is " + str(c.member(0).age))   # ann is 31
```

Why this way: the borrow shape (`fn(p: inout Person)`) does not exist, so
the copy boundary is explicit instead of implicit — the transformer cannot
leak access, and the writeback goes through the same place reads use.
