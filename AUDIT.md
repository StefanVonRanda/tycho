# Tycho language-semantics audit — 2026-07-13

> Multi-agent audit of language semantics (semantic corner-cases + value-semantics/
> determinism), run across both compilers with live repros and adversarial
> verification; resolutions defer to Odin/Swift/Go. **Status + resolution log
> reflect the completed fix campaign.**

## Summary

**19 confirmed inconsistencies** (12 high · 5 medium · 2 low); 4 dismissed.

**All 19 are resolved.** Every genuine cross-compiler divergence is fixed so the
two compilers agree; the few that turned out to be consistent-across-compilers were
closed by amending the spec (the used `string + char` ergonomic) or recorded as a
deferred enhancement (array/tuple expected-type push, u64-literal widening — both
spec-level, no memory unsafety).

### Resolution log

| Commit | Item | Findings closed |
|---|---|---|
| `6a784a3` | UB-1 | closure/array-element map UAF + aliasing |
| `b51786b` | UB-2 | scalar-array element inout codegen crash |
| `f78a80b` | UB-3 | `_`-not-last + duplicate match arms |
| `40c019e` | UB-4 | out-of-range integer literal wrap |
| `2ed088c` | UB-6 | silent scalar coercion + explicit-typearg arg mismatch |
| `538f9e3` | UB-7 | inout + by-value same-var alias |
| `a63f026` | UB-5 | affine Task + channel-escape + send payload |
| `a27e19b` | — | map-key read/get/delete; bare Some/Ok/Err payload; char-as-type; string+char spec |

### Newly surfaced (follow-up, not one of the 19) — FIXED

- **tychoc0 did not type-check an aggregate Some/Ok/Err payload against an
  annotation** — `x : Option(int) = Some("hi")` was accepted by tychoc0 (rejected by
  tychoc). Pre-existing; made visible by the bare-sum-ctor fix. **FIXED — d829375**
  (recursive `check_sum_annot` mirroring tychoc's `resolve_exp`; only a concrete leaf
  is compared, bare `None`/`Ok`/`Err` still adapt from context).
  - **Residual hole closed (follow-up).** d829375 compared only the `ok`-listed leaf
    kinds (`EStr`/`EBool`/`EChar`/`EVar`/`ECall`) and silently *skipped* everything else
    — so a concrete int/float literal, an arithmetic expr, and an array/tuple/map literal
    payload were never checked. `x : Option(string) = Some(42)` (and `Err(5):Result(int,string)`,
    `Some([1]):Option([u32])`, …) fail-opened in tychoc0 where tychoc rejects, and the
    accepted program stored an `int` in a `string` slot → **use-after-free / segfault**
    (type confusion) at read. `check_sum_leaf` now (a) gives int/float literals their real
    adaptation rule (numeric slots only, mirroring `lit_ok`) and (b) compares the remaining
    concrete payload kinds (`EBin`/`EArrLit`/`EMapLit`/`ETuple`/`EIndex`/`ESlice`) via
    `type_of`; only bare `[]`/`{}`, lambdas and `&x` still adapt/skip. Locked by
    `tests/reject/sum_annot_{int_payload_nonnumeric,err_payload_mismatch,array_payload_widen}.ty`
    (differential: both compilers reject). fixpoint B==C, test 347/0, type/eq-parity green.
  - **Extended to ALL supplying positions (follow-up 2).** The above wired the check only
    into a typed *declaration* (`STypedDecl`). The same fail-open persisted at every other
    position that supplies a value against a known type — a `return`, a call *argument*, a
    place assignment (`b.o = Some(42)`, `m[k] = Some(42)`), a plain reassignment, and a
    struct-literal field — all accepted `Some(42)` into `Option(string)` in tychoc0 where
    tychoc rejects (same int-in-string UAF). A shared helper `check_sum_pos` (guarded: sum
    ctor against an Option/Result want) is now called alongside the existing `nt_check` at
    `SReturn`, `SAssign`, `check_place_assign`, `check_struct_ctor`, and `check_call_args`.
    Locked by `tests/reject/sum_annot_{return,arg,place}_payload.ty`. fixpoint B==C, corelib
    3-way green, test 351/0, type/eq-parity green, fuzz N=1000 FAIL=0 (no false-reject at the
    new positions across the differential accept-fuzzer's Some/Ok/Err arg/return sites).

- **Parity-discipline sweep (2026-07-17) — 6 position gaps FIXED.** Generalizing the
  sum-payload lesson: a check wired at a typed decl often isn't wired at the other value-
  supplying positions. A differential battery (mismatch-class x position) found the same
  fail-open shape for two more classes, all tychoc0-accepts-where-tychoc-rejects:
  - **Silent scalar coercion (UB-6 completion).** 2ed088c added `scalar_coercion_bad` at
    decl/arg/return/array-elem but omitted **reassignment** (`x = 2.5`), **place assign**
    (`b.f = 2.5`, `m[k] = 2.5`), **struct-literal field** (`S(2.5)`), and the **map-key
    write** (`m[2.5] = 1`, whose write path used bare `nt_check` while the read path
    base-checks via `check_mkey`). Each silently truncated a float into an int slot.
  - **Newtype identity at the array-element boundary.** `[Id(1), y]` (y a raw int) erased
    the identity — the elem loop only ran `scalar_coercion_bad` (base-equal, so blind to
    identity); added `nt_check_e` between the first element and each subsequent one.
  Fixes wired `scalar_coercion_bad` at SAssign / check_place_assign / check_struct_ctor /
  the map-key write, and `nt_check_e` at the array-element loop. Locked by
  `tests/reject/{scalar_coerce_reassign,scalar_coerce_place,scalar_coerce_struct_field,scalar_coerce_map_key,newtype_array_elem}.ty`.
  - **Newtype element/value identity survives a binding — FIXED (follow-up).** The sweep
    surfaced a pre-existing OVER-strict divergence (tychoc0 rejects valid code, fail-closed):
    `for k in keys(m)` over a newtype-keyed map then `m[k]`, and more generally `for x in
    [Id]` / `x := a[i]` / `x := m[key]` for a newtype element/value — tychoc0 rejected a
    later `m[k]` or newtype-typed use as "a plain int" where tychoc accepts. Root cause:
    `type_of(EIndex)` `resolve_nt`-erased a newtype array-element / map-value to its base, so
    the `SDecl` binding stored the base and the value's identity was lost (direct use still
    worked via `nt_skin_of` off the container, but a bound variable has no container to
    recover from). Fix: `type_of(EIndex)` keeps a newtype element/value's identity (returns
    `Id`, not the erased base), so the binding stores it and every pass agrees. `nt_check`
    reads identity only via `nt_skin_of`, so nothing double-counts. Matches tychoc across a
    broad battery (str/to_under/compare/nested/Some-payload/struct-field/map-key-from-array);
    raw-base-key / float-key / array-elem-mismatch still reject. Locked by
    `tests/newtype_elem_identity.ty`. fixpoint B==C, corelib 3-way green.

- **inout / sink argument type-checks (2026-07-17) — 4 fail-open gaps FIXED.** Extending
  the sweep to the inout/sink and generic-instantiation classes: generics were clean, but
  `check_call_args` type-checked only plain by-value params. An **inout** param ran ONLY
  the `is_eaddr` "pass &variable" check — never a type check — and a **sink** param was
  checked NOWHERE. So `f(&y)` with `y:int` into `inout Id` (and `Id` into `inout int`),
  `f(2.5)` into `sink int`, and `f(y:int)` into `sink Id` all fail-opened in tychoc0 where
  tychoc rejects. (Scalar float/string through inout already fail-closed via a downstream
  cc error; only the newtype-inout and sink cases were true fail-opens.) Fix: the inout
  branch now runs `nt_check` against `base_ty(pp[k])` (the `&place`'s identity must match;
  `nt_skin_of(EAddr)` recurses to the inner), and the sink branch runs the full
  `nt_check` + `check_sum_pos` + `scalar_coercion_bad` like a normal by-value arg. Locked by
  `tests/reject/{inout_arg_newtype,sink_arg_scalar,sink_arg_newtype}.ty`. fixpoint B==C,
  corelib 3-way green. Broader probe (inout composite / sink Option-payload / inout
  exclusivity `swap(&x,&x)` / explicit-typearg / nested + newtype generics) found no further
  divergence — the parity sweep is closed across sum-payload, scalar-coercion, newtype-
  identity, inout/sink, and generic-instantiation.

- **Base-type mismatches upgraded to clean diagnostics (2026-07-17).** The remaining
  fail-CLOSED cases — a base-type mismatch that is neither a newtype-identity nor a
  scalar-numeric coercion (string↔int, `[int]`↔int, a tuple with a wrong element, an int
  literal into `string`) — used to be rejected only by the emitted C failing to compile
  (`cc` error), not by tychoc0 itself. A new guarded helper `base_type_mismatch` gives a
  clean diagnostic at every supplying position (decl / reassign / return / argument /
  place / struct field / map key / array element / inout `&place` / sink). It fires ONLY on
  SYNTACTICALLY-typed literals (`EStr`/`EBool`/`EChar`/array/map/tuple literals) and an inout
  `&place` (compared directly) — a var / call / index / binop is skipped because its `type_of`
  is unreliable inside a generic body (a match binding over a generic enum reports the
  pre-mono typaram; that false-rejected `generic_enum_array.ty` until the guard was added),
  and scalar-vs-scalar is left to `scalar_coercion_bad` (which owns literal adaptation). Not a
  soundness or parity change (both compilers already rejected these) — purely a better error.
  Locked by `tests/reject/base_mismatch_*.ty`. fixpoint B==C, corelib 3-way green, full gate
  green. (A base mismatch supplied by a var/call/index still falls back to the cc error.)

## Confirmed inconsistencies

### C1. [HIGH] Escaping closure capturing a bare map is a use-after-free in tychoc0 (segfault / silently wrong value); tychoc is correct

- **Status:** **FIXED** — 6a784a3 (UB-1).
- **Dimension:** `closures` · **Kind:** unsound-or-ub
- **Repro:**

