# Hindley-Milner type inference for hier — feasibility study

Status: DECIDED AND SHIPPED — **all four stages, both compilers.** B-0
(bare `[]` consumes expectations at decls/args/returns/stores/elements),
B-1 (int literals adapt to float contexts, literals only), B-2 (lambda
param/return elision from expected fn types), and B-3 (block-local
grounding: a bare `xs := []` / `x := None` declares pending and its FIRST
grounding use in the block — assignment, push, or any expected-type
position — types it retroactively; a use needing the type first, or a block
ending with it still pending, errors locally at that line). Still no type
variables: B-3 is a flow-sensitive forward scan, not unification — hierc
keeps a pending registry through its in-order resolve; hierc0 rewrites the
decl to its annotated form via a forward seek in the lift. Gated by
tests/inference.hi (through the fixpoint differential, both compilers) and
tests/reject/infer_*.hi.

**The grounding contract (both compilers, fixture-locked):** a pending decl
grounds at its first occurrence in any of — an assignment to it (`o =
Some(5)`), `push` on it, a typed argument position (named fn,
fn-typed variable or call-on-expression, struct/enum construction, `send`,
`spawn`), `return`, a field/element store, or an array-literal element
position (typed by the literal's first element). Anything else that needs
the type first is the local "used before its type can be inferred" error; a
block ending with the decl still pending errors at the declaration. The
fuzzer's `infer_*` kinds exercise the contract differentially.

Original investigation below. Question: should hier adopt
HM-style inference — type variables, unification, let-generalization,
principal types? Verdict up front: **full HM is infeasible for hier by its
own prior decisions, function-local unification is feasible but poor ROI,
and the real annotation pain is better served by spreading the
expected-type (bidirectional) channel hier already has.** Numbers and
reasoning below.

## 1. What hier infers today

Locals are already inferred: `x := e` types `x` from `e`, forward-only,
bottom-up (`resolve_expr` in src/hierc.c; `type_of` in compiler/hierc0.hi).
And hier already has ONE backward channel: `resolve_exp(e, expected)`
pushes an expected type down at specific sites — it is what fixes a bare
`None` against a declared `Option(T)`, completes `Ok(v)`/`Err(e)` partial
Results against a function's return type (the `T_NONE`/`T_OK_PARTIAL`
sentinels), and types `return` expressions. So the question is not
"inference: yes/no" — hier has forward inference plus a one-step
bidirectional channel. HM would add *type variables + unification +
generalization*.

## 2. The measured annotation burden

Across the full corpus (compiler/hierc0.hi, corelib, examples, tests —
~21k lines of hier):

| annotation kind | count | what HM would do |
|---|---:|---|
| `x := e` local decls (already inferred) | 1730 | nothing — already free |
| fn signatures (params + return) | 586 | could elide — see §4, we shouldn't |
| empty literals `[]int` / `[]K: V` | 289 | infer from later use — needs only backward flow, not HM |
| typed decls `x : T = e` | 69 | mostly `Option`/`Result` context-fixing; backward flow covers |
| bare `None` needing an annotation | 6 | the canonical HM win — six occurrences |

The burden HM uniquely removes is measured in dozens of sites, in a corpus
where seventeen hundred declarations are already annotation-free. The
biggest real annoyance (empty literals, 289 sites) needs *expected types at
declaration-and-argument positions*, which is bidirectional propagation —
unification machinery is not required for any of it.

## 3. Why full HM is infeasible here

**(a) Generics are explicit, not inferred.** HM's center of gravity is
let-generalization: `id` gets type `forall a. a -> a` and every use
instantiates it — so *every* generalizable function silently becomes generic.
hier's generics are the opposite — **opt-in and explicit** (`$T`, Odin-style),
inferred by one-directional structural matching and monomorphized
([generics.md](generics.md)). HM minus generalization is not HM; it is local
unification (§4), which bidirectional inference already covers without a
union-find. HM *with* generalization would make the first
`fn first(xs): return xs[0]` implicitly generic and demand type variables to
track it pervasively, whereas hier monomorphizes only the `$T` sites the
programmer marked. Two consequences follow:

