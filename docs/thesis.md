# The Tycho thesis: value semantics makes implicit arenas work

This is the *why* behind Tycho. The
[language reference](reference/index.md) describes *what* the language does;
the [aggregates design note](internals/design-aggregates.md) covers how
deep-copied aggregates stay sound. Here is the case for the model, backed by
running, measured code.

Tycho is a small, ahead-of-time language: Python-looking syntax,
value semantics, no garbage collector, no `malloc`/`free`, no
lifetime annotations, no borrow checker. Programs are written as if memory were
managed, and it is. The claim is not "arenas are good" — that is well known.
The claim is sharper:

> **Value semantics is the missing ingredient that lets hierarchical arena
> allocation be fully *implicit* — inferred from lexical scope and signatures
> alone, with no whole-program analysis and no visible memory constructs —
> while staying competitive on performance.**

It is data-oriented: built for a domain (see §6), not general-purpose.

Measurement method: cross-language *ratios* are the claim, not absolute times,
and every fixture is checked under sanitizers against its golden output. See
[performance.md](performance.md) and [controls.md](controls.md) for the method;
it is not repeated per figure below.

## 1. The idea

Two old ideas, and the hinge between them.

**Hierarchical arenas.** Every scope — each function, each `if`/`else` block,
each loop body — owns an arena: a bump allocator backed by a linked list of
blocks. Allocation is a pointer increment. Reclamation is freeing the whole
arena at scope exit; nothing is freed individually. A loop body's arena is
*reset* each iteration, so a million iterations of throwaway temporaries run in
constant memory. This is well-trodden ground (region inference, MLKit, Cyclone,
arena-per-request servers, every game engine's frame allocator).

**Value semantics.** There is **no reference type**. You cannot name, store, or
return a pointer into another value's storage. Two variables never share
mutable state: `b := a` copies (deeply), so mutating `b` can never be observed
through `a`. Assignment, parameters, and returns all copy.

**The hinge.** Arenas are usually *explicit* — you pick the arena, you scope it
— precisely because, in a language with pointers, the compiler cannot in
general prove where a value may escape to. That needs whole-program may-alias
analysis, which is exactly the hard problem. **Value semantics dissolves that
problem.** With no reference type, a value can escape a scope in only two ways,
both visible in the syntax:

1. **Down** — passed as an argument to a callee (whose arena is a child, so the
   value outlives the call). No copy.
2. **Up** — returned, or assigned to an outer-scope variable (allocated
   directly in that destination's arena, so it survives the inner scope's
   collapse).

That's the whole escape rule, and it's *locally decidable* — from the
statement and the signature, never from a global analysis. So the transpiler can
insert every allocation, promotion, and free itself. The arenas become
invisible. **The no-pointer rule turns the arena allocator's hardest
prerequisite into something easy.** That's the thesis in one sentence.

## 2. The seam, and the one invariant that governs everything

Building the language out — strings, then `[int]`/`[string]` arrays, then
heap-bearing and nested structs — kept hitting the same hazard at every level,
and every level wanted the same fix. Call it **the seam**:

> A heap-backed value (a string's bytes, an array's buffer, recursively a
> struct's heap fields) moved into a longer-lived place must have its bytes in
> *that place's* arena.

A bare variable read allocates nothing — it's a pointer copy. So returning or
assigning a bare local heap value, naively, leaves a pointer into a scope
that's about to be freed: a use-after-free. The fix is **deep copy on
cross-arena move** (`copy_into` in the transpiler; per-struct `tycho_copy_S_X`
deep-copy functions generated into the output C). It nests: copying a
`[string]` copies the buffer *and* every element's bytes; copying a struct
copies each heap field, recursing. Structural equality (`==`) is the mirror
image — compare by content, recursing the same way (`gen_eq`,
`tycho_eq_S_X`) — so `a == b` is true exactly when `b` is an independent copy
of `a`.

Immutability is not a substitute for this copy. Immutability makes *aliasing*
safe (no one can mutate a shared buffer); it does nothing for *lifetime* (a
frozen buffer in a freed arena is still freed). The invariant above is what
actually holds the model together, and it is the same at every level of
nesting.

## 3. Why it's sound, said precisely

Correctness rests on a single property:

> Under value semantics, a wrong *escape* decision can only change **when**
> memory is freed — never **whether** a pointer dangles.

Over-approximate "this might escape" and you allocate in a longer-lived arena
than strictly needed: mild retention, never a bug. There's no symmetric
disaster, because there are no aliases to invalidate. In a pointer-having
language the same decision is a *correctness* obligation backed by alias
analysis — which is why such languages reach for explicit arenas, lifetimes, or
GC. Removing pointers demotes it to a *performance* knob the transpiler can set
freely. Every optimization below leans on exactly that asymmetry.

## 4. Where the abstraction would leak — two optimizations that seal it, and one shape that stays visible

Value semantics buys safety with copies. **Three** patterns make the copies
bite. Two of them the compiler seals *without making the model visible* — same
source, same semantics, same bounded memory, no copy; each is the asymmetry in
§3 applied locally. The third (§4c) it does not seal: that one is closed by choosing a different representation, which
means the programmer has to see the copy. It is the largest cost in this
document, so it is written as a shape of its own here and carried into §5 as a
wall — not left as a footnote to §4b.

### 4a. The return-path copy tax → return-slot move

"Build a value locally, then return it" (`r := []int; …; return r`) naively
deep-copies the whole result into the caller on the way out. But a function
escape analysis (`collect_escapes`) can see that `r` is returned by name and
allocate it in the *caller's* arena from birth — so the `return` is a header
move, not an O(n) copy. It composes across call frames: a value returned up
several levels is built once, in the final consumer's arena, with **zero copy
call-sites** (destination-passing, emergent). Soundness comes from §3:
allocating in the parent is always safe; the copy is skipped only when the
value provably already lives there.

*Measured* (`fn build(n)->[int]` returned 20000×, against the compiler just
before this optimization): **0.91s → 0.52s (~1.75×)**.

### 4b. Accumulation retention → in-place append

`acc = acc + e` in a loop is the textbook O(n²) trap: each step allocates a
fresh buffer, copies the whole accumulator, and abandons the old one in the
bump arena (which can't reclaim it) — O(n²) in *both* time and memory. Tycho
recognizes the *self-append* shape (`acc` on the left of `+`, reassigned to
`acc`) and grows `acc`'s buffer in place with geometric capacity, exactly like
an array's `push`. This is the Tycho analog of Perceus reuse — and the
uniqueness check Perceus needs reference counting to perform is **free here**:
value semantics already guarantees `acc` is uniquely owned at the rebind (any
`b := acc` took its own deep copy), so growing it in place is invisible to
everyone else.

*Measured* (`acc = acc + "x"`, peak RSS, baseline vs optimized):

| N | baseline | optimized |
|---|---|---|
| 10 000 | 33 MB | (flat) |
| 20 000 | 191 MB | (flat) |
| 40 000 | 828 MB | **< 4 MB** |
| 400 000 | (would be ~80 GB) | **< 8 MB** |

Quadratic → linear, and the optimized memory does not scale with N.

The pattern across 4a/4b: **the optimization is the model's own asymmetry,
applied locally.** Neither touches the source language or the value-semantic
guarantee.

### 4c. Rebuild-per-operation on a long-lived value — the shape the compiler does not seal

Where 4a and 4b have a recognizable shape, some programs have none: a
long-lived mutable value that is *rebuilt* rather than *mutated* pays a full
copy of the whole value on every operation. The worked case is `tools/tycho-ed/`,
whose line buffer rebuilds a string per keystroke: per-edit cost drifts ~7×
over a million edits while four controls (5–35 ns/step, flat) rule out the
arena, concatenation, and `inout` as the cause. Rebuilding the line as a gap
buffer — one byte written into a hole per keystroke instead of one copy per
keystroke — flattens the drift to ~100% with the millionth keystroke ~10×
cheaper, while peak RSS stays at 95.7 MB in both modes (the resident set is the
undo journal doing its job, not churn). The full journal with all controls is in
[internals/editor-churn-2026-08-12.md](internals/editor-churn-2026-08-12.md);
the representation idiom is in
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).

The boundary as a rule of thumb: **if a mutable value outlives every scope
that would reclaim it, rebuilding it per operation is the expensive shape, and
mutating it in place is the supported one.** §5 carries this as a wall.

## 5. The walls — what's genuinely hard

A model is defined as much by what it *can't* do. **Four** patterns threaten the model, and they are not equally hard.
Two the compiler seals outright. One the compiler cannot seal but a
representation can, at the cost of the programmer seeing the arena. One is not a
limit to be lifted at all — it is the thesis.

**The return copy tax** and **accumulation retention** are sealed, in §4a and
§4b respectively. They cost the reader a paragraph each and the programmer
nothing.

**Rebuild-per-operation on a long-lived value** (§4c) is the one that stays
visible, and it is the wall a working programmer is most likely to hit, because
unlike the next one it shows up in ordinary tree-shaped code with nothing
graph-shaped about it. An editor's buffer, a session cache, a server's
accumulated state: change one byte by rebuilding the value and you copy the
whole value, every time. 4a and 4b do not fire — there is no shape to
recognize — and the arena recycling the old bytes does not refund the copy. It
is *reachable*: mutate in place instead of rebuilding, and the measured curve
flattens completely (§4c). But reaching it is a decision the programmer makes,
which makes this the one place the model is not invisible.

**Shared mutable state** is the genuinely hard one — sometimes described as
"non-tree data: graphs, cycles, caches." Most of it dissolves on inspection:

- **Cyclic and graph-shaped data is *not* a wall.** A directed graph *with a
  cycle* is traversed correctly today using **indices instead of references** —
  CSR adjacency arrays plus a `visited[]` array. (BFS over a literal 3-cycle
  works; this is also how cache-efficient graph code is written anyway.) Value
  semantics forbids *pointer* cycles, not *modeled* cycles. *Measured*: a
  by-value recursive trie costs **~1.55× C's memory** (children stored inline,
  not shared by pointer — down from ~3.2× once its per-node maps moved to a
  compact indexed-dict layout), and the same shape as a flat `[Node]` pool
  linked by integer indices is **~1.3× C** on a 300k-node graph (`bench/trie`,
  `bench/dijkstra`). Even the naive by-value form is now within a small factor
  of C; the index-pool idiom closes the rest and is what makes genuine *sharing*
  (DAGs, cycles) expressible at all. The full measured loss column with the
  idiom for each case is in
  [the value-semantics limits note](internals/value-semantics-limits.md).

- The real, narrow wall is **shared mutable state threaded through function
  calls** — canonically a memoization table that recursive calls must all
  write to. The thesis-preserving answer is `inout`: an exclusive,
  copy-in/copy-out mutable borrow. `inout` does not break value semantics: it is equivalent to
  `x = f(x)`, made safe by an exclusivity rule (the same variable cannot be
  passed to two `inout` parameters of one call). Heap `inout` — `[int]`,
  `[string]`, and heap-bearing structs, including `push`/growth and
  element/field mutation through the borrow — lets the callee share and mutate
  the caller's aggregate in place, so a memo table (or a recursive output
  collector, or a mutable context object) is genuinely shared across all
  frames. The owning arena is threaded as a hidden parameter, so an allocating
  mutation lands where the value lives and survives the call.

  *Measured*: the naive exponential `fib(40)` computes 102334155 in **0.60s**;
  the memoized version returns the same answer in **under a millisecond** — O(n)
  vs O(2ⁿ).