```tycho
fn make() -> fn(int) -> int:
    m := []int: int
    m[10] = 1
    m[20] = 2
    return fn(k: int) -> int: m[k]
fn main():
    f := make()
    println(str(f(10)) + " " + str(f(20)))
```

- **tychoc (C reference):** Int-keyed repro: accepts, compiles native binary, runs exit 0, prints "1 2" (correct — captured map deep-copied/re-homed). String-keyed repro: accepts, exit 0, prints "1 2". Both correct, matching spec's deep-copy re-home.
- **tychoc0 (self-hosted):** Int-keyed repro: accepts, generated C compiles+runs exit 0, prints "0 2" (WRONG — first key reads recycled arena memory). Confirmed miscompile: generated Env_0_copy (line 250) is `Env_0* d = hbox(a,sizeof(Env_0),s); return d;` — a shallow bitwise copy with NO map deep-copy; h_m's slots live in &_scope which is arena_free'd on the same return line while the re-homed env still points at them. ASan build also prints "0 2" exit 0 (arena_free recycles blocks, not malloc-free, so no ASan trap — silent wrong value, not a crash). String-keyed repro: accepts, exit 0, prints "1 2" reproduced 5x — does NOT segfault.
- **Evidence:** Root cause: compiler/tychoc0.ty:6190-6203 elem_deepcopy enumerates str/bytes/func/array/tuple/option/result/enum/heap-struct but has NO is_map branch (returns false for maps). This gate controls the escape re-home thunk at compiler/tychoc0.ty:14657 (if elem_deepcopy(...) d->cN = cp_field(...)), so the map capture stays a shallow hbox copy pointing into the freed function arena. The sibling deep-copier cp_field DOES handle maps (compiler/tychoc0.ty:6266, `if is_map(rt): return mfam(rt)+"_copy"`), proving the omission is internal. tychoc deep-copies correctly. Spec docs/spec/11-functions.md:63-66 and docs/spec/09-expressions.md:136 require an escaping closure's environment to be re-homed (deep-copied) into the caller's storage on return. Struct-WRAPPING-a-map capture (routes through struct _copy) and map-via-returned-array both happen to work, isolating the bug to a bare map on the direct SReturn->copyenv path.
- **Precedent (Odin/Swift/Go):** Swift: a closure that captures a Dictionary and is returned from a function keeps it alive via ARC and reads back correct values (no crash). Go: a closure capturing a map and returned keeps the map reachable via GC and reads correct values. Neither language permits a returned closure's captured collection to dangle. Tycho's own spec mandates the deep-copy re-home that tychoc already performs.
- **Recommendation:** Add `if is_map(et): return true` to elem_deepcopy (compiler/tychoc0.ty:6190) so captured maps route through cp_field's existing mfam(rt)+"_copy" path in both the env-build (line 5596) and escape re-home (line 14657) sites, matching tychoc and the spec's deep-copy-and-re-home rule.
- **Verifier note:** Real cross-compiler divergence for the int-keyed case: tychoc "1 2" vs tychoc0 "0 2", both accept and exit 0. Genuine soundness bug in tychoc0 — its Env copier (Env_0_copy) shallow-copies an env struct containing a map, so the map's backing slots dangle into the arena_free'd _scope; the escaping closure reads recycled memory and returns 0. tychoc deep-copies (map_int_int_copy), matching the spec/thesis deep-copy re-home contract. The claim's string-keyed "SEGFAULTS exit 139" sub-claim is FALSE (string variant reliably prints "1 2" on both, 5 runs), and the failure mode for int is a silent wrong value, not a segfault — but the core claim (escaping closure capturing a bare map is unsound/UB in tychoc0 while tychoc is correct) is confirmed. Precedent: Go (map+GC) and Swift (Dictionary+ARC) both keep a returned closure's captured collection alive and read correct values; closest cite Go/Swift, and Tycho's own contract already mandates the deep-copy tychoc performs.

### C2. [HIGH] Non-escaping closure capturing a map aliases the original in tychoc0 (value-semantics / determinism violation); tychoc deep-copies

- **Status:** **FIXED** — 6a784a3 (UB-1).
- **Dimension:** `closures` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn main():
    m := []string: int
    m["a"] = 1
    cl := fn(k: string) -> int: m[k]
    m["a"] = 99
    m["b"] = 2
    println(str(cl("a")) + " " + str(cl("b")))
    println(str(m["a"]) + " " + str(m["b"]))
```

- **tychoc (C reference):** Accepts, compiles, runs. Output:
"1 0"
"99 2"
Closure captured a deep copy of the map at creation: cl("a")=1 (pre-mutation value), cl("b")=0 (key "b" absent in the captured copy). The original map still reflects its own later inserts (99, 2). Matches spec.
- **tychoc0 (self-hosted):** Accepts, generated C compiles clean (cc_exit=0), runs. Output:
"99 2"
"99 2"
The closure shares the original map's buckets: post-capture mutations m["a"]=99 and the brand-new m["b"]=2 are BOTH observed through cl(). Aliasing, not deep copy.
- **Evidence:** Same root cause: compiler/tychoc0.ty:6190 elem_deepcopy omits is_map, so the env-build capture copy at compiler/tychoc0.ty:5589-5601 (guarded `if elem_deepcopy(dc, ctx, ct[j])` at line 5596) skips the deep copy and stores the map header/bucket pointer by shallow value, aliasing the original. Contrast: capturing an ARRAY under the same construction IS deep-copied (tests/closures.ty vals/vsum keeps 6 after vals[0]=100), so arrays and maps are treated inconsistently at capture. Spec docs/spec/09-expressions.md:136: 'A closure captures by deep copy at creation, so later mutation of the originals is not observed by the closure (value semantics).' tychoc honors this; tychoc0 does not.
- **Precedent (Odin/Swift/Go):** Decisive precedent is Tycho's own value-semantics thesis and internal consistency: arrays already deep-copy on capture (tests/closures.ty) and the spec (09-expressions.md:136) says captures do not observe later mutation. Swift value types (Array/Dictionary are copy-on-write value types) match this no-aliasing spirit. Go/Swift closures over a var map would instead SHARE it, but Tycho deliberately rejects aliasing, so the correct behavior is tychoc's deep copy, not tychoc0's sharing.
- **Recommendation:** Same one-line fix: add `if is_map(et): return true` to elem_deepcopy (compiler/tychoc0.ty:6190). Then the env-build path (line 5596) invokes cp_field's map deep-copier, matching array-capture behavior, tychoc, and the spec. Add a regression test alongside tests/map_closure_value.ty covering capture-then-mutate and escaping-map-capture.
- **Verifier note:** REAL cross-compiler divergence AND spec mismatch. `diff` shows line 1 differs ("1 0" vs "99 2"); both accept the program but produce different runtime output.

Spec is decisive and unambiguous. docs/spec/09-expressions.md:122-125: "A closure captures by deep copy at creation: the values of the free variables it references are copied into the closure's environment when the closure is formed, so later mutation of the originals is not observed by the closure (value semantics)." Reinforced verbatim at 11-functions.md:63-64 and 02-grammar.md:327. The array precedent tests/closures.ty:2-3 states the same rule ("captures are deep-copied into the env so the closure is independent of later mutation of the captured variable").

tychoc is correct (deep copy). tychoc0 aliases the map's buckets = value-semantics / determinism violation, directly contradicting the spec and its own sibling array-capture behavior.

Precedent note: Go and Swift closures would both SHARE the variable (Go captures by reference; Swift captures the var by reference even though Dictionary is a COW value type), so under those languages the tychoc0 result would be "correct". But Tycho's own spec DELIBERATELY rejects capture-by-reference in favor of deep-copy value semantics (its central thesis). This is a documented Tycho-specific choice, so the resolution defers to Tycho's spec, not to Go/Swift: fix tychoc0 to deep-copy captured maps, matching tychoc and the already-correct array-capture path.

### C3. [HIGH] tychoc0 does not enforce the non-escaping / non-storable Channel rule (§23.1); an escaping channel handle compiles and runs instead of being rejected

- **Status:** **FIXED** — a63f026 (UB-5).
- **Dimension:** `concurrency-determinism` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn mk() -> Channel(int):
    ch := channel(int, 4)
    return ch
fn main():
    print("x")
```

- **tychoc (C reference):** REJECTED at compile time. cc_compile_exit=1, no binary produced. stderr: "/tmp/.../prog.ty:1: error: a function cannot return a channel -- create it in the owning scope and pass it down" with the source line + caret. Matches claimed behavior exactly.
- **tychoc0 (self-hosted):** ACCEPTED. c0_gen_exit=0 (emitted C), c0_cc_exit=0 (cc compiled clean, no warnings), c0_run_exit=0, stdout="x". The escaping-channel-return program compiles and runs instead of being rejected. Matches claimed behavior exactly.
- **Evidence:** spec docs/spec/13-concurrency.md:100-105; runtime free-at-creating-scope runtime/tycho_rt.c:441-444; reject fixtures tests/conc/reject/chan_{ret,field,reassign,inline,capture,arr}.ty; tychoc rejects (messages quoted), tychoc0 accepts (chan_ret/field/reassign/inline/capture run to completion)
- **Precedent (Odin/Swift/Go):** Swift enforces escape analysis at compile time (`@escaping` / non-escaping closures, noncopyable non-escapable types). Go is the deliberate counter-precedent: Go channels ARE first-class (returnable/storable) because Go has a GC — Tycho chose the non-escape restriction precisely to keep its arena/value-semantics invariant, so the check must actually hold. Defer to the spec's own rule (Swift-style compile-time escape rejection), not Go's first-class model.
- **Recommendation:** Port tychoc's channel-escape/non-storable checker into tychoc0 (reject channel as return type, struct field, container element, reassignment target, closure capture, and non-declaration channel() sites). Rule is normative in spec §23.1: "A Channel(T) cannot be returned, stored in a container, or captured." Add to the proposed conc parity CI lane.
- **Verifier note:** REAL cross-compiler divergence AND spec mismatch. docs/spec/13-concurrency.md:103 states verbatim: "A `Channel(T)` cannot be returned, stored in a container, [captured...]". tychoc enforces this rule (rejects with a precise diagnostic); tychoc0 silently accepts the same program, emitting C that compiles and runs. This is (a) accept-vs-reject divergence and (c) spec-mismatch. The claimed UAF is a real latent hazard: the runtime frees a channel at its creating scope's exit, so a returned handle would dangle the moment a caller used it — the minimal repro doesn't dereference the returned channel (main just prints "x"), so the run itself doesn't crash, but the checker that should have blocked it is absent in tychoc0. This is precisely the kind of check the arena/value-semantics invariant depends on. Precedent: defer to Swift — compile-time escape rejection of non-escapable handles (the spec's own model), NOT Go's GC-backed first-class channels. High severity: a soundness/escape-analysis check the reference compiler enforces is entirely missing from the self-hosted compiler, and the design explicitly relies on it holding.

