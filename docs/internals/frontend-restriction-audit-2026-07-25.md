# Frontend-restriction audit — every rule `tychoc` enforces, measured against `tychoc0`

**Date:** 2026-07-25 · **Class:** measurement only. No compiler source was changed by
this audit. Every divergence it found is filed as its own phase in `plan.md`.

## 1. Why this audit exists

Six consecutive phases each found another instance of one shape: `tychoc` enforces a
frontend restriction, `tychoc0` does not, and `tychoc0` **fail-opens** — Phase 9
(`str`/`void` as written type names), Phase 15 (non-`bool` conditions), Phase 18
(`bounded` capacity/element), Phase 20 (affine handles in containers — an 11-shape
sample became 50 real divergences once swept properly), Phase 22 (newtype underlying
types). Finding these one at a time was not converging. This audit stops chasing
instances and enumerates the **population**.

### The root asymmetry (established by Phase 20, commit `187b9d3`)

`tychoc` **interns** types. Every composite type is built through a find-or-create
function, so one check inside a constructor covers every spelling of that restriction:

```
chan_of      src/tychoc.c:610      arrc_sized_b :668     opt_of :751    res_of :774
tup_of       :837                  mapc_of      :1004    funcc_of :1027
```

(Phase 20's evidence names the seventh interner `func_of`; the symbol is actually
`funcc_of`, declared at `:1027`, and its two guards are at `:1030` and `:1032` exactly
as Phase 20 recorded them.)

`tychoc0` has **no intern step** — a type *is* the string that spells it, constructed
ad hoc at many sites. `tychoc`'s restrictions are therefore structurally centralized
and `tychoc0`'s are structurally scattered. That is the mechanism behind all six
findings, and it is why enumerating `tychoc`'s choke points is a tractable way to
enumerate the population.

## 2. Enumeration method (so a reader can judge completeness)

The enumeration was built from `tychoc`'s structure, not from memory, in four passes:

1. **Mechanical extraction of every abort site.** A script walked `src/tychoc.c`
   (11 689 lines) attributing each `die_at(` / `die(` / `*_err(` call to its enclosing
   function: **501 sites**. Raw list: `/tmp/ph25/dieat.txt`.
2. **Restriction of the population to the type/declaration layer.** Of those 501,
   `resolve_expr_inner` (188) and `resolve_stmt` (69) are expression- and
   statement-level type errors — the diagnostic-parity domain Phase 2/3 already
   measured across all reject fixtures. The population this audit targets is the layer
   where the intern/no-intern asymmetry lives: the seven **type interners**, the
   **`parse_type_inner`** written-type constructor (26 sites), the **declaration
   parsers** (`parse_fn` 9, `parse_extern_fn` 3, `parse_handle` 3, `parse_struct` 6,
   `parse_enum` 7, `parse_typedecl` 3), and **`resolve_program`**'s declaration scan
   (14). That is 60 distinct rules after grouping variants of one rule together.
3. **Spec attribution.** Every row was looked up in `docs/spec/`. A row with no spec
   clause is itself a finding and is marked `unspec'd`.
4. **Cross-check against `tests/reject/` for completeness.** All 182 single-file reject
   fixtures were run through `tychoc` and their diagnostics collected
   (`/tmp/ph25/rjmsgs.json`): 182 fixtures, **135 distinct messages**. Every message was
   matched back to an enumerated rule. No fixture exercised a rule this enumeration
   had missed.

### The structural filter that makes the population tractable

`tests/run.sh:150-164` runs **both** compilers over every `tests/reject/*.ty` and fails
the suite with `"tychoc0 ACCEPTED an invalid program (fail-open)"` if `tychoc0` accepts
one. So **a rule with a reject fixture cannot be a live fail-open** — the gate would
already be red. The candidate population is exactly the set of rules `tychoc` enforces
that have **no** reject fixture. That is what this audit probes, and it is why 60 rows
was a complete rather than an arbitrary boundary.

## 3. Measurement method — FRONT / CC / RUN, never raw `rc`