**This boundary is where the safety comes from.** What stays genuinely
impossible is *pointer-identity aliasing of two named variables in one scope* —
two handles to one mutable object, a write through one seen through the other,
held beyond any single call. The observer pattern, a shared mutable cache held
in a field, doubly-linked structures by reference, **and dynamic dispatch
through an interface value**. This is forbidden **by construction**, and `inout`
doesn't provide it (it's call-scoped, not storable). That
forbiddance is *what value semantics is*. Removing it wouldn't extend Tycho; it
would make it a different language.

A trait object — Go's `interface`, Java's, Rust's `dyn Trait` — is *a
pointer to someone else's value plus a vtable*, which is exactly the aliasing
value semantics forbids, so a heterogeneous container of implementors or an
interface value held in a struct is the same wall, not a missing feature.
Static polymorphism is unaffected and already works: a generic body is
type-checked after substitution, so a generic can call an ordinary function on
`$T` and monomorphize per instantiation, with no vtable and no indirection. What
is refused is the *dynamic* form, and it is refused for the same one reason
everything else in this section is.

## 6. Where that leaves the idea

The verdict:

- For **tree-shaped, scope-shaped, build-and-return, accumulate** programs —
  compilers and compiler passes, request handlers, batch transforms, CLI
  tools, frame-loop logic — the model delivers **zero memory cognition with
  performance competitive with manual approaches**. The two optimizations are
  what move it from "cute" to "competitive," and they were free because the
  model's safety asymmetry hands them over.