### C4. [HIGH] tychoc0 does not enforce the affine Task rule (§21); copying a Task handle miscompiles to a SIGSEGV that tychoc statically rejects

- **Status:** **FIXED** — a63f026 (UB-5).
- **Dimension:** `concurrency-determinism` · **Kind:** unsound-or-ub
- **Repro:**

```tycho
fn f(n: int) -> int:
    return n
fn main():
    t := spawn f(1)
    u := t
    print(str(wait(u)))
```

- **tychoc (C reference):** REJECTED at compile time. cc_compile_exit=1, stderr: `prog.ty:5: error: a task handle cannot be copied or re-bound -- bind the spawn directly (t := spawn f(...))` with the caret line `5 |     u := t`. No binary produced. Matches the claim exactly.
- **tychoc0 (self-hosted):** ACCEPTED. c0_gen_exit=0 (emitted C, no error), cc_exit=0 (C compiled clean), then the binary CRASHED at runtime: c0_run_exit=139 (128+11 = SIGSEGV) with empty stdout. Double-owned/double-joined Task, exactly as claimed.
- **Evidence:** spec docs/spec/13-concurrency.md:52-61 (affine+structured Task); reject fixtures tests/conc/reject/{alias,reassign,container,capture}.ty; tychoc rejects (messages quoted), tychoc0 accepts+SIGSEGV (observed exit=139)
- **Precedent (Odin/Swift/Go):** Swift's noncopyable types (`~Copyable` + `consuming`) enforce move-only, use-exactly-once, no-copy/no-store at COMPILE time — exactly Tycho's affine Task contract in §21. Swift rejects a second use of a consumed value at compile time rather than crashing at run time; tychoc matches this, tychoc0 must too.
- **Recommendation:** Port tychoc's affine-Task checker into tychoc0 so the copy/reassign/store/capture of a `Task(T)` are front-end rejected. The rule is already normative (spec §21: "MUST NOT be copied, reassigned, stored in a container, captured by a closure"); tychoc is the reference — tychoc0 is the divergent side. Add a conc affine-parity CI lane (none of these are covered by the existing parforparity lane).
- **Verifier note:** Real cross-compiler divergence (accept vs reject) + spec mismatch + UB. Spec §21 at docs/spec/13-concurrency.md:70 explicitly says "A Task handle cannot be copied, reassigned, stored in a container, captured by..."; docs/spec/03-types.md:223 and 16-builtins.md:213 reinforce that Task(T) is "affine, non-storable". tychoc enforces this rule statically with the exact message quoted; tychoc0 silently accepts the copy `u := t`, emits C, and the resulting binary SIGSEGVs (exit 139) from the double-owned/double-joined Task. This is unsound: a program the spec forbids is miscompiled into a runtime crash. Swift precedent: ~Copyable + consuming types reject a second use of a consumed value at compile time rather than crashing at runtime — tychoc matches Swift, tychoc0 must too. Severity high stands: silent acceptance of memory-unsafe UB is the worst class. Note: I verified only the copy case (`u := t`) end-to-end; the claim's sibling cases (reassign/container/capture) are plausible and consistent with the same missing check but I did not run them — the confirmed copy case alone establishes the inconsistency.

### C5. [HIGH] tychoc0 skips argument/parameter matching on explicit-type-argument generic calls, silently coercing (float→int) and emitting ill-typed C

- **Status:** **RESOLVED** — closed by 2ed088c (UB-6): tychoc0's argument type-check now catches the mismatch on explicit-typearg calls; both compilers agree.
- **Dimension:** `generics-newtype` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn id(x: $T) -> $T:
    return x
fn main():
    f := 3.9
    r := id$(int)(f)
    println(str(r))
```

- **tychoc (C reference):** Rejects all four cases at compile time with a clean Tycho diagnostic. Primary: `prog.ty:5: error: argument 1 of 'id' is float, which does not fit the parameter pattern`. Mirrors: `id$(float)(7)` -> "argument 1 of 'id' is int..."; `id$(string)(42)` -> "argument 1 of 'id' is int..."; `pair$(int,string)(5,9)` -> "argument 2 of 'pair' is int...". Exit 1 in every case, no binary produced.
- **tychoc0 (self-hosted):** Accepts all four, skipping argument/parameter type-checking on explicit-type-arg generic calls. `id$(int)(3.9)` -> emits C, compiles clean, RUNS and prints "3" (3.9 silently truncated to int). `id$(float)(7)` -> compiles clean, runs, prints "7.0" (int silently widened to float). `id$(string)(42)` and `pair$(int,string)(5,9)` -> emit ill-typed C (int passed where char* expected) that fails only at the cc stage with a raw GCC error ("In function 'h_main'": -Wint-conversion), never a Tycho diagnostic. c0 gen exit is 0 in all four.
- **Evidence:** compiler/tychoc0.ty:12667 `if len(explicit) > 0:` branch binds the explicit params and SKIPS the match_typaram_str inference loop (else branch, tychoc0.ty:12694-12698) that structurally validates each value arg against its parameter pattern; the only remaining per-arg guard, nt_check (tychoc0.ty:9778-9787), compares newtype *skins* only (it catches UserId-vs-int but es==ask=='' for raw float-vs-int, so it passes). tychoc's instantiate_generic runs match_type for EVERY value arg unconditionally (src/tychoc.c:6524-6530) even after explicit typeargs bind the params (src/tychoc.c:6517-6523), so it catches the base-type mismatch. Observed: /tmp/pr/p24.ty (float var), p20.ty (float literal), p21.ty (int->float), p18.ty, p26.ty.
- **Precedent (Odin/Swift/Go):** Go generics are the closest precedent (identical shape: explicit instantiation `f[T](args)`). `func id[T any](x T) T { return x }; id[int](3.9)` is a compile error — once T is fixed by the explicit type argument, each value argument must be assignable to the instantiated parameter type, and float→int is not implicitly assignable. Go rejects; tychoc's reject matches Go. Swift and Odin likewise forbid implicit Double/Float→Int narrowing.
- **Recommendation:** In tychoc0's mono_instantiate, when explicit type args are supplied, still run match_typaram_str (or an equivalent base-type + newtype-identity check) of each value argument against the now-concrete parameter type, exactly as tychoc's instantiate_generic does — do not rely on nt_check (skin-only) or the downstream C compiler. Defer to Go: the argument must be assignable to the instantiated parameter type; no implicit numeric coercion.
- **Verifier note:** Real cross-compiler divergence (accept-vs-reject) AND unsoundness. tychoc fixes the type parameter to the explicit arg then checks each value arg against the instantiated parameter pattern; tychoc0 binds the explicit type args but never re-checks the value arguments, so it coerces float<->int silently and emits ill-typed C for pointer-repr mismatches. The float/int cases are worse than a cc-stage failure: they compile clean and run with wrong values (truncation/widening). Any two types sharing a C representation would compile+run silently wrong. Precedent: Go generics are the exact shape -- `func id[T any](x T) T; id[int](3.9)` is a compile error because once T is fixed by the explicit type argument each value arg must be assignable to the instantiated parameter type, and float->int is not implicitly assignable. tychoc matches Go; tychoc0 does not. Swift/Odin likewise forbid implicit Double->Int narrowing. Fix belongs in tychoc0: instantiate then run the same arg/param match tychoc does.

### C6. [HIGH] tychoc0 silently accepts out-of-range integer literals and wraps them; tychoc rejects

- **Status:** **FIXED** — 40c019e (UB-4).
- **Dimension:** `integer-overflow` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn main():
    x := 99999999999999999999999999
    println(str(x))
```

- **tychoc (C reference):** Rejected at compile time. Exit 1, no binary produced. stderr: "/tmp/.../prog.ty:2: error: integer literal out of range" with source line + caret. Matches claimed lexer overflow check.
- **tychoc0 (self-hosted):** Accepted. Emitted C `long h_x = 99999999999999999999999999;` (cc warned "integer constant is too large for its type"), compiled clean, ran exit 0, printed the wrapped value -2537764290115403777. No range check in tychoc0's lexer/lit_to_int.
- **Evidence:** spec: docs/spec/01-lexical.md:201-205 (literal overflow rule + provenance). tychoc: src/tychoc.c:290-296 (lexer accumulation w/ LONG_MAX guard). tychoc0: compiler/tychoc0.ty:2799 lit_to_int (no range guard); literal text passed through to codegen. Observed runs quoted above.
- **Precedent (Odin/Swift/Go):** Go rejects with `constant 99999... overflows int` at compile time; Swift rejects with `integer literal ... overflows when stored into 'Int'`; Odin rejects an overflowing integer constant. All three fail closed at compile time — none silently wraps an out-of-range literal to an arbitrary runtime value.
- **Recommendation:** Add the same accumulation overflow check to tychoc0's integer lexer/lit_to_int that tychoc has (src/tychoc.c:279-294), so an out-of-range literal is a compile error in both compilers. Defer to Go/Swift/Odin: reject at compile time, never wrap. This is also a spec mismatch — docs/spec/01-lexical.md:205 names the check (`src/tychoc.c:279-285 accumulation + overflow check`) as the normative rule, and tychoc0 does not implement it.
- **Verifier note:** Real cross-compiler divergence, reproduced. tychoc fails closed (rejects out-of-range int literal); tychoc0 silently accepts and wraps to an arbitrary runtime value. One compiler rejects, the other accepts and runs the same program with garbage output — category (a). The C constant folding relies on implementation-defined/UB-adjacent handling of an out-of-range integer constant. Precedent: Go rejects ("constant overflows int"), Swift rejects ("integer literal overflows when stored into 'Int'"), Odin rejects — closest fix is to make tychoc0 match tychoc/Go/Swift/Odin and reject at compile time. Severity high stands: silent wrong runtime value with no diagnostic.

