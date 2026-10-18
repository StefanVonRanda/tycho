# Optimizing data structures for the arena model
> Status: evergreen analysis (current — reflects shipped behaviour).

| workload | cost vs C | idiom |
|---|---|---|
| value-shaped trees (`bench/json`: 37 MB vs C 35 MB) | ≈ C | none needed |
| flat data + libraries (`bench/dbquery`, `bench/invindex`) | ≈ C, < Go | none needed |
| pointer-shaped structures by value (`bench/trie`) | ~1.55× memory | flat node pool + index children (§1); `core:pool` when nodes are deleted |
| graphs as adjacency indices (`bench/dijkstra`) | ~1.3× memory, ~1.2× wall | same pool idiom |
| fixed-capacity LRU as index pool (`bench/lru`) | ~2.8× memory, ~2× wall, ahead of Go | same pool idiom |
| long-lived scope holding transients | arena/pool retains high-water storage | scope transients for reuse, not an RSS-drop guarantee (§2) |
| rebuilding a long-lived value per edit (`tools/tycho-ed/`) | one full copy per operation | mutate in place; hold hot text as `[int]` (§2) |
| read-only traversal with a reassigned subtree cursor | repeated copies of the remaining subtree | bounded read-only helper calls (§5) |

Tycho's model — value semantics with no pointers, memory managed by implicit arenas — shapes
how you represent data, and the shapes it rewards are not always the ones you'd reach for in C.
The intended trade: no manual `malloc`/`free` in ordinary Tycho code, independent
values and no collector pauses; the price is the table above. These are design
properties, not proof that the compiler/runtime has no memory-safety bugs or
that execution has no latency spikes. All figures measured on one machine (AMD Ryzen 7 7735HS,
Debian x86-64); where a workload's shape fights the model even after the right representation,
the note says so and names a different tool.

**If you read one section, read §2.** §1 is the better introduction to *why* the model
behaves as it does, but §2 — rebuilding a long-lived value on every edit pays a full copy each
time — is the limit ordinary code with nothing graph-shaped in it is most likely to hit.

## Where the model is competitive (for context)

Measured head-to-head against hand-written C and Go (`bench/`, same checksum):

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

```tycho
struct TrieNode:
    kids: [int: int]           # next-byte -> node INDEX (not a Trie by value)
    word: bool

struct Trie:
    nodes: [TrieNode]          # the pool; node 0 is the root

fn insert(t: inout Trie, s: string):
    cur := 0
    for i := 0; i < len(s); i += 1:
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
node without compacting — the packaged answer to that is `core:pool`, below.

**Stale-index protection — generational indices (`core:pool`).** The raw pool's one hazard
is deletion: removing a node, reusing its slot, and later dereferencing the old index
silently aliases the new occupant (the ABA problem). The fix — the same design standardized
across the Rust ecosystem (`slotmap`, `generational-arena`) and every ECS entity store — is
a **generation counter per slot**: an index is `(slot, generation)`, the generation is
bumped whenever the slot is reused, and a stale generation is rejected at lookup instead of
aliased. Cost is one word per slot plus one compare per dereference. Tycho packages this as
`core:pool` (`corelib/pool/pool.ty`): a `Handle` newtype packs `(generation << 24) | slot`
so a handle is type-distinct from a raw index, `remove` bumps the slot's generation, and
`get`/`set`/`remove` die on a stale handle rather than silently reading the new occupant.
Handles are **pool-relative coordinates**, not identity tokens for a particular
pool. A handle from pool P can pass pool Q's check and select an unrelated value
if its slot and generation match. Generation checks reject stale use within the
intended pool; they do not detect mixing pools. Keep each handle associated with
its intended pool at API boundaries.

Use the raw `[Node]` pool when it only grows; reach for `core:pool` the moment deletion
enters. For graphs, the same pattern (`[Node]` + `[[int]]` adjacency by
index) is the idiomatic Tycho representation. Use it whenever the C/Go version would lean on
shared pointers.

Measured on `bench/dijkstra`, which runs single-source shortest paths on a
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
traversal instead of a map lookup. The progression is the point: each step toward C's
memory trades away ergonomics, and you choose how far to walk it.

### 2. Long-lived scope holding transient garbage ("build-and-hold")

An arena releases its ownership at **scope exit**, not necessarily its memory to
the OS — and eligible dead buffers can be reused incrementally. At supported
reassignment sites the compiler checks that a buffer is dead and uniquely owned,
then hands it back to
the arena via `arena_recycle` (`runtime/tycho_rt.c@arena_recycle`, emitted by the compiler at
`src/tychoc.c:13544@arena_recycle`); the next same-or-smaller allocation in that arena reuses it instead
of bumping — the reuse analysis Perceus derives from runtime refcounts, derived here from
static value semantics. Supported churn patterns can reuse buffers rather than
retain every intermediate; this is not a general bound for all transient churn.
On scope exit, arena blocks move into a thread-local reuse pool and may remain
resident. A large touched transient can therefore leave RSS high after its helper
returns. In `bench/json`, the ~2.5 MB input string tycho/C hold to the end while
Go's GC drops it contributes to Go's lower peak.

**Idiom — scope the transient in an inner function/block** so its arena storage
becomes available for reuse before the long-lived work continues. This shortens
ownership lifetime; it does not promise an immediate OS release or lower RSS:

```text
fn load() -> Doc:
    raw := read_huge_input()       # the transient
    return parse(raw)              # `raw`'s arena is reclaimed when load() returns