- **monomorphization is fine — bounded.** It is the per-instantiation
  specialization pass generics already use (in both compilers, composing with
  packages, lifted lambdas, spawn-site trampolines, and the byte-identical
  fixpoint), but driven by explicit `$T`, not by generalizing every function —
  so it stays an opt-in phase, not a whole-program obligation.
- **uniform boxing is not.** The other way HM could erase types — boxing every
  value behind pointers so one body serves all — destroys the memory model.
  `type_is_heap`, `copy_into`/`cp_field` dispatch, per-type channel wrappers,
  move-on-last-use, recycling, SOA — every load-bearing optimization keys on
  *ground monotypes at every site*. Boxing is the thing the whole thesis exists
  to avoid paying, which is exactly why hier's generics monomorphize.

**(b) Type-directed features break under deferred types.** Resolution in
both compilers is not a pure annotation pass — it REWRITES the AST using
types the moment it sees them: UFCS dispatch picks the method by the
receiver's type (`x.foo(a)` -> `foo(x, a)` only if `foo`'s first param
matches `typeof(x)`); fn-typed struct fields win over methods; `map_of`
picks the map family; spawn sites validate and register against the
callee's signature; enum constructors and struct literals reinterpret
call nodes. Under unification the receiver's type may be an unsolved
variable when the call is encountered, so dispatch must be deferred,
which turns resolve into constraint-generation + solve + a second rewrite
pass — a structural rewrite of ~1.5k lines of resolve logic, twice. (This
ordering problem is exactly why Go chose forward-only inference, and why
Swift's bidirectional constraint solver is a notorious source of slow
compile times.)

**(c) The dual-compiler tax.** Unification needs type variables and a
substitution (union-find). hierc's `Type` is an interned int — type vars
are addable. hierc0's types are *strings* (`"Option(int)"`, `"[str]"`);
variables become `"?17"` strings with a substitution map and occurs
checks over string parsing. Entirely possible — and a large, subtle,
performance-sensitive component built twice and locked by the fixpoint.

**(d) Error quality.** hier just invested in diagnostics (caret, source
line, did-you-mean). Unification failures surface far from their cause
("expected ?3 but got [int]" two functions later); keeping errors local is
a known hard problem in HM compilers. Forward inference's errors are
always at the offending line.

## 4. What about HM-without-generalization (function-local unification)?

Rust-style: signatures stay mandatory and monomorphic, but within a body
types may be variables solved by unification before codegen. Feasible —
no boxing, no monomorphization, signatures keep package boundaries and
the Sig table exactly as today. What it would buy over targeted backward
flow:

- `x := []` with the element type discovered from a later `push(x, 3)`;
- `x := None` fixed by a later `x = Some(5)`;
- declaring before first meaningful use generally.

Cost: the two-phase resolve restructuring from §3(b) (UFCS/dispatch still
needs solved receivers), the dual implementation, and codegen ordering:
copy/move/accumulator analyses must run post-solve. That is a multi-week
effort for the delta between "later use" and "annotate the decl" — a
delta the corpus prices at well under a hundred sites. **Feasible, not
worth it.**

Function *parameter* elision (`fn f(x): ...`) is deliberately excluded
even from this option: signatures are hier's module interface (packages
build the Sig table from them up front, before bodies), they are what
keeps errors local, and eliding them re-imports the generalization
problem the moment two call sites disagree.

## 5. The answer: bidirectional (local) type inference