### C7. [HIGH] tychoc0 skips map-key type-checking on rvalue read (m[k]), m.get, and delete m[k] — accepts wrong-typed keys that tychoc rejects

- **Status:** **FIXED** — a27e19b (tychoc0 checks read/get/delete keys).
- **Dimension:** `maps` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
type Slot = int
fn main():
    m : [Slot: int] = []
    m[Slot(3)] = 30
    println(str(m[3]))
```

- **tychoc (C reference):** Rejects wrong-typed map keys at compile time on ALL read/query/delete sites, exactly as claimed. Primary repro `m[3]` on `[Slot:int]`: `prog.ty:5: error: map key must be Slot, got int` (compile_exit=1, no binary produced). Siblings all reject too: `m.get(3,0)` -> "map_get key must be Slot"; `delete m[3]` -> "map_del key must be Slot"; raw `i32` key on `[int:int]` -> "map key must be int, got i32"; `m[B(1)]` on `[A:int]` -> "map key must be A, got B".
- **tychoc0 (self-hosted):** Silently ACCEPTS all wrong-typed keys on read/get/delete, compiles clean, runs. Primary `m[3]`: c0_gen=0, prints "30". Siblings: `m.get(3,0)` prints "30"; `delete m[3]` -> len "0" (deletion succeeded via reinterpreted key); raw `i32` read on `[int:int]` prints "50"; `m[B(1)]` cross-newtype read on `[A:int]` prints "10". BUT tychoc0's write-place and `in` DO reject: `m[3]=30` -> "line 4: map key expects Slot, got a plain int value (newtype identity differs)"; `3 in m` -> "line 5: `in` key must be Slot".
- **Evidence:** tychoc reject sites: src/tychoc.c:4527,:5112,:5131,:5376. tychoc0 key-check sites exist only at compiler/tychoc0.ty:9787 (write) and :5670 (`in`); read lowers via :5515/:5624 and delete via :1188/:1220 with no check. Spec rule violated: docs/spec/03-types.md:166-180 (legal key types are exactly the declared K) and tests/reject/newtype_key_mix.ty ("BOTH compilers reject ... a raw base-typed key").
- **Precedent (Odin/Swift/Go):** Go statically rejects an index/delete/get whose key type differs from the map's key type (map[MyInt]int cannot be indexed by an int variable; map[int32]int cannot be indexed by an int without conversion). Odin distinct types require an explicit cast between a distinct type and its base in both directions. Both make tychoc's reject the correct behavior; tychoc0's base-representation coercion is the defect.
- **Recommendation:** Defer to Go/Odin: tychoc's reject is correct. Add the map_key(mt) identity check to tychoc0's map_get lowering (compiler/tychoc0.ty:5515/5624) and to the delete→map_del desugar (:1188/:1220), mirroring the existing write-place check at :9787 and the `in` check at :5670, so all five key positions (read, write, get, in, delete) enforce identical key-type identity. This closes both the cross-compiler divergence and tychoc0's own internal write-vs-read inconsistency. Consider a parity lane covering wrong-key rvalue read/get/delete, which the current typeparity/eqparity lanes miss.
- **Verifier note:** Confirmed on both axes. (a) Cross-compiler divergence: for identical programs, tychoc rejects at compile time while tychoc0 accepts, compiles, and runs — 5/5 repros. (b) Internal inconsistency within tychoc0: for the SAME wrong key on the SAME map, its write-place and `in` reject, but its read/get/delete accept — arbitrary, not a documented choice. The base-representation reinterpretation is unsound (newtype identity silently coerced away on read). Reference: Go statically rejects indexing/deleting/getting a map with a key of the wrong type (`map[MyInt]int` cannot be indexed by a plain `int`; `map[int32]int` needs explicit conversion) — closest precedent, makes tychoc's reject correct. Odin distinct types likewise require explicit casts both directions. MEMORY note hier-newtype-identity claims tychoc0 "now ENFORCES newtype identity through its checker at 8 boundaries (SHIPPED 2026-06-28)" — the map read/get/delete key sites are a gap in that enforcement. Fix: tychoc0 should emit the key-type check at the read-lowering-to-map_get and delete->map_del sites, matching its own write-place/`in` checks and tychoc's behavior.

### C8. [HIGH] `_` wildcard in non-last position: tychoc rejects, tychoc0 silently accepts (and emits uncompilable C for Option)

- **Status:** **FIXED** — f78a80b (UB-3).
- **Dimension:** `match` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
enum Color:
    Red
    Green
    Blue
fn main():
    c := Green
    match c:
        Red: println("red")
        _: println("other")
        Green: println("green")
```

- **tychoc (C reference):** REJECTS both forms cleanly at compile time. Enum repro: cc_compile_exit=1, stderr `/tmp/.../prog.ty:9: error: a \`_\` wildcard must be the last match arm` with source line + caret. Option repro (Some/_/None): same rejection at line 5. Source confirms: src/tychoc.c:6225 `if (i != s->narms - 1) die_at(..., "a \`_\` wildcard must be the last match arm");`
- **tychoc0 (self-hosted):** ACCEPTS both, position-blind. Enum repro: c0_gen_exit=0, cc_exit=0, run_exit=0, prints "green" — i.e. the `Green` arm written AFTER `_` is still reached, so tychoc0 silently reorders match semantics (hoists `_` to trailing else). Option repro (Some/_/None): emits uncompilable C — cc fails `a.c:195:7: error: 'else' without a previous 'if'` on `} else if (_msub.tag == 0) {` (the `_` branch emitted as bare `} else {` before the None `else if`).
- **Evidence:** src/tychoc.c:6220-6227 (wildcard-last + binds-nothing checks); compiler/tychoc0.ty:9940-9979 (check_match — no position check); tychoc0.ty:8080-8130 (enum codegen hoists `_`); observed C error a.c:195 'else without a previous if'; docs/spec/10-statements.md:34-45.
- **Precedent (Odin/Swift/Go):** Swift: a `switch` case rendered unreachable by an earlier catch-all is a compile-time error/warning ("case is already handled by previous patterns; consider removing it"); Swift's `default` is conventionally last. Go: `default` is explicitly position-independent (taken only when no case matches), so Go would ACCEPT tychoc0's behavior — but never miscompiles. Note: spec §14.3 (docs/spec/10-statements.md:41) only requires a `_` arm be "present", it does not state it must be last — so tychoc is stricter than the spec here.
- **Recommendation:** Pick one rule and make both compilers agree. Recommend deferring to Swift/reference-compiler discipline (fail-closed): teach tychoc0's check_match to require `_` be the last arm and reject arms after it, matching tychoc:6225, and add that rule to spec §14.3. Independently, tychoc0 must never emit uncompilable C — the Option `_`-not-last codegen path (gen_match_optres) is an outright bug even under the current lax frontend.
- **Verifier note:** Genuine cross-compiler divergence (accept vs reject) AND unsoundness (tychoc0 emits uncompilable C for Option; silently reorders arm semantics for enums, printing "green" for an arm that follows the catch-all). Reproduced both repros exactly as claimed; output quoted above. Spec §14.3 (docs/spec/10-statements.md:41) only requires a `_` arm be "present"/"a `_` arm MUST be present" — it does NOT state it must be last, so tychoc is stricter than spec and tychoc0 is looser+broken. Precedent: defer to Swift — a case rendered unreachable by an earlier catch-all is a compile error ("case is already handled by previous patterns"). Go's `default` is position-independent but Go never miscompiles; Swift is the closer cousin (Option/fail-closed lineage per project memory) and matches tychoc's reject. Fix direction: tychoc0's check_match (compiler/tychoc0.ty:9940) should adopt the position check tychoc has, rejecting non-last `_`; the spec should also be tightened to say "last".

### C9. [HIGH] tychoc0 annotated binding does no declared-vs-value type check — silently narrows float→int and widens int→float; tychoc rejects

- **Status:** **FIXED** — 2ed088c (UB-6).
- **Dimension:** `numeric-coercion` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn main():
    y := 2.5
    x: int = y
    println(str(x))
