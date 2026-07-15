# Optimizing data structures for the arena model
> Status: evergreen analysis (current — reflects shipped behaviour).

Tycho's model — value semantics with no pointers, memory managed by implicit arenas — shapes
how you represent data, and the shapes it rewards are not always the ones you'd reach for in C.
It is a deliberate trade: you give up manual control (and some workloads) to get no
`malloc`/`free`, no use-after-free, no GC pauses, and value equality that just works. This note
maps the terrain honestly — where the model is already competitive, and where a workload wants a
data-oriented representation (a flat pool, an index, a scoped transient) to stay on the
value-semantics path. The numbers are measured, not asserted; and where a workload's natural
shape fights the model even after the right representation, the note says so and points you at a
different tool. The aim is to make the good representation the obvious one, not to apologize for
the model's edges.

## Where the model is competitive (for context)

Measured head-to-head against hand-written C and Go (`bench/`, same machine, same
checksum):

- **Value-shaped trees** — `bench/json`: parse a 50k-object document into a tagged value
  tree. tycho 37 MB vs C 35 MB vs Go 29 MB. Essentially **C's memory profile with zero
  manual management.** This is the model at its best: data that is naturally owned in one
  place, copied rarely, and freed all at once.
- **Flat data + libraries** — `bench/dbquery`, `bench/invindex`: tycho ≈ C, < Go on memory.

## Where the model wants a data-oriented representation

### 1. Pointer-shaped / structurally-shared structures (the big one)

Tries, graphs with shared nodes, DAGs, adjacency structures, anything where the natural C/Go
representation is *a node referenced by many parents via 8-byte pointers*.

Value semantics store children **by value**, not by reference. A `[int: Trie]` field holds
whole `Trie` structs inline; there are no pointers to share. So:

- every "edge" costs the size of a **node** (tens–hundreds of bytes), not a pointer (8 B);
- a node reachable from N parents is **copied N times** — there is no sharing.

Measured (`bench/trie`, a prefix tree, each node owning an `int -> child` map):

| | tycho | C | Go |
|---|--:|--:|--:|
| peak RSS | 58.7 MB | 37.8 MB | 33.8 MB |

~1.55× C — down from ~3.2× (119 MB) once the map moved to a **compact indexed-dict**
layout: an `int32` index table pointing at dense value entries sized to the live child
count (a 1-child node no longer carries three empty inline-struct slots), and no per-slot
insertion-order list. That roughly halved the trie and, at ~59 ms, brought its wall below
Go's (~66 ms). The residual gap below is value-vs-pointer storage and is structural — see
`bench/trie/RESULTS.md`.

**Idiom — a flat node pool with integer-index children.** Keep all nodes in one array and
make children indices into it, so an "edge" is an 8-byte `int`, sharing is just two indices
pointing at the same slot, and the whole pool is one arena-backed array:

```
struct TrieNode:
    kids: [int: int]           # next-byte -> node INDEX (not a Trie by value)
    word: bool

struct Trie:
    nodes: [TrieNode]          # the pool; node 0 is the root

fn insert(t: inout Trie, s: string):
    cur := 0
    i := 0
    for i in range(0, len(s)):
        c := s[i]
        if not (c in t.nodes[cur].kids):
            push(t.nodes, TrieNode([]int: int, false))
            t.nodes[cur].kids[c] = len(t.nodes) - 1
        cur = t.nodes[cur].kids[c]
    t.nodes[cur].word = true
```

Indices are pointer-sized, this allows sharing (DAGs: point two parents at one index), and it
stays within the model — the pool grows, the arena reclaims it, no `free`. This is the same
data-oriented layout high-performance C engines reach for *on purpose*: one contiguous array,
8-byte indices instead of scattered pointers, traversal that walks cache lines instead of chasing
them. So it is a representation worth choosing, not merely a constraint to satisfy — value
semantics just make it the default rather than the optimization you remember to apply. The cost
is ergonomic: you index a pool instead of following references, and you cannot delete a single
node without compacting. For graphs, the same pattern (`[Node]` + `[[int]]` adjacency by
index) is the idiomatic Tycho representation. Use it whenever the C/Go version would lean on
shared pointers.

