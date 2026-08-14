# The Tycho thesis: value semantics makes implicit arenas work

This is the core idea behind Tycho — the *why*. The
[language reference](reference/index.md) describes *what* the language does;
the [aggregates design note](guides/arrays-structs.md) pushes this argument harder. Here
I make the case for it, backed by running, measured code.

Tycho is a small, experimental, ahead-of-time language: Python-looking syntax,
value semantics, no garbage collector, no `malloc`/`free`, no
lifetime annotations, no borrow checker. You write code as if memory were
managed for you, and it is. I'm not claiming "arenas are good" — everyone knows
that. The claim is sharper:

> **Value semantics is the missing ingredient that lets hierarchical arena
> allocation be fully *implicit* — inferred from lexical scope and signatures
> alone, with no whole-program analysis and no visible memory constructs —
> while staying competitive on performance.**

This is a proof-of-concept exploration of that claim, not a production system.
It's general-purpose within a domain (see §6) and deliberately not beyond it.

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

The verification standard for this repo follows from it: every codegen change is
checked under `cc -fsanitize=address,undefined`, asserting (a) exit 0, (b) clean
sanitizers, and (c) ASan output byte-identical to native `-O2` output. The full
`tests/` + `examples/` suite holds to this.

## 4. Where the abstraction would leak — and the two optimizations that seal it

Value semantics buys safety with copies. Two patterns make the copies bite, and
both are sealed *without making the model visible* — same source, same
semantics, same bounded memory, no copy. Each one is the asymmetry in §3 applied
locally.

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

> **Benchmark setup.** Figures here were measured on a single machine — AMD Ryzen 7 7735HS (16 hardware threads), Debian x86-64 — except where a different machine is noted. Toolchain versions and per-suite detail are in the matching `bench/*/RESULTS.md`. `tychoc` is the C-hosted compiler, `tychoc0` the self-hosted one.
>
> Some figures below carry their own measurement date; those that do not were taken on or before **2026-08-12**, when this page was last revised, and have not been re-measured since. Each is tied to the optimization it motivated, so read them as that change's effect rather than as today's absolute numbers.

*Measured* (`fn build(n)->[int]` returned 20000×, against the compiler just
before this optimization): **0.91s → 0.52s (~1.75×)**, output byte-identical.

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

#### A second shape the append optimization does not cover: replacement

Self-append is one way a loop abandons its accumulator. There is another, and
`tools/tycho-ed/` is a worked case of it. A text editor's line buffer does

```
b.lines[ln] = s[0:c] + t + s[c:len(s)]
```

on every keystroke. That is not `acc = acc + e` — the old line is not a prefix
of the new one, it is *replaced* — so the self-append shape does not match and
nothing grows in place. Each edit allocates a whole new line and abandons the
previous copy inside a scope that never ends: an editing session is one arena
that lives as long as the program.

*Measured* on a 16-core box, 2026-08-12, `tychoc tools/tycho-ed/main.ty` then
`--stress=N` (see `tools/tycho-ed/main.ty@stress`; N edits in ten equal
buckets, one character each with Enter every 60 so the *line* stays bounded
while the *document* grows):

| N edits | first bucket | last bucket | last/first | document | peak RSS |
|---|---|---|---|---|---|
| 10 000 | 133 ns/edit | 119 ns/edit | 89% | 15 KB | 5.8 MB |
| 100 000 | 133 ns/edit | 298 ns/edit | 224% | 147 KB | 11.8 MB |
| 1 000 000 | 214 ns/edit | 1413 ns/edit | 660% | 1.46 MB | 95.7 MB |

Flat through 10 000 edits, already drifting at 100 000, and about 7× slower per
edit by a million — with 65× more memory resident than the document it is
holding.

**It is not the arena's allocation cost, and that is what makes the case worth
recording.** Four controls, each 1 000 000 steps in the same ten-bucket shape:
pushing a two-field struct onto an array, bounded string concatenation (reset
every 60 steps, so it measures churn and not quadratic growth), pushing into an
array field of an `inout` struct, and pushing an enum variant carrying a string.
All four land between 5 and 35 ns/step and none degrades meaningfully across the
range — the worst, the enum carrying a string, goes 17 → 35 ns and is still
*forty times* under what the editor costs at the same point. No single
ingredient of an edit is slow. What the editor adds is **volume**: a million
abandoned line copies that nothing reclaims.

The mitigation is the same one 4b already names, in the shape this workload
needs: **mutate in place instead of rebuilding**. For an editor that means a gap
buffer — a line held as text-before-cursor, a gap, and text-after-cursor, where
inserting a character writes one byte into the gap and allocates nothing.

##### The gap buffer, built and measured

`tools/tycho-ed/` now has both. `--backend=gap` keeps the line being edited in a
mutable `[int]` with a hole in it (`bytes` is immutable, so it cannot be the
store); a keystroke writes bytes into the hole and allocates nothing, and the
line is rebuilt as a string once per *focus change* — an Enter, an arrow key —
instead of once per keystroke. The journal, the API and every other operation
are the same code in both modes, and `make ed-check` leg [6] re-runs the demo
transcript and the six-edit undo/redo roundtrip under the gap backend and
requires them **byte-identical** to the string backend, so this is a change of
representation and of nothing else.

*Measured* the same day on the same box, three runs of every cell — last bucket
in ns/edit, and `last/first` as the drift:

| N edits | string, last | gap, last | string last/first | gap last/first |
|---|---|---|---|---|
| 10 000 | 145 / 121 / 124 | 114 / 177 / 129 | 96 / 50 / 89 % | 85 / 119 / 96 % |
| 100 000 | 281 / 267 / 279 | 119 / 120 / 119 | 181 / 161 / 187 % | 69 / 91 / 67 % |
| 1 000 000 | 1490 / 1443 / 1947 | 141 / 155 / 147 | 764 / 747 / 959 % | **97 / 113 / 103 %** |