```

- **tychoc (C reference):** REJECTS at compile time. For the primary repro `x: int = y` (y:=2.5): `prog.ty:3: error: declared type int but value is float` (compile exit 1, no binary produced). All variants rejected with position-tagged errors: int->float `declared type float but value is int`; bool->int `declared type int but value is bool`; `g(9.9)` -> `argument 1 of 'g' is float, expected int`; `return 7.9` -> `returning float but proc returns int`; `[1,2.5,3]` -> `array elements must all have the same type`. Binop `2.5 + y` also rejected (`arithmetic requires two ints or two floats`).
- **tychoc0 (self-hosted):** ACCEPTS + generates valid C that compiles clean and runs, silently coercing. Primary repro prints `2` (float 2.5 truncated to int). int->float prints `5.0` (silent widen); bool->int prints `1`; `g(9.9)` prints `9`; `return 7.9` prints `7`; `xs:[int]=[1,2.5,3]` -> xs[1] prints `2`. Only the pure binop position `2.5 + y` is correctly rejected by c0 (gen exit 1: `arithmetic requires two ints or two floats`), confirming the hole is specifically the expected-type/assignment path, not the binop unifier.
- **Evidence:** cc reject: src/tychoc.c:6078 and :6109 (die_at "declared type %s but value is %s"). c0 STypedDecl checker: compiler/tychoc0.ty:5177 (no compat check). Spec: docs/spec/06-conversions.md:3-6 ("no value silently widens or narrows", only literals adapt), 06-conversions.md:11-28 (§8.1 permitted adaptations — float→int NOT among them), 06-conversions.md:64-72 (§8.4). Reproduced runs quoted above.
- **Precedent (Odin/Swift/Go):** Go, Swift, and Odin all reject this. Go: `var x int = f` where f is float64 is a compile error ("cannot use f (variable of type float64) as int value"); even the constant form `var x int = 3.5` errors ("constant 3.5 truncated to integer"). Swift: `let x: Int = someDouble` is an error — no implicit numeric conversion; you must write `Int(someDouble)`. Odin: assigning an f64 to an `int` requires an explicit cast. tychoc matches this; tychoc0 is the outlier.
- **Recommendation:** Fix tychoc0: STypedDecl (and the arg/return/array-element expected-type paths) must reject an initializer whose type is not identical to the declared type, except for the literal adaptations enumerated in spec §8.1 (int-lit→float/u32/u64/f32, float-lit→f32). Spec §8 opening line is explicit: "no value silently widens or narrows" and §8.4 lists mixed-scalar ops as hard errors. Defer to Swift/Go: silent float→int truncation is banned; require to_int()/to_float(). This is the highest-severity item: it is silent lossy data conversion that passes the self-hosted compiler, a value-semantics/determinism violation.
- **Verifier note:** Real cross-compiler divergence (type (a)) AND unsoundness (type (d)): tychoc0's annotated-binding / argument / return / array-element checkers perform no declared-vs-value compatibility check, so lossy narrowing (float->int truncation, bool->int) and silent widening (int->float) are miscompiled instead of rejected. tychoc rejects all of them. Divergence spans 5+ positions, all reproduced with quoted output above; only the binop-unifier position agrees. Precedent (all reject, tychoc matches, tychoc0 is the outlier): Go — `var x int = f` (f float64) is a compile error, and even `var x int = 3.5` errors "constant truncated to integer"; Swift — `let x: Int = someDouble` requires explicit `Int(...)`, no implicit numeric conversion; Odin — assigning f64 to `int` requires an explicit cast. Closest cousin precedent is Swift/Odin (distinct types + explicit casts). tychoc0 should fail-closed and reject; fix belongs in its STypedDecl/expected-type path plus the arg/return/array-element checks.

### C10. [HIGH] Bare None/Ok/Err as the payload of an enclosing Some/Ok/Err: tychoc rejects, tychoc0 accepts (correctly)

- **Status:** **FIXED** — a27e19b (tychoc's resolve_exp pushes the inner type; both accept, identical output).
- **Dimension:** `option-result-none` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn main():
    x : Option(Option(int)) = Some(None)
    match x:
        Some(inner):
            match inner:
                Some(v): println("v " + str(v))
                None: println("inner none")
        None: println("outer none")

(also diverges: `return Some(None)` -> Option(Option(int)); `x : Result(Option(int), string) = Ok(None)`; `x : Option(Result(int,string)) = Some(Ok(1))`)
```

- **tychoc (C reference):** REJECT (compile-fail) on all four variants. Some(None): "prog.ty:2: error: Some(...) needs a concrete value". Ok(None): "error: Ok(...) needs a concrete value". Some(Ok(1)): "error: declared type Option(Result(int, string)) but value is Option(Ok(...))". return Some(None): "error: Some(...) needs a concrete value". Reject site: src/tychoc.c:4374 (die_at "Some(...) needs a concrete value") and :4381 (generic "%s(...) needs a concrete value"). tychoc does not flow the enclosing contextual Option/Result type into a bare nullary/inner variant payload.
- **tychoc0 (self-hosted):** ACCEPT + compile + run cleanly on all four, run_exit=0, semantically-sound output. Option(Option(int))=Some(None) -> "inner none"; Result(Option(int),string)=Ok(None) -> "inner none"; Option(Result(int,string))=Some(Ok(1)) -> "ok 1"; return Some(None) -> "inner none". Emitted C compiled with cc -O2 -fwrapv and ran without error. tychoc0 flows the declared/return contextual type into the nested constructor.
- **Evidence:** tychoc E_SOME handler synthesizes its argument and dies on a bare None/partial: src/tychoc.c:4371-4376 (`Type inner = resolve_expr(e->lhs); if (inner==T_VOID||inner==T_NONE) die_at(..."Some(...) needs a concrete value")`), E_OK/E_ERR same at :4377-4382. The check-mode fn resolve_exp (:5499-5556) calls resolve_expr(e) at :5549 FIRST and only fixes a *top-level* T_NONE/partial at :5550-5555 — it has no E_SOME/E_OK/E_ERR case that threads want's payload type into the constructor argument, so the inner bare None dies before context can reach it. tychoc0 threads the expected payload type inward (accepts). Spec §6.2 rule 6 (docs/spec/04-inference.md:49-50): a bare None/Ok/Err takes its missing type param from the expected T.
- **Precedent (Odin/Swift/Go):** Swift: contextual type flows through `.some(...)` into a nested Optional — `let x: Int?? = .some(nil)` and `let x: Int?? = .some(.none)` both compile, nil/`.none` taking Optional<Int> from context. Swift is the closest precedent (built-in Optional with the same one-free-param shape); it matches tychoc0's accept. tychoc0 is also the spec-faithful reading of §6.2 rule 6.
- **Recommendation:** Defer to Swift/tychoc0: tychoc should accept. Give resolve_exp (src/tychoc.c:5499) an E_SOME/E_OK/E_ERR check-mode case that recurses resolve_exp(e->lhs, payload_of(want)) when want is the matching Option/Result, instead of falling through to synthesis-only E_SOME at :4371. This makes a bare None/Ok/Err fixable at any nesting depth, matching the top-level behavior already at :5550-5555.
- **Verifier note:** CONFIRMED cross-compiler divergence (type (a): accept vs reject on the same program), reproduced on all four cited variants via actual runs. tychoc rejects a bare inner None/Ok/Err as the payload of an enclosing Some/Ok/Err at src/tychoc.c:4374/4381 because it demands a syntactically-concrete payload and does not propagate the enclosing contextual type inward. tychoc0 accepts, flows the declared type in, and produces sound output (verified by compiling+running the emitted C). Precedent: Swift is closest (built-in Optional, same one-free-param shape) — `let x: Int?? = .some(nil)` and `.some(.none)` both compile, the inner nil/.none taking its Optional type from context; this matches tychoc0's accepting, contextual-type-flow behavior. So tychoc0 is the correct side and tychoc is the over-strict outlier. High severity: it is an accept-vs-reject fork on a legitimate, common nested-Option/Result construct (including `return Some(None)`), not a mere output nuance.

### C11. [HIGH] tychoc0 accepts `char` as a spellable type keyword everywhere; tychoc rejects it as an unknown type

- **Status:** **FIXED** — a27e19b (tychoc0 rejects `char` as a written type).
- **Dimension:** `strings-char` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn f(c: char) -> char:
  return c
fn main():
  println(str(f('a')))
```

- **tychoc (C reference):** Rejected at compile: `prog.ty:1: error: unknown type 'char'` (cc_compile_exit=1, no binary produced). Matches spec: char has no spellable type keyword.
- **tychoc0 (self-hosted):** Accepted: emitted C, cc compiled clean (c0_cc_exit=0), ran and printed `a` (c0_run_exit=0). char used as a spellable type in param/return positions.
- **Evidence:** src/tychoc.c:1863-1864 (unknown-type die); compiler/tychoc0.ty:2663 (char in builtin-scalar list); docs/spec/03-types.md:68-70; docs/spec/01-lexical.md:116-117. Observed: 4 separate repros (param/return, local annotation, []char, struct field) all reject on tychoc, all accept+run on tychoc0.
- **Precedent (Odin/Swift/Go):** Go makes the one-byte type spellable (`byte` = alias for `uint8`, `rune` = alias for `int32`); Odin has `u8`/`byte`/`rune`; Swift has `UInt8`/`Character`. All three give the byte/char type a written name. But Tycho's own spec deliberately forbids it: docs/spec/03-types.md:68-70 "`char` ... arises by inference; there is no `char` type keyword" and docs/spec/01-lexical.md:116-117 "there is no `char` or `void` type keyword".
- **Recommendation:** The two compilers must agree. Two ways to resolve: (a) align tychoc0 to the current spec by removing "char" from its spellable-type-name set (tychoc0.ty:2663 and the type-parse path) so it also rejects `char` annotations — matches tychoc + spec today; or (b) if deferring to Go/Odin/Swift precedent (which all name the byte type), promote `char` to a real type keyword in tychoc and update docs/spec/03-types.md:68 and 01-lexical.md:116. Either is fine, but pick one — right now tychoc0 compiles programs tychoc cannot, breaking parity.
- **Verifier note:** Real cross-compiler divergence (accept vs reject on identical program) AND a spec mismatch. docs/spec/03-types.md:70: "there is no `char` type keyword" and docs/spec/01-lexical.md:116-117: "there is no `char` or `void` type keyword (the char type arises only from character literals and inference)". tychoc enforces the spec (src/tychoc.c die_at "unknown type 'char'"); tychoc0 lists "char" among builtin scalar type names (tychoc0.ty:2663) so its type parser accepts it everywhere. tychoc0 is the buggy side. Precedent doesn't rescue it: Go/Odin/Swift all give the byte type a written name, but Tycho's spec is a DELIBERATE choice to omit one, and both compilers must agree with the spec. Fix = make tychoc0 reject `char` in type positions (arises by inference only). Verified by direct run of both compilers; outputs quoted above.

### C12. [HIGH] Same variable passed as inout AND by-value in one call leaks the mutation (value-semantics violation, both compilers)

- **Status:** **FIXED** — 538f9e3 (UB-7).
- **Dimension:** `value-semantics` · **Kind:** unsound-or-ub
- **Repro:**

```tycho
fn f(x: inout [int], y: [int]) -> int:
    x[0] = 99
    return y[0]
fn main():
    a := [5]
    r := f(&a, a)
    println("a=" + str(a[0]) + " r=" + str(r) + " (expect a=99 r=5)")
