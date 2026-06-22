# Type inference in hier

Hier infers most local types without annotations, but it deliberately does
**not** use Hindley-Milner (HM) inference. Instead it uses *bidirectional*
(local) type inference: bottom-up synthesis for most expressions, plus a
top-down checking channel that pushes an expected type into the positions
that need one. There are no type variables and no constraint solver.

This document explains what hier infers, the rules that govern it, and why
HM-style inference was rejected. The reasoning is part of the design: it
shows why hier's memory model and type-directed features make
argument-directed inference a better fit than unification.

> Hier is an experimental, proof-of-concept language. The inference rules
> here are implemented in both compilers — the C reference compiler
> (`src/hierc.c`) and the self-hosted compiler (`compiler/hierc0.hi`) — and
> are exercised by a differential test suite. This is research-quality
> work, not a production type system.

## 1. What hier infers today

Locals are inferred. `x := e` types `x` from `e`, forward-only and
bottom-up (`resolve_expr` in src/hierc.c; `type_of` in
compiler/hierc0.hi).

Hier also has one *backward* channel: `resolve_exp(e, expected)` pushes an
expected type down at specific sites. It is what fixes a bare `None`
against a declared `Option(T)`, completes partial `Ok(v)`/`Err(e)` Results
against a function's return type (via the `T_NONE`/`T_OK_PARTIAL`
sentinels), and types `return` expressions.

So the question is not "inference: yes or no" — hier already has forward
inference plus a one-step bidirectional channel. HM would add *type
variables, unification, and let-generalization* on top of that. The rest
of this document explains why it doesn't.

## 2. The annotation burden, measured

Across the full corpus (compiler/hierc0.hi, corelib, examples, tests —
~21k lines of hier):

| annotation kind | count | what HM would do |
|---|---:|---|
| `x := e` local decls (already inferred) | 1730 | nothing — already free |
| fn signatures (params + return) | 586 | could elide — see §4, hier shouldn't |
| empty literals `[]int` / `[]K: V` | 289 | infer from later use — needs only backward flow, not HM |
| typed decls `x : T = e` | 69 | mostly `Option`/`Result` context-fixing; backward flow covers |
| bare `None` needing an annotation | 6 | the canonical HM win — six occurrences |

The burden HM uniquely removes is measured in dozens of sites, in a corpus
where seventeen hundred declarations are already annotation-free. The
biggest real annoyance — empty literals, 289 sites — needs *expected types
at declaration and argument positions*, which is bidirectional
propagation. Unification machinery is not required for any of it.

## 3. Why full HM is infeasible here

**(a) Generics are explicit, not inferred.** HM's center of gravity is
let-generalization: `id` gets type `forall a. a -> a` and every use
instantiates it — so *every* generalizable function silently becomes
generic. hier's generics are the opposite — **opt-in and explicit** (`$T`,
Odin-style), inferred by one-directional structural matching and
monomorphized ([generics.md](generics.md)). HM minus generalization is not
HM; it is local unification (§4), which bidirectional inference already
covers without a union-find. HM *with* generalization would make the first
`fn first(xs): return xs[0]` implicitly generic and demand type variables
to track it pervasively, whereas hier monomorphizes only the `$T` sites
the programmer marked. Two consequences follow:

- **monomorphization is fine — it is bounded.** It is the
  per-instantiation specialization pass generics already use (in both
  compilers, composing with packages, lifted lambdas, and spawn-site
  trampolines), but driven by explicit `$T`, not by generalizing every
  function — so it stays an opt-in phase, not a whole-program obligation.
- **uniform boxing is not.** The other way HM could erase types — boxing
  every value behind pointers so one body serves all — destroys the memory
  model. `type_is_heap`, `copy_into`/`cp_field` dispatch, per-type channel
  wrappers, move-on-last-use, recycling, SOA — every load-bearing
  optimization keys on *ground monotypes at every site*. Boxing is the
  thing the whole thesis exists to avoid paying, which is exactly why
  hier's generics monomorphize.

**(b) Type-directed features break under deferred types.** Resolution in
both compilers is not a pure annotation pass — it REWRITES the AST using
types the moment it sees them: UFCS dispatch picks the method by the
receiver's type (`x.foo(a)` -> `foo(x, a)` only if `foo`'s first param
matches `typeof(x)`); fn-typed struct fields win over methods; `map_of`
picks the map family; spawn sites validate and register against the
callee's signature; enum constructors and struct literals reinterpret call
nodes. Under unification the receiver's type may be an unsolved variable
when the call is encountered, so dispatch must be deferred, which turns
resolve into constraint-generation + solve + a second rewrite pass — a
structural rewrite of ~1.5k lines of resolve logic, twice. (This ordering
problem is exactly why Go chose forward-only inference, and why Swift's
bidirectional constraint solver is a notorious source of slow compile
times.)

**(c) The dual-compiler cost.** Unification needs type variables and a
substitution (union-find). hierc's `Type` is an interned int — type vars
are addable. hierc0's types are *strings* (`"Option(int)"`, `"[str]"`);
variables become `"?17"` strings with a substitution map and occurs checks
over string parsing. Entirely possible — and a large, subtle,
performance-sensitive component that would have to be built twice and kept
byte-identical between the two compilers.

**(d) Error quality.** hier invested in diagnostics — caret, source line,
did-you-mean suggestions. Unification failures surface far from their cause
("expected ?3 but got [int]" two functions later); keeping errors local is
a known hard problem in HM compilers. Forward inference's errors are always
at the offending line.

## 4. What about HM-without-generalization (function-local unification)?

Rust-style: signatures stay mandatory and monomorphic, but within a body
types may be variables solved by unification before codegen. Feasible — no
boxing, no monomorphization, signatures keep package boundaries and the Sig
table exactly as today. What it would buy over targeted backward flow:

- `x := []` with the element type discovered from a later `push(x, 3)`;
- `x := None` fixed by a later `x = Some(5)`;
- declaring before first meaningful use generally.

Cost: the two-phase resolve restructuring from §3(b) (UFCS/dispatch still
needs solved receivers), the dual implementation, and codegen ordering —
copy/move/accumulator analyses must run post-solve. That is a multi-week
effort for the delta between "later use" and "annotate the decl" — a delta
the corpus prices at well under a hundred sites. **Feasible, not worth
it.**

Function *parameter* elision (`fn f(x): ...`) is deliberately excluded even
from this option: signatures are hier's module interface (packages build
the Sig table from them up front, before bodies), they are what keeps
errors local, and eliding them re-imports the generalization problem the
moment two call sites disagree.

## 5. The model: bidirectional (local) type inference

Hier uses **bidirectional typing** (Pierce & Turner's *Local Type
Inference*, 2000) — the approach taken in practice by Kotlin, Scala,
TypeScript's contextual typing, and operationally by Zig's result-location
semantics. Two modes, no type variables ever:

- **synthesis (⇑)** — bottom-up, "what type does this expression have?" —
  what hier does everywhere today;
- **checking (⇓)** — top-down, "this expression sits in a position whose
  type is known; push that expectation into it."

Hier already runs checking mode in a handful of places — `resolve_exp(e,
expected)` is what fixes `None` against `Option(T)`, completes partial
`Ok`/`Err` against return types, and types match payloads. The design
makes this channel *systematic*: every position with a known destination
type supplies an expectation, and a defined set of expressions consume one.

**Positions that supply an expected type:** typed decls, assignment to an
existing variable, call arguments (the param type), `return` (the fn's
return type), field-set / index-set / `push` / `send` (the field / element
/ payload type), struct, enum, tuple, and array-literal construction
arguments (component-wise), and the right operand of `==` (the left
synthesizes).

**Expressions that consume one:**

| expression | under expectation | example that becomes legal |
|---|---|---|
| `[]` / `[:]` empty literals | takes the expected array/map type | `xs : [int] = []`, `f([])`, `return []`, `push(grid, [])` |
| `None` / `Ok(v)` / `Err(e)` | completes against the expected `Option`/`Result` | — |
| integer literals | adapt to `float` in a float context (value-directional, literals only — never variables, never float→int) | `f + 2`, `sqrt(2)`, `scale(xs, 3)` |
| array/map/tuple literals | recurse the expectation into elements | `[None, Some(3)]`, `((1, 2.5), [])` |
| lambda literals | params + return from the expected `fn` type | `iter.map(xs, fn(x): x * 2)` |

**Why this fits hier where HM doesn't:**

- *Ground monotypes at every line.* An expression is typed the moment it is
  visited — synthesized or checked, never deferred. `type_is_heap`,
  `copy_into`, the monomorphized families, move-on-last-use: all untouched.
- *Type-directed dispatch stays eager.* UFCS receivers always
  **synthesize** (the rule), so `x.foo()` resolves exactly as today;
  expectations only flow *down* into literal-ish constructs that cannot
  dispatch anything.
- *Error locality.* If neither mode grounds a type, the error is at that
  line — "cannot type `[]` here: no expected type" — never a unification
  residue two functions away.
- *Per-site, in both compilers.* Each rule is an extension of the existing
  mechanism rather than a new solver. (hierc0 note: its `type_of`/`gen_rhs`
  lack an expected parameter — the expectation is threaded as a `want:
  string` ("" = none) through the consuming sites; contained, unlike a
  solver.)

## 6. What stays annotated, and the grounding rule

**Function signatures stay annotated.** They are the module interface;
packages build the Sig table from them before any body is typed, and they
are what keeps errors local.

Beyond signatures, the only remaining case is a *bare* incomplete decl with
no context on the same line — `xs := []` or `t := None`. For these, hier
applies a flow-sensitive **block-local grounding** rule (no unification):
the decl defers, and its first *grounding* use in the same block fixes the
type retroactively.

The grounding rule (identical in both compilers, fixture-locked): a pending
decl grounds at its first occurrence in any of —

- an assignment to it (`o = Some(5)`),
- `push` on it,
- a typed argument position (named fn, fn-typed variable or
  call-on-expression, struct/enum construction, `send`, `spawn`),
- `return`,
- a field/element store,
- or an array-literal element position (typed by the literal's first
  element).

Example: `xs := []` followed later in the block by `push(xs, 3)` grounds
`xs` to `[int]`.

Anything else that needs the type *before* it is grounded raises the local
"used before its type can be inferred" error at that use. A block that ends
with the decl still pending errors at the declaration. There are no type
variables: this is a flow-sensitive forward scan, not unification — hierc
keeps a pending registry through its in-order resolve, while hierc0
rewrites the decl to its annotated form via a forward seek in the lift.

## 7. Summary

- **Full HM: rejected.** Let-generalization is generics by the back door
  (hier's generics are explicit and monomorphized instead); its escape
  hatches are a dual-compiler monomorphization phase or boxing that attacks
  the arena memory model; deferred types break the resolve-time
  type-directed rewrites in both compilers.
- **Local (function-body) unification: feasible, declined.** Most of HM's
  cost for dozens of sites of benefit.
- **Bidirectional local inference: adopted.** It is hier's existing
  `resolve_exp` channel made systematic — no type variables, no solver,
  ground monotypes at every line, eager dispatch, local errors. Inference
  spreads through the cases described above (empty literals, int-literal
  adaptation, lambda elision, and block-local grounding for bare incomplete
  decls), all implemented in both compilers and held to byte-identical
  output by the self-hosting fixpoint and the differential test suite.
