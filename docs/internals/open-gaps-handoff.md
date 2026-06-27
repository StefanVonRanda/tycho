# Open gaps — session handoff (2026-06-27)

> Working/TODO doc, NOT reference documentation. Captures known-open tychoc0 gaps
> found by the integration-dogfood probing campaign, with enough detail to resume
> each fix in a fresh session. Delete once the gaps are closed.

## Current state (baseline)

- `main` @ `861486f`, clean working tree, fully pushed. `origin/main` is the **only**
  remote branch.
- `security-hardening` (245 unique commits — the Zig impl + `SECURITY_REVIEW.md`) was
  deleted from the remote but **archived locally** as tag `archive/security-hardening`.
  Re-push: `git push origin archive/security-hardening:refs/heads/security-hardening`.
- All gates green: `make test` (185), `make fixpoint`, `make corelib` (3-way), `make fuzz`,
  ASan/UBSan.
- The dogfood already fixed **5 real compiler bugs** this session (map×closures `1e546a3`;
  generic-struct: container-field `c702dd1`, nested-provenance/W `ca8ea45`, instance-type
  cluster `861486f`). The gaps below are what remains.

---

## GAP 1 — tychoc0 lacks ARRAY + substr bounds checks (memory safety) — HIGH VALUE

**Symptom (differential, tychoc vs tychoc0):**
- `a[5]` on a len-3 array: tychoc aborts (`tycho: index 5 out of bounds (len 3)`);
  tychoc0 returns garbage (reads adjacent arena memory), rc=0.
- `a[-1]`: same — tychoc aborts, tychoc0 reads garbage.
- `substr("hi", 0, 10)`: tychoc clamps to `"hi"`; tychoc0 over-reads the buffer.
- No ASan hit on the array OOB because the read lands inside the one big arena block
  (it's reading other live data, not past the malloc'd region) — still a real OOB /
  info-leak, just not heap-overflow-detectable.

**Why fixpoint missed it:** tychoc0.ty never indexes out of bounds, so the differential
never triggers the divergence.

**Current tychoc0 state (inconsistent):**
- STRING index `s[i]` is ALREADY checked — `hi_sidx` at `compiler/tychoc0.ty:7394`
  (`if (i < 0 || i >= n) { ...out of bounds...; exit(1); }`). Good model to mirror.
- ARRAY index is UNCHECKED — emitted as raw `.data[i]`:
  - read: `compiler/tychoc0.ty:4298`  (`gen_expr(base) + ".data[" + gen_expr(idx) + "]"`)
  - write/place: `compiler/tychoc0.ty:4927` (`gen_place(b) + ".data[" + gen_expr(i) + "]"`)
- `substr` helper UNCHECKED — `compiler/tychoc0.ty:7418`
  (`static char* substr(Arena* ar, const char* s, long a, long b) { long n = b - a;
  char* r = hs(ar, n); memcpy(r, s + a, (size_t)n); return r; }`) — no clamp/validate of a,b.

**Reference contract to match (tychoc / runtime):** `runtime/tycho_rt.c` emits
`tycho: index %ld out of bounds (len %ld)` for arrays (lines ~1037/1045/1134/1142/1220/1228)
and `tycho: string index %ld out of bounds (len %ld)` (~788/800). Match these messages so
behavior is identical on OOB.

**Fix scope (by difficulty):**
1. **substr — SIMPLE.** Add a clamp/bounds check inside the emitted `substr` helper (one
   string literal at 7418). Decide: clamp like tychoc, or abort? Check what tychoc's substr
   does (clamp to len) and match it exactly.
2. **array read (4298) — MODERATE.** Wrap in an eval-once checked access, e.g.
   `({ <cty(arrtype)> _a = <base>; _a.data[hi_bchk(<idx>, _a.len)]; })` where a new preamble
   helper `static inline long hi_bchk(long i, long n){ if(i<0||i>=n){fprintf(stderr,
   "tycho: index %ld out of bounds (len %ld)\n", i, n); exit(1);} return i; }`. Need the
   array's C type (cty of type_of(base)). Nested `a[i][j]` -> nested `({...})`, each with
   its own `_a` (block-scoped, fine).
3. **array WRITE / place (4927) — TRICKIER.** It's an lvalue (`a[i] = v`), so the
   statement-expression trick does NOT work (GCC stmt-expr result is an rvalue, not
   assignable). Options: (a) a pointer-returning checked helper `*hi_aptr(arr.data,
   hi_bchk(i, arr.len))` — but evaluates base twice (bad if base has side effects);
   (b) emit a statement-level guard before the assignment (in the SFieldAssign/SAssign
   codegen, check the index first); (c) check how `gen_place` is consumed and whether a
   helper that takes `&arr` + i and returns `T*` is feasible. The place form is the real
   work — study how tychoc emits checked array writes for the pattern.

