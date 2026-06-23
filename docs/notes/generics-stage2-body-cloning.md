# Generics Stage-2: non-trivial generic-function bodies

> Status: **plan for review — nothing implemented.** This document is for the
> maintainer to approve or amend. Every claim about current behaviour was
> verified by reading the cited `path:line` and/or by running the compiler on a
> reproduction; the reproductions are quoted verbatim. Where a claim is an
> assumption it is flagged as such.

## Summary & recommendation

Stage-1 generics (binding-based monomorphization) ships in both compilers and
works for bodies that only *use* type parameters (e.g. `fn id(x: $T) -> T: x`).
Three patterns a generic corelib needs do **not** work yet:

1. A generic that builds a fresh collection (`out := []` then `push`),
   instantiated for more than one type.
2. A typaram-typed local annotation (`out : [$T] = []`).
3. A generic calling another (unqualified) generic.

**Key finding that reframes the whole task: the two compilers do NOT both fail
all three. They have already silently diverged on generics, and no fixture
exercises the divergence.** Verified by running each compiler on minimal
reproductions (see "Why the three patterns fail"):

| Pattern | `tychoc` (C, `src/tychoc.c`) | `tychoc0` (self-hosted, `compiler/tychoc0.ty`) |
| --- | --- | --- |
| #1 fresh collection, multi-type | **FAIL** `push's value must be int` | **PASS** (prints `3` / `2`) |
| #2 `out : [$T] = []` | **FAIL** `declared type [void] but value is [int]` | **PASS** |
| #3 nested generic call | **FAIL** (missing prototype / `unknown procedure`) | **FAIL** `unknown function 'id'` |

The reason for the asymmetry is architectural and is the crux of the whole
design:

- `tychoc` keeps **one shared template AST** and re-resolves it per instance at
  codegen, writing resolved types *back into that shared AST*
  (`src/tychoc.c:8204`-`8215`). Resolution is destructive and sticky, so the
  second instance inherits the first instance's grounding/decl types/rewritten
  call names.
- `tychoc0` already does a **source-to-source AST clone + type-string
  substitution per instance** (`mono_instantiate` → `subst_block_t`,
  `compiler/tychoc0.ty:9049`, `:8854`). Each instance is an independent `Func`,
  so #1 and #2 fall out for free. It only misses #3 because instance bodies are
  appended without a second mono pass to discover nested generic calls
  (`compiler/tychoc0.ty:9603`).

**Recommendation: adopt `tychoc0`'s already-shipping approach as the canonical
Stage-2 model and bring `tychoc` up to it — i.e. Option (i), full per-instance
body clone + substitute — rather than trying to make `tychoc`'s shared-body
re-resolve correct (Option (ii)).** Reasons:

- Option (i) is *already implemented and proven self-hosting* in `tychoc0`. We
  are not inventing the "riskiest part" (the AST cloner) from scratch; it
  exists, is exercised by every generic fixture today, and survives the
  fixpoint. The work in `tychoc0` is small (drive nested calls). The bulk of the
  work is in `tychoc`, where we replace the shared-body re-resolve with a clone.
- Option (ii) keeps the destructive-shared-AST hazard that is *the* source of
  bugs #1–#3 in `tychoc`. Making "reset everything between instances" airtight
  (every `T_PENDING` slot, every `e->ival`, every rewritten `e->sval`, every
  `s->decl_type`) is a whack-a-mole over the entire resolver — strictly higher
  risk for strictly less determinism guarantee.
- Fixpoint requires byte-identical C from both compilers. `tychoc0` is the
  reference for what that C looks like today; matching *its* model in `tychoc` is
  the shortest path to keeping `cB.c == cA.c`.

The plan below is therefore: **(a) finish #3 in `tychoc0`** (small, ~1 function),
**(b) reimplement generic instances in `tychoc` as cloned-and-substituted
`Proc`s** so #1/#2/#3 all work there, **(c) lock the two together with shared
fixtures + fixpoint**, staged smallest-risk-first.

---

## Current mechanism

### `tychoc` (C reference) — shared template, re-resolved per instance

