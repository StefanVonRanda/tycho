# Hier aggregates: arrays & structs under value semantics

Status: **implemented** (this began as a design pressure-test; arrays,
structs, heap-bearing/nested struct fields, structural equality, and `inout`
all ship as described — see the [README](../README.md) and
[thesis.md](thesis.md)). It answered one question: *do implicit hierarchical
arenas still hold once the language has arrays and structs, given strict value
semantics and no pointer type?* Conclusion: **yes**, subject to five
invariants and three honest costs, both listed below — and the costs are now
largely sealed by two compiler optimizations (return-slot move, in-place
append) documented in [thesis.md](thesis.md) §4.

## 1. The model in one paragraph

Every value is a **wholly-owned tree**: a struct owns its fields, an array
owns its elements, recursively down to leaves (`int`, `bool`, `string`
bytes). A value and *all* its interior storage live in exactly **one
arena** — the arena of the variable (slot) that owns the root. There is
**no pointer/reference type**: you cannot name, store, or return a
reference to a value's interior. Every operation that would create a
second name for the same storage instead **copies** (deep, by default).
Mutation happens in place through `inout` parameters, which are an
exclusive borrow for the duration of a call and cannot be stored.

## 2. Types & syntax

```
# arrays — homogeneous, growable
xs := [1, 2, 3]          # [int] inferred
ys := []string           # empty; element type required when empty
xs[0]                     # index read
xs[0] = 9                 # index write (in place)
len(xs)                   # length
push(xs, 4)               # append in place  (xs is inout)
pop(xs)                    # remove last, returns it

# structs — nominal, fields are values
struct Point:
    x: int
    y: int

p := Point{ x: 1, y: 2 }
p.x                        # field read
p.y = 5                    # field write (in place)

# nesting is just trees of values
struct Line:
    a: Point
    b: Point
labels: [string]           # a struct field that is an array, etc.
```

No `null`. "Maybe a value" needs an optional/sum type (see §7); that is a
companion feature, not part of this spec, but the model assumes it exists.

## 3. The five invariants that make it sound

1. **Single-arena ownership.** A value's root and every byte it
   transitively owns are allocated in one arena — the owner slot's arena.
   `acc.items`'s backing buffer lives wherever `acc` lives.
2. **No aliasing.** `b := a`, passing by value, and `field = v` all
   *copy*. After `b := a`, `a` and `b` share nothing.
3. **Mutation via `inout` only.** `fn f(inout xs: [int])` borrows the
   caller's value exclusively for the call and writes through it. The
   value never changes arena. The borrow is *not a value* — it can't be
   assigned, stored in a field, or returned.
4. **No reference type.** There is nothing whose value is "the address of
   another value." This is the keystone: it is *why* invariant 3's borrow
   can't escape, and why no operation can manufacture a lasting second
   reference.
5. **Cross-arena moves are deep copies.** Returning a value, or assigning
   to a variable in an outer scope, copies the whole tree into the
   destination arena. Correctness never depends on analysis — only speed
   does (see §5).

## 4. Arena mechanics, worked

```
fn read_lines() -> [string]:
    lines := []string
    line := input()
    for line != "":
        push(lines, line)     # 'line' (scratch) is COPIED into lines' buffer
        line = input()
    return lines              # whole array deep-copied into caller's arena

fn main():
    ls := read_lines()        # ls owns its buffer + string bytes, in main's arena
```

- `push` grows `lines`' backing buffer **in `lines`' arena** (geometric
  growth; old buffers are wasted within the arena — bounded, reclaimed
  when the arena ends).
- `line` lives in the loop's scratch arena, reset each iteration. `push`
  copies its bytes into `lines`' buffer, so resetting scratch is safe.
- `return lines` deep-copies the array into the caller's arena, then
  `read_lines`' arena is freed. The returned value is fully independent.

```
fn main():
    acc := []string                 # in main's arena
    for i in range(1_000_000):
        push(acc, str(i))           # str(i) temp in scratch; copied into acc
    # scratch stays bounded; acc grows O(n) — intended, not a leak
```

## 5. The three honest costs