- For **long-lived mutable structures edited over a session** — an editor
  buffer, a session cache, a server's accumulated state — it works, and it is
  the one case where the model is **not invisible**. Rebuilding the value per
  operation copies the whole value per operation; mutating it in place copies
  nothing and flattens the curve (§4c, measured). Both compile, both are safe,
  and only one is cheap — so here the programmer, not the compiler, makes the
  decision. Zero memory cognition is claimed for the bullet above this one and
  is not claimed for this one.

- For **shared-mutable-graph** programs (long-lived shared caches, observer
  graphs, reference-cyclic structures) it is a poor fit, and no optimization
  changes that — it is the thesis's defining boundary, not a missing feature.

So: data-oriented, not general-purpose, and not trying to be. Within its domain
the wager holds — *value semantics is precisely the constraint that lets the arenas
disappear* — and the domain is large and real.

## 7. Reproducing the numbers

The figures above are measured on the committed compiler. To reproduce:

```sh
make                                    # build ./tychoc
./tychoc examples/accumulate_big.ty     # in-place append, large N
./tychoc examples/memo.ty               # inout memoized fib(40)
# (the return-slot A/B -- this compiler vs a pre-return-slot baseline built from an
#  earlier commit against a regenerated embed header -- is scripted in bench/*/RESULTS.md)
```

Peak-RSS and sanitizer method are as stated in [performance.md](performance.md)
and [controls.md](controls.md): ratios across languages are the claim, and every
example and feature program is checked against its golden under sanitizers.

Three further things back the thesis up, written up separately.

**Head-to-head performance.** The cross-language benchmark suite under `bench/`
(Tycho vs C, Go, Rust, and Koka's Perceus reference-counting) and
the compiler and codegen numbers are in [docs/perf.md](perf.md).

**Concurrency falls out for free.** The same call convention — deep-copy in, copy
out, a private arena per call — is already a sound thread boundary, so
`spawn`/`wait`, `parallel for`, channels, and `select` are just that convention run
on threads: race-free by construction, with no Sendable/lifetime/lock machinery
in the language. Measured: `parallel for` at C-pthreads parity on a
compute-bound reduction; and a lock-free-channel pipeline that runs ~9× faster than a
hand-written C *mutex-ring* baseline (73 ms vs 654 ms) — a design-expressiveness result
(lock-free vs mutex), not a throughput claim over optimal C, and still paying the
value-semantic copies C doesn't ([reference/concurrency.md](reference/concurrency.md), `bench/conc/`).