```

- **tychoc (C reference):** Accepts and runs (no diagnostic). Prints `a=99 r=99 (expect a=99 r=5)`. cc_compile_exit=0, cc_run_exit=0. Control run with a distinct copy `b := a; f(&a, b)` prints `a=99 r=5` — value semantics holds only when the by-value arg is not the inout root.
- **tychoc0 (self-hosted):** Accepts, emits C, compiles clean, runs. Prints `a=99 r=99 (expect a=99 r=5)` — byte-identical to tychoc (diff says IDENTICAL). Control `f(&a, b)` also prints `a=99 r=5`. Both compilers agree on the corrupted value exactly as claimed.
- **Evidence:** docs/spec/07-memory-model.md:28-44 (§9.2 deep-copy invariant), :168-180 (§11.2 exclusivity, only two-inout overlap banned); observed: /tmp/A.ty both print r=99; /tmp/A3.ty (read y before mutate) prints r=5 proving y is not copied; /tmp/F.ty (explicit b:=a copy) prints r=5; reproduced identically for structs (/tmp/E.ty "v=99 r=99") and maps (/tmp/I.ty "m=99 r=99").
- **Precedent (Odin/Swift/Go):** Swift — the Law of Exclusivity. `f(&a, a)` requests an exclusive (modify) access to `a` for the inout argument while simultaneously reading `a` for the by-value argument; Swift rejects this at compile time: "overlapping accesses to 'a', but modification requires exclusive access." Tycho's inout is spec'd as Swift-style copy-in/copy-out (docs/spec/07-memory-model.md:158-165), so Swift is the exact precedent. (Go/Odin have no inout mode; Odin's explicit pointer pass makes such aliasing the programmer's deliberate choice, not an implicit copy.)
- **Recommendation:** Extend the §11.2 exclusivity check (currently only bans passing the same root to TWO inout params — src/tychoc.c:5019, tests/reject/inout_alias.ty) to also reject a call where one argument is `&root...` (inout) and another argument reads the same root variable by value. Defer to Swift: reject at compile time ("overlapping mutable access"). Under value semantics the by-value arg is an independent deep copy (§9.2), so the observed leak is unsound regardless; rejecting is the clean, Swift-consistent resolution.
- **Verifier note:** CONFIRMED as a spec-mismatch / unsound-miscompile — NOT a cross-compiler divergence (both compilers produce identical wrong output a=99 r=99). The claim's observed outputs are accurate.

Why it's a real violation: spec §11.1 (docs/spec/07-memory-model.md, "Copy-in / copy-out semantics") defines `f(&x)` as equivalent to `x = f_body_applied_to(x)` — copy-in/copy-out. Under that model the by-value arg `y` is an independent value snapshot of `a` (=5) taken for the call, so `y[0]` must read 5 regardless of the inout mutation, giving a=99 r=5. The implementation instead passes the by-value array as a shared borrow (same backing storage) and mutates the inout in place, so the write `x[0]=99` leaks into the by-value read `y[0]` → r=99. The control test proves the mechanism: swap in a distinct copy and r=5 on both compilers; alias directly and r=99 on both.

The exclusivity guard is incomplete. Spec §11.2 and the checks at src/tychoc.c:5019 and tychoc0 check_call_args only reject inout-vs-inout root overlap (locked by tests/reject/inout_alias.ty). There is no check for inout-vs-by-value overlap on the same root variable, which is exactly this hole.

Precedent: Swift, cited correctly. This is a Law of Exclusivity violation — `f(&a, a)` requests exclusive (modify) access to `a` for the inout parameter while simultaneously reading `a` for the by-value parameter; Swift rejects at compile time ("overlapping accesses to 'a', but modification requires exclusive access"). Since Tycho's inout is explicitly spec'd as Swift-style copy-in/copy-out, the fix is to extend the §11.2 conservative by-root exclusivity check to also reject a by-value argument that shares a root with an inout argument in the same call (fail closed at compile time). Go/Odin lack an inout mode so are not the precedent.

Severity high (kept): silent wrong-value miscompile with no diagnostic, and it breaks value semantics — the language's core thesis/soundness guarantee — rather than a peripheral feature. Mitigating factor is that it requires deliberately naming the same variable as both inout and by-value in one call, but the correct outcome per the language's own model is a compile-time rejection, and instead it compiles clean and corrupts the read.

### C13. [MEDIUM] tychoc0 does not type-check the send() payload against the channel element type; a wrong-typed send passes the front-end and emits invalid C

- **Status:** **FIXED** — a63f026 (UB-5).
- **Dimension:** `concurrency-determinism` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn main():
    ch := channel(int, 4)
    send(ch, "nope")
    close(ch)
```

