# Research: what Hylo (mutable value semantics) can teach Tycho
> Status: **concluded — no open questions.** Historical research record, retained
> because shipped docs cite it for grounding (the public `bench/README.md`,
> `value-semantics-limits.md`, `sink-prototype.md`). Both directions it opened are
> settled: the copy-cost side shipped as `sink` (`sink-prototype.md`), and the
> storage-shape / limited-references side was decided against
> (`../rfc/limited-references-spike.md`). Nothing here is open work.

Hylo (formerly Val) is the closest prior art to Tycho: a systems language built on
**mutable value semantics (MVS)** — mutation is allowed, but mutable *references* can
never be observed or aliased, so a value is always independent of every other value.
That is Tycho's model too. This note records what their approach offers for the
performance challenges we measured in `bench/trie` and `docs/internals/value-semantics-limits.md`,
and — honestly — what it does *not* solve.

Sources: "Implementation Strategies for Mutable Value Semantics", Racordon, Shabalin,
Zheng, Abrahams, Saeta, *JOT* 21(2), 2022 (https://www.jot.fm/issues/issue_2022_02/article2.pdf);
the Hylo language tour (https://docs.hylo-lang.org/language-tour/functions-and-methods);
and Jamie Brandon's analysis "Ruminating about mutable value semantics"
(https://www.scattered-thoughts.net/writing/ruminating-about-mutable-value-semantics/).

## The key framing: three ways to enforce MVS

Brandon's analysis names the design axis cleanly. To stop `let ys = xs` from aliasing
`xs`, an MVS language can:

1. **Implicit copies** — deep-copy on every move into/out of a mutable value. Correct,
   simple, but copies you didn't want (e.g. just reading `length(xs)` after).
2. **Dynamic alias tracking** — reference-count large values and copy-on-write: sharing is
   a refcount bump, a copy happens only when you mutate a shared buffer. This is Swift, and
   the JOT paper above. Cost: refcount traffic (atomic if threads can see the value), easy
   accidental copies, and "sophisticated optimizations" needed to claw back the overhead.
3. **Static alias tracking** — track aliasing at compile time via explicit passing
   conventions + a law of exclusivity. No refcounts, no implicit copies; removing a needed
   copy is a *compile error*. This is **Hylo**. Cost: annotation burden and a Rust-like
   population of programs that no longer type-check.

**Where each language sits — and where Tycho already is.** Tycho is *not* the naive option
1. We already implement much of option 3:

- immutable value parameters are passed as **borrows, not copies** (`src/tychoc.c:3216`
  "parameters are immutable borrows"; the read-only error at `:4108`) — this is Hylo's
  `let`;
- `inout` parameters are exclusive in-place mutation — Hylo's `inout`;
- **move-on-last-use** elides the copy when a source is dead after the move;
- `match`/`for` bindings borrow rather than copy when not mutated;
- map/array places (`&m[k]`) yield a slot lvalue rather than a copy.

So the interesting question is narrow: **what does Hylo's fuller static machinery buy that
our partial version doesn't — and is any of it compatible with our arena + no-refcount +
deep-copy-as-thread-boundary design?**

## Hylo's machinery, and whether it ports

**Four conventions: `let`, `inout`, `sink`, `set`.** We have `let` and `inout`. The two we
lack are explicit:

- **`sink`** — the callee *consumes* the argument; ownership transfers and the value may
  escape (be returned/stored). Today Tycho approximates this only *implicitly*, via
  move-on-last-use. An explicit `sink` is **predictable**: you can state "this call takes
  ownership," get a compile error if a copy would be needed, and stop depending on the
  optimizer noticing. Arena-compatible (no refcount). This is the **highest-leverage,
  lowest-risk** idea here — it makes existing copy elimination *guaranteed and visible*
  rather than best-effort.
- **`set`** — an out-parameter that the callee must initialize. We already have the FFI
  out-param shim; a first-class `set` would generalize it.

**Method bundles** — one method (e.g. `offset(by:)`) declares `let`/`inout`/`sink`
variants together and the compiler picks by call context. Pure ergonomics, but it removes
the let-vs-inout duplication that explicit conventions otherwise create. Worth it only if
we add `sink`.

**Subscripts / projections** — a subscript *yields* (projects) a part of a value with a
convention instead of returning a copy. Our `&m[k]` slot lvalue is exactly this idea,
built in; Hylo lets *users* define projections. A plausible future feature (zero-copy
views), not a near-term need.

**Copy-on-write (the JOT paper's core technique)** — share a container's buffer, copy only
on mutation-of-shared. This is the big lever for *copy-heavy* code, and it is **largely off
the table for us**: COW needs reference counting to know if a buffer is uniquely owned, and
atomic refcounts are exactly the cost our arena + lock-free thread model was designed to
avoid (a deep copy is our thread boundary). Adopting COW would mean adopting Swift's whole
refcount machinery and its atomics-on-shared-values tax. Not a fit.

## What Hylo does *not* solve: the trie/graph storage wall

This is the honest headline. Hylo's static tracking is about **copy cost**, not **storage
shape**. A Hylo trie is still `Dictionary<UInt8, TrieNode>` storing child nodes *by value*
— it hits the same per-node, value-inline cost we measured (`bench/trie`: ~3.2× C). MVS
forbids the shared mutable node that a C/Go trie or graph is built from, so Hylo faces the
identical wall, and its answers are the same two we already reached:

1. **Indices into a pool** — the flat node-pool idiom (`examples/triepool.ty`,
   `value-semantics-limits.md`). This is the standard MVS answer, and the same cache-friendly,
   data-oriented layout Hylo's own graph code reaches for.
2. **`remote parts`** — Hylo's *experimental* gap-filler (hylo-lang/discussions/754): a
   *limited* form of reference in the type system, preserving no-observable-aliasing and
   deterministic deallocation. It is the one genuinely new idea for the graph problem — but
   it is unproven even in Hylo, and (as Brandon notes) it is "very hard to square with
   transparent serialization": a value containing a remote part is no longer trivially
   `deserialize(serialize(x)) == x`. It buys graph ergonomics by spending some of the
   value-semantics purity that motivates the model. A real option, with eyes open.

So the trie limit we documented is **fundamental to mutable value semantics**, confirmed by
the most mature MVS language hitting the same wall — not a gap unique to Tycho's
implementation.

## Recommendation

Ranked candidate directions, all needing real design work and verification against current
internals — none are quick wins:

1. **Explicit `sink` / consuming convention.** Turns our implicit move-on-last-use into a
   guaranteed, visible, compiler-checked move. Arena-compatible, no refcounting, attacks
   real copy costs (passing a large value you're done with into a collection/constructor).
   Best effort-to-value ratio. Pairs with method bundles for ergonomics. **Update —
   prototyped in tychoc (`sink-prototype.md`): sound and arena-compatible. It adds
   owned-mutable params and elides the call-site copy for fresh values AND dead named
   variables. The arena-placement step (flagged below as the real work) landed as a small
   relaxation of move-on-last-use — drop the same-arena match for a *consuming* call, gated
   by read-once-outside-loops, since the callee only needs the buffer to outlive the call
   (any enclosing local's does) and the mutation to be unobserved. Verified against an
   adversarial soundness battery (loop, closure-capture-after-sink, use-after) + make test
   227/0 + fixpoint. Shipped in full on both compilers — direct and UFCS calls, the use-after-`sink` diagnostic, and the tychoc0 mirror (see sink-prototype.md);
   *escape* (returning the param) is still a copy — the arena's hard limit. So `sink` is a
   real copy-eliminating convention here, narrower than Hylo's only at escape.**
2. **`remote-parts`-style limited references** for graph/cyclic structures — **RESOLVED as a
   decision, not built** (`../rfc/limited-references-spike.md`, 2026-06-26). Grounded in
   Hylo's primary sources: Hylo's references are *second-class* (param modes — Tycho already
   has them) and *projections* are scoped/transient (Tycho has `&m[k]`); only **stored
   projections / remote parts** could store a graph edge by reference, and a value holding one
   is itself demoted to non-escaping second-class. That is incompatible with Tycho's two
   load-bearing invariants — implicit scope arenas (no escape analysis) and **deep-copy as the
   thread boundary** (a stored reference has no sound deep-copy across `spawn`) — and is
   unproven even in Hylo (its maintainer: "not obvious how one can build such an optimizer …
   too hard"). Conclusion: limited references do **not** unlock graphs-by-reference; the
   index-pool stays the answer (the same fallback Hylo makes).
3. **User-defined projections (yielding subscripts)** — generalize our built-in `&m[k]`. The
   spike confirms this is the *only* arena- and thread-compatible slice of the
   limited-reference space (scoped, non-escaping, no RC), but it is ergonomics over the index
   idiom, not a storage fix. Nice-to-have, lower priority.

Explicitly **rejected**: Swift/JOT-style refcounted copy-on-write. It would undo the
arena + lock-free design that is the point of the project.

Net: Hylo confirms our diagnosis rather than overturning it. The copy-cost side has a clean,
arena-compatible next step (explicit `sink`); the storage-shape side (trie/graph) is a hard
limit of MVS itself, where even Hylo only offers indices or an experimental, purity-spending
reference feature.
