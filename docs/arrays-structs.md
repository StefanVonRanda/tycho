# Tycho aggregates: arrays & structs under value semantics

Tycho is an experimental, value-semantic language with implicit hierarchical
arenas and **no pointer or reference type**. This document explains how
aggregates — arrays and structs, including heap-bearing and nested fields —
behave under those rules, and why the memory model stays sound without a
borrow checker or whole-program alias analysis.

The central question it answers: *do implicit arenas still hold once the
language has arrays and structs, given strict value semantics and no pointer
type?* The answer is **yes**, subject to five invariants and three honest
costs, both listed below. Two compiler optimizations (return-slot move,
in-place append) keep the costs from biting in practice; see
[thesis.md](thesis.md) §4. Arrays, structs, nested struct fields, structural
equality, and `mut` all ship as described — see the [README](../README.md)
and [thesis.md](thesis.md).

## 1. The model in one paragraph

Every value is a **wholly-owned tree**: a struct owns its fields, an array
owns its elements, recursively down to leaves (`int`, `bool`, `string`
bytes). A value and *all* its interior storage live in exactly **one
arena** — the arena of the variable (slot) that owns the root. There is
**no pointer/reference type**: you cannot name, store, or return a
reference to a value's interior. Every operation that would create a
second name for the same storage instead **copies** (deep, by default).
Mutation happens in place through `mut` parameters, which are an
exclusive borrow for the duration of a call and cannot be stored.

## 2. Types & syntax

```
# arrays — homogeneous, growable
xs := [1, 2, 3]          # [int] inferred
ys := []string           # empty; element type required when empty
xs[0]                     # index read
xs[0] = 9                 # index write (in place)
len(xs)                   # length
push(xs, 4)               # append in place  (xs is mut)
pop(xs)                    # remove last, returns it

# structs — nominal, fields are values
struct Point:
    x: int
    y: int

p := Point(1, 2)           # positional construction, fields in declaration order
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
3. **Mutation via `mut` only.** `fn f(mut xs: [int])` borrows the
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
     allocates it in the parent arena from the start → the return needs no copy.
     (The leaf version applies today: a return expression targets the
     parent arena.) Conservative when a value *may* be returned on some
     path; still sound, still local.
   - *Move on last use*: `b := a` / `b = a` where `a` is a
     uniquely-owned local read exactly once (so this is its last use on every
     path), not inside a loop, and in the same arena as the destination, hands
     off `a`'s buffer instead of deep-copying it. Conservative and static; a
     parameter (which borrows the caller's buffer) is never moved.
   - *Borrow on read*: a by-value parameter the callee only reads is
     passed as a transient borrow (zero copy) — safe because the borrow
     can't be stored (invariant 4) and the caller is suspended.
   The important part: in this model escape analysis is an **optimization**.
   In a pointer-having language the same analysis is a **soundness
   requirement**. This is why value semantics is viable here.

2. **No recursive types.** `struct Node: next: Node` is infinite-size and
   illegal — there is no pointer to break the cycle. Trees, lists, and
   graphs are expressed as **arrays + integer indices** (a node holds an
   `int` index into a `[Node]`, not a reference). The entire graph is then
   *one value* (the array) with *one* lifetime — which is an excellent fit
   for arenas, and a well-trodden data-oriented pattern. The cost is
   ergonomic: you write `nodes[i].next_idx` instead of `node.next`, and
   you need a sentinel or optional for "no node." A generational-handle
   scheme keeps it safe: make handles **generational** (index + a generation
   counter; a stale handle fails its generation check instead of silently
   reading a recycled slot). One extra word per handle buys use-after-free
   *detection* for the index-as-pointer pattern, with no pointer type.

3. **Idiomatic building must be in place.** The immutable form
   `total = total + str(i)` in a loop leaves dead intermediate buffers in
   the arena (bounded by scope, but wasteful). The blessed form is
   in-place growth — `push(builder, str(i))` / `mut` — which is
   geometric and tight. "One right way" should make in-place building the
   one way; immutable rebuild stays *correct* but is the slow path.

## 6. Aliasing and lifetime hazards, by scenario

| Scenario | Aliasing/lifetime hazard? | Resolution |
| --- | --- | --- |
| `b := a` (array/struct) | none | deep copy; independent |
| pass by value, read-only | transient borrow aliases source | safe: borrow can't be stored/returned (inv. 4); caller suspended |
| `mut` mutate | exclusive borrow | safe: in place, same arena, borrow non-storable |
| build in loop, return | value escapes scope | deep copy up, or build-in-parent (opt.) |
| push scratch value into outer array | element outlives iteration | safe: push *copies* into outer arena |
| `a[i] = someString` | old element becomes garbage | sound; wasted-in-arena, bounded by `a`'s life |
| array slice `xs[a:b]` | view aliases parent buffer | safe: a view only as a read-only arg (zero-copy borrow, like any array param); storing/returning/pushing it deep-copies, so it is non-storable. A slice + a `mut` of the same var in one call is rejected. |
| substring | view would alias parent buffer | **substr is a copy** (owns its bytes); no aliasing string views (a string slice can't be NUL-terminated) |
| recursive/graph types | would need pointers | not allowed; use array + indices |
| return a reference to a local | classic dangling | **inexpressible** — no reference type exists |

The last row is the key point: the dangling-pointer bug that forces every other
region system to ship escape analysis *for correctness* is not expressible in
Tycho, so it cannot occur.

## 7. Generics — monomorphized over the built-in container machinery

Tycho has **monomorphized generics**: a `$T` type parameter on a
function or struct, inferred at the use site and stamped out to concrete code at
compile time. They reuse the *same* per-concrete-type interning + emission the
compiler already runs for its built-in parametric types:

- `[T]` — growable array, any element type T (incl. structs, strings).
- `Option(T)` / `Result(T, E)` — optional / fallible, since there is no `null`
  and no exceptions.
- `[K: V]` — hash map; keys are `string`, `int`, a newtype over either, a fieldless enum, or any hashable composite (struct/tuple/array).

```
fn first(xs: [$T]) -> Option(T):        # generic function
    if len(xs) > 0: return Some(xs[0])
    return None

