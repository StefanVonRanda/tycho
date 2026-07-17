# Language ergonomics plan — writing-experience improvements

A prioritized plan to improve what it's actually like to *write* Tycho, drawn from
hands-on experience (editing the 15k-line self-hosted compiler `compiler/tychoc0.ty`
plus writing many small programs). This is a fresh-session handoff: it assumes no prior
context. Everything marked "verified 2026-07-17" was checked against the tree that day —
**re-verify against current code before acting**, since the tree moves.

## Ground rules that apply to every item here

- **Two compilers, always in lockstep.** `src/tychoc.c` (~11k LoC, the C reference) and
  `compiler/tychoc0.ty` (~15k LoC, self-hosted). Any language change must land in *both* or
  `make fixpoint` goes red (it asserts the self-hosted compiler reproduces its own emitted
  C byte-for-byte and matches the reference's output). There is no shipping one side only.
- **The gate is local.** `env -u LD_PRELOAD make ci` must stay green (the machine has an
  `LD_PRELOAD` that aborts every ASan binary — always unset it for gate runs).
  `TYCHO_CORELIB=$PWD/corelib` for corelib-using programs. No hosted CI, by policy.
- **Test discipline.** Golden tests: `tests/<name>.ty` + committed `tests/<name>.out`
  (also covers `examples/<name>.ty`). Must-fail tests: `tests/reject/<name>.ty` — the
  harness asserts *both* compilers reject with a diagnostic. Add both kinds as relevant.
- **Fuzz new constructs.** The differential generator is `fuzz/gen.py`; `make fuzz`. If you
  add a construct, teach the generator to emit it (see the `sum_empty_payload` kind for the
  pattern) so parity stays enforced.
- **Doc-link gate.** `make check-links` (step 18 of `make ci`) fails on any dead relative
  Markdown link. Update `docs/reference/` (behavior) and `docs/guides/` (rationale) for
  user-visible changes.
- **Honest framing.** This is a research project; don't overstate anything in docs.

## Definition of done (every item)

1. Works in **both** compilers; `make fixpoint` green.
2. Golden test and/or `tests/reject/` case added.
3. Fuzzer emits the construct (if it's new surface).
4. `env -u LD_PRELOAD make ci` green.
5. Docs updated if user-visible.

---

## P0 — UFCS on built-ins  *(start here: cheapest, highest-frequency)*

**Problem (verified 2026-07-17).** UFCS works on user functions (`x.myFn(a)` desugars to
`myFn(x, a)`), but NOT on built-ins: `s.split(" ")`, `arr.push(x)`, `xs.len()`,
`xs.sort()` all fail with `error: '.split' on a non-struct value`. The standard operations
you touch most can't be called with method syntax, even though your own functions can.
This inconsistency is felt in the first five minutes and constantly after.

**Where.** The UFCS / method-call resolution in both compilers. User functions are found in
the signature map (`sigmap`); the built-ins (`split`, `find`, `push`, `pop`, `len`,
`reserve`, `keys`, `map_get`/`map_has`/`map_set`/`map_del`, `str`, `chr`, `substr`, the
`to_*` conversions, `sort.*`, etc.) are special-cased and not in `sigmap`, so the method
desugar never reaches them. Grep both compilers for where `x.field(args)` (a call on a
field access) is lowered, and where the "on a non-struct value" error is raised.

**Approach.** When the receiver of `.name(args)` is not a struct and `name` is a known
built-in whose first parameter fits the receiver, desugar `x.name(args)` → `name(x, args)`.
Route through the *same* built-in arg-shape handling the direct-call form uses (several
built-ins have bespoke arg checking — don't bypass it). Do it in both compilers; keep the
fixpoint byte-identical. Add a golden exercising `s.split()`, `arr.push()`, `x.len()`,
`xs.sort()`, and a `tests/reject/` case for a genuinely-wrong receiver type.

**Watch out.** Built-ins with unusual shapes (`map_get(m, k, default)`, `reserve(a, n)`,
`push(a, v)` mutating). Confirm each common one works as a method before declaring done.

---

## P1 — First-class `Pool(T)` (the node-pool as a type, not a pattern)  *(biggest structural win)*

**Problem (verified 2026-07-17: no such type in 37 corelib packages).** The moment you need
a graph, a shared-mutable tree, or a doubly-linked structure, value semantics forbids the
pointer approach and you must hand-roll the idiom: hold all nodes in one `[Node]` and link
them by integer index, with a generational counter for use-after-free detection (see
`examples/triepool.ty`; rationale in `docs/guides/` and
`docs/internals/value-semantics-limits.md`). It's the *right* idiom for the model, but
making every user re-derive it is where the language "fights you."

**Goal.** Package it. A `Pool(T)` providing: growable storage, a stable `Handle` (an `int`
newtype — stays value-semantic, reintroduces **no** aliasing), `pool.add(v) -> Handle`,
`pool.get(h) -> T`, `pool.set(h, v)`, and generational-index use-after-free detection.

**Decision to make first.** Start as a **corelib package** (`core:pool`, pure Tycho): it
needs no compiler change, and can be prototyped immediately against a real program. Only
promote to a language-level type if the corelib version proves too clumsy (e.g. hits a
generics limitation). Note that generics are monomorphized over a fixed constraint set —
check `docs/guides/generics.md` that `Pool($T)` with the operations you want is expressible.

**Validate.** Rewrite `examples/triepool.ty` (or a small graph program) against `core:pool`;
the boilerplate removed is the success metric. **Be honest in docs:** `Pool` improves
*ergonomics*, not memory — the ~1.55×C storage cost of pointer-shaped data is fundamental
to the model (see `docs/internals/hylo-mvs-research.md`), and `Pool` doesn't change it.

**DONE — shipped 2026-07-17 as `core:pool`, after fixing the compiler gap it exposed.**
A clean generic package needs `Pool($T)` as a *function parameter*
(`add(p: inout Pool($T), v: $T)`, `get(p: Pool($T), …)`). `src/tychoc.c` accepted this all
along; the self-hosted `compiler/tychoc0.ty` did **not** — a genuine tychoc/tychoc0 parity gap
(generic user-struct parameters) the fuzzer didn't emit. Fixed in tychoc0:

- **Record the instance on a generic-struct decl.** `mono_stmt`'s `SDecl` typed the *bare*
  template (`pool__Pool`), losing the instantiation. It now stores the **application form**
  (`pool__Pool(Node)`, via `ginst_app_form` off the mono'd construction's instance callee),
  which `match_typaram_str` binds a `Pool($T)` pattern against directly — no `sinsts` lookup
  needed downstream, so `type_of`'s `$T`-return substitution works too (the `get→set` chain).
- **Bind `$T` from an instance arg in the call matcher.** `mono_instantiate` now strips the
  inout/sink marker (`base_ty`) before matching and falls back to `recover_struct_binds` /
  `recover_enum_binds`, so an annotated (`b: Box(int)` → mangled `Box__int`) arg binds too.
- **Keep `&`/`~` out of the C identifier.** `gen_inst_mangle` strips the pass-mode marker, so
  an `inout Box($T)` instance name is `bump__Box_int`, not the invalid `bump__&Box_int`.
- **Hidden-arena agreement.** `inout_container_param` now `resolve_ginst`s the param type, so
  a heap generic-struct inout param's hidden `_ina_` arena matches what the call site passes.

Verified: minimal by-value/inout/two-param cases (`tests/generic_struct_param.ty`), the
`Pool(int)`/`Pool(Node)` package (`corelib/test/pool`, `examples/corelib/pool`) all agree
byte-for-byte across tychoc, tychoc0-bundle, and tychoc0-standalone; full gate green.

**`Handle` is a distinct `int` newtype (the newtype-through-generics gap was then closed).** A
*concrete newtype parameter alongside a generic one* (`get(p: Pool($T), h: Handle)`) used to be
rejected by tychoc0: `type_of` base-resolves a var/call arg (`Handle`→`int`), so the strict
generic matcher couldn't reconcile it with the `Handle` pattern. Fixed in tychoc0:

- **Keep a fn's newtype return unresolved** in `type_of` (like the `Nt(v)` constructor case), so
  a value carries its newtype through the generic call's `nt_check`.
- **Lenient concrete-param match** in `mono_instantiate`: a non-`$` param whose resolved base
  equals the arg's resolved base is accepted (identity is enforced separately by the existing
  post-mono `nt_check`, which rejects a bare `int` for a `Handle` param — parity with tychoc).
- **`str` resolves the newtype** for every scalar dispatch (a fn returning `Meters` now
  `str`s as its `float`, matching `str(Meters(3.0))`).
- Also closed the **direct generic-struct construction arg** (`f(Box(5))`) via the same
  `ginst_app_form` used for `x := Box(5)` decls.

Verified: `tests/generic_newtype_param.ty` + `tests/reject/generic_newtype_bare_int.ty`, and
`core:pool` with a distinct `Handle` (`add(...) -> Handle`, `get(p, h: Handle)`, `edges:
[pool.Handle]`) — identical across tychoc / tychoc0 bundle / standalone.

**Array literal of a newtype (`[a]` where `a: Id`) — FIXED.** It used to infer `[int]` (the
element read resolves the newtype), so a later index/iteration lost the identity. `type_of` and
the array-literal codegen (`Arr_<T>_from`) now both keep the element's newtype skin (mirrors the
existing `EIndex` element case), matching tychoc. Verified: `tests/newtype_array_literal.ty`.

**Residual narrow gap (not generics; noted for later):** a package-qualified newtype in a
**map-key annotation** (`[]pool.Handle: bool`) records the key unmangled (`pool.Handle` vs the
value's `pool__Handle`), a tychoc/tychoc0 divergence — sidestep with an `int` key until fixed.

**Remaining follow-up:** teach `fuzz/gen.py` to emit generic-struct + concrete-newtype params
(its generic generation is currently gated to literal scalar args — see its own comments) so
these gaps stay enforced by the differential fuzzer, not only the goldens.

---

## P1 — Finish diagnostics: no type error should reach the C compiler

**Problem.** A type mismatch that isn't a newtype-identity or scalar-coercion issue used to
surface as a raw `cc` error about the emitted C — the single worst moment when writing.
The literal / tuple-element / inout / sink cases were fixed 2026-07 (see git log around
commit `cec92ac`, and `base_type_mismatch` in `compiler/tychoc0.ty`). **Remaining:** a
base-type mismatch supplied by a *variable / call / index* (not a literal) still falls
through to `cc`.

**Why it was left, and the coupling.** `base_type_mismatch` deliberately fires only on
syntactically-typed literals and an inout `&place`, because `type_of` is unreliable for a
var/call/index inside a generic body (see P2 below — a generic-enum match binding reports
the pre-mono typaram). Closing this fully is coupled to P2: once `type_of` is trustworthy in
generic contexts, extend `base_type_mismatch` to safely handle `EVar`/`ECall`/`EBin`/
`EIndex`. Add `tests/reject/` cases for the var/call/index forms. Both compilers.

---

## P2 — Generic-context `type_of` robustness  *(foundational; unblocks the diagnostics finish)*

**Problem.** Inside a generic function body, `type_of` can return the wrong type — the
pre-monomorphization type parameter instead of the instantiated one. Concretely, a match
binding over a generic enum reports `int` for the payload of a `Tree(string)` `Leaf`. This
bit `tests/generic_enum_array.ty` during the base-type-check work and is why P1-diagnostics
had to skip non-literals.

**Where.** The `type_of` path and the monomorphization typing in both compilers. Root cause:
a value's type (especially a match-arm binding) isn't resolved to the instantiation's type
arguments before the mono pass runs its queries. This is a genuine compiler task — scope it
carefully and lean on the fixpoint + a targeted differential battery.

---

## P2 — Greenfield dogfood  *(discovery; run alongside the rest)*

Build something ambitious in Tycho **from scratch** — a key-value store, a small
interpreter/DSL, a network service. My experience is *editing* a large existing program;
the value-semantics constraints (no shared-mutable graph) bite hardest at **design** time,
not code time, and only greenfield surfaces where. Its friction is what should generate the
next round of ergonomic tickets — far better signal than speculation. The project already
has a dogfood habit (`examples/webserver`, `weblog`, `site`); this is the next, harder one.

---

## Suggested order

**P0 (UFCS built-ins)** first — small, high-frequency, immediate quality-of-life. Then
**P2 (generic `type_of`)** → **P1 (finish diagnostics)** since they're coupled. Then
**P1 (`Pool`)**, the bigger structural piece. **P2 (greenfield dogfood)** runs in parallel
and feeds everything else.

## Cheat sheet

| Thing | Where |
|---|---|
| Reference compiler | `src/tychoc.c` |
| Self-hosted compiler | `compiler/tychoc0.ty` |
| Build reference | `make tychoc` |
| Self-host parity | `make fixpoint` |
| Full gate | `env -u LD_PRELOAD make ci` (fast: `make fixpoint` + `make test`) |
| Golden tests | `tests/*.ty` + `tests/<x>.out` |
| Reject tests | `tests/reject/*.ty` (both compilers must reject) |
| Fuzzer | `fuzz/gen.py`, `make fuzz` |
| Doc-link gate | `make check-links` |
| Core library | `corelib/`, imported `core:<pkg>` |
| Behavior docs / rationale docs | `docs/reference/` / `docs/guides/` |