`tychoc <f> -o out` folds in the C compile; `tychoc0 <f>` emits C to stdout and never
invokes `cc`. Comparing raw exit statuses scored **5 fake divergences** in an earlier
phase. The harness therefore uses `--emit-c` on **both** sides plus a separate `cc`
step and records three columns per compiler:

- **FRONT** — the frontend accept/reject decision. This is the normative column:
  `tests/run.sh:155,:159` compares exactly this (`--emit-c` on both sides, no `cc`).
- **CC** — does the emitted C build (`cc -O1 -fwrapv -std=c11 -pthread`).
- **RUN** — stdout of the resulting binary.

A fail-open whose C **compiles and runs** is materially worse than one that fails to
compile: a `CCFAIL` is loud, whereas a running binary silently violates a rule the
language promises to enforce.

### Harness (`/tmp/ph25/probe.py`, inherited from Phases 11/18/20)

```python
#!/usr/bin/env python3
"""FRONT/CC/RUN differential probe."""
import os, subprocess

REPO = "/home/igzo/github/tycho"
TYCHOC = os.path.join(REPO, "tychoc")
TYCHOC0 = "/tmp/ph25/tychoc0"          # ./tychoc compiler/tychoc0.ty -o /tmp/ph25/tychoc0
CC = ["cc", "-O1", "-fwrapv", "-std=c11", "-pthread"]
WORK = "/tmp/ph25/work"
os.makedirs(WORK, exist_ok=True)
ENV = {k: v for k, v in os.environ.items() if k != "LD_PRELOAD"}   # see the ASan note

def probe(name, src):
    ty = os.path.join(WORK, name + ".ty")
    open(ty, "w").write(src)
    res = {}
    # --- tychoc: --emit-c writes <out>.c and does NOT run cc ---
    out = os.path.join(WORK, name + "_c")
    r = subprocess.run([TYCHOC, ty, "--emit-c", "-o", out],
                       capture_output=True, text=True, env=ENV)
    if r.returncode != 0:
        res["tychoc"] = ("REJECT", last_err(r.stderr + r.stdout), "", "")
    else:
        rc = subprocess.run(CC + ["-o", out + ".bin", out + ".c", "-lm"],
                            capture_output=True, text=True, env=ENV)
        if rc.returncode != 0:
            res["tychoc"] = ("ACCEPT", "", "CCFAIL", last_err(rc.stderr))
        else:
            rr = subprocess.run([out + ".bin"], capture_output=True, text=True,
                                env=ENV, timeout=30)
            res["tychoc"] = ("ACCEPT", "", "ok", " ".join(rr.stdout.split()))
    # --- tychoc0: emits C to STDOUT, never invokes cc ---
    r0 = subprocess.run([TYCHOC0, ty, "--emit-c"],
                        capture_output=True, text=True, env=ENV)
    if r0.returncode != 0:
        res["tychoc0"] = ("REJECT", last_err(r0.stderr), "", "")
    else:
        cfile = os.path.join(WORK, name + "_0.c")
        open(cfile, "w").write(r0.stdout)
        binf = os.path.join(WORK, name + "_0.bin")
        rc = subprocess.run(CC + ["-o", binf, cfile, "-lm"],
                            capture_output=True, text=True, env=ENV)
        if rc.returncode != 0:
            res["tychoc0"] = ("ACCEPT", "", "CCFAIL", last_err(rc.stderr))
        else:
            rr = subprocess.run([binf], capture_output=True, text=True,
                                env=ENV, timeout=30)
            res["tychoc0"] = ("ACCEPT", "", "ok", " ".join(rr.stdout.split()))
    return res

def run(cases):                 # a row DIVERGES iff the two FRONT decisions differ
    div = 0
    for name, src in cases:
        r = probe(name, src)
        if r["tychoc"][0] != r["tychoc0"][0]:
            div += 1
    return div
```

Probe sets: `/tmp/ph25/rows.py` (76 probes) and `/tmp/ph25/rows2.py` (37 probes) —
the second set re-probes rules whose first probe tripped an *earlier* error, and adds
"…\_used" variants that actually exercise the accepted construct so the RUN column
means something. **113 probes total.**