- `$T` is a transient type id `>= T_TYPARAM_BASE` (`src/tychoc.c:536`,
  `:570`-`:578`); the comment states "`T_TYPARAM` never reaches codegen".
- A `$T`-bearing function is held out of the `Sig` table in a separate generics
  registry (`g_generics`, `src/tychoc.c:2940`-`2951`; registered at
  `resolve_program`, `:4985`-`:4992`).
- A call site resolves to a generic via `generic_find` and calls
  `instantiate_generic` (`src/tychoc.c:3909`-`3910`).
- `instantiate_generic` (`src/tychoc.c:4893`): infers bindings by structural
  `match_type` of each parameter pattern against the concrete arg type
  (`:4905`-`:4911`); checks `where` constraints (`:4914`-`:4936`,
  `constraint_ok` at `:4886`); builds the concrete signature + mangled name via
  `subst_type` (`:1148`) + `type_mangle_ident`; **rewrites the call:
  `e->sval = nm` (`:4914`)**; dedups by `sig_find(nm)` (`:4915`); registers a
  real `Sig` and a `GInst` recording `tmpl`, `name`, concrete `params`/`ret`,
  and the `binds` array (`:4956`-`:4964`).
- `GInst` (`src/tychoc.c:2958`-`2959`) carries `Proc *tmpl` (the **shared**
  template) and `Type *binds`.
- `ginst_to_proc` (`src/tychoc.c:7539`) materializes an instance `Proc` by
  shallow-copying the template (`*p = *gi->tmpl;`, `:7541`) — **the body pointer
  is shared with the template and with every other instance** — and overwriting
  only name/ret/param types.
- Codegen (`gen_program`):
  - prototypes are emitted for each `GInst` (`src/tychoc.c:8126`-`8127`);
  - then instance **bodies are resolved + emitted sequentially**
    (`src/tychoc.c:8204`-`8216`): `g_active_binds = g_ginsts[i].binds;` then
    `resolve_block(p->body, ...)` then `gen_proc`. The comment at `:8202` is
    explicit: "resolve+emit each monomorphic instance from the SHARED template
    body, sequentially — the body's resolved types are consumed by gen_proc
    before the next instance re-resolves it with different bindings."
  - the *only* place `$T` substitution reaches the body today is an explicit
    `[]T` empty literal: `e->type = subst_type(e->ival, g_active_binds)`
    (`src/tychoc.c:3355`-`3356`). Nothing substitutes a `$T` in a *typed* local
    decl (`s->annot`), and grounding state is not reset between instances.

### `tychoc0` (self-hosted) — source-to-source clone + substitute

- Generics are monomorphized in a dedicated pass `monomorphize_program`
  (`compiler/tychoc0.ty:9559`) that runs over the whole `Program` and returns a
  new `Program` with templates dropped and concrete instances appended.
- It walks every concrete function's body with `mono_block`/`mono_stmt`/
  `mono_expr` (`compiler/tychoc0.ty:9549`, `:9475`, `:9328`). At an `ECall`
  whose name is a generic (`find_generic`), it computes arg types
  (`type_of`), then calls `mono_instantiate` and **rewrites the call name** to
  the instance (`compiler/tychoc0.ty:9373`-`9379`).
- `mono_instantiate` (`compiler/tychoc0.ty:8930`): infers bindings via
  `match_typaram_str` (`:8728`) or explicit type args; checks `where`
  constraints (`:8970`-`9007`, `constraint_ok_str` at `:8920`); substitutes the
  signature; dedups against `insts` by name (`:9032`-`9035`); and pushes a fully
  concrete `Func` whose body is **a deep clone with typarams substituted**:
  `subst_block_t(gt.body, bnames, btypes)` (`compiler/tychoc0.ty:9049`).