1. **Deep copy on cross-arena move.** Returning or out-assigning a big
   value is O(size). Mitigated, never for correctness, by:
   - *Build-in-destination*: if a local is returned, the compiler
     allocates it in the parent arena from the start → the return is free.
     (We already do the leaf version: a return expression targets the
     parent arena.) Conservative when a value *may* be returned on some
     path; still sound, still local.
   - *Move on last use* (✅ done): `b := a` / `b = a` where `a` is a
     uniquely-owned local read exactly once (so this is its last use on every
     path), not inside a loop, and in the same arena as the destination, hands
     off `a`'s buffer instead of deep-copying it. Conservative and static; a
     parameter (which borrows the caller's buffer) is never moved.
   - *Borrow on read*: a by-value parameter the callee only reads is
     passed as a transient borrow (zero copy) — safe because the borrow
     can't be stored (invariant 4) and the caller is suspended.
   The important part: in this model escape analysis is an **optimization**.
   In a pointer-having language the same analysis is a **soundness
   requirement**. That is the whole reason value semantics has legs here.

2. **No recursive types.** `struct Node: next: Node` is infinite-size and
   illegal — there is no pointer to break the cycle. Trees, lists, and
   graphs are expressed as **arrays + integer indices** (a node holds an
   `int` index into a `[Node]`, not a reference). The entire graph is then
   *one value* (the array) with *one* lifetime — which is an excellent fit
   for arenas, and a well-trodden data-oriented pattern. The cost is
   ergonomic: you write `nodes[i].next_idx` instead of `node.next`, and
   you need a sentinel or optional for "no node." Borrow Tycho's `Pool`
   trick for safety: make handles **generational** (index + a generation
   counter; a stale handle fails its generation check instead of silently
   reading a recycled slot). One extra word per handle buys use-after-free
   *detection* for the index-as-pointer pattern, with no pointer type.

3. **Idiomatic building must be in place.** The immutable form
   `total = total + str(i)` in a loop leaves dead intermediate buffers in
   the arena (bounded by scope, but wasteful). The blessed form is
   in-place growth — `push(builder, str(i))` / `inout` — which is
   geometric and tight. "One right way" should make in-place building the
   one way; immutable rebuild stays *correct* but is the slow path.

## 6. Pressure-test results

| Scenario | Aliasing/lifetime hazard? | Resolution |
| --- | --- | --- |
| `b := a` (array/struct) | none | deep copy; independent |
| pass by value, read-only | transient borrow aliases source | safe: borrow can't be stored/returned (inv. 4); caller suspended |
| `inout` mutate | exclusive borrow | safe: in place, same arena, borrow non-storable |
| build in loop, return | value escapes scope | deep copy up, or build-in-parent (opt.) |
| push scratch value into outer array | element outlives iteration | safe: push *copies* into outer arena |
| `a[i] = someString` | old element becomes garbage | sound; wasted-in-arena, bounded by `a`'s life |
| array slice `xs[a:b]` | view aliases parent buffer | safe: a view only as a read-only arg (zero-copy borrow, like any array param); storing/returning/pushing it deep-copies, so it is non-storable. A slice + an `inout` of the same var in one call is rejected. |
| substring | view would alias parent buffer | **substr is a copy** (owns its bytes); no aliasing string views (a string slice can't be NUL-terminated) |
| recursive/graph types | would need pointers | not allowed; use array + indices |
| return a reference to a local | classic dangling | **inexpressible** — no reference type exists |

The last row is the crux: the dangling-pointer bug that forces every other
region system to ship escape analysis *for correctness* is simply not
expressible in Hier, so it can't occur.

## 7. No generics — built-in containers instead

Hier has **no user-facing generics**: no generic functions, no generic
structs. That decision is firm. The few parametric types you genuinely
need are **built-in container intrinsics** the compiler monomorphizes
itself — the user cannot add new ones:

- `[T]` — growable array, any element type T (incl. structs, strings).
- `Option(T)` — optional, since there is no `null`.
- `Map(K, V)` — hash map (if/when added), comparable keys only.

You cannot write `struct Box(T)`. For "a box of T" you use a built-in
container or write a concrete struct per type. This is the Go-pre-1.18
line: ship the handful of parametric types people actually need, forbid
defining more. **Cost:** no user-defined generic data structures; compose
from built-ins or duplicate. **Benefit:** the checker and codegen never
grow a generics engine — the cross-module monomorphization registry that
was a major source of complexity in Tycho's compiler simply does not exist.

Still required, and none of these is generics:

- **`inout` parameters** (exclusive mutable borrow) — efficient mutation
  without copies or aliasing.
- **`Option(T)` + exhaustive `match`** — the no-`null` story.
- **Slices** (`xs[a:b]`) — ✅ done: a non-storable view (zero-copy when passed
  to a read-only param, deep-copied when stored), no borrow checker needed.
- **`distinct` newtypes** (`type Meters = float`) — ✅ done: a distinct,
  zero-cost type over `int`/`float` (same C rep, type-incompatible with the base
  and other newtypes); arithmetic/ordering/`str` only between the same newtype.

## 8. Allocation strategy: signature-directed escape (Tycho's lesson, improved)

Tycho passes each function an implicit arena parameter and has the callee
allocate into the *caller's* arena, so a returned value is already in the
right place — **zero-copy returns**. The price: a function's throwaway
temporaries also live in the caller's arena until the caller returns, so
Tycho recovers tight reclamation with *visible* tools — named sub-arena
blocks (`buffer: ...`) and `Pool(T)`. That works, but it puts memory back
in the programmer's face, the opposite of Hier's goal.

Hier can do better **because it has no pointers**. With value semantics and
no reference type, a value escapes a function only by being **returned** or
written through an **`inout`** parameter — both visible in the signature. A
callee cannot stash an argument anywhere that outlives the call (there is
nothing to stash it in). Therefore **escape is decided locally, from
signatures — no whole-program may-alias analysis**. Concretely:

- allocations that flow into the return value → emit in the caller's arena
  (Tycho's trick): zero-copy return;
- every other allocation in the call → emit in an auto-created sub-arena
  freed at scope exit: tight reclamation, invisibly;
- loop bodies → non-escaping allocations go in the per-iteration scratch
  (reset each iteration); an escaping `push` targets the destination's arena.

This is the synthesis: Tycho's zero-copy returns *and* per-scope
reclamation, with **no visible memory constructs at all**. It is sound by
construction because, under value semantics, a wrong escape decision can
only change *when* memory is freed, never *whether* a pointer dangles. In a
pointer-having language (Tycho) the same analysis is a correctness
obligation with alias tracking — which is exactly why Tycho reached for
explicit tools instead. The no-pointer rule turns Tycho's hardest problem
into a local optimization.

## 9. Verdict

Future A holds, and dropping generics does not weaken it. Correctness rests
entirely on **"no reference type + copy on cross-arena move."** What grows
is a *small, fixed* type surface (built-in containers, `inout`, `Option`,
optionally `distinct`) — not a generics engine, and not the memory model.
That keeps every lifetime question locally decidable from signatures, which
is exactly what lets the arenas stay invisible *and* lets the
signature-directed escape strategy (§8) reclaim memory without copies. The
predecessor (Tycho) proved the arena core works in production; Hier's wager
is that removing pointers and generics turns Tycho's *visible* memory tools
and *whole-program* analyses into *invisible*, *local* ones.