> Environment: every gate and every probe runs under `env -u LD_PRELOAD`. The dev
> shell sets `LD_PRELOAD=…/block-nnp.so`, which loads before `libasan.so.8` and makes
> ASan fixtures abort with "ASan runtime does not come first". That is a foreign
> preload, not a regression.

## 4. Totals

| metric | count |
|---|---|
| **rules enumerated** | **60** |
| AGREE | 40 |
| **tychoc0-FAIL-OPEN** (spec requires the reject) | **15** |
| **NEEDS-A-RULING** (spec silent, compilers differ) | **4** |
| tychoc-FAIL-OPEN | **0** |
| not probed (impractical to spell) | 1 |
| **divergent rows whose emitted C COMPILES AND RUNS** | **18 of 19** |
| probes executed | 113 |

Only one divergent row (`E1`, duplicate `handle` name) stops at `CCFAIL`. **Every other
divergence produces a working binary.**

A second, distinct class fell out of the sweep: three rows where **both** frontends
agree but `tychoc0`'s emitted C does not build (§7).

## 5. The row table

Legend — **FO** = `tychoc0` fail-open (spec requires the reject) · **RULING** = spec
silent, compilers differ · **A** = agree · **U** = not probed.
`RUNS` means the fail-open's emitted C compiled and the binary ran.

### Type interners (the affine-handle family)

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| A1 | a `Task`/`handle`/`Channel` in **any** container, aggregate, `Option`/`Result`, tuple, map, function type, struct field, enum payload or newtype underlying | `src/tychoc.c:611-613`, `:669-671`, `:752-754`, `:775-777`, `:839-841`, `:1009-1011`, `:1030-1032` + `resolve_program` `:7029`, `:7034`, `:7037` | `03-types.md:236-241` §5.3.9 | `ctl_arr_chan`, `ctl_struct_chan` | **A** — closed by Phase 20, 22 fixtures |

### `parse_type_inner` — written composite types

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| B1 | `soa` of a non-struct | `:1724` | `12-aggregates.md` | `soa_elem_int`, `soa_elem_arr` | **A** (message text differs) |
| B2 | `bounded` with no capacity | `:1737` | `02-grammar.md` §4.2 | `bounded_no_cap` | **A** |
| B3 | `bounded` capacity that is not an int literal / int `const` | `:1735` | `03-types.md` §5.3.10 | fixture `bounded_nonconst_cap` | **A** |
| B4 | `bounded` capacity ≤ 0 | `:1738` | §5.3.10 | `bounded_cap_zero` | **A** |
| B5 | `bounded` element `bool`/`void` | `:1742` | §5.3.10 | `bounded_elem_bool` | **A** |
| B6 | function type with > 8 parameters | `:1752` | `02-grammar.md:170` | `fnty_9_params` | **FO · RUNS** |
| B7 | function-type parameter of type `void` | `:1760` | — | `opt_void` family | **A** (unreachable: `void` is not a written type name on either compiler since Phase 9) |
| B8 | tuple type with > 8 elements | `:1768` | `02-grammar.md:137,:170`, `03-types.md:175` | `tuple_9`, `tuple_9_used` | **FO · RUNS** |
| B9 | tuple type with < 2 elements | `:1772` | same | `tuple_1` | **FO · RUNS** |
| B10 | tuple element of type `void` | `:1774` | — | — | **A** (unreachable, as B7) |
| B11 | > 16 type parameters per generic | `:1714` | `05-generics.md:20` | `typarams_17`, `typarams_17_used` | **FO · RUNS** |
| B12 | > 16 size parameters per generic | `:1801` | `05-generics.md:20` | `sizeparams_17`, `sizeparams_17_used` | **FO · RUNS** |
| B13 | type nesting deeper than 256 | `:1701` | unspec'd | `type_nest_300` / `type_nest_100` | **A** (both reject at depth, both accept at 100) |
| B14 | `[N]T` / `[$N]T` element `bool` or `void` | `:1827`, `:1808` | `03-types.md` §5.3.2 | `fixarr_bool`, `sizeparam_arr_bool`, control `dynarr_bool` | **A** |
| B15 | `[T]` element of type `void` | `:1843` | — | — | **A** (unreachable, as B7) |
| B16 | fixed-array length not an int literal / int `const` | `:1820` | §5.3.2 | fixture `fixed_array_nonconst_size` | **A** |
| B17 | fixed-array length ≤ 0 | `:1823` | §5.3.2 | fixture `fixed_array_zero_size` | **A** |
| B18 | map key not string / int / newtype-of-int / fieldless enum / hashable struct-tuple-array | `:1839` | `03-types.md:190-198` | `map_key_float`, `map_key_bool`, `map_key_fnty` | **A** |
| B19 | `Option(void)`, `Channel(void)`, `Result(void, …)` | `:1852`, `:1860`, `:1870` | — | `opt_void`, `chan_void`, `res_void` | **A** (unreachable, as B7) |
| B20 | generic **struct** type argument partially mentioning a type parameter | `:1905` | `05-generics.md:76-83` | `generic_partial_struct(_used)` | **FO · RUNS** |
| B21 | generic **enum** type argument, same | `:1935` | `05-generics.md:76-83` | `generic_partial_enum(_used)` | **FO · RUNS** |
| B22 | unknown written type name | `:1950-1951` | `appendix-b-keywords.md:18-19` | fixture `unknown_type` + Phase 9's 23-name sweep | **A** |

