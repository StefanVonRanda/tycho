# Generics gap-fixes plan (Stage 3)

> **STATUS: SHIPPED — both gaps closed in commit `05bd7f1` (2026-06-23),
> "feat(generics): composition + higher-order generics (both compilers)".**
> This note is the *design record*, not open work. Gap 1's Option-A branch
> lives at `compiler/tychoc0.ty` (the `type_of` ECall arm — infer bindings via
> `match_typaram_str`, substitute the concrete return). Gap 2 added the
> `fn(...)` arms to `match_type`/`subst_type`/`has_typaram` plus the FnC
> typedef skip-guard in `src/tychoc.c`. Regression fixtures:
> `tests/generic_compose.ty`, `tests/generic_hof.ty` (both pass under
> `make test`). Line numbers below are pre-ship and have since drifted.

Implementation plan for the two documented generics gaps remaining after
Stage-2 body cloning (`docs/internals/generics-stage2-body-cloning.md`). No code is
changed by this note. Every claim cites `path:line`.

The two gaps:

- **Gap 1 — generic-result typing in `tychoc0`.** A generic call's *result* is
  typed as the placeholder `$T` in the monomorphization pass, so feeding one
  generic's output into another (`firstn(box(xs), 3)`, `s := gc.sort(xs)` then
  `gc.join(s, ",")`, `v1 := fz_gfst(..)` then using `v1`) fails closed with
  `unknown function 'gc__join'` / `unknown type '$T'`. `tychoc` (the C reference
  compiler) handles all of these — it is a `tychoc0`-only parity gap. The scope
  exclusion is documented at `tests/generic_nested.ty:8`–`:10` ("A generic call
  whose ARGUMENT is itself a generic call ... is a separate, harder case not
  covered here").

- **Gap 2 — typaram inference through a function-typed parameter (higher-order
  generics), BOTH compilers.** `fn map(xs: [$T], f: fn($T) -> $T) -> [$T]`
  cannot be called (the matcher will not bind `$T` by structurally matching a
  `fn(...)` parameter against a concrete `fn(...)` argument) and, separately,
  cannot even be defined (its `fn($T)->$T` interns a function-type whose
  emission produces invalid C).

## Summary & recommended order

Do **Gap 1 first**. It is the higher-value, lower-risk change: it is confined to
`tychoc0`, it unlocks generic composition (the prerequisite for a real generic
corelib), and it reuses machinery that already exists (`match_typaram_str`
`compiler/tychoc0.ty:8734`, `subst_field_type`/`subst_body_type`
`compiler/tychoc0.ty:9060`/`:8790`). The `tychoc` reference already does the
right thing, so Gap 1 has a known-correct oracle: the existing
`tychoc`-vs-`tychoc0` differential turns green when `tychoc0` matches it, and
fixpoint stays byte-identical because the change is logically inert when there
are no nested/composed generic calls.

Do **Gap 2 second**. It touches *both* compilers, requires a new feature (a
language surface that does not exist today — no test exercises a `fn($T)->$T`
parameter), and carries the genuinely risky sub-step (the C `FnC` typedef
emission guard). The matcher extension is mechanical; the emission fix is the
single highest-risk step in the whole plan (see Risks).

---

## Gap 1 — generic-result typing in `tychoc0`

### Current mechanism (cited)

`type_of` is the type oracle: `fn type_of(e, names, types, dc, ctx) -> string`
at `compiler/tychoc0.ty:3029`. Its `ECall` arm is at `:3146`. For a call whose
name is a **generic template**, none of the builtin special-cases match, so it
falls through to the final line:

```
compiler/tychoc0.ty:3187   return resolve_nt(dc, ctx, sig_ret(dc, with_line(ctx, _el), name))
```

`sig_ret` (`compiler/tychoc0.ty:2807`) returns the template's *declared* return
type verbatim — which still contains `$T` (e.g. `[$T]` for `firstn`/`box`,
`$T` for `fz_gfst`). So `type_of` of a generic call yields a `$T`-bearing
string. That string then poisons two downstream sites in the mono pass:

1. **`SDecl` (variable typed from a generic-call rhs)** —
   `compiler/tychoc0.ty:9486`–`:9494`:

   ```
   SDecl(nm, rhs, ln):
       mr := mono_expr(rhs, ...)
       # type the ORIGINAL rhs ...resolves... to "$T" — a harmless env placeholder.
       # The rewritten `mr` names instances not yet in dc; the final codegen
       # recomputes the real type from the instance-bearing dc.
       ty := type_of(rhs, names, types, dc, ctx)
       push(names, nm); push(types, ty)
       return SDecl(nm, mr, ln)
   ```

   The comment is the design-as-shipped: `s := gc.sort(xs)` records `s : $T`.
   The "codegen recomputes" claim holds **only** when `s` is later used in a
   *non-generic* position (codegen re-derives the concrete type from the
   instance Sig). It breaks when `s` is fed to **another generic call**
   (`gc.join(s, ",")`): the mono pass at `:9387` computes that call's argument
   types via `type_of` (see #2) and gets `$T`, so `mono_instantiate`
   (`compiler/tychoc0.ty:8936`) matches `$T` against the join template and
   stamps the literal instance `gc__join__$T` ("instantiated with T = $T").

2. **Nested generic call as a direct argument** — the mono pass computes each
   argument's type to drive instantiation:

   ```
   compiler/tychoc0.ty:9383   gi := find_generic(gens, rn)
   compiler/tychoc0.ty:9384   if gi >= 0:
   compiler/tychoc0.ty:9387       push(at, type_of(margs[k], names, types, dc, ctx))   # per arg
   compiler/tychoc0.ty:9390       newnm := mono_instantiate(gens[gi], at, &insts, el, dc, explicit)
   ```

   For `firstn(box(xs), 3)`, `margs[0]` is the (already-rewritten) call to the
   `box` instance, but `type_of` of it still routes through `:3187` and returns
   `[$T]`. `mono_instantiate` then either dies with `unknown type '$T'` or names
   `firstn__arr_$T`, which has no Sig → `unknown function`.

### Why the existing UFCS-method case already works (the template to copy)

`type_of` **already** does the correct substitution for one shape: a generic
UFCS *method* call, in the `ECallV` arm at `compiler/tychoc0.ty:3115`–`:3122`:

```
msi := dc.sigmap.get(mn, -1)
if msi >= 0 and len(dc.sigs[msi].ptypes) >= 1 and ty_is_generic(dc.sigs[msi].ret):
    rbt := type_of(efield_base(callee), names, types, dc, ctx)   # the receiver's concrete type
    sbn := []string
    sbt := []string
    if match_typaram_str(dc.sigs[msi].ptypes[0], rbt, &sbn, &sbt):
        return resolve_nt(dc, ctx, subst_field_type(dc.sigs[msi].ret, sbn, sbt))
return resolve_nt(dc, ctx, sig_ret(dc, ctx, mn))
```

This is exactly the recipe Gap 1 needs, generalized from "first param vs
receiver" to "all params vs all args": infer the bindings from argument types,
substitute into the return type. It proves the substitution helpers
(`match_typaram_str`, `subst_field_type`) are reachable from `type_of` *given
the template's Sig*. Note it works off `dc.sigs` (the Sig table), not off the
`gens: [Func]` list.

### How `tychoc` (the reference) gets this right

`tychoc` never lets a generic call's result be `$T`, because it **rewrites the
call to its concrete instance during resolution**, in place, before anything
reads the result type. In `resolve_expr`'s `E_CALL` arm:

```
src/tychoc.c:3913   { Proc *gt = generic_find(e->sval);
                      if (gt && !e->qual && !e->lhs) instantiate_generic(gt, e); ... }
```

`instantiate_generic` (`src/tychoc.c:4896`) resolves each argument
(`Type at_ = resolve_expr(e->args[j]);` `:4910`), structurally binds via
`match_type` (`:4913`), computes `cret = subst_type(gt->ret, binds)`
(`src/tychoc.c:4958`), then **mutates the AST node**: `e->sval = nm;` (`:4961`)
and registers a concrete Sig with `s.ret = cret` (`:4966`–`:4969`). The very
next lines look that Sig up (`Sig *s = sig_find(e->sval);` `src/tychoc.c:3916`)
and `return e->type = ...` the concrete return. Because resolution is a single
bottom-up pass that *resolves arguments first* and writes the concrete type onto
each node, a nested/composed generic call already carries its concrete type by
the time an enclosing call reads it. There is no separate "type_of" oracle that
can disagree. That is the structural reason `tychoc` has no Gap 1.

`tychoc0` cannot adopt the in-place-rewrite model wholesale (its mono pass is a
*separate* tree-rewrite over an immutable `Decls`; `type_of` is a pure function
re-invoked from many places). The minimal port is to teach `type_of` the one
fact it lacks: a generic call's *concrete* return type.

### The `gens`-access question (resolved)

`type_of` takes only `(dc: Decls, ctx: Ctx)` (`compiler/tychoc0.ty:3029`).
`Decls` is defined at `compiler/tychoc0.ty:488`–`:499`; it has
`sigs/structs/enums/ntnames/ntunder/sigmap/pars/src_lines/vmap` and **no `gens`
field**. `Ctx` (`:500`–`:524`) is mutable per-scope state and also has no
`gens`. `type_of` is called from **115 sites** (`grep -c 'type_of(' ...` = 115),
so threading a new `gens` parameter through `type_of` and every transitive
caller is the most invasive option and is **not recommended**.

Two viable least-invasive options, in preference order:

**Option A (recommended) — use the Sig table, no `gens` needed.** The generic
UFCS-method case at `:3118` already infers + substitutes using only
`dc.sigs`/`dc.sigmap` — it never consults `gens`. A *generic template* function
has a Sig (the parser registers every proc's Sig; `func_is_generic` templates
are partitioned only in the mono *driver* at `:9572`, not removed from `dc`).
So `type_of`'s `ECall` arm can, just before the `:3187` fallthrough, do:

```
# (proposed, in type_of ECall arm, before the sig_ret fallthrough)
si := dc.sigmap.get(name, -1)
if si >= 0 and ty_is_generic(dc.sigs[si].ret):
    bn := []string
    bt := []string
    ok := true
    k := 0
    for k < len(dc.sigs[si].ptypes):
        if k < len(args):
            if not match_typaram_str(dc.sigs[si].ptypes[k], type_of(args[k], names, types, dc, ctx), &bn, &bt):
                ok = false
        k = k + 1
    if ok:
        cret := subst_field_type(dc.sigs[si].ret, bn, bt)
        if not ty_is_generic(cret):
            return resolve_nt(dc, ctx, cret)
# else fall through to the existing sig_ret(...) line (unchanged)
```

This is a pure, local addition to `type_of` with **no signature change**, so it
touches zero of the 115 call sites. It mirrors the proven `:3118` code exactly.
It must verify `dc.sigmap`/`dc.sigs` store the template's *declared* (`$T`-
bearing) ptypes and ret — confirm by reading the parser's Sig registration for a
generic proc (search the `register`/`push(sigs ...)` site; the UFCS case at
`:3118` already trusts `dc.sigs[msi].ret` to be `$T`-bearing for a generic
method, which is direct evidence the Sig carries the template types). **This is
the load-bearing assumption of Option A** — verify it before coding.

**Option B (fallback) — thread `gens` only into the mono layer.** The two
poisoned sites (`SDecl` at `:9486`, the arg-type loop at `:9387`) live inside
`mono_stmt`/`mono_expr`, which already carry `gens: [Func]` in scope. Add a
small helper `mono_type_of(e, names, types, gens, dc, ctx)` that wraps `type_of`
but, for an `ECall` to a `find_generic(gens, name) >= 0` template, infers +
substitutes (same body as above but reading `gens[gi].ptypes/ret`). Replace the
`type_of` calls *only at those two mono sites* with `mono_type_of`. This avoids
the Sig-table assumption (it reads the `Func` template directly) at the cost of
a new helper and ~2 edited call sites. Use this only if Option A's Sig-table
assumption proves false.

Either option reuses `match_typaram_str` (`:8734`) and `subst_field_type`
(`:9060`) unchanged; both already handle `[$T]`, `{$K:$V}`, `Option($T)`,
`Result($T,$E)` recursively (`:8746`–`:8773`).

### Recursion / termination

`type_of` already recurses into arguments (`type_of(args[k], ...)`); the
nested-generic case adds one more level per nesting but the tree is finite. The
`ty_is_generic(cret)` guard preserves fail-closed behaviour: if a binding can't
be inferred (a return-only `$T` with no fixing argument), it falls through to
the old `sig_ret` path rather than fabricating a type — same policy as
`mono_instantiate` at `compiler/tychoc0.ty:9022` and `tychoc` at
`src/tychoc.c:4959`.

---

## Gap 2 — higher-order generics (BOTH compilers)

Two independent defects; both must be fixed for the feature to work.

### 2a. The matcher does not recurse into function types

**`tychoc`.** `match_type` (`src/tychoc.c:1178`–`:1210`) recurses into array
(`:1184`), option (`:1185`), result (`:1186`), map (`:1188`), and generic
struct/enum self-references (`:1191`/`:1200`), then falls to `return pat ==
concrete;` (`:1209`). There is **no `IS_FUNC(pat) && IS_FUNC(concrete)` arm**.
So a parameter pattern `fn($T)->$T` (a `T_FUNC_BASE+i` whose `FuncTy.params[0]`
and `.ret` are `T_TYPARAM`) compared against a concrete `fn(int)->int` (a
different `FuncTy` id) hits `pat == concrete`, which is false → the call is
rejected with the exact error in the report ("argument 2 of 'gc__map' is
fn(int) -> int, which does not fit the parameter pattern", from `:4914`).

Fix: add a function-type arm to `match_type`, recursing into each param and the
return, mirroring the existing array/map arms:

```
# proposed, in match_type, before the final `return pat == concrete`
if (IS_FUNC(pat) && IS_FUNC(concrete)) {
    if (func_n(pat) != func_n(concrete)) return 0;
    for (int i = 0; i < func_n(pat); i++)
        if (!match_type(func_param(pat, i), func_param(concrete, i), binds)) return 0;
    return match_type(func_ret(pat), func_ret(concrete), binds);
}
```

Accessors `func_n`/`func_param`/`func_ret` exist at `src/tychoc.c:840`–`:842`.
`subst_type` (`src/tychoc.c:1148`) **also lacks a function-type arm** — confirm
and add the symmetric one, else `cparams[j] = subst_type(...)` (`:4943`) and
`cret = subst_type(gt->ret, binds)` (`:4958`) leave the `$T` inside the func
type unsubstituted, and `has_typaram(cret)` (`:1165`, also no `IS_FUNC` arm —
add it too) will mis-report. So 2a in `tychoc` is **three** parallel one-arm
additions: `match_type`, `subst_type`, `has_typaram` — all already have the
array/map template to copy, all in the same 60-line block (`:1148`–`:1210`).

**`tychoc0`.** `match_typaram_str` (`compiler/tychoc0.ty:8734`) is the
string-based equivalent. It handles `$X` (`:8737`), `[E]` (`:8746`), `{K:V}`
(`:8748`), and `Name(args)` (`:8767`), then `return pat == concrete` (`:8786`).
No `fn(...)` arm. Function types are the string `fn(P,...->R)` (`is_func`
`compiler/tychoc0.ty:2574`; `func_params`/`func_ret` near `:2616`). Add an arm
that detects both sides are `fn(`, splits params and return (reuse
`func_params`/`func_ret`/`mk_functype` at `:7413`), and recurses. The
substitution side: `subst_field_type` (`:9060`) and `subst_bare_typaram`
(`:1457`) must also descend into `fn(...)` strings — verify whether they already
do (the comment at `compiler/tychoc0.ty:1940`–`:1946` shows `mangle_type`
*does* special-case `fn(`, which is evidence the string format is handled
elsewhere and gives the exact split pattern to copy). If `subst_field_type`
already rewrites bare `$T` anywhere in the string, the fn-type case may work for
free — **confirm by reading `:9060`–`:9108` before assuming.**

### 2b. The emission bug — `FnC` typedef for a typaram-bearing function type

**`tychoc`.** Function types are interned globally into `g_functypes` at parse
time (the intern function ends at `src/tychoc.c:838`). A template's
`fn($T)->$T` parameter interns a `FuncTy` whose `params[0]`/`ret` are
`T_TYPARAM`. At codegen, the `FnC` typedef loop emits **every** entry with no
guard:

```
src/tychoc.c:7693   for (int i = 0; i < g_nfunctypes; i++) {
src/tychoc.c:7694       FuncTy *f = &g_functypes[i];
src/tychoc.c:7695       fprintf(o, "typedef struct { void *env; %s(*call)(void*, Arena*", c_type(f->ret));
src/tychoc.c:7696       for (int j = 0; j < f->n; j++) fprintf(o, ", %s", c_type(f->params[j]));
src/tychoc.c:7697       fprintf(o, "); void *(*copyenv)(Arena*, void*); } FnC%d;\n", i);
```

`c_type` (`src/tychoc.c:935`) has no `T_TYPARAM` case, so a typaram falls to its
`default: return "void ";` (`:982`) → it emits
`typedef struct { void *env; void (*call)(void*, Arena*, void); ... }` — a
`void` parameter, invalid C. This is exactly the report's symptom.

This is the **inverse** of how every other composite already guards itself.
`has_typaram` (`src/tychoc.c:1165`) exists precisely so typaram-bearing
composites are skipped from emission: enums skip at `src/tychoc.c:7681`
(`if (g_enums[i].generic) continue;`), and Option/Result/array emission is
gated by `has_typaram` (see e.g. `src/tychoc.c:1347`, `:1416`, `:1446`). The
function-type loop is the one composite that **forgot the guard**. Fix:

```
src/tychoc.c:7694 (proposed)   FuncTy *f = &g_functypes[i];
                               if (has_typaram(FUNC_OF(i))) continue;   /* a fn($T)->$T template type emits no C; its instances' fn types do */
```

This requires `has_typaram` to recurse through function types (the same arm
added in 2a — so 2a and 2b share that one addition) and a way to form the
`Type` from index `i` (the `T_FUNC_BASE + i` form, cf. `FUNC_ID`/`IS_FUNC`
macros at `src/tychoc.c:821`/`570`). Confirm whether a concrete instance
(`fn(int)->int`) is interned as a *separate* `g_functypes` entry from the
template's `fn($T)->$T` (it must be, since intern keys on the exact param/ret
types — `src/tychoc.c:831`–`:836`); if so, skipping the typaram-bearing one is
safe because the instance entry is emitted normally.

**`tychoc0`.** `tychoc0` collects fn-type families and emits a typedef per
family via `gen_func_typedef` (`compiler/tychoc0.ty:6314`), driven by
`note_arr_types(... mk_functype(...))` (`:7021`). Because `tychoc0` **drops
generic templates before codegen** (the mono driver partitions templates out at
`compiler/tychoc0.ty:9572`–`:9573` and only concrete instances + non-generic
funcs reach the emission walk), a `fn($T)->$T` family is never collected — *as
long as the collection walk runs over the post-mono program, not the templates*.
**Verify** that `note_arr_types`/the fn-type collection runs after
`monomorphize_program` and over its output (read the driver around
`compiler/tychoc0.ty:9838` "expand `$T` templates ... then rebuild"). If
collection ever touches a template body, `tychoc0` has the same bug; otherwise
2b is `tychoc`-only. The likely outcome: `tychoc0` avoids 2b structurally (drop-
before-codegen), and the only `tychoc0` work for Gap 2 is the 2a matcher arm —
**but this must be confirmed by reading the collection order, not assumed.**

---

## Fixpoint determinism

Hard invariant (`compiler/fixpoint.sh`, `make fixpoint`):
`A = tychoc·tychoc0.ty`, `B = A·tychoc0.ty`, `C = B·tychoc0.ty`; assert
`cmp cB.c cC.c` byte-identical AND `B` output matches the C compiler. Any edit to
`tychoc0.ty` changes what `tychoc0` *emits when compiling itself*, so:

- **Gap 1 (Option A).** The added `type_of` branch only fires for an `ECall`
  whose name is a generic template with a `$T`-bearing return. `tychoc0.ty`
  itself contains generic templates? If it does **not** call its own generics in
  a nested/composed way, the branch is logically inert during self-compilation
  and `cB.c`/`cC.c` stay byte-identical. Confirm by checking whether
  `tychoc0.ty` uses generic functions at all (grep `\$T` / `func_is_generic`
  usage in non-comment code). If `tychoc0.ty` is generic-free in its own body,
  fixpoint is trivially preserved. If not, the branch must produce a string
  *identical* to what the old `:3187` path produced for the cases `tychoc0.ty`
  itself hits — which it will, because for a *non-nested* generic call the old
  and new paths agree once the bindings resolve. **Risk noted: this needs the
  fixpoint run to confirm, not reasoning alone.**

- **Gap 2.** The `match_type`/`subst_type`/`has_typaram` arms and the `FnC`
  emission guard fire only in the presence of a `fn($T)->$T` type. `tychoc0.ty`
  and `tychoc.c` do not (today) use higher-order generics, so adding the arms is
  inert for self-compilation → fixpoint byte-identical. The new corelib `map`
  template (the eventual consumer) must NOT be added to the compiler sources;
  keep it in `corelib/` / `tests/`, exercised by `make corelib` / `make test`,
  not by the bootstrap.

Cross-compiler differential (`make test`, `make corelib`,
`tychoc`-vs-`tychoc0`): every new fixture must produce identical stdout under
both compilers. Gap 1's oracle is that `tychoc` already passes the fixtures, so
the differential is the pass/fail signal.

---

## Staged, each-step-verifiable plan (least-risk first)

Each step: add fixture → run that fixture under **both** compilers (identical
stdout) → `make test` (read the literal `passed: N failed: M` line) →
`make fixpoint` (assert `cmp` byte-identical). Do not proceed to the next step
until all four are green.

**Gap 1 (do first):**

1. **G1-fixture.** Add `tests/generic_compose.ty` (+ `.out`) covering all three
   shapes: nested-arg `firstn(box(xs), 3)`, decl-then-feed `s := sort(xs)` then
   `join(s, ",")`, and a return-typed local `v1 := gfst("a","b")` then using
   `v1`. Confirm `tychoc` passes and `tychoc0` fails (locks in the repro).
   *Effort: S. Risk: none (test only).*

2. **G1-fix (Option A).** Add the Sig-table inference branch to `type_of`'s
   `ECall` arm just before `compiler/tychoc0.ty:3187`. First *verify* the
   Sig-table assumption (templates' `$T`-bearing ptypes/ret are in
   `dc.sigs`/`dc.sigmap`) by reading the parser's Sig registration. If false,
   switch to Option B (mono-layer helper at the two sites `:9387`/`:9486`).
   *Effort: M. Risk: M — the Sig-table assumption and the fixpoint-inert claim
   both need the live runs to confirm.*

3. **G1-verify.** `make test` + `make fixpoint` + run the new fixture both
   compilers. Then add a corelib composition smoke test (e.g. a generic
   `sort`→`join` pipeline under `corelib/test/` or `tests/`) and `make corelib`.
   *Effort: S. Risk: L.*

**Gap 2 (do second):**

4. **G2-emission (tychoc).** Add the `has_typaram` function-type arm
   (`src/tychoc.c:1165`) and the `FnC` typedef skip guard at
   `src/tychoc.c:7694`. Add a fixture that merely *defines* `fn map(xs: [$T], f:
   fn($T)->$T) -> [$T]` and never calls it generically (or calls it
   monomorphically) — assert it now *compiles* (today it fails to emit valid C).
   `make test` + `make fixpoint`. *Effort: M. Risk: HIGH — see Risks; this is
   the flagged step.*

5. **G2-subst (tychoc).** Add the `subst_type` function-type arm
   (`src/tychoc.c:1148`); confirm `cret`/`cparams` substitute correctly.
   *Effort: S. Risk: M (codegen correctness of the instance's fn type).*

6. **G2-matcher (both).** Add the `match_type` fn arm (`src/tychoc.c:1178`) and
   the `match_typaram_str` fn arm (`compiler/tychoc0.ty:8734`); verify
   `subst_field_type`/`subst_bare_typaram` descend into `fn(...)` (read
   `:9060`/`:1457`; add an arm only if they don't). Add a fixture that *calls*
   `map([1,2,3], inc)` with a named or lambda fn argument, asserting both
   compilers produce identical output. *Effort: M. Risk: M.*

7. **G2-tychoc0-emission check.** Confirm `tychoc0` avoids 2b by collecting
   fn-types post-mono (read the collection order around
   `compiler/tychoc0.ty:9838` and `note_arr_types` at `:7021`). If it does NOT,
   add the analogous skip in the `tychoc0` collection/emission path. `make
   fixpoint`. *Effort: S–M. Risk: M (depends on the collection-order finding).*

8. **G2-corelib.** Add a generic higher-order corelib function (the real
   consumer) under `corelib/` + a `corelib/test/` case; `make corelib`. *Effort:
   S. Risk: L.*

---

## Risks

- **HIGHEST RISK — Step 4 (the `FnC` typedef skip guard, `src/tychoc.c:7694`).**
  This loop emits typedefs used by *every* function-value (closures, `core:iter`,
  the dispatch benchmark, fn-fields, fn-in-container). Skipping the wrong entry
  drops a typedef that a real instance or closure needs → either a C compile
  error (caught, recoverable) or — worse if the skip predicate is too broad — a
  silently missing typedef referenced elsewhere. Mitigation: the guard must be
  `has_typaram(fn-type)` ONLY (never `.generic`-of-something-else), and the
  function-value regression must stay green: run `make test`, `make corelib`,
  and the closure/iter fixtures (the `dispatch` benchmark, `core:iter` tests)
  after this step. Verify that a concrete `fn(int)->int` interns as a *distinct*
  `g_functypes` entry from `fn($T)->$T` (intern keys on exact types,
  `src/tychoc.c:831`) so skipping the template entry never starves an instance.

- **Gap 1 Option A's Sig-table assumption.** If the parser does NOT store a
  generic template's `$T`-bearing types in `dc.sigs`/`dc.sigmap` (e.g. it stores
  a resolved or stripped form), Option A silently infers nothing and falls
  through — a *missed* fix, not a corruption (fail-closed). Detect it by the G1
  fixture still failing after Step 2; the remedy is Option B. Verify the Sig
  contents before coding (the `:3118` UFCS-method case is strong evidence it
  works, but it is evidence, not proof for the non-method path).

- **Fixpoint regressions from Gap 1.** If `tychoc0.ty` uses its own generics in
  a way the new branch types differently, `cmp cB.c cC.c` breaks. This cannot be
  fully reasoned away — `make fixpoint` is the gate. Run it after Step 2 before
  anything else.

- **`subst_field_type`/`subst_bare_typaram` fn-string handling (tychoc0).** If
  these do not descend into `fn(...)` strings, a substituted instance keeps a
  bare `$T` inside its fn type and codegen emits a broken type
  (`compiler/tychoc0.ty:1940` warns this exact failure mode for `mangle_type`).
  Verify before Step 6; add the arm if missing.

- **Honest scope note.** Gap 2's matcher must decide how strict fn-type matching
  is (param arity, exact vs. compatible). Start strict (exact arity + structural
  match, mirroring the array/map arms) and widen only if a real corelib consumer
  needs it — strictness fails closed, looseness risks a wrong bind.
