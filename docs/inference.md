# Hindley-Milner type inference for hier — feasibility study

Status: INVESTIGATION (nothing implemented). Question: should hier adopt
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

**(a) No generics — by decision.** HM's center of gravity is
let-generalization: `id` gets type `forall a. a -> a` and every use
instantiates it. hier rejected generics deliberately
(the corelib is concrete free functions per element type; the array
families are monomorphized `Arr_T` per element type). HM minus
generalization is not HM — it is local unification (§4). HM *with*
generalization reverses a settled language decision through the back door:
the first `fn first(xs): return xs[0]` the inferencer generalizes IS a
generic function, and then either

- **monomorphization**: a whole-program specialization pass duplicating
  functions per instantiation — a new compiler phase, done twice (both
  compilers), interacting with packages, lifted lambdas, spawn-site
  trampolines, and the byte-identical fixpoint; or
- **uniform representation**: boxing every value behind pointers so one
  body serves all types — which destroys the memory model. `type_is_heap`,
  `copy_into`/`cp_field` dispatch, per-type channel wrappers, move-on-last-
  use, recycling, SOA — every load-bearing optimization keys on *ground
  monotypes at every site*. Boxing is the thing the whole thesis exists to
  avoid paying.

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
ordering problem is exactly why Go chose forward-only inference and why
Swift's bidirectional constraint solver is its compile-time bogeyman.)

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
campaign for the delta between "later use" and "annotate the decl" — a
delta the corpus prices at well under a hundred sites. **Feasible, not
worth it.**

Function *parameter* elision (`fn f(x): ...`) is deliberately excluded
even from this option: signatures are hier's module interface (packages
build the Sig table from them up front, before bodies), they are what
keeps errors local, and eliding them re-imports the generalization
problem the moment two call sites disagree.

## 5. Recommendation: spread the existing bidirectional channel

The `resolve_exp(e, expected)` mechanism already in both compilers covers
the real pain if extended to more positions — no type variables, no
unification, no new phase ordering, errors stay on the line:

1. **Empty literals from expected type** (the 289 sites):
   `xs : [int] = []`, `f([])` against a `[int]` param, `return []`
   against the return type, `push`-arg positions. The literal types
   itself from context exactly like `None` already does.
2. **`channel(T, cap)` element elision where the decl is annotated**
   (`ch : Channel(int) = channel(4)`) — symmetric with Option today.
   (Low value; the current form is fine.)
3. **Lambda parameter elision against an expected fn type**:
   `apply(fn(x): x + 1, 5)` where `apply` declares `fn(int)->int` — the
   expected type supplies param types. This is classic *bidirectional*
   typing (Pierce-Turner local type inference), not HM, and it composes
   with everything because the lambda's type is fully determined before
   its body resolves.
4. Keep `Some`/`Ok`/`Err`/`None` context-fixing as is; extend the same
   sentinel trick to array literals of `None` (`[None, Some(3)]`) if it
   ever bites.

Each item is a contained, per-site change in both compilers with the
existing test/fixpoint gates — the same shape as every shipped feature —
rather than a cross-cutting inference engine.

## 6. Bottom line

- **Full HM: no.** It contradicts the no-generics decision, and its only
  escape hatches (monomorphization pass / uniform boxing) are
  respectively a huge dual-compiler phase and a direct attack on the
  arena thesis. Type-directed UFCS dispatch and the resolve-time AST
  rewrites make deferred typing a structural rewrite of both compilers.
- **Local unification: feasible, declined.** All of HM's implementation
  cost minus most of its benefit; the corpus shows the annotations it
  would remove number in the dozens.
- **Do instead:** extend the existing expected-type channel to empty
  literals and lambda parameters (§5) when ergonomics demand it. That is
  bidirectional *checking*, hier already does it for Option/Result, and
  it delivers the visible wins at per-feature cost.