This is measured, not asserted: `bench/dijkstra` runs single-source shortest paths on a
300k-node graph stored as an **adjacency list of indices** (`[[Edge]]`), and tycho lands at
**~1.3× C memory / ~1.2× wall, ≈ Go** — *competitive*. Expressed the value-semantic way
(indices, not pointers), the graph is value-shaped — the edge arrays are flat in every
language — so the residual gap is just a 24-byte array descriptor per node, not the
pointer-vs-struct blowup. The index idiom still edges out the by-value recursive form
(~1.3× vs the trie's ~1.55× C), but since the compact map layout roughly halved the
recursive trie the memory delta is now modest — the idiom's decisive payoff is expressing
*sharing* (DAGs, cycles) that by-value storage cannot represent at all.

`bench/lru` is a second index-pool case: a fixed-capacity LRU cache as a `[Node]` pool
(`prev`/`next` are `int` indices) plus an `[int: int]` map, the tail slot recycled on
eviction — the pointer-linked list a textbook LRU uses, expressed value-shaped. It lands at
**~2.8× C memory, ~2× C wall, and now ahead of Go on both** (32.6 MB / 303 ms vs Go's
21.4 MB / 336 ms), after the map costs it surfaced were fixed: an O(1) churn-bounded delete
(deletes were once O(map size) via a flat order vector, and delete-churn once held ~19× C
until rehash-to-purge was bounded), and most recently the **compact indexed-dict** layout,
which dropped it from ~40 MB by deleting the per-slot insertion-order list entirely. The
residual ~2.8× is the index table's open-addressing load-factor slack, the same header-cost
family as the trie, not a pointer blowup. See `bench/lru/RESULTS.md`.

Measured on the same 150k-word workload, the by-value recursive version is now **~59 MB**
(the compact indexed-dict layout roughly halved it). The flat-pool trie stays below that by
pooling the nodes and holding an 8-byte index per edge instead of an inline child struct; it
still does *not* reach C's 38 MB, because each node keeps an `[int: int]` child map whose
header + backing arrays dominate. The most compact tries drop the per-node map entirely for
**first-child / next-sibling links** (each node is a handful of `int`s: child index, sibling
index, key, flag), which gets within a small factor of C at the cost of linked-list child
traversal instead of a map lookup. The progression is the honest point: each step toward C's
memory trades away ergonomics, and you choose how far to walk it.

### 2. Long-lived scope holding transient garbage ("build-and-hold")

An arena reclaims at **scope exit**, not incrementally. A function that builds a large
transient (parse buffer, intermediate collection) and then keeps working holds that
transient until it returns — where a GC would reclaim it mid-run. In `bench/json` this is
the ~2.5 MB input string tycho/C hold to the end while Go's GC drops it (Go's lower peak).

**Idiom — scope the transient in an inner function/block** so its arena reclaims before the
long-lived work continues:

```
fn load() -> Doc:
    raw := read_huge_input()       # the transient
    return parse(raw)              # `raw`'s arena is reclaimed when load() returns
# caller never holds `raw`
```

If a transient is built and consumed in the same scope as long-lived results, split it out.

### 3. Maps / arrays of a large value type, many small instances

A composite map with a big value type over-allocates its backing array (empty slots cost
value-size each). This was tuned (initial capacity 8→4) and is bounded by load factor, but
the slot waste is still value-size × spare slots.

**Idioms:** `reserve(m, n)` / `reserve(a, n)` when you know the size up front (one
right-sized allocation, no rehash garbage); prefer a small value type in hot maps (store an
index or a small handle, not a big struct inline — same move as §1). Note the arena twist
measured in `bench/trie`: a *smaller* initial capacity can make peak **worse**, because the
arena can't reclaim the abandoned arrays from the extra rehashing. Size up front instead of
starting tiny.

### 4. In-place mutation of a large value passed around

Assigning or passing a large value can deep-copy it. This is correct (value semantics) but
costs a copy.

**Idiom:** thread large values through `inout` (inout) parameters so they are passed by
reference and mutated in place, instead of returning rebuilt copies. The compiler already
borrows (does not copy) `match`/`for` bindings that aren't mutated; lean on that.

## Decision guide

- Owned, value-shaped, copied-rarely data, freed in bulk → **the model fits; expect ≈ C.**
- Shared / pointer-shaped / cyclic structures → **flat pool + index children** (§1), or a
  different tool if you need fine-grained per-node lifetime.
- Streaming / long-lived process with heavy transients → **scope transients in inner
  functions** (§2) so arenas reclaim.
- Known-size or hot maps of large values → **`reserve`, or store handles not structs** (§3).

None of these makes Tycho a systems allocator's equal on its worst cases. They keep you on
the value-semantics path — no `malloc`/`free`, no UAF — for the workloads where that path is
viable, and tell you honestly when to step off it.

Is the trie/graph cost fundamental to value semantics, or just our implementation? See
`hylo-mvs-research.md` — the most mature MVS language (Hylo) hits the same wall and answers
it with the same indices-into-a-pool idiom, so it is fundamental, not a Tycho shortfall.

And it can't be sidestepped by quietly adding back references: a spike on stored/"remote"
references (`../rfc/limited-references-spike.md`) settled this as a decision. A reference that
*stores* a graph edge is incompatible with the two invariants that make Tycho what it is — the
deep-copy thread boundary (a stored pointer can't survive a call's copy-in) and the no-escape
arena model — so adding it would dismantle the value-semantics guarantees, not extend them. The
index-pool isn't the fallback after references didn't make it; within this model it *is* the way
to store a graph, and the copy-cost side of the same question is what [`sink`](../reference/basics.md)
answers for call arguments. The only reference-shaped feature that fits — user-defined
projections, a generalization of the shipped `&m[k]` — is an ergonomic convenience over the
index idiom, not a different way to store the data.