struct Box($T):                          # generic struct
    v: T
```

You can write `struct Box($T)` and `fn id(x: $T) -> T`. The monomorphization
engine such generics need is the same one that stamps out `Option(int)` versus
`Option(string)`. User `$T` definitions reuse it directly rather than adding a
new subsystem, and stay memory-model-neutral (§9).
The full design is in [generics.md](generics.md).

The full set is available in both compilers: generic functions, generic structs
(construction + `Box(int)` type-position annotations), structured patterns
(`[$T]` → `Option(T)`), `[$K: $V]` map patterns, `where` constraints
(`numeric` / `comparable` / `has_str`), and explicit call-site type args
(`empty$(int)` for the non-inferable case).

Companion features that work alongside generics (none of these is generics):

- **`mut` parameters** (exclusive mutable borrow) — efficient mutation
  without copies or aliasing.
- **`Option(T)` + exhaustive `match`** — no `null`.
- **Slices** (`xs[a:b]`) — a non-storable view (zero-copy when passed
  to a read-only param, deep-copied when stored), no borrow checker needed.
- **`distinct` newtypes** (`type Meters = float`) — a distinct,
  zero-cost type over `int`/`float` (same C rep, type-incompatible with the base
  and other newtypes); arithmetic/ordering/`str` only between the same newtype.

## 8. Allocation strategy: signature-directed escape

One established way to run arenas is to pass each function an implicit arena
parameter and have the callee allocate into the *caller's* arena, so a
returned value is already in the right place — **zero-copy returns**. The
price: a function's throwaway temporaries also live in the caller's arena
until the caller returns, so reclamation has to be recovered with *visible*
tools — named sub-arena blocks and pools. That works, but it puts memory
back in the programmer's face, the opposite of Tycho's goal.

Tycho can do better **because it has no pointers**. With value semantics and
no reference type, a value escapes a function only by being **returned** or
written through an **`mut`** parameter — both visible in the signature. A
callee cannot stash an argument anywhere that outlives the call (there is
nothing to stash it in). Therefore **escape is decided locally, from
signatures — no whole-program may-alias analysis**. Concretely:

- allocations that flow into the return value → emit in the caller's arena:
  zero-copy return;
- every other allocation in the call → emit in an auto-created sub-arena
  freed at scope exit: tight reclamation, invisibly;
- loop bodies → non-escaping allocations go in the per-iteration scratch
  (reset each iteration); an escaping `push` targets the destination's arena.

This is the synthesis: zero-copy returns *and* per-scope reclamation, with
**no visible memory constructs at all**. It is sound by construction
because, under value semantics, a wrong escape decision can only change
*when* memory is freed, never *whether* a pointer dangles. In a
pointer-having language the same analysis is a correctness obligation with
alias tracking — which is exactly why such languages reach for explicit
tools instead. The no-pointer rule turns that hardest problem into a local
optimization.

## 9. Why generics don't weaken the model

The arena model holds, and **adding generics does not change that**: each `$T`
instantiation is monomorphized to concrete, value-semantic code *before* the
signature-directed escape analysis (§8) runs, so it is ordinary concrete code
with the same locally-decidable lifetimes — nothing generic survives to analyze.
Correctness still rests entirely on **"no reference type + copy on cross-arena
move."** What grows is a substitution pass feeding the *same* per-type
emission the built-in containers already use — not a new generics engine, and
not the memory model.

That keeps every lifetime question locally decidable from signatures, which
is exactly what lets the arenas stay invisible *and* lets the
signature-directed escape strategy (§8) reclaim memory without copies. Arena
allocation itself is well-proven; the claim Tycho tests is that removing pointers
turns the visible memory tools and whole-program analyses such systems usually
need into invisible, local ones — and generics, being monomorphized to concrete
code first, ride along without re-introducing either.