**The curve flattens completely.** At a million edits the string backend's last
bucket costs about 8× its first; the gap backend's costs what its first did, and
the millionth keystroke is ~10× cheaper in absolute terms. The mitigation §4b
names is the right one, and the diagnosis it rested on — abandoned line copies,
not arena allocation cost — is confirmed by the fix working.

**Peak RSS did not move at all**, and that is the part worth keeping. 95.7 MB
for the string backend at a million edits, 95.7 MB for the gap backend, with a
million fewer abandoned line copies between them. So the resident set was never
the churn. A fifth control settles where it does come from: a million pushes of
the editor's exact journal entry — the `Op` enum plus a `Cursor`, with no buffer
and no line behind it — reaches **94.0 MB on its own**, at 42–56 ns/step. The
memory is the **undo journal**, one retained entry per keystroke, which is the
feature doing its job rather than a leak; and because it is flat in time, it
never contributed to the drift. Removing the churn was always going to move one
number and not the other.

The correction matters more than the confirmation. The paragraph above this one
used to read "a resident set that grows with the session rather than with the
document" as if it were the same phenomenon as the slowdown. It is not: one is
churn the arena cannot reclaim and a gap buffer removes, the other is live data
the program is deliberately holding. Two costs that rose together are not one
cost, and only building the fix separated them.

Written down here rather than filed as a defect, because it is not one: it is a
measured instance of a limit this project already concedes. The memory-model
guide says the same thing in general terms — allocation-churn workloads are
where the arena is at its weakest, and the honest loss
([`docs/guides/memory-model.md:116-118`](guides/memory-model.md)). What the
controls add is the reason to believe the diagnosis: without them, "1 M edits
got 7× slower" is equally well explained by the arena, by string
concatenation, or by `inout`, and the fix would be aimed at the wrong one.

The pattern across 4a/4b: **the optimization is the model's own asymmetry,
applied locally.** Neither touches the source language or the value-semantic
guarantee.

## 5. The walls — what's genuinely hard, honestly mapped

A model is defined as much by what it *can't* do, so I want to be honest about
the limits. Three patterns threaten the model. Two are sealed by the
optimizations above. The third is narrower than it first looks, and is reachable
— but it has a residue that isn't a bug, it's the thesis itself.

**The return copy tax** and **accumulation retention** are handled in §4a and
§4b respectively.

**Shared mutable state** is the genuinely hard one — sometimes described as
"non-tree data: graphs, cycles, caches." Poking at it empirically dissolves most
of it:

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
  copy-in/copy-out mutable borrow (the Swift/Hylo model — *not* a stored
  reference). `inout` does not break value semantics: it is equivalent to
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
  vs O(2ⁿ). That collapse is proof the array is truly shared, not copied per call.

**This boundary is where the safety comes from.** What stays genuinely
impossible is *pointer-identity aliasing of two named variables in one scope* —
two handles to one mutable object, a write through one seen through the other,
held beyond any single call. The observer pattern, a shared mutable cache held
in a field, doubly-linked structures by reference. This is forbidden **by
construction**, and `inout` deliberately doesn't provide it (it's call-scoped,
not storable). That forbiddance is *what value semantics is*. Removing it
wouldn't extend Tycho; it would make it a different language.

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

So: not general-purpose, and not trying to be. Within its domain I think the
wager holds — *value semantics is precisely the constraint that lets the arenas
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

Peak RSS was read from `/proc/<pid>/status` `VmHWM`; the optimized append
ceiling was confirmed with an `ulimit -v` ladder (fits under 4 MB at
N=40 000, under 8 MB at N=400 000 — doesn't scale with N). Every example and
feature program is checked under `cc -fsanitize=address,undefined` with output
required to match native `-O2`.

Three further things back the thesis up, written up separately.

**Self-hosting.** A second transpiler written in Tycho itself
(`compiler/tychoc0.ty`) reached a byte-identical fixpoint — it reproduced its own
emitted C byte-for-byte — and its codegen ran on this same implicit-arena model.
**That gate was retired on 2026-07-29**: a breaking loop-syntax change left the
frozen `tychoc0` unable to parse the corpus, so `make fixpoint` no longer exists
and nothing replaces it. The claim stands as of the freeze; it is not re-checked
by anything today
([docs/memory-model.md](guides/memory-model.md)). Soundness is checked by that
byte-identical self-build and sanitizers. That makes the model eat its own dog
food on a real, allocation-heavy, deeply-recursive program. A differential fuzzer
and accept/reject parity lanes cross-check the two transpilers under AddressSanitizer,
holding them to identical compile decisions with **no divergence skips** — the
language `tychoc0` accepts is no longer a strict subset of the reference's.

**Head-to-head performance.** The cross-language benchmark suite under `bench/`
(Tycho vs C, Go, Rust, and Koka's Perceus reference-counting) and the
compiler-vs-generated-code numbers are in [docs/perf.md](guides/perf.md).

**Concurrency falls out for free.** The same call convention — deep-copy in, copy
out, a private arena per call — is already a sound thread boundary, so
`spawn`/`wait`, `parallel for`, channels, and `select` are just that convention run
on threads: race-free by construction, with no Sendable/lifetime/lock machinery
in the language. Measured: `parallel for` at C-pthreads parity on a
compute-bound reduction; and a lock-free-channel pipeline that runs ~9× faster than a
hand-written C *mutex-ring* baseline (73 ms vs 654 ms) — a design-expressiveness result
(lock-free vs mutex), not a throughput claim over optimal C, and still paying the
value-semantic copies C doesn't ([docs/concurrency.md](guides/concurrency.md), `bench/conc/`).