**Validation when fixing:** the 3 OOB probes must now match tychoc (abort/clamp), `make
test`, `make fixpoint` (B==C + differential — the added checks change emitted C but B==C
holds since tychoc0 emits them consistently; in-bounds behavior unchanged), `make corelib`,
ASan. Also re-run the bounds-probe batch (see "Probing method").

**Severity note:** tychoc (the primary/reference compiler) IS fully safe; tychoc0 is the
self-hosted bootstrap and its own code never OOBs, so this is defense-in-depth / parity, not
an active hole in shipped tooling. Still worth closing given the project's safety emphasis.

---

## GAP 2 — generic ENUM instance types not resolved (systemic) — tychoc0-only

The struct version of this was fully fixed (`861486f`: `resolve_ginst` + binding-inference
instance-name preference). The **enum parallel** is NOT fixed and is deliberately left
FAIL-CLOSED:
- `channel(Opt(int))` and a generic-enum fn-return fail on tychoc0 (`unknown type 'Opt(int)'`);
  tychoc handles them.
- A 1-line extension of `resolve_ginst` (`... or is_enum(dc,ctx,cand)`) FIXES the fn-return
  case but turns the channel case from a clean compile-error into a **fail-OPEN runtime
  miscompile** (`non-exhaustive match`): the unresolved `Opt(int)` element type breaks the
  channel DEEP-COPY's copier selection -> corrupted tag. So it was **reverted** (never ship
  fail-open).
- The gap is SYSTEMIC: many consumers see the unresolved type (cty, field_type, channel
  deep-copy, match-via-`variant_tag`), unlike the tidy 2-mechanism struct story.
- **Proper fix:** resolve the channel ELEMENT type at the SOURCE (where the channel/`recv`
  type is formed — `type_of` channel arm ~`3456`, `Channel(...)`/`chan_inner`), or a
  systemic mono-pass resolution — NOT whack-a-mole per consumer. Verify the deep-copy +
  match both use the resolved instance.

---

## GAP 3 — recursive generic ENUM with array payload (SHARED, both compilers)

`enum Tree($T): Leaf($T) Node([Tree($T)])` constructed `Node([Leaf(10), Leaf(20)])` fails on
BOTH compilers (tychoc: cc error `unknown type name 'E_Tree'`; tychoc0: `unknown type '$T'`).
This is a LANGUAGE-level gap, not a tychoc0 parity issue — needs a fix in tychoc (the
reference) first, then tychoc0. The recursive generic STRUCT equivalent (`LL($T): tail:
[LL($T)]`) works, so it's specific to the enum-with-array-payload monomorphization.

---

## Verified-solid this round (no bug)

Fundamentals are clean (byte-identical, correct): substr/split/find/string-index, signed
int div/mod (`-7/2=-3`, `-7%3=-1`), overflow wrap, char ops, array slice/pop, nested array
mutation, string-concat loops, generic structs everywhere (arrays/maps/channels/fn-returns/
nesting/closures), concurrency × composites (incl. closures through channels), recursive-enum
ASTs. `empty-array pop` and `div-by-zero` abort cleanly on both.

Probe-errors / non-bugs noted: `float(3)`/`int(3.9)` (no such cast fns — `float`/`int` are
type names; both reject); `soa [P]` (wrong soa literal syntax); `[fn(...)]` empty-array
literal (it's `[]fn(...)`, not `[fn(...)]`); the partial-mention generic form
`W($A,$B): x: Box($A)` is deliberately REJECTED by tychoc (and tychoc0 just doesn't
front-end-validate it — by design).

---

## Probing method (how to continue the dogfood)

Differential harness pattern (tychoc vs tychoc0, byte-identical = MATCH):

```sh
T=$(mktemp -d); ./tychoc compiler/tychoc0.ty -o "$T/tc0" 2>/dev/null
P() { label="$1"; shift; printf '%s\n' "$@" > "$T/m.ty"
  ./tychoc "$T/m.ty" -o "$T/a" 2>/dev/null && A=$(timeout 10 "$T/a" 2>&1) || A="T-FAIL"
  "$T/tc0" "$T/m.ty">"$T/b.c" 2>/dev/null && cc -O2 -fwrapv -o "$T/b" "$T/b.c" -lpthread -lm 2>/dev/null \
    && B=$(timeout 10 "$T/b" 2>&1) || B="T0-FAIL"
  [ "$A" = "$B" ] && echo "[MATCH] $label" || echo "[DIFF] $label  T=[$A] T0=[$B]"; }
```

Lessons: the highest-yield bugs are **feature COMBINATIONS** (generic × container/channel/
fn-return, map × closures) and **safety edge cases** (OOB, churn) — single-shape tests and
the fixpoint differential miss both. When a tychoc0-only fix is tempting, ALWAYS check it
doesn't turn a clean compile-error into a fail-OPEN miscompile (GAP 2 lesson). `_` match
wildcard DOES work on both compilers (an old note said it didn't).