- **tychoc (C reference):** Rejects in front-end. cc_compile_exit=1, stderr: `/tmp/.../prog.ty:3: error: send on Channel(int) needs a int value` with source line + caret. No binary produced.
- **tychoc0 (self-hosted):** Front-end ACCEPTS: c0_gen_exit=0, no stderr. Emitted C is invalid; cc rejects it (c0_cc_exit=1): `a.c:189:150: error: assignment to 'long int' from 'char *' makes integer from pointer without a cast [-Wint-conversion]` at the channel send-cell store (`long *_p = ...; *_p = ({... hi_intern("nope") ...});`). No binary; run exit 127 (file not found).
- **Evidence:** spec docs/spec/13-concurrency.md:107 (send deep-copies v of type T); tests/conc/reject/chan_sendmis.ty; tychoc rejects (message quoted), tychoc0 gen exit=0 then cc `-Wint-conversion` error quoted
- **Precedent (Odin/Swift/Go):** Go statically rejects `ch <- "nope"` on a `chan int` ("cannot use \"nope\" (untyped string constant) as int value in send"); Swift and Odin likewise reject a mistyped channel/typed-send at compile time. All three reject in the front end — none defer the type check to the backend C compiler.
- **Recommendation:** Add the send-payload element-type check to tychoc0 (match tychoc's "send on Channel(T) needs a T value"). Relying on cc to catch it is not sound in general (a same-C-representation mismatch, e.g. two distinct newtypes over int, would slip through cc silently). Defer to Go's front-end typed-send rule.
- **Verifier note:** Real cross-compiler divergence (category a): tychoc rejects the mistyped send in the front-end; tychoc0's front-end fails open and defers the type error to the backend C compiler. The two compilers disagree on where/whether the program is rejected — tychoc gives a clean Tycho diagnostic, tychoc0 leaks a raw cc -Wint-conversion error at the deep-copy send-cell store (the exact value-semantics thread-boundary site). Not silent UB here because cc happens to catch it via -Wint-conversion being an error, but that is incidental (a payload type that C would silently coerce, e.g. a differently-shaped int, could slip through). Precedent: Go statically rejects `ch <- \"nope\"` on `chan int` ("cannot use ... as int value in send"); Swift/Odin likewise reject typed sends in the front end. Fix belongs in tychoc0's send() checker to mirror tychoc's "send on Channel(int) needs a int value". Severity medium confirmed: caught before producing a bad binary in this case, but it is a genuine front-end type-hole across the thread/channel boundary.

### C14. [MEDIUM] Duplicate match arms: tychoc rejects, tychoc0 accepts (first arm wins)

- **Status:** **FIXED** — f78a80b (UB-3).
- **Dimension:** `match` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
enum Color:
    Red
    Green
    Blue
fn main():
    c := Red
    match c:
        Red: println("red1")
        Red: println("red2")
        Green: println("g")
        Blue: println("b")
```

- **tychoc (C reference):** REJECTS both. Enum: cc_compile_exit=1, stderr "prog.ty:9: error: duplicate arm for Red" with source line + caret. Option: cc_compile_exit=1, stderr "prog.ty:5: error: duplicate Some arm". No binary produced.
- **tychoc0 (self-hosted):** ACCEPTS both. Enum: c0_gen_exit=0, c0_cc_exit=0, runs c0_run_exit=0, prints "red1" (first arm wins). Option: gen/cc exit 0, runs exit 0, prints "a" (first Some arm wins). No diagnostic at all.
- **Evidence:** src/tychoc.c:6238 / :6261 / :6288 (duplicate-arm rejections); compiler/tychoc0.ty:9940-9979 (check_match has no duplicate detection); observed tychoc0 output "red1" and "a".
- **Precedent (Odin/Swift/Go):** Go: a duplicate case is a hard compile error ("duplicate case X in switch"). Odin: duplicate `case` in a `switch` is a compile error. Swift: an overlapping/duplicate case triggers the "case is already handled by previous patterns" diagnostic. All three of the user's reference languages reject or diagnose duplicate arms.
- **Recommendation:** Defer to Go/Odin (reject). Add a seen-variant set to tychoc0's check_match so a repeated arm name (enum variant, or Some/None/Ok/Err) is rejected with the same message as tychoc:6238/6261/6288, restoring parity.
- **Verifier note:** Real cross-compiler accept-vs-reject divergence, reproduced independently for BOTH the enum and Option forms exactly as claimed. tychoc's duplicate-arm check (src/tychoc.c) fires; tychoc0's check_match never detects repeated arm names, silently taking the first arm. This is precisely the class of divergence the accept/reject parity lanes can't catch (fixpoint is output-only). Reference precedent: Go rejects duplicate switch cases ("duplicate case"), Odin rejects duplicate case, Swift diagnoses "case is already handled by previous patterns" — all three of the user's cousin languages reject. tychoc is the correct side; fix is to add the duplicate-arm check to tychoc0's check_match so it also rejects. Severity medium is fair: not unsound/UB (deterministic first-arm-wins), but a silent accept of a program the reference compiler and all three reference languages reject.

### C15. [MEDIUM] tychoc0 accepts `char` as an annotatable type (and adapts int literal → char); tychoc rejects `char` as an unknown type

- **Status:** **FIXED** — a27e19b (char-as-type rejected) + 2ed088c (int→char coercion rejected, UB-6).
- **Dimension:** `numeric-coercion` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn main():
    c: char = 65
    println(str(c))
```

- **tychoc (C reference):** REJECTS. Both `c: char = 65` and `fn f(c: char)` die at compile: `prog.ty:2: error: unknown type 'char'` (cc_compile_exit=1, no binary produced). tychoc has an internal T_CHAR (src/tychoc.c:477) reachable only via inference (c := 'A'); its type-name parser never maps the identifier `char`.
- **tychoc0 (self-hosted):** ACCEPTS + runs. `c: char = 65; println(str(c))` compiles clean and prints `A`. `fn f(c: char) -> int: return to_int(c)` called as `f('Z')` prints `90`. tychoc0 lists `char` as a builtin scalar in its type-name parser (compiler/tychoc0.ty:2663) and adapts the int literal 65 into the char slot.
- **Evidence:** cc unknown-type: src/tychoc.c:1863-1864; internal T_CHAR exists: src/tychoc.c:477,:1046,:3299. c0 char as type-name: compiler/tychoc0.ty:2663. Grammar omission: docs/spec/02-grammar.md:151. Literal-adaptation rules: docs/spec/06-conversions.md:11-28 (char literals do not adapt; int-lit→char absent). Reproduced runs quoted above.
- **Precedent (Odin/Swift/Go):** Grammar PrimType (docs/spec/02-grammar.md:151) lists `int|float|bool|string|ptr|bytes` and omits `char`, so tychoc matches the published grammar; but spec prose (03-types.md §5.2) treats `char` as a first-class type, making the omission itself inconsistent. On the int-lit→char adaptation: Swift (`Character` is not initializable from an integer literal) and Odin (distinct types, explicit casts — an int does not become a rune/byte implicitly) both require an explicit conversion; the repo's own idiom is `chr(65)`.
- **Recommendation:** Reconcile the two compilers. Either (a) add `char` to PrimType in the grammar and to tychoc's type-name parser so both accept the annotation, or (b) remove it from tychoc0's type-name set so both reject. Given `char` is a real type in tychoc's type system and throughout the spec prose, (a) is the natural fix — but if adopted, tychoc0 must stop silently adapting an int literal into a `char` annotation (deferring to Swift/Odin: require chr()/an explicit cast), otherwise it reintroduces the silent-coercion class from the first finding.
- **Verifier note:** Real cross-compiler divergence, reproduced: tychoc rejects `char` as an unknown type; tychoc0 accepts it and prints "A" (diff shows non-IDENTICAL: cc errors vs c0 output "A"). Spec resolves it AGAINST tychoc0: docs/spec/03-types.md:70 (§5.2.4) states verbatim "there is no `char` type keyword" — char "arises by inference" only. Grammar PrimType (docs/spec/02-grammar.md:151) lists int|float|bool|string|ptr|bytes and omits char, consistent with that. So tychoc's rejection is spec-correct; tychoc0 both (1) admits an unsanctioned type keyword and (2) performs an int-lit→char adaptation the spec's narrow char/int interop (§5.2.4: only `char ± int`) never grants. Precedent aligns with the spec: Swift's Character is not initializable from an integer literal, Odin uses distinct types with explicit casts (int does not implicitly become a rune/byte); repo idiom is chr(65). Fix = tychoc0 should reject the `char` keyword to match tychoc and the spec. Severity medium is fair: not just cosmetic — tychoc0 silently coerces int→char with no spec basis.

### C16. [MEDIUM] Dynamic array/tuple literal does not push the declared element type into a bare None element (order-dependent; both compilers, contra spec)

- **Status:** **RESOLVED** — not a cross-compiler divergence (both reject `[None]`); the Swift expected-type-push (accept `xs : [Option(int)] = [None]`) is a deferred type-inference enhancement, not a soundness gap.
- **Dimension:** `option-result-none` · **Kind:** spec-mismatch
- **Repro:**

```tycho
fn main():
    xs : [Option(int)] = [None]        # rejected
    # xs : [Option(int)] = [Some(1), None]   # ACCEPTED (None fixed from sibling Some)
    # xs : [Option(int)] = [None, Some(1)]   # rejected (None first)
    # t  : (Option(int), int) = (None, 5)    # rejected
    println(str(len(xs)))
```

- **tychoc (C reference):** Rejects [None] and [None, Some(1)] with "error: cannot infer the array's element type from None — put a Some(...) first"; rejects (None, 5) with "error: tuple element 1 needs a concrete value". Accepts [Some(1), None] (element type synthesized from the leading Some), prints len 2. Order-dependent: identical element set accepted or rejected purely on whether a concrete sibling comes first.
- **tychoc0 (self-hosted):** Same accept/reject decisions as tychoc: rejects [None], [None, Some(1)], and (None, 5), each with the generic "type: unknown type ''"; accepts [Some(1), None], emits C that compiles and prints len 2. No cross-compiler divergence in accept/reject or output.
- **Evidence:** E_ARRLIT synthesizes the element type from args[0] (src/tychoc.c:4509 `Type elem = resolve_expr(e->args[0])`) and explicitly rejects a leading None (:4512-4513 "the first element fixes the type, so it can't be a bare None"); siblings are then checked against that (:4514-4515). resolve_exp (:5499) only pushes want into a *bare []* (:5508) or a *fixed-size* array literal (:5516) — there is no case pushing want's element type into a non-empty dynamic array literal's elements, so the `[Option(int)]` annotation never reaches them. E_TUPLE synthesizes every element and rejects a bare None (:4384-4394). Spec §6.1 (docs/spec/04-inference.md:29-30) lists "a tuple or array literal's element type" as an expected-type context, and §6.2 rule 6 (:49-50) fixes a bare None from T — so `x:[Option(int)]=[None]` should type. Implementation ignores the annotation and infers order-dependently.
- **Precedent (Odin/Swift/Go):** Swift: array-literal element type comes from the contextual type — `let a: [Int?] = [nil]` and `let a: [Int?] = [nil, .some(1)]` both compile regardless of order; tuple `let t: (Int?, Int) = (nil, 5)` compiles. Odin similarly propagates the declared element type into a compound literal. Both make element inference position-independent, unlike Tycho.
- **Recommendation:** Defer to Swift: when a dynamic array/tuple literal is checked against a known type, push the expected element/slot type into each element via resolve_exp before synthesizing from args[0] (add the E_ARRLIT-non-empty and E_TUPLE cases to resolve_exp at src/tychoc.c:5499, and mirror in tychoc0). This removes the order-dependence and honors §6.1/§6.2. Consistency argument: bare None is already coercible in return/arg/struct-field/map-value/assignment positions (verified: all accept) — array/tuple literal elements are the lone exception.
- **Verifier note:** Not a cross-compiler divergence — both compilers agree. It is a genuine spec mismatch plus internal (order-dependent) inconsistency. Spec 04-inference.md:26-30 states an expected type flows into "a tuple or array literal's element type," and 04-inference.md:49-50 rule 6 states a bare None "takes its missing type parameter from T (an Option/Result)." Composing these, `xs : [Option(int)] = [None]` should push Option(int) into the element position and fix each None from it — spec says ACCEPT. Both compilers instead synthesize the element type from the sibling elements (accepting only when a concrete Some appears first), so the explicit annotation is ignored for the bare-None case. Same for the tuple (None,5) despite (Option(int),int). Closest precedent is Swift, which the claim cites correctly: `let a:[Int?]=[nil]`, `let a:[Int?]=[nil,.some(1)]`, and `let t:(Int?,Int)=(nil,5)` all compile, position-independent. Severity medium is fair: contradicts the spec's stated inference rule and is ergonomically surprising, but sound and has an easy workaround (annotate or lead with Some). Separately, tychoc0's "unknown type ''" message is unhelpful vs tychoc's targeted diagnostic, but that is a known diagnostic-quality gap, not the semantic finding.

### C17. [MEDIUM] Taking `&arr[i]` of a scalar-element array as an inout argument: tychoc emits invalid C (build fails); tychoc0 accepts and runs

- **Status:** **FIXED** — b51786b (UB-2).
- **Dimension:** `value-semantics` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn inc(x: inout int):
    x = x + 1
fn main():
    a := [5, 6]
    inc(&a[0])
    println(str(a[0]))
```

- **tychoc (C reference):** Rejected via broken codegen. tychoc emitted uncompilable C and cc failed (cc_compile_exit=1). Emitted line: `h_inc(&_t, &((*tycho_arr_C-1020_ptr(&(h_a), 0L))));` producing errors: "'tycho_arr_C' undeclared (first use in this function); did you mean 'tycho_argc'?" and "invalid suffix '_ptr' on integer constant", then "tychoc: C compilation failed". The intended mangled helper name `tycho_arr_C1020_ptr` was corrupted to `tycho_arr_C-1020_ptr` (stray `-` splitting the identifier). No binary produced; run exit 127 (file not found).
- **tychoc0 (self-hosted):** Accepted and ran correctly. c0_gen_exit=0, c0_cc_exit=0, c0_run_exit=0, printed "6". Matches expected (5+1).
- **Evidence:** src/tychoc.c:8398-8400 (E_INDEX lvalue always uses composite ARRC_ID); src/tychoc.c:9880,9894,9995,10060 (tycho_arr_C<id>_ptr declared/defined only for composite array types). Observed bad C: "h_inc(&_t, &((*tycho_arr_C-1020_ptr(&(h_a), 0L))))". Reproduced on [int] (/tmp/K3.ty), [string] (/tmp/L3.ty -> tychoc0 "hi!"), and struct-field scalar array &s.xs[0] (/tmp/O.ty -> tychoc0 "6"). Contrast: &arr[i].field (tests/projections.ty:44) and whole-struct-element &ps[0] (/tmp/L2.ty) compile+run on tychoc, isolating the bug to scalar element types.
- **Precedent (Odin/Swift/Go):** This is a compiler defect, not a design fork — the language admits `&a[i]` as an inout argument (docs/spec/07-memory-model.md:172 discusses `&a[i]`/`&a[j]` overlap rejection, i.e. a single `&a[i]` inout is legal) and tychoc0 already implements it. Swift fully supports inout to an array element (`inc(&a[i])`); so does the value-semantics contract. tychoc must match tychoc0 (and the spec) here.
- **Recommendation:** Fix tychoc's gen_lvalue for E_INDEX at src/tychoc.c:8398: it unconditionally emits `tycho_arr_C%d_ptr` with ARRC_ID(e->lhs->type), but scalar-element arrays ([int]/[string]) have no registered composite id, so ARRC_ID yields a garbage negative token (-1020) and no such helper is declared (helpers emitted only for composite arrays, src/tychoc.c:9880-10060). Emit the scalar-array element-pointer helper (the `tycho_arr_%s_ptr` / arr_fn family used elsewhere) for scalar element types, matching tychoc0's working output. Lock with a differential golden.
- **Verifier note:** Real cross-compiler divergence on a spec-legal program. docs/spec/07-memory-model.md:171-173 states two `inout` args must not share a root variable, giving `&a[i]`/`&a[j]` as the REJECTED overlap case — which entails a single `&a[i]` inout is LEGAL. tychoc0 correctly compiles+runs it (prints 6); tychoc mangles the array-element-pointer helper name (`tycho_arr_C-1020_ptr`, a stray `-` breaking the identifier) and cc rejects the emitted C. This is a compiler codegen defect, not a design fork. Precedent: Swift fully supports `inout` to an array element (`inc(&a[i])`); the value-semantics contract and the Tycho spec both admit it. tychoc must match tychoc0 and the spec. Severity downgraded from high to medium: tychoc fails LOUDLY at C-compile time (fail-closed, no binary, no silent miscompile, no data corruption or UB) — it blocks a legal construct rather than mis-running it. Note the name mangling glitch (negative-number-like `C-1020`) suggests the array type id is being formatted with a sign; the actual defect is in tychoc's `&a[i]`-as-inout-arg codegen path in src/tychoc.c.

### C18. [LOW] tychoc rejects valid u64 literals >= 2^63 and the minimal int literal; tychoc0 accepts them

- **Status:** **RESOLVED** — the divergence is closed by 40c019e (UB-4): both compilers now reject a literal > 2^63-1. Widening the bound to the full u64 range (Go/Swift precedent) is a separate spec amendment (docs/spec/01-lexical.md), deliberately deferred.
- **Dimension:** `integer-overflow` · **Kind:** cross-compiler-divergence
- **Repro:**

```tycho
fn main():
    x := to_u64(18446744073709551615)
    println(str(x))
```

- **tychoc (C reference):** Rejects BOTH programs at compile time. `to_u64(18446744073709551615)` -> `prog.ty:2: error: integer literal out of range` (compile_exit=1, no binary produced). `x := -9223372036854775808` -> same error `prog.ty:2: error: integer literal out of range`. Lexer bounds every int literal by signed LONG_MAX (src/tychoc.c:291-296).
- **tychoc0 (self-hosted):** Accepts BOTH. `to_u64(18446744073709551615)` compiles and prints `18446744073709551615` (run_exit=0; emitted C had a benign "integer constant so large it is unsigned" gcc warning). `-9223372036854775808` compiles and prints `-9223372036854775808` (run_exit=0). No range check performed.
- **Evidence:** tychoc: src/tychoc.c:294 (single LONG_MAX bound, not type-directed). tychoc0: compiler/tychoc0.ty:2799 (no bound). Observed: tychoc `integer literal out of range` vs tychoc0 prints 18446744073709551615 and -9223372036854775808.
- **Precedent (Odin/Swift/Go):** Go, Swift, and Odin all allow the full unsigned range for unsigned-typed literals (e.g. Swift `UInt64.max` = 18446744073709551615 is a valid literal) and all can express the minimum signed value. On these inputs tychoc0's acceptance matches Go/Swift/Odin; tychoc's blanket signed bound is the outlier.
- **Recommendation:** Make the literal-range check target-type-aware (defer to Go/Swift/Odin): permit up to 2^64-1 when the literal's inferred type is u64/unsigned, and permit the minimal signed value. Resolve jointly with the other finding: tychoc0 should gain range checking (reject true garbage) while tychoc should widen its bound to the target type (accept valid u64 / INT_MIN). After the joint fix both compilers accept exactly the representable literals and reject the rest.
- **Verifier note:** CONFIRMED as a genuine cross-compiler divergence: tychoc rejects, tychoc0 accepts the same two programs (accept-vs-reject, category (a)). BUT the claim's framing is inverted. The spec explicitly documents tychoc's rejection as the intended rule: docs/spec/01-lexical.md:195-196 ("literals 0..2^63-1; a larger literal is rejected -- integer literal out of range"), :199 ("the most negative int value -9223372036854775808 has no literal form"), and docs/spec/03-types.md:46 ("-2^63 is obtained only by computation"), with provenance pointing at src/tychoc.c. So tychoc is spec-conformant; tychoc0 is the outlier that silently violates the documented lexer rule. Correct fix = make tychoc0 reject (add the range check to match spec + tychoc), NOT make tychoc accept. The Go/Swift/Odin full-unsigned-range precedent is a real design departure, but Tycho deliberately took the opposite position: there is no unsigned-typed literal syntax; to_u64 receives a signed literal that must fit int, and -2^63 is a computed value not a literal. This is a real tychoc0 parity/soundness gap (self-hosted compiler under-enforces), not evidence that tychoc's bound is wrong. Both repros run and quoted above.

### C19. [LOW] `string + char` is accepted (appends one byte) but `char + string` is rejected — asymmetric, and unsanctioned by the spec's same-type rule

- **Status:** **RESOLVED** — consistent between both compilers (both accept `s + c`, both reject `c + s`) and load-bearing (char_basic, json); documented as the explicit exception to the same-type rule in docs/spec/09-expressions.md §13.2 (a27e19b) rather than removed.
- **Dimension:** `strings-char` · **Kind:** spec-mismatch
- **Repro:**

```tycho
fn main():
  s := "ab"
  c := 'c'
  println(s + c)   # both compilers: "abc"
  # println(c + s) # both compilers: reject
```

- **tychoc (C reference):** Ran all 4 cases. `s + c` (string+char): ACCEPTED, compiled+ran, prints `abc`. `c + s` (char+string): REJECTED at compile — `cs.ty:4: error: arithmetic requires two ints or two floats (got void, string) -- convert one side...`. `s += c`: ACCEPTED, prints `abc`. `s + n` (string+int): REJECTED — `si.ty:4: error: cannot concatenate string with int`. Matches the claim exactly.
- **tychoc0 (self-hosted):** Ran all 4 through tychoc0 (emit C -> cc -O2 -> run). `s + c`: ACCEPTED, prints `abc` (identical to tychoc). `c + s`: REJECTED at gen — `line 4: arithmetic requires two ints or two floats` + caret. `s += c`: ACCEPTED, prints `abc`. `s + n`: REJECTED — `line 4: arithmetic requires two ints or two floats` (generic msg; tychoc gives the more specific "cannot concatenate string with int" but same reject outcome). Both compilers agree on every accept/reject and every runtime output.
- **Evidence:** docs/spec/09-expressions.md:25-26 (arithmetic operands MUST share a type); compiler/tychoc0.ty:5674 ("str + char: append one byte"), :5896 (char->glyph). Observed: `s + c`=abc (accept both), `c + s`=reject both, `s + n`=reject both. The spec never sanctions mixed string/char `+`.
- **Precedent (Odin/Swift/Go):** Go requires explicit conversion: `s + string(c)` — `string + byte`/`string + rune` is a type error. Swift requires `s + String(c)` (no `String + Character` with `+`). Odin requires explicit conversion too. None of the three allow implicit string+char concatenation, and certainly none allow it in only one operand order.
- **Recommendation:** Defer to Go/Swift/Odin: reject `string + char` and require an explicit `str(c)`, matching the same-type operand rule in docs/spec/09-expressions.md:25-26 ("Both operands MUST have the same type"). If the one-byte-append ergonomic is worth keeping, it must at minimum be made symmetric AND written into the spec as an explicit exception — but precedent favors requiring the explicit `str()` conversion.
- **Verifier note:** REAL internal asymmetry (category b), NOT a cross-compiler split — both compilers accept `string+char`, both reject `char+string`, identical output `abc`. Root cause verified in source: tychoc.c:5428-5431 hard-codes the string-concat branch on `lt == T_STRING` only (append one byte, no alloc), with no symmetric char-on-left branch, so `char + string` drops to the numeric arithmetic path and dies. Spec silent: docs/spec/09-expressions.md:25-27 same-type arithmetic rule never lists `string`, and neither string+string nor string+char is sanctioned there — so the whole string+char append is an implementation convenience, and its order-asymmetry is a spec-unsanctioned wart. Precedent (user defers to these): Go requires `s + string(c)` (string+byte/rune is a type error, both orders), Swift requires `s + String(c)` (no `String + Character`), Odin requires explicit conversion — none of the three allow implicit string+char in EITHER order. So the principled fix per cousins is to require an explicit `str(c)` (reject string+char symmetrically), or at minimum make `+` order-symmetric. Severity downgraded medium->low: no soundness/UB, no cross-compiler divergence, both compilers consistent; purely an ergonomic/consistency nit on a convenience feature. The claim's own framing ("internal asymmetry present in both, not a cross-compiler split") is accurate.

## Raised but dismissed on verification

- **[HIGH] Plain indexed assignment `a[i] = rhs` evaluates LHS index vs RHS in opposite order across the two compilers, silently producing different program state** — Eval order of `a[i] = rhs` differs, but the spec (§13.4) declares subexpression evaluation order unspecified by design (C/Go stance). Not a bug.
- **[HIGH] -0.0 vs +0.0 in a composite (float-leaf) map key: hash-by-bits vs eq-by-value breaks the equal-implies-hash-equal invariant, giving seed-visible nondeterministic (tychoc) and cross-compiler-divergent map contents** — Verifier did not confirm a cross-compiler divergence on the ±0.0 composite-key probe.
- **[LOW] tychoc0 emits a generic "type: unknown type ''" where tychoc gives a specific actionable message, for every bare-None-in-aggregate rejection** — Both compilers reject; only the diagnostic text differs.
- **[LOW] has_str(T) predicate rejects char even though str(char) is a valid builtin call** — `has_str(T)` rejects `char` though `str(char)` works; verifier did not confirm it as a cc/c0 divergence.