- `subst_block_t`/`subst_stmt_t`/`subst_expr_t` (`compiler/tychoc0.ty:8854`,
  `:8862`, `:8803`) deep-copy the AST and rewrite the typaram type strings in the
  nodes that carry an explicit type: `EArrEmpty` (`:8849`), `EMapEmpty`
  (`:8851`), `STypedDecl` (`:8897`), `ELambda` (`:8843`). `subst_body_type`
  (`:8784`) first re-applies the bare→`$` rewrite (`subst_bare_typaram`,
  `:1459`) then the `$`-keyed concrete substitution (`subst_field_type`,
  `:9052`). **This is why #1 and #2 already pass in `tychoc0`.**
- Naming: `inst_name` (`compiler/tychoc0.ty:8701`) + `gen_inst_mangle`
  (`:8674`). `gen_inst_mangle` deliberately maps `"str" -> "string"` and arrays
  to `arr_<E>` etc. "byte-for-byte" with `tychoc`'s `type_mangle_ident`
  (`src/tychoc.c:4849`) so the two compilers agree on instance names.
- Instances are appended to the output `in first-encounter order (matches
  tychoc)` (`compiler/tychoc0.ty:9603`-`9605`) and **not re-driven through
  `mono_block`** — the gap behind #3.

---

## Why the three patterns fail (verified)

Reproductions were run with `./tychoc` (committed binary) and a `tychoc0`
binary built via `./tychoc compiler/tychoc0.ty -o tychoc0` (the same A-stage the
fixpoint uses), feeding the program on stdin.

### Pattern #1 — fresh collection, multiple instantiations

```tycho
fn dup(xs: [$T]) -> [T]:
    out := []
    i := 0
    for i < len(xs):
        push(out, xs[i]); i = i + 1
    return out
fn main():
    a := dup([1, 2, 3]); b := dup(["x", "y"])
```

- `tychoc`: `error: push's value must be int` at the `push`. **Root cause:**
  `out := []` is a bare-`[]` decl, so it goes through B-3 deferred grounding —
  registered in `g_pend` and set `T_PENDING` (`src/tychoc.c:4528`-`4535`). The
  first instance (`dup__int`) grounds `out` to `[int]` via the `push`
  (`pend_ground`, `src/tychoc.c:3842`-`3843`) and writes `s->decl_type = [int]`
  into the **shared** AST (`:4556`). When `dup__string` re-resolves the *same*
  AST (`:8214`), the decl's grounding/`decl_type` state is stale `[int]`, so the
  string `push` is checked against an `int` array. The bare-`[]` element type is
  *not* a `$T` carrier (it is `T_VOID` awaiting grounding, `:4530`), so the
  `g_active_binds` substitution at `:3355` never applies to it. Single-type works
  (verified: `dup([1,2,3])` alone prints `3`) because there is only one
  grounding.
- `tychoc0`: **PASS** (`3` / `2`). Each instance is an independent cloned `Func`
  with its own `out := []`, grounded fresh during that `Func`'s normal type
  pass. No shared state.

### Pattern #2 — typaram-typed local annotation

```tycho
fn dup(xs: [$T]) -> [T]:
    out : [$T] = []
    ...
```

- `tychoc`: `error: declared type [void] but value is [int]`. **Root cause:**
  the annotation is parsed once into `s->annot` (`src/tychoc.c:2347`-`2348`) as
  a type containing the `$T` id, and the decl check uses `s->annot` **raw**
  (`:4537` `resolve_exp(s->expr, s->annot)`, compared at `:4540`, assigned at
  `:4543`). There is no `subst_type(s->annot, g_active_binds)` anywhere, so `$T`
  collapses to `[void]` (an unbound typaram mangles/prints as void here).
- `tychoc0`: **PASS.** `subst_stmt_t`'s `STypedDecl` arm substitutes the
  annotation: `STypedDecl(n, subst_body_type(ty, bn, bt), ...)`
  (`compiler/tychoc0.ty:8897`).

### Pattern #3 — generic calling another generic

```tycho
fn id(x: $T) -> T: return x
fn twice(x: $T) -> T: return id(x)
fn main(): println(str(twice(5))); println(twice("hi"))
```

