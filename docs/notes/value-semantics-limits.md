# Where value semantics + implicit arenas lose — and what to do instead

Tycho's model — value semantics with no pointers, memory managed by implicit arenas — is
not free. It is a deliberate trade: you give up manual control (and some workloads) to get
no `malloc`/`free`, no use-after-free, no GC pauses, and value equality that just works.
This note is the honest accounting of where that trade goes against you, with the idiom to
reach for in each case. It is not marketing; if your workload is in the "loses" column,
use the idiom or use a different tool.

## Where the model is competitive (for context)

Measured head-to-head against hand-written C and Go (`bench/`, same machine, same
checksum):

- **Value-shaped trees** — `bench/json`: parse a 50k-object document into a tagged value
  tree. tycho 37 MB vs C 35 MB vs Go 29 MB. Essentially **C's memory profile with zero
  manual management.** This is the model at its best: data that is naturally owned in one
  place, copied rarely, and freed all at once.
- **Flat data + libraries** — `bench/dbquery`, `bench/invindex`: tycho ≈ C, < Go on memory.

## Where the model loses

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
| peak RSS | 103 MB | 38 MB | 34 MB |

~2.7× C, the opposite of the JSON result. (The composite-map initial capacity was already
tuned 8→4 for this — see `bench/trie/RESULTS.md`; the gap below is what remains and is
structural.)

**Idiom — a flat node pool with integer-index children.** Keep all nodes in one array and
make children indices into it, so an "edge" is an 8-byte `int`, sharing is just two indices
pointing at the same slot, and the whole pool is one arena-backed array:

```
struct TrieNode:
    kids: [int: int]           # next-byte -> node INDEX (not a Trie by value)
    word: bool

struct Trie:
    nodes: [TrieNode]          # the pool; node 0 is the root

fn insert(t: mut Trie, s: string):
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
stays within the model — the pool grows, the arena reclaims it, no `free`. The cost is
ergonomic: you index a pool instead of following references, and you cannot delete a single
node without compacting. For graphs, the same pattern (`[Node]` + `[[int]]` adjacency by
index) is the idiomatic Tycho representation. Use it whenever the C/Go version would lean on
shared pointers.

Measured on the same 150k-word workload, the flat-pool trie above lands at **69 MB** — down
from 103 MB for the by-value recursive version (−33%), correct on both compilers. It does
*not* reach C's 38 MB, because each node still owns an `[int: int]` child map and that
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

**Idiom:** thread large values through `mut` (inout) parameters so they are passed by
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
