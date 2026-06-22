# The Hier thesis: value semantics makes implicit arenas work

This document explains the core idea behind Hier — the *why*. The
[README](../README.md) is the reference overview;
[arrays-structs.md](arrays-structs.md) works through the design under pressure.
This document makes the argument the implementation backs up with running,
measured code.

Hier is an experimental, small, ahead-of-time language: Python-looking syntax,
value semantics, no garbage collector, no `malloc`/`free`, no
lifetime annotations, no borrow checker. You write code as if memory were
managed for you, and it is. The claim is not "arenas are good" — everyone knows
that. The claim is sharper:

> **Value semantics is the missing ingredient that lets hierarchical arena
> allocation be fully *implicit* — inferred from lexical scope and signatures
> alone, with no whole-program analysis and no visible memory constructs —
> while staying competitive on performance.**

This is a proof-of-concept exploration of that claim, not a production system.
It is general-purpose within a domain (see §6) and deliberately not beyond it.

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

That is the whole escape story, and it is *locally decidable* — from the
statement and the signature, never from a global analysis. So the compiler can
insert every allocation, promotion, and free itself. The arenas become
invisible. **The no-pointer rule turns the arena allocator's hardest
prerequisite into a triviality.** That is the thesis in one sentence.

## 2. The seam, and the one invariant that governs everything

Building the language out — strings, then `[int]`/`[string]` arrays, then
heap-bearing and nested structs — hits the same hazard at every level, and
every level wants the same fix. Call it **the seam**:

> A heap-backed value (a string's bytes, an array's buffer, recursively a
> struct's heap fields) moved into a longer-lived place must have its bytes in
> *that place's* arena.

A bare variable read allocates nothing — it is a pointer copy. So returning or
assigning a bare local heap value, naively, leaves a pointer into a scope
that is about to be freed: a use-after-free. The fix is **deep copy on
cross-arena move** (`copy_into` in the compiler; per-struct `hier_copy_S_X`
deep-copy functions generated into the output C). It nests: copying a
`[string]` copies the buffer *and* every element's bytes; copying a struct
copies each heap field, recursing. Structural equality (`==`) is the mirror
image — compare by content, recursing the same way (`gen_eq`,
`hier_eq_S_X`) — so `a == b` is true exactly when `b` is an independent copy
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
than strictly needed: mild retention, never a bug. There is no symmetric
disaster, because there are no aliases to invalidate. In a pointer-having
language the same decision is a *correctness* obligation backed by alias
analysis — which is why such languages reach for explicit arenas, lifetimes, or
GC. Removing pointers demotes it to a *performance* knob the compiler may set
freely. Every optimization below exploits exactly that asymmetry.

The verification standard for this repo follows from it: every codegen change is
checked under `cc -fsanitize=address,undefined`, asserting (a) exit 0, (b) clean
sanitizers, and (c) ASan output byte-identical to native `-O2` output. The full
`tests/` + `examples/` suite holds to this.

## 4. Where the abstraction would leak — and the two optimizations that seal it

Value semantics buys safety with copies. Two patterns make the copies bite, and
both are sealed *without making the model visible* — same source, same
semantics, same bounded memory, just no copy. Each is the local exploitation of
the asymmetry in §3.

### 4a. The return-path copy tax → return-slot move

"Build a value locally, then return it" (`r := []int; …; return r`) naively
deep-copies the whole result into the caller on the way out. But a function
escape analysis (`collect_escapes`) can see that `r` is returned by name and
allocate it in the *caller's* arena from birth — so the `return` is a header
move, not an O(n) copy. It composes across call frames: a value returned up
several levels is built once, in the final consumer's arena, with **zero copy
call-sites** along the way (destination-passing, emergent). Soundness is §3:
allocating in the parent is always safe; the copy is skipped only when the
value provably already lives there.

*Measured* (`fn build(n)->[int]` returned 20000×, against the compiler just
before this optimization): **0.91s → 0.52s (~1.75×)**, output byte-identical.

### 4b. Accumulation retention → in-place append

`acc = acc + e` in a loop is the textbook O(n²) trap: each step allocates a
fresh buffer, copies the whole accumulator, and abandons the old one in the
bump arena (which can't reclaim it) — O(n²) in *both* time and memory. Hier
recognizes the *self-append* shape (`acc` on the left of `+`, reassigned to
`acc`) and grows `acc`'s buffer in place with geometric capacity, exactly like
an array's `push`. This is the Hier analog of Perceus reuse — and the
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

## 5. The walls — what's genuinely hard, honestly mapped

A model is defined as much by what it *can't* do. Three patterns threaten the
model. Two are sealed by the optimizations above. The third is narrower than it
first looks, and is reachable — but it has a residue that is not a bug, it is
the thesis.

**The return copy tax** and **accumulation retention** are handled in §4a and
§4b respectively.

**Shared mutable state** is the genuinely hard one — sometimes described as
"non-tree data: graphs, cycles, caches." Probing it empirically dissolves most
of it:

- **Cyclic and graph-shaped data is *not* a wall.** A directed graph *with a
  cycle* is traversed correctly today using **indices instead of references** —
  CSR adjacency arrays plus a `visited[]` array. (BFS over a literal 3-cycle
  works; this is also how cache-efficient graph code is written anyway.) Value
  semantics forbids *pointer* cycles, not *modeled* cycles.

- The real, narrow wall is **shared mutable state threaded through function
  calls** — canonically a memoization table that recursive calls must all
  write to. The thesis-preserving answer is `mut`: an exclusive,
  copy-in/copy-out mutable borrow (the Swift/Hylo model — *not* a stored
  reference). `mut` does not break value semantics: it is equivalent to
  `x = f(x)`, made safe by an exclusivity rule (the same variable cannot be
  passed to two `mut` parameters of one call). Heap `mut` — `[int]`,
  `[string]`, and heap-bearing structs, including `push`/growth and
  element/field mutation through the borrow — lets the callee share and mutate
  the caller's aggregate in place, so a memo table (or a recursive output
  collector, or a mutable context object) is genuinely shared across all
  frames. The owning arena is threaded as a hidden parameter, so an allocating
  mutation lands where the value lives and survives the call.

  *Measured*: memoized `fib(40)` = 102334155 in **0.001s**; the naive
  exponential `fib(40)` computes the same answer in **0.60s**. The `mut` memo
  makes it O(n) — proof the array is truly shared, not copied per call.

**The residue is the boundary, not a defect.** What remains genuinely
impossible is *pointer-identity aliasing of two named variables in one scope* —
two handles to one mutable object, a write through one seen through the other,
held beyond any single call. The observer pattern, a shared mutable cache held
in a field, doubly-linked structures by reference. This is forbidden **by
construction**, and `mut` deliberately does not provide it (it is call-scoped,
not storable). That forbiddance is *what value semantics is*. Removing it would
not extend Hier; it would make it a different language.

## 6. Where that leaves the idea

The honest verdict, backed by measurement rather than intuition:

- For **tree-shaped, scope-shaped, build-and-return, accumulate** programs —
  compilers and compiler passes, request handlers, batch transforms, CLI
  tools, frame-loop logic — the model delivers **zero memory cognition with
  performance competitive with manual approaches**. The two optimizations are
  what move it from "cute" to "competitive," and they were free because the
  model's safety asymmetry hands them over.

- For **shared-mutable-graph** programs (long-lived shared caches, observer
  graphs, reference-cyclic structures) it is a poor fit, and no optimization
  changes that — it is the thesis's defining boundary, not a missing feature.

So: not general-purpose, and not trying to be. Within its domain the wager
holds — *value semantics is precisely the constraint that lets the arenas
disappear* — and the domain is large and real.

## 7. Reproducing the numbers

The figures above are measured on the committed compiler. To reproduce:

```
make                                  # build ./hierc
# return-slot (build a baseline compiler from an earlier commit to compare):
git show 9d3367f:src/hierc.c > /tmp/b.c   # last commit before return-slot
# ...build it against a regenerated embed header, then A/B the same .hi
examples/accumulate_big.hi            # in-place append, large N
examples/memo.hi                      # mut memoized fib(40)
```

Peak RSS was read from `/proc/<pid>/status` `VmHWM`; the optimized append
ceiling was confirmed with an `ulimit -v` ladder (fits under 4 MB at
N=40 000, under 8 MB at N=400 000 — does not scale with N). Every example and
feature program is checked under `cc -fsanitize=address,undefined` with output
required to match native `-O2`.

Three further validations back the thesis, written up separately.

**Self-hosting.** A second compiler written in Hier itself
(`compiler/hierc0.hi`) reaches a byte-identical fixpoint (`make fixpoint`), and
its codegen runs on this same implicit-arena model — migrated onto it one type
family at a time, each step gated by the fixpoint plus sanitizers
([docs/memory-model.md](memory-model.md)). That makes the model eat its own dog
food on a real, allocation-heavy, deeply-recursive program. A differential
fuzzer cross-checks the two compilers under AddressSanitizer to keep them in
agreement.

**Head-to-head performance.** A cross-language benchmark suite (`bench/prongB/`,
Hier vs C, Go, Rust, and Koka's Perceus reference-counting) and the
compiler-vs-generated-code numbers are in [docs/perf.md](perf.md).

**Concurrency as a corollary.** The same call convention — deep-copy in, copy
out, a private arena per call — is already a sound thread boundary, so
`spawn`/`wait`, `parallel for`, channels, and `select` are that convention run
on threads: race-free by construction, with no Sendable/lifetime/lock machinery
in the language. Measured: `parallel for` at C-pthreads parity on a
compute-bound reduction; lock-free channels 2.6x faster than a hand-written C
mutex ring while still paying the value-semantic copies C doesn't
([docs/concurrency.md](concurrency.md), `bench/conc/`).