If not HM, then the mechanism is **bidirectional typing** (Pierce &
Turner's *Local Type Inference*, 2000) — the road taken in practice by
Kotlin, Scala, TypeScript's contextual typing, and operationally by Zig's
result-location semantics. Two modes, no type variables ever:

- **synthesis (⇑)** — bottom-up, "what type does this expression have?" —
  what hier does everywhere today;
- **checking (⇓)** — top-down, "this expression sits in a position whose
  type is known; push that expectation into it."

hier already runs checking mode in a handful of places — `resolve_exp(e,
expected)` is what fixes `None` against `Option(T)`, completes partial
`Ok`/`Err` against return types, and types match payloads. The design is
to make this channel *systematic*: every position with a known
destination type supplies an expectation, and a defined set of
expressions consume one.

**Positions that supply an expected type** (most already plumbed):
typed decls, assignment to an existing variable, call arguments (the
param type), `return` (the fn's return type), field-set / index-set /
`push` / `send` (the field / element / payload type), struct, enum,
tuple, and array-literal construction arguments (component-wise), and
the right operand of `==` (the left synthesizes).

**Expressions that consume one:**

| expression | under expectation | example that becomes legal |
|---|---|---|
| `[]` / `[:]` empty literals | takes the expected array/map type | `xs : [int] = []`, `f([])`, `return []`, `push(grid, [])` |
| `None` / `Ok(v)` / `Err(e)` | already shipped | — |
| integer literals | adapt to `float` in a float context (value-directional, literals only — never variables, never float→int) | `f + 2`, `sqrt(2)`, `scale(xs, 3)` |
| array/map/tuple literals | recurse the expectation into elements | `[None, Some(3)]`, `((1, 2.5), [])` |
| lambda literals | params + return from the expected `fn` type | `iter.map(xs, fn(x): x * 2)` |

**Why this fits hier where HM doesn't:**

- *Ground monotypes at every line.* An expression is typed the moment it
  is visited — synthesized or checked, never deferred. `type_is_heap`,
  `copy_into`, the monomorphized families, move-on-last-use: all
  untouched.
- *Type-directed dispatch stays eager.* UFCS receivers always
  **synthesize** (the rule), so `x.foo()` resolves exactly as today;
  expectations only flow *down* into literal-ish constructs that cannot
  dispatch anything.
- *Error locality.* If neither mode grounds a type, the error is at that
  line — "cannot type `[]` here: no expected type" — never a unification
  residue two functions away.
- *It is the same implementation shape hier always lands*: per-site
  extensions of an existing mechanism, in both compilers, gated by
  test/fixpoint. (hierc0 note: its `type_of`/`gen_rhs` lack an expected
  parameter — thread a `want: string` ("" = none) through the consuming
  sites; contained, unlike a solver.)

**What stays annotated — the seeds.** Function signatures (they are the
module interface; packages build the Sig table from them before any body
is typed, and they are what keeps errors local). And a *bare*
`xs := []` or `t := None` with no context on the same line — annotate the
decl or initialize meaningfully... unless stage B-3:

**Optional extension — block-local grounding (B-3).** For exactly the
bare-incomplete-decl case, a flow-sensitive rule with no unification: the
decl defers, the first *grounding* use in the same block fixes the type
(`xs := []` ... `push(xs, 3)` → `[int]`), and any use before grounding
that needs the type is an error at that use. Implementable as a two-pass
over the statement list. Honest assessment: moderate complexity for the
last few dozen sites — do it only if B-0..B-2 leave it noticeable.

## 6. Staged plan (when ergonomics demand)

| stage | content | wins (corpus) |
|---|---|---|
| B-0 | empty literals consume expectations at every supplying position | the 289 `[]T` annotations |
| B-1 | untyped integer literals adapt in float checking contexts | every `x + 2.0`-style wart |
| B-2 | lambda param/return elision from expected fn types | closure call sites (core:iter et al) |
| B-3 | (optional) block-local grounding for bare incomplete decls | the residue |

Each stage: both compilers, `make test`/`fixpoint` gates, `tests/diag`
goldens for the new "no expected type here" errors.

## 7. Bottom line

- **Full HM: no.** Let-generalization is generics by the back door
  (rejected); its escape hatches are a dual-compiler monomorphization
  phase or boxing that attacks the arena thesis; deferred types break the
  resolve-time type-directed rewrites in both compilers.
- **Local unification: feasible, declined.** Most of HM's cost, dozens of
  sites of benefit.
- **Bidirectional local inference: yes — it is hier's existing
  `resolve_exp` channel made systematic.** No type variables, no solver,
  monotypes at every line, eager dispatch, local errors, per-stage
  landings. That is "how else".
