# Diagnostic-text parity between `tychoc` and `tychoc0` — measurement, 2026-07-25

Phase 2 of the front-door-defects plan (`plan-front-door-DONE.md` finding #3). **Measurement only.**
Nothing here is a fix, a gate, or a spec claim. Phase 3 decides what — if anything — to do
with these numbers.

## Totals

| Verdict | Count |
|---|---|
| IDENTICAL — both reject, normalized bodies match | **68** |
| DIVERGENT — both reject, bodies differ | **75** |
| DECISION-DIVERGENT — one accepts, one rejects | **0** |
| **Total** | **143** |

**DECISION-DIVERGENT is 0.** All 143 fixtures exit non-zero from
both compilers, and both emit a non-empty diagnostic. The conformance oracle
(`../spec/00-conventions.md` §1.3, `../spec/appendix-f-impl-defined.md:63-64`), which makes the
*accept/reject decision* normative and says nothing about message text, is intact. The plan
continues.

Secondary observation, not part of the three totals: where **both** compilers report a line
number, they agree on it in 87 of 93 cases. The six disagreements are
`if_expr_no_else` (3 vs 5), `if_expr_type_mismatch` (6 vs 3), `len_scalar` (1 vs 6),
`match_dup_arm` (10 vs 8), `match_wildcard_not_last` (12 vs 10), `subscript_type_mismatch` (5 vs 8).
tychoc0 reports **no** line number at all on 50 of 143; tychoc on 1 (`no_main`, which is a
whole-file condition).

## How the two compilers are run

`./tychoc` is the `make tychoc` binary. `tychoc0` is built with
`./tychoc compiler/tychoc0.ty -o <outside-the-tree>/tychoc0` — there is no `make tychoc0` target.
Invocation mirrors `tests/run.sh:155` and `tests/run.sh:159` exactly:

```sh
for f in tests/reject/*.ty; do
    b=$(basename "$f" .ty)
    ./tychoc  "$f" --emit-c -o "$W/rj.c" >"$OUT/$b.tychoc"  2>&1; echo $? > "$OUT/$b.tychoc.rc"
    "$W/tychoc0" "$f" --emit-c >/dev/null 2>"$OUT/$b.tychoc0"; echo $? > "$OUT/$b.tychoc0.rc"
done
```

tychoc0 writes emitted C to stdout, so its diagnostics are taken from stderr alone; tychoc
writes C to `-o`, so its stdout+stderr are captured together.

## The normalization, as code

This is the script that produced the table below, verbatim.

```python
ECHO_C  = re.compile(r'^\s*\d+ \| ')      # tychoc source echo   "     5 |     x := 1e"
CARET_C = re.compile(r'^\s*\|\s*\^')      # tychoc caret row     "       |           ^"
CARET_0 = re.compile(r'^\s*\^\s*$')        # tychoc0 caret row    "        ^"
LOC_C   = re.compile(r'^tests/reject/[A-Za-z0-9_]+\.ty(:(?P<ln>\d+))?: error: ')
PHASE_0 = re.compile(r'^(lex|parse|type|resolve|generics|codegen): ')
LINE_0  = re.compile(r'^line (?P<ln>\d+): ')
WARN_C  = re.compile(r': warning: ')

def headers_tychoc(text):                  # drop the echoed source line and caret row
    return [l for l in text.splitlines()
            if not (ECHO_C.match(l) or CARET_C.match(l))]

def headers_tychoc0(text):                 # tychoc0's echo has no gutter: it is the line
    L = text.splitlines()                  # immediately above a caret row
    skip = set()
    for i, l in enumerate(L):
        if CARET_0.match(l):
            skip.add(i)
            if i - 1 >= 0: skip.add(i - 1)
    return [l for i, l in enumerate(L) if i not in skip]

def fatal(hs):                             # the fatal diagnostic is the last non-warning
    real = [h for h in hs if not WARN_C.search(h)]   # header; die() prints last, then exits
    return real[-1] if real else (hs[-1] if hs else '')

def body_tychoc(h):
    m = LOC_C.match(h)
    return (h, None) if not m else (h[m.end():], m.group('ln'))

def body_tychoc0(h):
    ln, s = None, h
    for _ in range(4):        # both orders occur in the wild:
        m = LINE_0.match(s)   #   "parse: line 8: expected ..."   (phase, then line)
        if m:                 #   "line 3: type: send on ..."    (line, then phase)
            ln = ln or m.group('ln'); s = s[m.end():]; continue
        m = PHASE_0.match(s)
        if m:
            s = s[m.end():]; continue
        break
    return s, ln

# verdict
if (rc_tychoc == 0) != (rc_tychoc0 == 0):  v = 'DECISION-DIVERGENT'
elif body_tychoc == body_tychoc0:          v = 'IDENTICAL'
else:                                      v = 'DIVERGENT'
```

### What the normalization removes, and why

| Removed | Why | Source |
|---|---|---|
| `path:LINE: error: ` (tychoc) | Location + severity tag, not message text. | `src/tychoc.c:41` |
| `line LINE: ` (tychoc0) | Same. | `compiler/tychoc0.ty:5448` and `:5450` |
| `lex:` `parse:` `type:` `resolve:` `generics:` `codegen:` (tychoc0) | A phase taxonomy baked into the message string (e.g. `compiler/tychoc0.ty:622`, `:142`). tychoc has no equivalent field at all, so leaving it in would score a structural difference as a wording difference. **This is the one judgement call in the normalization** — see the sensitivity note below. |  |
| The echoed source line and caret row | It is the fixture's own source text, byte-identical by construction. Only the gutter differs (`     5 \| ` + original indentation vs 8-space indent). Including it would measure gutter formatting, not diagnostics. **Decision: excluded.** | `src/tychoc.c:52-55`, `compiler/tychoc0.ty:5448` |
| Warning lines (`: warning: `) | Only the fatal diagnostic is compared. Exactly one fixture is affected (`send_wrong_type`, where tychoc0 warns about an unreceived channel before dying); tychoc emitted no warnings on any fixture. | |

**Sensitivity of the phase-tag decision.** 59 of the 143 tychoc0 fatal lines carry a phase tag.
If the tag were *kept* rather than stripped, 27 currently-IDENTICAL fixtures would flip, and the
totals would read **41 IDENTICAL / 102 DIVERGENT / 0 DECISION-DIVERGENT**. Both numbers are
defensible; 68/75 is reported as the headline because the tag is a per-compiler structural field,
not a statement about the program. Phase 3 must pick one explicitly if it builds a gate.

### What this number does NOT cover

- **Only `tests/reject/*.ty`.** The 1 package-reject directory (`tests/reject/pkg/`) and the 15
  runtime-abort fixtures (`tests/abort/*.ty`) were not measured.
- **A fraction of the diagnostic surface.** `src/tychoc.c` has 455 `die_at(` call sites plus one
  `die(`; `compiler/tychoc0.ty` has 256 `die(` + 92 `die_at(` + 18 `lex_err(`. 143 fixtures cannot
  reach more than a minority of those. A green parity number here says nothing about the
  unexercised majority.
- **Only the first fatal diagnostic.** Both compilers stop at the first error, so a fixture with
  several defects is scored on whichever one its compiler reaches first — and G6 below shows they
  do not always reach the same one.
- **One build, one platform.** Native x86-64, default flags, `--emit-c`. No ILP32 run, no
  sanitizer build, no alternate flag paths.
- **Wording only.** Two messages scored IDENTICAL can still differ in the line they point at
  (6 fixtures do), and two scored DIVERGENT can be equally correct.

## The DIVERGENT set, grouped by cause

Grouping method: G1 is rule-derived (bodies become equal under a single mechanical substitution);
G2–G6 are assigned by reading each pair. Every one of the
75 DIVERGENT fixtures is in exactly one group.

| Group | Cause | Count |
|---|---|---|
| G1 | Cosmetic rendering of the same message | 20 |
| G2 | Same rule, same types — one message carries detail the other omits | 16 |
| G3 | Newtype-identity family — different sentence shape for one rule | 6 |
| G4 | Sum-payload naming — whole type vs payload type | 7 |
| G5 | Reworded, same rule, same types | 17 |
| G6 | **Different diagnosis** — the compilers disagree about *why* the program is bad | 9 |
| | **Total** | **75** |

### G1 — Cosmetic rendering of the same message (20)

The two messages say the same thing about the same types; they differ only in how a token is *rendered*. Sub-causes: tychoc0 spells the string type `str` where tychoc spells it `string` (10); tychoc wraps a suggested snippet in backticks and tychoc0 does not (6); tychoc uses an em dash where tychoc0 uses `--` (1); tychoc renders a tuple type with a space after the comma, tychoc0 without (1); tychoc renders a bounded array as `bounded[4]int`, tychoc0 as `[b4]int` (1); tychoc0 keeps the leading `.` of the tuple-index token in the message (1).

| Fixture | tychoc | tychoc0 |
|---|---|---|
| `base_mismatch_arg` | argument 1 of 'f' is string, expected int | argument 1 of 'f' is str, expected int |
| `base_mismatch_call_decl` | declared type int but value is string | declared type int but value is str |
| `base_mismatch_decl` | declared type int but value is string | declared type int but value is str |
| `base_mismatch_index_arg` | argument 1 of 'need_str' is int, expected string | argument 1 of 'need_str' is int, expected str |
| `base_mismatch_int_into_string` | declared type string but value is int | declared type str but value is int |
| `base_mismatch_tuple` | declared type (int, int) but value is (int, string) | declared type (int,int) but value is (int,str) |
| `base_mismatch_var_arg` | argument 1 of 'need_int' is string, expected int | argument 1 of 'need_int' is str, expected int |
| `fixarr_into_bounded_arg` | argument 1 of 'take' is [3]int, expected bounded[4]int | argument 1 of 'take' is [3]int, expected [b4]int |
| `if_expr_type_mismatch` | if/match branches produce different types (int and string) | if/match branches produce different types (int and str) |
| `map_del_removed` | map_del was removed; use `delete m[k]` | map_del was removed; use delete m[k] |
| `map_get_removed` | map_get was removed; use `m.get(k, default)` | map_get was removed; use m.get(k, default) |
| `map_has_removed` | map_has was removed; use `k in m` | map_has was removed; use k in m |
| `map_set_removed` | map_set was removed; use `m[k] = v` | map_set was removed; use m[k] = v |
| `result_bare_decl` | cannot infer the Result type of Ok(...) — annotate it (x : Result(T, E) = Ok(...)) | cannot infer the Result type of Ok(...) -- annotate it (x : Result(T, E) = Ok(...)) |
| `subscript_type_mismatch` | subscript 'at' is declared `-> inout string` but yields a place of type int | subscript 'at' is declared `-> inout str` but yields a place of type int |
| `tuple_index_range` | tuple index 10 out of range (the tuple has 2 elements) | tuple index .10 out of range (the tuple has 2 elements) |
| `typeset_notin` | 'pick' instantiated with T = float, which is not in the type set { int \| string } | 'pick' instantiated with T = float, which is not in the type set { int \| str } |
| `variadic_spread_mixed` | a spread argument `x...` must be the only variadic argument to 'sum' | a spread argument x... must be the only variadic argument to 'sum' |
| `variadic_spread_nonvariadic` | spread `...` is only valid as the argument to a variadic parameter | spread ... is only valid as the argument to a variadic parameter |
| `where_numeric` | 'sm' instantiated with T = string, which does not satisfy `numeric(T)` | 'sm' instantiated with T = str, which does not satisfy `numeric(T)` |

### G2 — Same rule, same types — one message carries detail the other omits (16)

Both compilers cite the same rule and name the same types. One adds a fix hint, a containing declaration, or a `got X` tail that the other drops. Direction is not consistent: tychoc is the richer side in 10 of these, tychoc0 in 6.

| Fixture | tychoc | tychoc0 |
|---|---|---|
| `char_int_mul` | arithmetic requires two ints or two floats (got char, int) -- convert one side, e.g. to_float(x) to compute in floats, or to_int(x) in ints | arithmetic requires two ints or two floats |
| `const_generic_size_return_only` | the return type of 'mk' has a type parameter not fixed by any argument; pass it explicitly, e.g. mk$(int) | the return type of 'mk' has a type parameter not fixed by any argument |
| `dup_field` | duplicate field 'x' | duplicate field 'x' in struct 'P' |
| `dup_param` | duplicate parameter 'x' | duplicate parameter 'x' in function 'f' |
| `explicit_noarg` | the return type of 'empty' has a type parameter not fixed by any argument; pass it explicitly, e.g. empty$(int) | the return type of 'empty' has a type parameter not fixed by any argument |
| `infer_lambda_param` | lambda parameter 'x' needs a type here -- no expected fn type supplies it (annotate: fn(x: T)) | lambda parameter 'x' needs a type here -- no expected fn type supplies it |
| `send_wrong_type` | send on Channel(int) needs a int value | send on Channel(int) needs a int value, got str |
| `tab_mix` | mixed tabs and spaces in indentation; use one consistently | mixed tabs and spaces in indentation |
| `dup_struct_enum` | 'X' is already defined | type 'X' is already defined |
| `infer_bare_empty` | could not infer the type of 'xs' -- no grounding use in its block (annotate: xs : [T] = [] / Option(T) = None) | could not infer the type of 'xs' -- no grounding use in its block (annotate the declaration) |
| `inout_byval_alias` | variable 'a' is passed to an inout parameter and also by value in the same call to 'f' (overlapping access — the by-value copy would alias the inout'd value) | variable 'a' is passed to an inout parameter and also by value in the same call to 'f' (overlapping access) |
| `match_expr_nonexhaustive` | match on an Option must cover both Some and None | non-exhaustive match on Option: arms must cover Some and None (or add `_`) |
| `place_call_receiver` | can only index-assign an array or map variable or field | cannot assign to this expression |
| `place_call_receiver_field` | cannot assign to a field of a temporary (only variables, fields, and composite-array elements are places) | cannot assign to this expression |
| `newtype_array_elem` | array elements must all have the same type | array element expects Id, got a non-newtype value |
| `type_mismatch` | argument 1 of 'print' is int, expected string -- wrap it with str(...), e.g. print(str(x)) | print expects a string |

### G3 — Newtype-identity family — different sentence shape for one rule (6)

tychoc renders a newtype mismatch in its ordinary shape (`argument 1 of 'f' is int, expected Id`); tychoc0 has a dedicated shape (`argument 1 of 'f' expects Id, got a plain int value (newtype identity differs)`). One rule, two templates, applied consistently across the family.

| Fixture | tychoc | tychoc0 |
|---|---|---|
| `inout_arg_newtype` | argument 1 of 'f' is int, expected Id | argument 1 of 'f' expects Id, got a plain int value (newtype identity differs) |
| `map_key_wrong_read` | map key must be Slot, got int | map key expects Slot, got a plain int value (newtype identity differs) |
| `newtype_agg_mix` | argument 1 of 'total' is [int], expected Ids | argument 1 of 'total' expects Ids, got a plain [int] value (newtype identity differs) |
| `newtype_key_mix` | map key must be UserId, got string | map key expects UserId, got a plain str value (newtype identity differs) |
| `sink_arg_newtype` | argument 1 of 'f' is int, expected Id | argument 1 of 'f' expects Id, got a plain int value (newtype identity differs) |
| `generic_newtype_bare_int` | argument 2 of 'slot' is int, which does not fit the parameter pattern | argument 2 of 'slot__Box_int__Handle' expects Handle, got a plain int value (newtype identity differs) |

### G4 — Sum-payload naming — whole type vs payload type (7)

For an Option/Result payload mismatch, tychoc names the full constructed type on both sides (`declared type Option(string) but value is Some(int)`); tychoc0 names only the payloads (`declared type str but value is int`). tychoc is strictly more informative here; the difference is one template, seven fixtures.

| Fixture | tychoc | tychoc0 |
|---|---|---|
| `sum_annot_arg_payload` | declared type Option(string) but value is Some(int) | declared type str but value is int |
| `sum_annot_array_payload_widen` | declared type Option([u32]) but value is Some([int]) | declared type [u32] but value is [int] |
| `sum_annot_err_payload_mismatch` | declared type Result(int, string) but value is Err(int) | declared type str but value is int |
| `sum_annot_int_payload_nonnumeric` | declared type Option(string) but value is Some(int) | declared type str but value is int |
| `sum_annot_place_payload` | declared type Option(string) but value is Some(int) | declared type str but value is int |
| `sum_annot_return_payload` | declared type Option(string) but value is Some(int) | declared type str but value is int |
| `sum_ctor_payload_mismatch` | declared type Option(int) but value is Some(string) | declared type int but value is str |

### G5 — Reworded, same rule, same types (17)

Plain wording differences with no information gap: `proc returns` vs `this function returns`, `unknown procedure` vs `unknown function`, `field 'v' of S is X, got Y` vs `field 'v' is X but the value is Y`, and similar.

| Fixture | tychoc | tychoc0 |
|---|---|---|
| `base_mismatch_return` | returning string but proc returns int | returning str but this function returns int |
| `chan_capture` | a closure cannot capture a channel handle -- take it as a parameter instead | a closure cannot capture a task or channel handle |
| `coerce_return_narrow` | returning float but proc returns int | returning float but this function returns int |
| `fixarr_size_field` | field 'v' of S is [4]int, got [3]int | field 'v' is [4]int but the value is [3]int |
| `fixarr_size_return` | returning [3]int but proc returns [4]int | returning [3]int but this function returns [4]int |
| `fstring_escape` | unsupported escape \  (use \n \t \\ \") | unsupported string escape (use \n \t \\ \") |
| `generic_recur_grow` | too many generic instantiations (> 1024) -- a recursive generic at a growing type? | too many instantiations (> 1024) -- a recursive generic at a growing type? |
| `in_wrong_key` | `in` key must be string | map key must be str, got int |
| `scalar_coerce_place` | cannot assign float to a int field | storing float into a place of type int |
| `scalar_coerce_reassign` | cannot assign float to 'x' of type int | assigning float to 'x' which is int |
| `scalar_coerce_struct_field` | field 'v' of S is int, got float | field 'v' is int but the value is float |
| `string_escape` | unsupported escape \  (use \n \t \\ \") | unsupported string escape (use \n \t \\ \") |
| `tab_mix2` | mixed tabs and spaces in indentation; use one consistently | mixed tabs and spaces in indentation |
| `unknown_fn_stmt` | unknown procedure 'nofunc' | unknown function 'nofunc' |
| `unknown_type` | unknown type 'cool'; did you mean 'bool'? | unknown type 'cool' |
| `where_bad_tp` | `where` refers to 'Q', which is not a type parameter of this function | `where` refers to 'Q', not a type parameter of this function |
| `where_nongeneric` | `where` constraints require a generic function (one with a `$T` parameter) | `where` constraints require a generic function (a `$T` parameter) |

### G6 — **Different diagnosis** — the compilers disagree about *why* the program is bad (9)

The interesting group. The accept/reject decision matches (that is the normative part), but the two compilers reach it through different rules — different phase, different cited constraint, or, in one case, disagreement about which type the offending expression has. These are the fixtures where a parity gate would be measuring something real rather than measuring wording.

| Fixture | tychoc | tychoc0 |
|---|---|---|
| `bare_expr_stmt` | a statement must be a declaration, assignment, or call -- a bare expression has no effect | expected ':=', '=', or '(' |
| `chan_reassign` | a channel variable cannot be reassigned | a channel must be created directly in a declaration: ch := channel(T, cap) |
| `char_as_type` | unknown type 'char' | there is no `char` type keyword -- a char arises by inference (e.g. c := 'x'), not as a written type annotation |
| `explicit_count` | unknown type 'str' | 'empty' has 1 type parameter(s), but 2 explicit type argument(s) were given |
| `explicit_nongeneric` | explicit type arguments given, but 'f' is not a generic function | unknown function 'f$<int>' |
| `fixed_array_nonconst_size` | a fixed-size array length must be an integer literal or an int `const` -- 'n' is not | declared type [#n]int but value is [int] |
| `genenum_bare_nullary` | Empty is a variant of generic enum Box; supply the type explicitly, e.g. Empty$(int) | unknown variable 'Empty' |
| `infer_use_before_ground` | 'xs' is used before its type can be inferred -- assign/push/pass it first, or annotate the declaration | could not infer the type of 'xs' -- no grounding use in its block (annotate the declaration) |
| `base_mismatch_inout` | argument 1 of 'f' is float, expected int | argument 1 of 'f' is int but the &place is a different type |

## Reading for Phase 3

Stated as findings, not as a recommendation — Phase 3 owns the decision.

- Divergence is **75/143 = 52%** of the measured fixtures. It is not a handful.
- It is, however, **concentrated in templates, not scattered**. G1 (20), G3 (6) and G4 (7) are
  three shared sentence shapes and one type-name spelling — 33 fixtures reachable by editing a
  small number of format strings, and G1's largest sub-cause (10 fixtures) is the single word
  `str` vs `string`.
- G2 (16) and G5 (17) are 33 independent hand-written strings. Forcing these equal is
  33 separate edits in two compilers, with no correctness payoff — and `make fixpoint` means
  every one of them must land in both files together.
- **G6 (9) is the only group where the divergence indicates something other than wording.** Those
  nine programs are rejected for genuinely different reasons by the two compilers. `explicit_count`
  is the sharpest: tychoc says `unknown type 'str'` while tychoc0 says
  `'empty' has 1 type parameter(s), but 2 explicit type argument(s) were given`. `base_mismatch_inout`
  is the one where they disagree about a *type*: tychoc calls the argument `float`, tychoc0 calls it
  `int`. Neither is a soundness hole — both reject — but both are worth a look on their own merits,
  independently of whether any parity gate is built.
- A gate that locks all 143 normalized bodies would be **new failure surface on every future
  diagnostic edit**, in exchange for locking text the spec deliberately does not make normative.
  A gate scoped to G6-style *decision-reason* divergence would be narrower but is much harder to
  express mechanically. Phase 3's call.

## Per-fixture table (all 143)

Bodies shown for DIVERGENT rows only, per the phase brief.

| # | Fixture | Verdict | Group | tychoc body | tychoc0 body |
|---|---|---|---|---|---|
| 1 | `bare_expr_stmt` | DIVERGENT | G6 | a statement must be a declaration, assignment, or call -- a bare expression has no effect | expected ':=', '=', or '(' |
| 2 | `base_mismatch_arg` | DIVERGENT | G1 | argument 1 of 'f' is string, expected int | argument 1 of 'f' is str, expected int |
| 3 | `base_mismatch_call_decl` | DIVERGENT | G1 | declared type int but value is string | declared type int but value is str |
| 4 | `base_mismatch_decl` | DIVERGENT | G1 | declared type int but value is string | declared type int but value is str |
| 5 | `base_mismatch_index_arg` | DIVERGENT | G1 | argument 1 of 'need_str' is int, expected string | argument 1 of 'need_str' is int, expected str |
| 6 | `base_mismatch_inout` | DIVERGENT | G6 | argument 1 of 'f' is float, expected int | argument 1 of 'f' is int but the &place is a different type |
| 7 | `base_mismatch_int_into_string` | DIVERGENT | G1 | declared type string but value is int | declared type str but value is int |
| 8 | `base_mismatch_return` | DIVERGENT | G5 | returning string but proc returns int | returning str but this function returns int |
| 9 | `base_mismatch_tuple` | DIVERGENT | G1 | declared type (int, int) but value is (int, string) | declared type (int,int) but value is (int,str) |
| 10 | `base_mismatch_var_arg` | DIVERGENT | G1 | argument 1 of 'need_int' is string, expected int | argument 1 of 'need_int' is str, expected int |
| 11 | `chan_capture` | DIVERGENT | G5 | a closure cannot capture a channel handle -- take it as a parameter instead | a closure cannot capture a task or channel handle |
| 12 | `chan_in_container` | IDENTICAL | — | a channel must be created directly in a declaration: ch := channel(T, cap) | *(same)* |
| 13 | `chan_reassign` | DIVERGENT | G6 | a channel variable cannot be reassigned | a channel must be created directly in a declaration: ch := channel(T, cap) |
| 14 | `char_as_type` | DIVERGENT | G6 | unknown type 'char' | there is no `char` type keyword -- a char arises by inference (e.g. c := 'x'), not as a written type annotation |
| 15 | `char_int_eq` | IDENTICAL | — | cannot compare char with int | *(same)* |
| 16 | `char_int_mul` | DIVERGENT | G2 | arithmetic requires two ints or two floats (got char, int) -- convert one side, e.g. to_float(x) to compute in floats, or to_int(x) in ints | arithmetic requires two ints or two floats |
| 17 | `char_int_ord` | IDENTICAL | — | ordering compares two ints, two floats, two strings, or two values of the same numeric newtype | *(same)* |
| 18 | `close_handle_nonvar` | IDENTICAL | — | close(h) takes a handle variable | *(same)* |
| 19 | `coerce_annot_narrow` | IDENTICAL | — | declared type int but value is float | *(same)* |
| 20 | `coerce_arg_narrow` | IDENTICAL | — | argument 1 of 'g' is float, expected int | *(same)* |
| 21 | `coerce_array_mixed` | IDENTICAL | — | array elements must all have the same type | *(same)* |
| 22 | `coerce_return_narrow` | DIVERGENT | G5 | returning float but proc returns int | returning float but this function returns int |
| 23 | `const_dup` | IDENTICAL | — | 'MAX' is already defined | *(same)* |
| 24 | `const_expr_divzero` | IDENTICAL | — | const expression divides by zero | *(same)* |
| 25 | `const_expr_float` | IDENTICAL | — | const value must be a literal | *(same)* |
| 26 | `const_expr_localref` | IDENTICAL | — | const value must be a literal | *(same)* |
| 27 | `const_generic_size_conflict` | IDENTICAL | — | argument 2 of 'g' is [4]int, which does not fit the parameter pattern | *(same)* |
| 28 | `const_generic_size_dynamic_arg` | IDENTICAL | — | argument 1 of 'f' is [int], which does not fit the parameter pattern | *(same)* |
| 29 | `const_generic_size_return_only` | DIVERGENT | G2 | the return type of 'mk' has a type parameter not fixed by any argument; pass it explicitly, e.g. mk$(int) | the return type of 'mk' has a type parameter not fixed by any argument |
| 30 | `const_generic_size_struct_field` | IDENTICAL | — | a struct field cannot be a `[$N]T` size-parameterized array -- use a fixed `[N]T` or a dynamic `[T]` | *(same)* |
| 31 | `const_nonliteral` | IDENTICAL | — | const value must be a literal | *(same)* |
| 32 | `const_reassign` | IDENTICAL | — | cannot assign to constant 'X' | *(same)* |
| 33 | `dup_field` | DIVERGENT | G2 | duplicate field 'x' | duplicate field 'x' in struct 'P' |
| 34 | `dup_fn` | IDENTICAL | — | 'foo' is already defined | *(same)* |
| 35 | `dup_local` | IDENTICAL | — | 'a' is already declared in this scope | *(same)* |
| 36 | `dup_param` | DIVERGENT | G2 | duplicate parameter 'x' | duplicate parameter 'x' in function 'f' |
| 37 | `dup_struct_enum` | DIVERGENT | G2 | 'X' is already defined | type 'X' is already defined |
| 38 | `dup_variant` | IDENTICAL | — | variant name 'W' is already used in this package | *(same)* |
| 39 | `enum_key_payload` | IDENTICAL | — | map keys must be string, int (directly or through a newtype), a fieldless enum, or a hashable struct/tuple/array | *(same)* |
| 40 | `explicit_count` | DIVERGENT | G6 | unknown type 'str' | 'empty' has 1 type parameter(s), but 2 explicit type argument(s) were given |
| 41 | `explicit_noarg` | DIVERGENT | G2 | the return type of 'empty' has a type parameter not fixed by any argument; pass it explicitly, e.g. empty$(int) | the return type of 'empty' has a type parameter not fixed by any argument |
| 42 | `explicit_nongeneric` | DIVERGENT | G6 | explicit type arguments given, but 'f' is not a generic function | unknown function 'f$<int>' |
| 43 | `fixarr_into_bounded_arg` | DIVERGENT | G1 | argument 1 of 'take' is [3]int, expected bounded[4]int | argument 1 of 'take' is [3]int, expected [b4]int |
| 44 | `fixarr_into_dynamic_arg` | IDENTICAL | — | argument 1 of 'take' is [3]int, expected [int] | *(same)* |
| 45 | `fixarr_size_arg` | IDENTICAL | — | argument 1 of 'take' is [3]int, expected [4]int | *(same)* |
| 46 | `fixarr_size_decl` | IDENTICAL | — | declared type [4]int but value is [3]int | *(same)* |
| 47 | `fixarr_size_field` | DIVERGENT | G5 | field 'v' of S is [4]int, got [3]int | field 'v' is [4]int but the value is [3]int |
| 48 | `fixarr_size_return` | DIVERGENT | G5 | returning [3]int but proc returns [4]int | returning [3]int but this function returns [4]int |
| 49 | `fixed_array_bad_length` | IDENTICAL | — | a fixed-size array of length 3 needs 3 elements, got 2 | *(same)* |
| 50 | `fixed_array_nonconst_size` | DIVERGENT | G6 | a fixed-size array length must be an integer literal or an int `const` -- 'n' is not | declared type [#n]int but value is [int] |
| 51 | `fixed_array_zero_size` | IDENTICAL | — | a fixed-size array length must be positive | *(same)* |
| 52 | `float_exp_bad` | IDENTICAL | — | expected newline | *(same)* |
| 53 | `float_int_eq` | IDENTICAL | — | cannot compare float with int | *(same)* |
| 54 | `float_int_var_ord` | IDENTICAL | — | ordering compares two ints, two floats, two strings, or two values of the same numeric newtype | *(same)* |
| 55 | `fn_eq` | IDENTICAL | — | functions are not comparable -- closures have no structural equality; compare the values they produce instead | *(same)* |
| 56 | `fstring_escape` | DIVERGENT | G5 | unsupported escape \  (use \n \t \\ \") | unsupported string escape (use \n \t \\ \") |
| 57 | `genenum_bare_nullary` | DIVERGENT | G6 | Empty is a variant of generic enum Box; supply the type explicitly, e.g. Empty$(int) | unknown variable 'Empty' |
| 58 | `generic_newtype_bare_int` | DIVERGENT | G3 | argument 2 of 'slot' is int, which does not fit the parameter pattern | argument 2 of 'slot__Box_int__Handle' expects Handle, got a plain int value (newtype identity differs) |
| 59 | `generic_recur_grow` | DIVERGENT | G5 | too many generic instantiations (> 1024) -- a recursive generic at a growing type? | too many instantiations (> 1024) -- a recursive generic at a growing type? |
| 60 | `if_expr_no_else` | IDENTICAL | — | an `if` used as a value must have an `else` — every path must produce a value | *(same)* |
| 61 | `if_expr_type_mismatch` | DIVERGENT | G1 | if/match branches produce different types (int and string) | if/match branches produce different types (int and str) |
| 62 | `in_array` | IDENTICAL | — | `in` tests membership in a map; the right operand must be a map | *(same)* |
| 63 | `in_string` | IDENTICAL | — | `in` tests membership in a map; the right operand must be a map | *(same)* |
| 64 | `in_wrong_key` | DIVERGENT | G5 | `in` key must be string | map key must be str, got int |
| 65 | `infer_bare_empty` | DIVERGENT | G2 | could not infer the type of 'xs' -- no grounding use in its block (annotate: xs : [T] = [] / Option(T) = None) | could not infer the type of 'xs' -- no grounding use in its block (annotate the declaration) |
| 66 | `infer_lambda_param` | DIVERGENT | G2 | lambda parameter 'x' needs a type here -- no expected fn type supplies it (annotate: fn(x: T)) | lambda parameter 'x' needs a type here -- no expected fn type supplies it |
| 67 | `infer_use_before_ground` | DIVERGENT | G6 | 'xs' is used before its type can be inferred -- assign/push/pass it first, or annotate the declaration | could not infer the type of 'xs' -- no grounding use in its block (annotate the declaration) |
| 68 | `inout_alias` | IDENTICAL | — | variable 'n' passed to two inout parameters of 'swap2' (overlapping mutable access) | *(same)* |
| 69 | `inout_arg_newtype` | DIVERGENT | G3 | argument 1 of 'f' is int, expected Id | argument 1 of 'f' expects Id, got a plain int value (newtype identity differs) |
| 70 | `inout_byval_alias` | DIVERGENT | G2 | variable 'a' is passed to an inout parameter and also by value in the same call to 'f' (overlapping access — the by-value copy would alias the inout'd value) | variable 'a' is passed to an inout parameter and also by value in the same call to 'f' (overlapping access) |
| 71 | `int_literal_overflow` | IDENTICAL | — | integer literal out of range | *(same)* |
| 72 | `len_scalar` | IDENTICAL | — | len(...) takes an array, a string, bytes, a map, or a soa | *(same)* |
| 73 | `map_as_key` | IDENTICAL | — | map keys must be string, int (directly or through a newtype), a fieldless enum, or a hashable struct/tuple/array | *(same)* |
| 74 | `map_del_removed` | DIVERGENT | G1 | map_del was removed; use `delete m[k]` | map_del was removed; use delete m[k] |
| 75 | `map_get_removed` | DIVERGENT | G1 | map_get was removed; use `m.get(k, default)` | map_get was removed; use m.get(k, default) |
| 76 | `map_has_removed` | DIVERGENT | G1 | map_has was removed; use `k in m` | map_has was removed; use k in m |
| 77 | `map_key_wrong_read` | DIVERGENT | G3 | map key must be Slot, got int | map key expects Slot, got a plain int value (newtype identity differs) |
| 78 | `map_set_removed` | DIVERGENT | G1 | map_set was removed; use `m[k] = v` | map_set was removed; use m[k] = v |
| 79 | `match_dup_arm` | IDENTICAL | — | duplicate arm for Red | *(same)* |
| 80 | `match_expr_nonexhaustive` | DIVERGENT | G2 | match on an Option must cover both Some and None | non-exhaustive match on Option: arms must cover Some and None (or add `_`) |
| 81 | `match_non_exhaustive` | IDENTICAL | — | non-exhaustive match: missing variant B of E | *(same)* |
| 82 | `match_wildcard_not_last` | IDENTICAL | — | a `_` wildcard must be the last match arm | *(same)* |
| 83 | `mut_arg_no_amp` | IDENTICAL | — | argument 1 of 'g' is inout; pass it as '&variable' | *(same)* |
| 84 | `newtype_agg_mix` | DIVERGENT | G3 | argument 1 of 'total' is [int], expected Ids | argument 1 of 'total' expects Ids, got a plain [int] value (newtype identity differs) |
| 85 | `newtype_array_elem` | DIVERGENT | G2 | array elements must all have the same type | array element expects Id, got a non-newtype value |
| 86 | `newtype_key_mix` | DIVERGENT | G3 | map key must be UserId, got string | map key expects UserId, got a plain str value (newtype identity differs) |
| 87 | `no_main` | IDENTICAL | — | no 'main' procedure | *(same)* |
| 88 | `param_shadow` | IDENTICAL | — | 'x' is already declared in this scope | *(same)* |
| 89 | `place_call_receiver` | DIVERGENT | G2 | can only index-assign an array or map variable or field | cannot assign to this expression |
| 90 | `place_call_receiver_field` | DIVERGENT | G2 | cannot assign to a field of a temporary (only variables, fields, and composite-array elements are places) | cannot assign to this expression |
| 91 | `push_scalar` | IDENTICAL | — | push's first argument must be an array or soa | *(same)* |
| 92 | `range_step_zero_lit` | IDENTICAL | — | range step is zero (the loop would never terminate) | *(same)* |
| 93 | `result_bare_decl` | DIVERGENT | G1 | cannot infer the Result type of Ok(...) — annotate it (x : Result(T, E) = Ok(...)) | cannot infer the Result type of Ok(...) -- annotate it (x : Result(T, E) = Ok(...)) |
| 94 | `scalar_coerce_map_key` | IDENTICAL | — | map key must be int, got float | *(same)* |
| 95 | `scalar_coerce_place` | DIVERGENT | G5 | cannot assign float to a int field | storing float into a place of type int |
| 96 | `scalar_coerce_reassign` | DIVERGENT | G5 | cannot assign float to 'x' of type int | assigning float to 'x' which is int |
| 97 | `scalar_coerce_struct_field` | DIVERGENT | G5 | field 'v' of S is int, got float | field 'v' is int but the value is float |
| 98 | `send_wrong_type` | DIVERGENT | G2 | send on Channel(int) needs a int value | send on Channel(int) needs a int value, got str |
| 99 | `sink_arg_newtype` | DIVERGENT | G3 | argument 1 of 'f' is int, expected Id | argument 1 of 'f' expects Id, got a plain int value (newtype identity differs) |
| 100 | `sink_arg_scalar` | IDENTICAL | — | argument 1 of 'f' is float, expected int | *(same)* |
| 101 | `sink_use_after` | IDENTICAL | — | 'b' is consumed by a `sink` parameter but used again (or inside a loop); pass a copy you keep (`y := b`) or make this its last use | *(same)* |
| 102 | `sized_conv_string` | IDENTICAL | — | to_u8(x) takes a numeric value | *(same)* |
| 103 | `slice_inout_alias` | IDENTICAL | — | cannot pass a slice of 'a' and an inout of 'a' in one call (the inout may reallocate the buffer the slice views) | *(same)* |
| 104 | `string_escape` | DIVERGENT | G5 | unsupported escape \  (use \n \t \\ \") | unsupported string escape (use \n \t \\ \") |
| 105 | `subscript_dangling` | IDENTICAL | — | a subscript must yield a place rooted in one of its parameters (else the projection would dangle) | *(same)* |
| 106 | `subscript_not_place` | IDENTICAL | — | a subscript must yield a place: `yield &<place>` (e.g. `yield &g.nodes[i]`) | *(same)* |
| 107 | `subscript_param_twice` | IDENTICAL | — | subscript parameter 'i' is used more than once in the yielded place (v1: at most once) | *(same)* |
| 108 | `subscript_type_mismatch` | DIVERGENT | G1 | subscript 'at' is declared `-> inout string` but yields a place of type int | subscript 'at' is declared `-> inout str` but yields a place of type int |
| 109 | `sum_annot_arg_payload` | DIVERGENT | G4 | declared type Option(string) but value is Some(int) | declared type str but value is int |
| 110 | `sum_annot_array_payload_widen` | DIVERGENT | G4 | declared type Option([u32]) but value is Some([int]) | declared type [u32] but value is [int] |
| 111 | `sum_annot_err_payload_mismatch` | DIVERGENT | G4 | declared type Result(int, string) but value is Err(int) | declared type str but value is int |
| 112 | `sum_annot_int_payload_nonnumeric` | DIVERGENT | G4 | declared type Option(string) but value is Some(int) | declared type str but value is int |
| 113 | `sum_annot_place_payload` | DIVERGENT | G4 | declared type Option(string) but value is Some(int) | declared type str but value is int |
| 114 | `sum_annot_return_payload` | DIVERGENT | G4 | declared type Option(string) but value is Some(int) | declared type str but value is int |
| 115 | `sum_ctor_payload_mismatch` | DIVERGENT | G4 | declared type Option(int) but value is Some(string) | declared type int but value is str |
| 116 | `tab_mix` | DIVERGENT | G2 | mixed tabs and spaces in indentation; use one consistently | mixed tabs and spaces in indentation |
| 117 | `tab_mix2` | DIVERGENT | G5 | mixed tabs and spaces in indentation; use one consistently | mixed tabs and spaces in indentation |
| 118 | `task_copy` | IDENTICAL | — | a task handle cannot be copied or re-bound -- bind the spawn directly (t := spawn f(...)) | *(same)* |
| 119 | `to_float_already_float` | IDENTICAL | — | to_float(n) takes an int, a sized int, f32, or a float newtype | *(same)* |
| 120 | `to_int_already_int` | IDENTICAL | — | to_int(x): x is already an int -- indexing a string (s[i]) yields the byte value as an int; use chr(x) for its one-character string | *(same)* |
| 121 | `to_int_string` | IDENTICAL | — | to_int(x) takes a float (truncates toward zero), a sized int, f32, a char, or an int newtype | *(same)* |
| 122 | `tuple_elem_index_assign` | IDENTICAL | — | can only index-assign an array or map variable or field | *(same)* |
| 123 | `tuple_index_range` | DIVERGENT | G1 | tuple index 10 out of range (the tuple has 2 elements) | tuple index .10 out of range (the tuple has 2 elements) |
| 124 | `two_stmts_one_line` | IDENTICAL | — | expected newline | *(same)* |
| 125 | `type_mismatch` | DIVERGENT | G2 | argument 1 of 'print' is int, expected string -- wrap it with str(...), e.g. print(str(x)) | print expects a string |
| 126 | `typeset_badtp` | IDENTICAL | — | `where Q: ...`: 'Q' is not a type parameter of this function | *(same)* |
| 127 | `typeset_notin` | DIVERGENT | G1 | 'pick' instantiated with T = float, which is not in the type set { int \| string } | 'pick' instantiated with T = float, which is not in the type set { int \| str } |
| 128 | `ufcs_builtin_bad_recv` | IDENTICAL | — | push's first argument must be an array or soa | *(same)* |
| 129 | `unknown_fn_stmt` | DIVERGENT | G5 | unknown procedure 'nofunc' | unknown function 'nofunc' |
| 130 | `unknown_type` | DIVERGENT | G5 | unknown type 'cool'; did you mean 'bool'? | unknown type 'cool' |
| 131 | `unknown_var` | IDENTICAL | — | unknown variable 'nosuchvar' | *(same)* |
| 132 | `value_arm_no_tail` | IDENTICAL | — | a value branch must end in a value expression | *(same)* |
| 133 | `variadic_empty_generic` | IDENTICAL | — | cannot infer the element type of an empty variadic call to generic 'count'; pass at least one argument | *(same)* |
| 134 | `variadic_not_last` | IDENTICAL | — | a variadic parameter must be the last parameter of 'f' | *(same)* |
| 135 | `variadic_spread_mixed` | DIVERGENT | G1 | a spread argument `x...` must be the only variadic argument to 'sum' | a spread argument x... must be the only variadic argument to 'sum' |
| 136 | `variadic_spread_nonvariadic` | DIVERGENT | G1 | spread `...` is only valid as the argument to a variadic parameter | spread ... is only valid as the argument to a variadic parameter |
| 137 | `where_bad_tp` | DIVERGENT | G5 | `where` refers to 'Q', which is not a type parameter of this function | `where` refers to 'Q', not a type parameter of this function |
| 138 | `where_defaultable_bad` | IDENTICAL | — | 'total' instantiated with T = P, which does not satisfy `defaultable(T)` | *(same)* |
| 139 | `where_hashable_bad` | IDENTICAL | — | 'distinct' instantiated with T = float, which does not satisfy `hashable(T)` | *(same)* |
| 140 | `where_nongeneric` | DIVERGENT | G5 | `where` constraints require a generic function (one with a `$T` parameter) | `where` constraints require a generic function (a `$T` parameter) |
| 141 | `where_numeric` | DIVERGENT | G1 | 'sm' instantiated with T = string, which does not satisfy `numeric(T)` | 'sm' instantiated with T = str, which does not satisfy `numeric(T)` |
| 142 | `where_unknown_pred` | IDENTICAL | — | unknown `where` predicate 'fancy' (known: numeric, comparable, has_str, hashable, defaultable -- or use a type set, `T: int \| float`) | *(same)* |
| 143 | `zero_bad_type` | IDENTICAL | — | zero$([int]): only int, float, bool, and string are defaultable | *(same)* |

## Decision (Phase 3, 2026-07-25)

**No blanket diagnostic-text parity gate. The divergence is recorded as ACCEPTED.**

The criterion was pre-registered in `plan-front-door-DONE.md` Pre-flight: *"If DIVERGENT is large or
the differences are deliberate, do NOT force them equal — record the divergence as accepted,
write the rationale, and drop the gate."* The measurement above returned 75/143 = **52%**
DIVERGENT. That is the "large" branch, and it is taken.

Rationale, in the order it matters:

1. **The spec does not make message text normative.** `../spec/00-conventions.md` §1.3 and
   `../spec/appendix-f-impl-defined.md:63-64` make the *accept/reject decision* normative and
   say nothing about wording. A text gate would lock, on every future edit, a surface the
   language deliberately leaves free.
2. **The divergence is mostly deliberate.** G2 (16) and G5 (17) are cases where one compiler
   carries a hint or a containing declaration the other omits — the C compiler is the
   user-facing one and is *supposed* to be richer. Forcing equality means deleting good
   messages or hand-writing 33 matching strings in two languages, every one of which must
   land in both files together or `make fixpoint` goes red. No correctness payoff.
3. **The repo already made this call, in code.** `../../tests/run.sh:248-254` keeps tychoc0's
   diagnostic goldens in a *separate* file (`tests/diag/<name>.h0err`) precisely because
   "tychoc0's format and its wording are behind the C compiler's on purpose … holding them to
   one golden would either block this lane or force a premature rewrite". A parity gate would
   contradict a decision the harness already documents.
4. **Cost of the gate is permanent, its benefit one-off.** 33 of the 75 (G1/G3/G4) are
   reachable by editing a few format strings, but the gate that locks the result is new
   failure surface on every diagnostic edit thereafter.

**What IS gated — and it was already gated before this phase.** The normative property (both
compilers agree on accept/reject for every reject fixture) is asserted by `tests/run.sh`, in
`make test`, in `scripts/ci.sh` step [2/19]:

```text
tests/run.sh:148   "$TYCHOC" compiler/tychoc0.ty -o "$TMP/h0" … || { echo "could not build tychoc0 for reject checks"; exit 2; }
tests/run.sh:150   for hi in tests/reject/*.ty; do
tests/run.sh:155       if "$TYCHOC" "$hi" --emit-c -o "$TMP/rj" …; then
tests/run.sh:156           note "$name" "tychoc ACCEPTED an invalid program"; …
tests/run.sh:159       elif [ "$skip0" = 0 ] && "$TMP/h0" "$hi" --emit-c >/dev/null 2>"$TMP/rj0.log"; then
tests/run.sh:160           note "$name" "tychoc0 ACCEPTED an invalid program (fail-open)"; …
```

with the same shape for the package rejects at `tests/run.sh:169-183`. The skip-list
(`H0_REJECT_SKIP`, `:149`) is empty, so all 143 single-file fixtures plus the package fixture
are covered on both sides. **No new lane was added: one already exists, it is decision-only,
and it deliberately does not compare text.**

### What Phase 3 did change: four G6 misdiagnoses

G6 was the only group where the divergence was not wording — the two compilers disagreed
about *what is wrong with the program*. All nine were classified against the source; four
were genuine misdiagnoses in tychoc0 (a message that names the wrong type or the wrong rule,
sending the reader to the wrong place in their own program) and were fixed. The other five
are two independently-true diagnoses of the same program, or the same rule said better on one
side; those were left alone. The classification table and the fixtures that lock the new
wording (`tests/diag/g6_*.ty`, with per-compiler `.err`/`.h0err` goldens) are in
`plan-front-door-DONE.md` under Phase 3.

## Reproducing this

```sh
env -u LD_PRELOAD make tychoc
./tychoc compiler/tychoc0.ty -o /tmp/parity/tychoc0    # no `make tychoc0` target exists
# then the loop and the normalizer above
```

Build tychoc0 outside the repo tree: the compile drops a sibling `.c` next to `-o`.