### `parse_fn`

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| C1 | variadic parameter also `inout`/`sink` | `:3278` | `02-grammar.md:61-62` | `variadic_inout`, `variadic_sink` | **A** (tychoc0 rejects with a different message) |
| C2 | variadic parameter not last | `:3294` | `11-functions.md:53` | fixture `variadic_not_last` | **A** |
| C3 | a Tycho `fn` returning a `handle` | `:3299` | `02-grammar.md:63` §25 | `fn_return_handle` | **A** |
| C4 | `where` on a non-generic function | `:3309` | `05-generics.md:63` | fixture `where_nongeneric` | **A** |
| C5 | > 8 `where` constraints | `:3312` | `02-grammar.md:78`, `05-generics.md:63` | `where_9_constraints`, `where_9_used` | **FO · RUNS** |
| C6 | `where` naming a non-type-parameter | `:3317`, `:3335` | `05-generics.md` §7 | fixtures `typeset_badtp`, `where_bad_tp` | **A** |
| C7 | > 16 types in a `where` type set | `:3324` | `02-grammar.md:78`, `05-generics.md:61` | `typeset_17b` (control `typeset_8b`) | **FO · RUNS** |
| C8 | unknown `where` predicate | `:3330` | `05-generics.md` §7 | fixture `where_unknown_pred` | **A** |

### `parse_extern_fn`

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| D1 | `inout` out-parameter that is `string`/`bytes`/handle/composite | `:3528` | `14-ffi.md:43-45` | `extern_inout_string` | **A** |
| D2 | `extern` parameter that is a map, struct, or non-scalar array | `:3530` | `14-ffi.md:21-22,:38-39` | `extern_param_arr_string` (agree) · **`extern_param_struct`** | **FO · RUNS** — partially enforced by tychoc0: it rejects `[string]` but **accepts a struct parameter** |
| D3 | `extern` return that is not FFI-representable | `:3556` | `14-ffi.md:21-39` | `extern_ret_arr_string`, `extern_ret_tuple` | **A** |

### `parse_handle`

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| E1 | a `handle` name that is already a struct/enum/newtype/handle | `:3579` | **unspec'd** | `handle_dup_name(_used)` | **RULING** (tychoc0 accepts; CCFAIL — the only divergence that does not run) |
| E2 | > 256 handle types | `:3580` | unspec'd | — | **U** — not probed (would need 257 handle declarations; no evidence either way) |
| E3 | a handle body that is not exactly `free: <c_free_fn>` | `:3585` | `14-ffi.md:68` §25 | `handle_body_bad` | **A** |