- `tychoc`: emitted C **defines** `h_id__int` (verified: body at
  `p3.c:1747`) but the call site `h_id__int(...)` (`p3.c:1744`) has **no
  prototype**, so `cc` errors `implicit declaration of function 'h_id__int'`.
  **Root cause is ordering, not discovery:** `twice__int`'s body is resolved at
  `src/tychoc.c:8214`, and *that* resolution is what runs `instantiate_generic`
  for `id` (rewriting `e->sval = "id__int"`, appending a `GInst`). But the
  per-instance prototype loop already ran earlier (`:8126`-`8127`), so the newly
  discovered `id__int` instance never gets a prototype. The body-emit loop
  (`:8204`) re-reads `g_nginsts` each iteration so it *does* emit `id__int`'s
  body — hence "defined but not declared". (For the *cross-package / qualified*
  variant noted in the task, the symptom is instead `unknown procedure`, because
  the qualified-call path at `src/tychoc.c:3577`-`3579` resolves the qualifier
  to a generic but the instance Sig is not yet interned at that resolve point.)
- `tychoc0`: `line 5: type: unknown function 'id'`. **Root cause:** the cloned
  body of `twice__int` still literally contains `ECall("id", ...)` — `mono_expr`
  rewrote the *outer* `twice` call but the instance body is appended raw
  (`compiler/tychoc0.ty:9603`) and never re-driven through `mono_expr`, so the
  inner `id` call is never instantiated/renamed. At codegen `id` is not a known
  function.

---

## Stage-2 design (chosen option: full per-instance clone + substitute)

The two compilers need different work because one already has the cloner.

### `tychoc0` changes (small — finish #3)

The clone + substitution machinery is complete; only nested-call discovery is
missing. Two viable shapes:

- **Preferred — iterate instances to a fixpoint in `monomorphize_program`.**
  Replace the plain append loop (`compiler/tychoc0.ty:9603`-`9605`) with a
  worklist: process `insts[i]` by running `mono_block` over its (already
  type-substituted) body, which discovers and interns nested generic calls (more
  `insts` may be appended); continue until the index reaches the end of a list
  that has stopped growing. `mono_instantiate` already dedups by name
  (`:9032`-`9035`), so the worklist terminates for non-recursive and
  mutually-recursive generics; recursion at the *same* concrete type is a dedup
  hit (terminates), recursion at *strictly growing* types is unbounded and must
  be diagnosed (see "recursion" below).
  - Note the ordering subtlety: today the body is `subst_block_t`'d *then*
    appended. With the worklist, each instance body must be run through
    `mono_block` to rename nested generic calls. The cleanest sequencing is:
    `mono_instantiate` pushes the *substituted-but-not-yet-mono'd* body; the
    worklist then `mono_block`s it. The type environment for that `mono_block`
    is the instance's concrete params (seed via `seed_env` over the instance
    `Func`), so `type_of` inside resolves against concrete types — this is what
    lets `id(x)`'s arg type be known.
- Alternative (rejected): make `mono_instantiate` itself recursively `mono_block`
  the body before pushing. This re-enters `mono_instantiate` mid-push and makes
  the dedup/ordering harder to reason about than an explicit worklist.

`$T` already reaches local typed decls (`STypedDecl`, `:8897`) and bare/empty
literals (`EArrEmpty`/`EMapEmpty`, `:8849`/`:8851`); the worklist adds nested
call sites. **All three patterns then pass in `tychoc0`.**

### `tychoc` changes (larger — replace shared-body re-resolve with a clone)

Bring `tychoc` to the same model: each `GInst` gets its **own cloned body** with
`$T` substituted, resolved independently. Concretely:

1. **Add an AST cloner for `Proc` bodies.** New functions, mirroring the
   existing tree shapes (`Stmt`, `Expr`):
   `static Stmt **clone_block(Stmt **body, int n, Type *binds);`
   `static Stmt *clone_stmt(Stmt *s, Type *binds);`
   `static Expr *clone_expr(Expr *e, Type *binds);`
   The clone is deep for every node that resolution mutates, and applies
   `subst_type(t, binds)` to every embedded `Type`:
   - `E_ARRLIT` empty: substitute `e->ival` (the declared element type) — this is
     what `:3355` does today but only at resolve time; doing it at clone time
     makes it permanent and instance-local.
   - `S_DECL` typed: substitute `s->annot` (**fixes #2** — the missing
     substitution).
   - bare `[]`/`None` decls: clone resets resolved state so each instance
     re-grounds independently (`s->decl_type` back to its pre-resolve value;
     fresh `g_pend` entry per instance because the cloned `Stmt` is a distinct
     pointer) (**fixes #1**).
   - `E_CALL`: clone with `e->sval` reset to the *original* template name (not
     any previously-rewritten instance name), so each instance re-runs
     `instantiate_generic` for nested calls against *its* bindings (**fixes
     #3**, discovery side).
   - everything else: copy the node, recurse into children, leave scalar fields.
   This is the piece the design notes call "the riskiest part." It is bounded:
   the node set is fixed and small, and `tychoc0`'s `subst_*_t` (already shipping)
   is a complete worked reference for exactly which fields carry types.
2. **Store the cloned body on the `GInst`** (add `Stmt **body; int nbody;` to
   `GInst`, `src/tychoc.c:2958`). `ginst_to_proc` (`:7539`) uses the cloned body
   instead of the shared `gi->tmpl` body. Delete the `g_active_binds` mechanism
   (`:2963`, `:3355`-`3356`, `:8206`/`8216`) — substitution now happens once at
   clone time, not repeatedly at resolve time.
3. **Resolve instance bodies to a fixpoint and emit prototypes after.** Reorder
   `gen_program`:
   - move the per-instance **resolve** out of the emit loop and run it as a
     worklist *before* any prototype/emit, so all transitively-needed instances
     (and their `Sig`s + `GInst`s) exist first (**fixes #3** ordering — the
     missing prototype);
   - then emit prototypes for every `GInst` (`:8126`), then emit bodies
     (`:8204`). With all instances discovered up front, the prototype loop covers
     `id__int` too.
   - the worklist: index over `g_nginsts`; for each not-yet-resolved instance,
     `resolve_block` its cloned body (which may append new `GInst`s); stop when
     the index catches an unchanged `g_nginsts`. Dedup is already `sig_find`
     (`:4915`), so it terminates on the same conditions as `tychoc0`.

How `$T` substitution reaches each surface, in `tychoc` after the change:
- **local typed decls**: `clone_stmt` substitutes `s->annot` (#2).
- **bare `[]`/empty-literal element types**: `clone_expr` substitutes
  `E_ARRLIT.ival`; bare `[]` re-grounds per cloned instance (#1).
- **nested generic call sites**: `clone_expr` keeps the original callee name; the
  resolve-worklist runs `instantiate_generic` per instance, and the reordered
  prototype emission covers the discovered instances (#3).

---

## Fixpoint determinism

Both compilers must emit byte-identical C (`compiler/fixpoint.sh`: `cmp -s
cA.c cB.c`, and B differentially matches the C compiler on every `tests/*.ty`,
`examples/*.ty`, and `tests/pkg/*`). Generics add two determinism obligations:

1. **Instance naming must match exactly.** Already handled and must be
   preserved: `gen_inst_mangle` (`compiler/tychoc0.ty:8674`) is documented to
   mirror `type_mangle_ident` (`src/tychoc.c:4849`) byte-for-byte, including the
   `str -> string` spelling and the array/map/Option manglings, plus the
   return-only-typaram suffix rule (`src/tychoc.c:4946`-`4953` ↔
   `compiler/tychoc0.ty:9038`-`9047`). Any new substitution must route type
   spelling through these same functions — never format a type name ad hoc.

2. **Instance discovery ORDER must match.** Today both use "first-encounter
   order" and append instances after the concrete functions
   (`src/tychoc.c` emits `g_ginsts` in registration order; `tychoc0`
   `:9603` "instances last, in first-encounter order (matches tychoc)").
   Nested instantiation (#3) makes this the sharp edge:
   - The two architectures discover nested calls at *different times*: `tychoc`
     discovers during `resolve_block` of an instance body (a worklist over
     `g_nginsts`); `tychoc0` discovers during `mono_block` of an instance body
     (a worklist over `insts`). **To stay identical, both worklists must use the
     same traversal order:** outer instances in source/first-encounter order,
     and within each instance body a left-to-right, pre-order expression walk,
     appending newly discovered instances to the end of the list. Both
     compilers already walk expressions left-to-right and append; the design
     must *keep* that and not, e.g., resolve arguments in a different order on
     one side.
   - **Concrete rule to write into both implementations and the doc:** "An
     instance is appended to the global instance list at the moment its call site
     is first resolved, scanning instance bodies in list order and each body in
     source order." This makes the list a deterministic BFS/worklist identical on
     both sides.
   - The fixpoint's `cmp -s cA.c cB.c` plus the per-fixture differential will
     catch *any* divergence in instance order or naming as a non-identical `.c`
     (it already gates `tests/pkg/generic`). Add nested-generic fixtures (below)
     so this coverage actually exercises the worklist.

3. **Prototype vs body emission order** must be internally consistent within each
   compiler but need not match the *other* compiler line-for-line — only the
   final `.c` bytes must match. Since both already emit `prototypes …; bodies …`
   in instance-list order, keeping that invariant after the reorder is sufficient.

What in `compiler/fixpoint.sh` catches a divergence: the `cmp -s "$T/cA.c"
"$T/cB.c"` line (self-emission fixed point) and the `tests/*.ty` / `tests/pkg/*`
differential loops. A nested-generic fixture that instantiates the same callee
at two types in two different orders is the regression that would expose an
ordering bug.

---

## Constraints, privacy, recursion

- **`where` constraints** (`numeric`/`comparable`/`has_str`, type-sets) are
  checked at instantiation *before* the body is cloned/substituted, in both
  compilers (`src/tychoc.c:4914`-`4936`; `compiler/tychoc0.ty:8970`-`9007`).
  Per-instance bodies do not change this — the check runs once per *(generic,
  binding)* pair at the `instantiate_generic`/`mono_instantiate` call, which is
  exactly when a nested instance is discovered too. So a nested generic with its
  own `where` clause is checked when the outer body's worklist reaches it, with
  the inner concrete binding. The doc's existing promise ("substitute, then
  type-check the concrete body", `docs/generics.md` §5) still holds: a violation
  the `where` clause didn't pre-empt surfaces as an ordinary body type error
  during the per-instance `resolve_block`/codegen, attributed to that instance.
- **Recursive generics** (a generic calling itself at a different type):
  termination depends on the type strictly *not* growing without bound. Same
  concrete type → dedup hit (terminates). Strictly growing type (e.g.
  `f($T)` calling `f([$T])`) → unbounded instantiation. Both worklists must
  **bound recursion depth and fail closed** with a clear error
  ("generic instantiation too deep — recursive generic at growing type") rather
  than looping/OOMing. This is new in both compilers (Stage-1 has no nested
  calls, so it never recurses). Add a depth/count cap on the instance list.
- **Privacy + qualified generic calls.** The leading-underscore package-private
  rule and the qualified-generic-call path were just added
  (`src/tychoc.c` ~`3577`; `tychoc0` `mangle_type` ~`1921` with the `$`/empty
  guards). Per-instance bodies interact with these in #3's *cross-package*
  variant: a generic in package `a` calling a generic in package `b` must
  resolve `b.callee` to `b`'s generics registry and intern the instance under
  the mangled `b__callee__<T>` name, and the privacy check must run on the
  *template* name before mangling. The plan: nested-call discovery must reuse the
  *existing* qualified-resolution path (not a fresh ad-hoc lookup), so privacy
  and package mangling are inherited unchanged. The `$`/empty guards in
  `tychoc0`'s `mangle_type` (~`1921`) exist precisely so a typaram-bearing type
  isn't package-mangled; the cloner/worklist must preserve that (substitute `$T`
  to a concrete type *before* any package mangling, never after).

---

## Staged, verifiable plan (least-risky first)

Each step ships a fixture under `tests/` (or `tests/pkg/`) with a golden
`tests/<name>.out`, and must pass `make test` (both compilers + ASan + golden +
byte-identical) and `make fixpoint` (B==C and B matches the C compiler). A step
is not done until both gates are green. **No language-feature step may land
`tychoc`-only** — a `tychoc`-only generic feature turns the fixpoint differential
red (it is a parity violation); the two compilers must reach each behaviour in
the same commit. (This is why the steps below pair the compilers.)

> Sequencing note: `tychoc0` is built *by* `tychoc`, so a change that alters
> emitted C for generics changes both `tychoc0`'s own build and the fixtures.
> Land the `tychoc` cloner and the `tychoc0` worklist behind fixtures that no
> existing program triggers first (no current fixture uses #1/#2/#3), so the
> bootstrap stays green until the paired behaviour is in.

1. **Step 0 — lock the current divergence as a known-state test (risk: low,
   effort: ~1h).** Add the three reproductions as fixtures *expected to pass*,
   confirm `tychoc0` already passes #1/#2 and both fail #3. This pins the
   baseline and makes every later step's diff meaningful. (These fixtures will be
   red until the matching step lands — keep them in a scratch list, not in
   `make test`, until then; or add them per-step.)

2. **Step 1 — `tychoc0` nested-call worklist (#3) (risk: medium, effort:
   ~0.5 day).** Implement the instance worklist in `monomorphize_program`
   (`compiler/tychoc0.ty:9603`). Fixture: `tests/generic_nested.ty` (a generic
   calling another, each at ≥2 types). Gate: `tychoc0` now passes #3; `tychoc`
   still fails it — so this step must land *together with* `tychoc`'s #3 fix
   (Step 4) to keep the fixpoint green, OR the fixture is added only when both
   sides pass. **Recommend: do Steps 2–4 for `tychoc` first, then land Step 1 +
   the fixture in the same commit as Step 4.**

3. **Step 2 — `tychoc` AST cloner skeleton (risk: high, effort: ~1.5 days).**
   Add `clone_block`/`clone_stmt`/`clone_expr` with `subst_type` applied to all
   embedded types, plus `GInst.body`/`nbody`. **Do not yet change behaviour** —
   wire the cloner so `ginst_to_proc` uses a clone of the template body with
   `binds` substituted, and *delete* `g_active_binds`. Verify the existing
   generic fixtures (`tests/generic_*.ty`) still emit byte-identical C
   (regression: the clone of a Stage-1 body must produce the same resolved code
   as the shared-body re-resolve did). **This is the highest-risk step** — the
   cloner must cover every node kind resolution mutates; a missed field is a
   silent miscompile. Mitigation: diff emitted C against the pre-change compiler
   on all current generic fixtures (must be identical), and run under ASan.

4. **Step 3 — `tychoc` #1 + #2 via the cloner (risk: medium, effort: ~0.5 day).**
   With the cloner in place, #1 (per-instance bare-`[]` re-grounding) and #2
   (`subst_type(s->annot)` at clone time) should fall out. Fixtures:
   `tests/generic_build.ty` (#1, ≥2 types), `tests/generic_typed_local.ty` (#2).
   Confirm `tychoc0` already passes these (it does), so adding the fixtures keeps
   the fixpoint green the moment `tychoc` catches up.

5. **Step 4 — `tychoc` #3: resolve-worklist + prototype reorder (risk: medium,
   effort: ~0.5 day).** Reorder `gen_program` so instance bodies resolve to a
   fixpoint before prototypes emit. Land together with Step 1 and the
   `tests/generic_nested.ty` fixture. Add the cross-package nested case under
   `tests/pkg/` (a generic in `lib` calling another generic in `lib`, used from
   `main`) to cover the qualified path + privacy.

6. **Step 5 — recursion guard (risk: low, effort: ~0.5 day).** Add the
   depth/count cap + fail-closed error to both worklists. Fixtures under
   `tests/reject/`: a recursive generic at a growing type must error cleanly
   (golden `.err`, matching the existing `tests/reject/*` harness for generics,
   e.g. `tests/reject/where_numeric.ty`).

7. **Step 6 — generic corelib dogfood (risk: low, effort: ~1 day).** Once #1–#3
   work, write the actual motivating program: a single generic
   `arrays`/`iter`-style module over `int`/`str`/`float` (the corelib goal),
   as a `tests/pkg/` fixture, validated 3-way + golden. This is the real
   acceptance test that the feature is usable, not just that the micro-patterns
   compile.

8. **Step 7 — fuzzer extension (risk: medium, effort: ~1–1.5 days).** `fuzz/gen.py`
   (1003 lines) currently generates **neither generics nor packages** (verified:
   no `generic`/`$T`/`package`/`import`/`where` tokens in the generator). Extend
   it to occasionally: (a) define 1–2 generic free functions with simple bodies
   (id/build/nested-call shapes), and (b) call them at ≥2 concrete types. This
   makes the differential+ASan campaign cover the new clone/worklist paths.
   Packages can be a later, separate fuzzer extension; generics first, since
   that is the feature under change. Keep the fail-closed harness discipline.

**Highest-risk step: Step 2 (the `tychoc` AST cloner).** It is the one piece
without an existing in-tree analogue *on the C side* (though `tychoc0`'s
`subst_*_t` is a complete reference for which fields carry types). A missed
mutable field produces a silent wrong-code miscompile rather than a clean error.
The mitigation — diffing emitted C against the unchanged compiler on every
existing generic fixture, under ASan, before adding any new behaviour — is
mandatory for this step.

---

## Risks

- **Silent miscompile from an incomplete `tychoc` cloner (Step 2).** Highest
  risk. Mitigation above (byte-diff against the pre-change compiler on all
  generic fixtures + ASan). A cloner that forgets to reset `e->sval` on a nested
  call, or to substitute `E_ARRLIT.ival`, fails *loudly* (type error / missing
  symbol); one that forgets to deep-copy a mutated child fails *silently* (two
  instances share a node) — the latter is the dangerous class and is exactly what
  the differential byte-diff catches.
- **Fixpoint divergence from instance-discovery order.** If the two worklists
  ever visit call sites in different orders, `cmp -s cA.c cB.c` goes red. The
  "append at first-resolve, list order, source order" rule (Fixpoint
  Determinism §2) must be written identically into both and asserted by a
  nested-generic fixture that instantiates a callee at two types.
- **Unbounded recursive instantiation.** Until Step 5's cap lands, a recursive
  generic at a growing type loops/OOMs. Land the guard before advertising
  recursive generics; document recursion-at-growing-type as unsupported.
- **Bootstrap fragility during the transition.** Because `tychoc0` is built by
  `tychoc`, the `tychoc` changes alter how `tychoc0.ty` itself is compiled. Keep
  every behavioural change gated behind fixtures no existing program triggers,
  and land paired commits, so the bootstrap never goes red mid-sequence.
- **Scope creep into generic methods/lambdas.** `tychoc0` already marks
  generic-call-bearing lambda bodies as out of scope ("1c",
  `compiler/tychoc0.ty:9468`). Stage-2 here is free *functions* with non-trivial
  bodies; generic UFCS methods already have partial support
  (`compiler/tychoc0.ty:3015`, `:9357`-`9371`) and generic-inside-lambda should
  stay explicitly out of scope for this stage to bound risk.

---

### Appendix — verification log

- Patterns reproduced with the committed `./tychoc` and a `tychoc0` built via
  `./tychoc compiler/tychoc0.ty -o tychoc0`, programs on stdin.
- `tychoc`: #1 → `push's value must be int`; #2 → `declared type [void] but
  value is [int]`; #3 → emitted C defines `h_id__int` but omits its prototype →
  `cc` "implicit declaration of function 'h_id__int'".
- `tychoc0`: #1 → prints `3`/`2` (PASS); #2 → PASS; #3 → `type: unknown
  function 'id'`.
- `fuzz/gen.py` (1003 lines): no `generic`/`$T`/`package`/`import`/`where`
  tokens — generates neither generics nor packages (verified by grep).