# caller never holds `raw`
```

If a transient is built and consumed in the same scope as long-lived results, split it out.

#### Rebuilding a long-lived value on every operation

Recycling is broader than the paragraph above suggests, and it is worth knowing
exactly how far it reaches before concluding that a workload is doomed. There are
several supported paths:

- **Loop-carried reassignment of a named array local** — `a = f(a)` inside a loop
  (`src/tychoc.c:13536@do_recycle`). Narrow by design: it requires a *named
  variable* (not an element or field store), a type for which `is_array` holds
  (`src/tychoc.c:1282@is_array` — note a `string` is **not** an array here), a
  loop, a non-`inout` non-accumulator target, and at least two reads in the
  function (the soundness condition — a read-once variable may have been moved
  out from under it).
- **Heap-bearing array-element overwrite** — `xs[i] = v` copies the new element
  into the array's owner before recursively recycling the evicted value. This
  covers strings and nested arrays, maps, structs, enums, tuples, options/results,
  struct-of-arrays values and closure environments; owner checks keep literals and
  cross-arena values safe. The string leaf uses
  `runtime/tycho_rt.c@tycho_str_recycle`, and the sliding-window ring-buffer case
  remains bounded.
- **Composite-map value overwrite or deletion** — `m[k] = v` likewise copies the
  replacement first, then releases the old value's nested owned graph. `delete`
  releases both the removed composite key and value. Both compiler paths emit
  typed recyclers for arrays, maps, structs, enums, tuples, options/results,
  struct-of-arrays and captured closures; leaf allocations are recycled only by
  their owning arena. `tests/map_overwrite_scale.sh` bounds repeated string,
  nested-array, struct, enum and closure replacements under both compilers.

**So the bytes are usually reused. The cost that survives is the work.** Rebuilding
a value on every operation pays an allocation, a deep copy of the whole value, and
the recycle bookkeeping *per operation*, regardless of whether the old bytes come
back. Where the value is a whole line, a whole document, or a whole record, that
per-operation copy is the dominant term and it scales with the value's size, not
with the size of the edit.

The worked case is `tools/tycho-ed/`, measured in both directions in
[the thesis §4c](../thesis.md). A text editor whose line buffer does

```text
buf.lines[ln] = s[0:c] + t + s[c:len(s)]      # a whole new line, per keystroke
```

degrades to ~7× its starting cost per edit by a million edits. Holding the edited
line in a mutable `[int]` gap buffer instead — a keystroke writes one byte into a
hole and rebuilds the string once per *focus change* rather than per keystroke —
flattens the curve completely. **Peak RSS is identical between the two backends**
(95.7 MB), which is the measurement that says the retained memory was never the
line churn: it is the undo journal, one entry per keystroke, which is the feature
working as intended. Two costs that rose together turned out to be one cost and
one coincidence, and only building the fix separated them.

**Idiom — mutate, don't rebuild, whenever a value is edited many times.** Hold
hot mutable text as `[int]` rather than `string` and convert at
the boundary; mutate through `inout` rather than returning a rebuilt copy (§4);
and where a loop must rebuild, rebuild a *named array local* so the first recycle
path above can fire. The test is not "will the memory come back" — usually it
will — but "am I copying the whole value to change a small part of it".

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

Assigning or passing a large value can deep-copy it. Read-only parameters can
borrow, but a mutable by-value heap-bearing struct parameter can require a deep
copy without a copy warning. Copy warnings cover selected source patterns, not
every emitted copy; silence does not mean an operation is allocation-free.

**Idiom:** thread large values through `inout` parameters so they are passed by
reference and mutated in place, instead of returning rebuilt copies. The compiler already
borrows (does not copy) `match`/`for` bindings that aren't mutated; lean on that.
Named `string`/`bytes` append bindings also retain capacity through `inout` helper
calls, while projections without persistent metadata adopt storage afresh per
call. See the [storage-reuse plan](plan-value-storage-places.md). Other edits,
such as inserting in the middle of a string, can still rebuild the whole value.

### 5. Read-only traversal with an owned cursor

A cursor such as `cur := tree`, followed by `cur = child`, is an independent
value, not a reference. Rebinding it can copy the remaining subtree on every
step; collecting whole subtrees in a work array can also copy them. For a
read-only walk, passing children to a read-only helper can use the existing
parameter borrowing instead, without changing value semantics. Keep recursion
bounded: the compiler's long-addition checker uses this pattern in
`compiler/types/tcheck.ty@_int_add_depth`, retaining its 2000-operator guard.
Returning or retaining a subtree still requires the normal ownership rules.

## Decision guide

- Owned, value-shaped, copied-rarely data, freed in bulk → **the model fits; expect ≈ C.**
- Shared / pointer-shaped / cyclic structures → **flat pool + index children** (§1);
  `core:pool` (the packaged generational pool) when nodes must be deleted, not just appended.
- Streaming / long-lived process with heavy transients → **scope transients in inner
  functions** (§2) so arenas reclaim.
- A long-lived mutable value edited many times (editor buffer, session cache, server
  state) → **mutate it in place; do not rebuild it per operation** (§2). The arena usually
  recycles the old bytes; what it cannot give back is the full copy you paid to make them.
- Known-size or hot maps of large values → **`reserve`, or store handles not structs** (§3).

None of these makes Tycho a systems allocator's equal on its worst cases. They keep you on
the value-semantics path — no manual `malloc`/`free` in ordinary code — where it is
viable, and tell you when to step off it.

Is the trie/graph cost fundamental to value semantics, or just this implementation?
The most mature mutable-value-semantics language (Hylo) hits the same wall and answers
it with the same indices-into-a-pool idiom, so it is fundamental, not a Tycho shortfall.

And it can't be sidestepped by quietly adding back references: a spike on stored
references ([limited-references spike](../rfc/limited-references-spike.md)) settled this as a decision. A reference that
*stores* a graph edge is incompatible with the two invariants that make Tycho what it is — the
deep-copy thread boundary (a stored pointer can't survive a call's copy-in) and the no-escape
arena model — so adding it would dismantle the value-semantics guarantees, not extend them. The
index-pool isn't the fallback after references didn't make it; within this model it *is* the way
to store a graph, and the copy-cost side of the same question is what [`sink`](../reference/basics.md)
answers for call arguments. The only reference-shaped feature that fits — user-defined
projections, a generalization of the shipped `&m[k]` — is an ergonomic convenience over the
index idiom, not a different way to store the data.