### `parse_struct`

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| F1 | struct type parameter not written `$Name` | `:3604` | `05-generics.md:76` | `struct_typaram_bare` | **A** |
| F2 | > 8 struct type parameters | `:3605` | `05-generics.md:20` says **16** per generic — the spec and `tychoc` disagree on the number | `struct_9_typarams` | **FO · RUNS** (+ spec conflict) |
| F3 | a struct name already defined | `:3615` | `02-grammar.md:95` | `struct_dup_name` | **A** |
| F4 | duplicate struct field | `:3638` | `12-aggregates.md` §17 | fixture `dup_field` | **A** |
| F5 | a struct with zero fields | `:3648` | `02-grammar.md:95` | `struct_no_fields`, `struct_only_comment` | **A** on the decision — but neither compiler reaches this rule: the parser demands an indented field list first, so `:3648` is defensive |

### `parse_enum`

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| G1 | enum type parameter not written `$Name` | `:3660` | `05-generics.md:76` | `enum_typaram_bare` | **A** |
| G2 | > 8 enum type parameters | `:3661` | `05-generics.md:20` says 16 — same conflict as F2 | `enum_9_typarams(_used)` | **FO · RUNS** (+ spec conflict) |
| G3 | an enum name already defined | `:3668` | `02-grammar.md:95` | `nt_dup_name`, `struct_dup_name` | **A** |
| G4 | a variant name already used in the package | `:3689` | `12-aggregates.md` §18 | fixture `dup_variant` | **A** |
| G5 | > 8 payload fields on a variant | `:3695` | `02-grammar.md:97`, `12-aggregates.md:484` | `enum_9_payload`, `enum_9_payload_used` | **FO · RUNS** (prints `9`) |
| G6 | an enum with zero variants | `:3705` | `02-grammar.md:95` | `enum_no_variants`, `enum_only_comment` | **A** on the decision — defensive, as F5 |

### `parse_typedecl` — the newtype (this subsumes plan Phase 22)

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| H1 | a newtype name already defined | `:3714` | `02-grammar.md:98-99` | `nt_dup_name` | **A** |
| H2 | a newtype underlying type outside {`int`, `float`, `string`, `bool`, array, map, struct} | `:3719-3721` | `03-types.md:317-321` — "It MUST NOT be an enum, a tuple, a sized numeric (`u32`/`u64`/`f32`), `char`, `bytes`, `ptr`, an `Option`/`Result`, a function type, a handle, or another newtype" | 20 probes, below | **FO · RUNS** |

**H2 in full** — `tychoc` rejects eleven underlying types that `tychoc0` accepts:

| underlying | tychoc | tychoc0 | runs? |
|---|---|---|---|
| `int`, `float`, `string`, `bool`, `[int]`, `[2]int`, `[string:int]`, a struct, `bounded[4]int` | ACCEPT | ACCEPT | control — agree |
| `(int,int)` | REJECT | **ACCEPT** | CCFAIL |
| `Option(int)` | REJECT | **ACCEPT** | **RUNS** |
| `Result(int,string)` | REJECT | **ACCEPT** | **RUNS** |
| `fn(int)->int` | REJECT | **ACCEPT** | CCFAIL |
| an enum | REJECT | **ACCEPT** | **RUNS** |
| `soa [P]` | REJECT | **ACCEPT** | **RUNS** (`nt_soa_used`; CCFAIL in parameter position) |
| another newtype | REJECT | **ACCEPT** | **RUNS** |
| `ptr` | REJECT | **ACCEPT** | **RUNS** |
| `bytes` | REJECT | **ACCEPT** | **RUNS** |
| `u8` | REJECT | **ACCEPT** | **RUNS** |
| `f32` | REJECT | **ACCEPT** | **RUNS** |

Phase 20 closed only the affine-handle case (`type C = Channel(int)`,
`compiler/tychoc0.ty:2799`). Everything else in `tychoc`'s general underlying-type rule
is absent from `tychoc0`. `nt_soa` is worth calling out separately: `soa` is not in the
spec's permitted list *or* its forbidden list, so it is excluded by "MUST be one of"
rather than named.

### `resolve_program` — the declaration scan

| # | forbidden | tychoc site | spec | probe | verdict |
|---|---|---|---|---|---|
| I1 | a `[$N]T` size-parameterized array as a struct field | `:7044` | `05-generics.md` §7.4 | fixture `const_generic_size_struct_field` | **A** |
| I2 | same, as an enum payload | `:7049` | §7.4 | `sizeparam_enum_payload` | **A** (identical text) |
| I3 | same, as a newtype underlying | `:7052` | §7.4 | `sizeparam_newtype` | **A** (identical text) |
| I4 | a procedure name already defined | `:7059`, `:7070` | `15-program.md` | fixture `const_dup` | **A** |
| I5 | a function returning a `Channel` | `:7065` | `13-concurrency.md`, `03-types.md:236-241` | `fn_ret_chan` | **A** (tychoc0 rejects with a different message and line) |
| I6 | an `inout Channel(T)` parameter | `:7068` | **unspec'd** | `chan_inout_param`, `chan_inout_used` | **RULING · RUNS** — and the run is wrong: `tychoc0` prints `Some(7)` where a `recv` should yield `7` |
| I7 | > 16 function parameters | `:7075` | **unspec'd** (`05-generics.md:20`'s 16 is about *type* parameters) | `params_17`, `params_17_used` | **RULING · RUNS** (prints `18`) |
| I8 | an `inout` parameter whose type is a function value | `:7090` | **unspec'd** | `inout_fnvalue` | **RULING · RUNS** |
| I9 | duplicate parameter name | `:7120` | `11-functions.md` | fixture `dup_param` | **A** |
| I10 | a `main` with any parameter or a non-`void` return | `:7123-7124` | `15-program.md:27-32` — "MUST reject a `main` that declares any parameter or a non-`void` return type" | `main_with_param(_used)`, `main_with_ret(_used)` | **FO** — `main_with_param` **RUNS**; `main_with_ret` CCFAIL |

## 6. The dangerous rows, by name

Eighteen of the nineteen divergent rows produce a **working binary** from a program the
language says must be rejected. In descending order of how load-bearing the rule is:

1. **I10 `main` signature** — `fn main(x: int):` compiles and runs on `tychoc0`. The
   spec's §15 makes this a MUST-reject and even names the reference sites.
2. **H2 newtype underlying type** — eleven forbidden underlying types accepted; eight of
   them run. `03-types.md:317-321` is an explicit MUST NOT list.
3. **I6 `inout Channel(T)`** — accepted, runs, and produces *different output*
   (`Some(7)` vs a rejected program): an aliasing rule and a semantic divergence in one.
4. **D2 `extern fn` struct parameter** — a struct crosses the C boundary with no flat
   ABI. `14-ffi.md:38-39` rejects it explicitly; `tychoc0` emits the call.
5. **B20/B21 partial generic type arguments** — `05-generics.md:76-83` MUST.
6. **B8/B9 tuple arity**, **B6 function-type arity**, **G5 enum payload arity**,
   **B11/B12 generic parameter caps**, **C5/C7 `where` caps**, **F2/G2 aggregate type
   parameter caps** — every one of these is a *fixed-size array in the reference
   implementation* (`Type params[8]`, `Type elems[8]`, `g_cur_typarams[16]`, …). The
   limits are what keep those arrays in bounds in `tychoc`. `tychoc0` has no equivalent
   limit, so it accepts and runs the over-wide forms.
7. **I7 > 16 parameters**, **I8 `inout` function value** — unspec'd, both run.
8. **E1 duplicate `handle` name** — the only divergence that stops at `CCFAIL`.

**No memory-unsafety was found.** Every divergence is "accepted when it should have been
rejected"; none produced a program that reads or writes out of bounds. The arity rows are
the closest call — they are bounds-keeping limits in `tychoc`, but `tychoc`'s own arrays
are never reached, because `tychoc` is the side that rejects.

## 7. A second class the sweep surfaced: agree-on-frontend, `tychoc0` C does not build

Not fail-opens — both frontends accept — but `tychoc0`'s emitted C is invalid:

| probe | form | tychoc | tychoc0 |
|---|---|---|---|
| `nt_fixarr` | `type C = [2]int` used as a parameter type | ACCEPT, builds, runs | ACCEPT, **CCFAIL** `unknown type name 'Arr_f2_int'` |
| `nt_map` | `type C = [string: int]`, same | ACCEPT, builds, runs | ACCEPT, **CCFAIL** `unknown type name 'Map_str_int'` |
| `nt_bounded` | `type C = bounded[4]int`, same | ACCEPT, builds, runs | ACCEPT, **CCFAIL** `unknown type name 'Arr_b4_int'` |

`tychoc0` emits the mangled aggregate type name for a newtype's underlying type without
ever emitting that type's declaration. Same family as Phases 19 and 23.

## 8. Incidental: a stale spec citation

`docs/spec/15-program.md:31-32` cites `src/tychoc.c:6354-6355` and `:6379-6380` for the
`main`-signature rules. Those lines are now `s->decl_type = t; vars_push(…)` and the
`declared type %s but value is %s` diagnostic — unrelated code. The live sites are
`src/tychoc.c:7097-7098` (`no 'main' procedure`) and `:7123-7124` (the signature rule).

**Corrected 2026-07-25 (plan.md Phase 43).** Those replacement lines were *also* wrong —
they named the "a function cannot return a channel" die and the `inout` function-value
die. Traced from `tests/reject/main_with_param.ty`, whose real message is `'main' must be
'fn main():' with no return`: both rules live in `resolve_program` —
`src/tychoc.c:7797@no 'main' procedure` (`no 'main' procedure`) and
`src/tychoc.c:7822-7823@'main' must be` (the signature rule, one combined test on
`nparams != 0 || ret != T_VOID`). tychoc0's twin is at
`compiler/tychoc0.ty:3911@'main' must be`.

## 9. What this audit does NOT cover

Stated plainly so the coverage claim is not read wider than it is.

- **Expression- and statement-level type rules.** The 188 `resolve_expr_inner` and 69
  `resolve_stmt` abort sites — operand types, arity at call sites, exhaustiveness,
  place/assignment rules, `inout` aliasing at the call, builtin argument checking. Those
  are the Phase 2/3 diagnostic-parity domain and are heavily fixture-covered. This audit
  asserts nothing about them beyond the completeness cross-check in §2.4.
- **The lexer** (28 sites) and general parse-shape errors (`expected …`). Divergence
  there is a *message* question, not an accept/reject question, and both compilers
  reject the same shapes in the reject corpus.
- **Runtime traps.** `tests/abort/` already locks those differentially.
- **Codegen correctness beyond "does the C build and did it print something".** The RUN
  column is a liveness signal, not an output-equivalence check — with the single
  exception of `chan_inout_used`, where the output was inspected and is wrong.
- **`E2`** (> 256 handle types) — not probed.
- **Multi-package programs.** Every probe is a single file. `tests/reject/pkg/` covers
  the package-private rule; the interaction of these rules with package merging is
  unmeasured. Phase 20 already logged one known package-mode residual
  (`declares_type_name` scans one token stream).
- **`tychoc`-side fail-opens.** The harness flags divergence in **both** directions and
  found **zero** rows where `tychoc0` rejects and `tychoc` accepts. That is a measured
  result, not an assumption — but it is measured only over these 60 rows.

## 10. Reproducing

```sh
mkdir -p /tmp/ph25
env -u LD_PRELOAD ./tychoc compiler/tychoc0.ty -o /tmp/ph25/tychoc0
env -u LD_PRELOAD python3 /tmp/ph25/rows.py        # 76 probes, 29 frontend-divergent
env -u LD_PRELOAD python3 /tmp/ph25/rows2.py       # 37 probes, 22 frontend-divergent
```

The two probe runs overlap deliberately: `rows2.py` re-probes rules whose first probe
tripped an earlier error and adds "…\_used" variants, so the 29 + 22 probe-level counts
collapse to **19 divergent rules**.
