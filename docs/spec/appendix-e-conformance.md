# Appendix E — Conformance test map

This appendix maps every normative clause of the specification to at least one
test that exercises it, so that "conforming" is a checkable claim rather than a
prose assertion. It is a **living artifact**: the matrix in E.2 is populated and
every cited fixture is verified to exist; clauses with no dedicated fixture are
flagged in E.2.1.

## E.1 The conformance oracle

Conformance is defined against **this specification**
([§1.3](00-conventions.md#13-conformance)), and *checked* against the fixture
corpus below: across the conformance suite an implementation MUST accept every
program the suite records as accepted, reject every program it records as
must-fail, and reproduce each behavioral fixture's recorded output. The suite is
drawn from:

- the existing golden fixtures under `tests/` (behavioral) and `tests/reject/`
  (must-fail);
- the corelib fixtures under `corelib/test/`;
- the fuzz corpora under `fuzz/` (random well-typed programs, whose optimized and
  sanitized builds must agree; malformed input, which must be refused without a
  crash and without emitting invalid C);
- new probe fixtures written to pin previously-untested corners (the resolved
  items in `spec-plan.md §6a` each become a fixture).

> **Historical note.** Through 2026-07-25 this appendix defined conformance as a
> **two-implementation oracle** — agreement between `tychoc` and the self-hosted
> `tychoc0`, enforced by the `fixpoint`, `frontparity`, `typeparity`, `eqparity`,
> `unaryparity` and `parforparity` gates. `tychoc0` was frozen on 2026-07-26 and
> now diverges from `tychoc` (see
> [§1.2](00-conventions.md#12-the-reference-implementation)); those gates were
> removed, and every requirement they backed is now stated against this
> specification and locked by a recorded fixture. Rows below that cite a
> `*parity` **lane** name refer to gates that no longer run; the clause is
> normative regardless, and the fixture citations beside it still hold.

## E.2 The coverage matrix

Each row binds a normative clause (section + requirement) to one or more fixtures
in the suite of E.1. A behavioral fixture is `tests/<name>` (golden output); a
must-fail fixture is `tests/reject/<name>`. A clause with no dedicated fixture is
flagged in E.2.1, exactly as an untested branch is. Citations naming a
`typeparity`/`eqparity`/`unaryparity`/`parforparity` **lane** are historical: those
gates compared `tychoc` against the now-frozen `tychoc0` and were removed on
2026-07-26 (see the note in E.1). The clause each backs is normative regardless.

### §3 Lexical structure

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §3.2 | one logical statement per line | `tests/reject/two_stmts_one_line`, `reject/bare_expr_stmt` |
| §3.4 | indentation; mixed tabs/spaces rejected | `tests/tab_indent`, `reject/tab_mix`, `reject/tab_mix2` |
| §3.9.1 | integer literals; overflow rejected | `tests/float_int_lit`, `reject/int_literal_overflow` |
| §3.9.2 | float literals (exp / leading-dot forms) | `tests/float_exp`, `tests/float_dot`, `reject/float_exp_bad` |
| §3.9.3 | character literals | `tests/char_basic`, `tests/char_byte` |
| §3.9.4 | string literals; escapes; interior NUL | `tests/multiline_literals`, `tests/string_nul`, `reject/string_escape` |
| §3.9.4 | `\r` escape; adjacent-literal join (multi-line string) | `corelib/test/csv`, `corelib/test/httpd` (`\r`), `server/main.ty`'s `error_body`/`usage` (join) — see the note below |
| §3.9.5 | f-string escape rule | `tests/reject/fstring_escape` |

### §5 Types

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §5.1 | distinct type identity (no erasure) | `tests/reject/char_as_type`, `reject/newtype_key_mix`, `reject/newtype_agg_mix` |
| §5.2.1 | `int` = required 64-bit two's-complement (range, defined wrap) | `tests/int_overflow` |
| §5.2.1 | `int` stays 64-bit under a non-LP64 C data model (no truncation of values, literal arithmetic or length headers) | `tests/int64_width`, the `make ilp32` lane (whole suite rebuilt `gcc -m32`, 64-bit goldens unchanged) |
| §5.2.3 | `char` is not `int` | `tests/char_ops`, `reject/char_int_eq`, `reject/char_int_mul`, `reject/char_int_ord` |
| §5.2.6 | `bytes` operators: `b[i]` yields `int` (not a 1-length `bytes`) and is not a place; `b[i:j]` yields `bytes` and clamps; `a + b` and `b + 'c'` concatenate; no implicit `string` mixing; every one byte-safe across an interior `0x00` | `corelib/test/io` (`byte_index`, `byte_slice`, `byte_cat`, `byte_rebuild`), the §5.2.6 example (`scripts/spec_check.sh`), `server/main.ty` (`log_safe`) — no `tests/` fixture, see the note below |
| §5.2.7 | fixed-width `u32`/`u64`/`f32` | `tests/sized_ints`, `tests/sized_family`, `corelib/test/sha256` |
| §5.3.2 | fixed-size arrays `[N]T` | `tests/fixed_array`, `reject/fixed_array_bad_length`, `reject/fixed_array_zero_size`, `reject/fixed_array_nonconst_size` |
| §5.3.5 | maps; composite keys | `tests/maps`, `tests/map_literal_composite_key`, `tests/mapstructkey` |
| §5.3.9 | typed handles (affine, RAII free) | `tests/ffi` (`use_res_close`), `reject/close_handle_nonvar` |
| §5.3.10 | `bounded[N]T` inline fixed capacity; runtime count; push traps when full | `tests/bounded`, `tests/bounded_const_cap`, `reject/fixarr_into_bounded_arg`, `reject/bounded_nonconst_cap`, `reject/bounded_const_cap_zero`, `reject/bounded_chan_elem`, `reject/bounded_task_elem` |
| §5.4 | newtypes; unwrap; key/agg mixing rejected | `tests/newtypes`, `tests/newtype_key`, `reject/newtype_key_mix` |
| §5.5 | structural `==`; functions not comparable | `tests/map_eq`, `tests/option_eq`, `eqparity` lane, `reject/fn_eq` |

### §6 Type inference

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §6.3 | `:=` / typed-decl synthesis | `tests/inference`, `reject/result_bare_decl` |
| §6.4 | pending (ungrounded) types rejected | `tests/reject/infer_bare_empty`, `reject/infer_use_before_ground` |
| §6.2(7) | a tuple literal is checked element-wise (a `Result` element grounds) | `corelib/test/result` (`outcome`) — no `tests/` fixture, see the note below |
| §6.5 | branch unification for value `if`/`match` | `tests/if_expr`, `tests/match_expr`, `reject/if_expr_type_mismatch` |

### §7 Generics

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §7.2 | `where` predicate accept/reject | `tests/generic_where`, `reject/where_numeric`, `reject/where_unknown_pred`, `reject/where_nongeneric` |
| §7.2 | `hashable`/`defaultable` predicates | `tests/generic_hashable`, `tests/generic_defaultable`, `reject/where_hashable_bad`, `reject/zero_bad_type` |
| §7.4 | const generics `[N]T` / `[$N]T` | `tests/const_generic_size`, `reject/const_generic_size_return_only`, `reject/const_generic_size_struct_field`, `reject/const_generic_size_dynamic_arg` |
| §7.5 | explicit type args `f$(T)` | `tests/generic_explicit`, `reject/explicit_count`, `reject/explicit_noarg`, `reject/explicit_nongeneric` |
| §7.6 | generic UFCS method-style calls | `tests/generic_ufcs` |
| §7.7 | variadic (generic) parameters | `tests/variadic`, `reject/variadic_not_last`, `reject/variadic_empty_generic`, `reject/variadic_spread_mixed` |

### §8 Conversions

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §8.1 | literal adaptation to context type | `typeparity` lane, `tests/floats` |
| §8.3 | newtype unwrapping | `tests/newtype_elem_unwrap`, `tests/newtype_strbool` |
| §8.4 | no implicit narrowing (hard error) | `tests/reject/coerce_annot_narrow`, `reject/coerce_arg_narrow`, `reject/coerce_return_narrow`, `reject/coerce_array_mixed` |
| §8.5 | out-of-range conversion aborts | `tests/abort/to_int_oob` |

### §9–11 Memory & object model

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §9.2 | copy on assign / argument / return | `tests/value_semantics`, `tests/move`, `tests/ctor_move` |
| §9.3 | structural `==` mirrors the copy | `tests/map_eq`, `tests/option_eq`, `eqparity` lane |
| §10.2 | escape rule (closures re-home) | `tests/closures`, `tests/closure_fncap`, `tests/closure_loop_fuse` |
| §11.1 | `inout` copy-in / copy-out | `tests/inout_string`, `tests/scalar_elem_inout` |
| §11.2 | `inout` exclusivity (aliasing rejected) | `tests/reject/inout_alias`, `reject/inout_byval_alias`, `reject/slice_inout_alias` |
| §11.4 | `sink` move convention | `tests/sink`, `reject/sink_use_after` |
| §11.5 | types that cannot be `inout` (channel, function value) | `tests/reject/chan_inout_param`, `reject/inout_fnvalue`, `tests/chan_param_recv` |

### §12 Declarations & scoping

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §12.2 | `const` (literal/folded); reassign/nonliteral rejected | `tests/const_toplevel`, `tests/const_expr`, `reject/const_reassign`, `reject/const_expr_divzero`, `reject/const_expr_localref` |
| §12.2 | `const` folds `+` over two string literals | no `tests/` fixture — see the note below |
| §12.3 | scope & shadowing rules | `tests/shadow_string`, `tests/shadow_call`, `reject/param_shadow`, `reject/dup_local` |
| §12.4 | compound assignment | `tests/compound_assign`, `tests/compound_index_eval` |
| §12.5 | name resolution; unknown name rejected | `tests/reject/unknown_var`, `reject/unknown_type`, `reject/unknown_fn_stmt` |

### §13 Expressions

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §13.1 | place expressions | `tests/delete_place`, `tests/option_place_store`, `tests/projections` |
| §13.2 | binary operators; `in` membership | `tests/bitops`, `tests/logic`, `reject/in_array`, `reject/in_string`, `reject/in_wrong_key` |
| §13.3 | unary `-` `~` `not` | `tests/shift_edge`, `unaryparity` lane |
| §13.4 | evaluation order; subject evaluated once | `tests/compound_index_eval`, `tests/match_subject_once` |
| §13.4 | place-evaluation order: every place leg left-to-right, before the RHS | `tests/place_eval_order`, `tests/eval_order` |
| §13.4 | compound assignment through a user subscript: the argument is evaluated once | `tests/compound_subscript_eval` |
| §13.5 | expression-valued `if`/`match` (tail) | `tests/if_expr`, `tests/match_expr`, `tests/match_inline`, `reject/if_expr_no_else`, `reject/match_expr_nonexhaustive` |
| §13.5 | multi-statement value arms (block ending in a value expr) | `tests/if_expr_block`, `tests/match_expr_block`, `reject/value_arm_no_tail` |
| §13.6 | closures & function values | `tests/closures`, `tests/funcvalues`, `tests/combinator`, `reject/infer_lambda_param` |

### §14 Statements & control flow

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §14.4 | loops; `range` step 0 reject/abort | `tests/foreach`, `tests/while_loop`, `tests/range_negative_step`, `reject/range_step_zero_lit`, `tests/abort/range_step_zero` |
| §14.4 | `break` / `continue` | `tests/break_continue`, `tests/loop_return` |
| §19.4 | `match` statement; exhaustive; wildcard-last | `tests/enums`, `tests/matchwild`, `reject/match_non_exhaustive`, `reject/match_dup_arm`, `reject/match_wildcard_not_last` |
| §14.3.1 | nested patterns on an `Ok`/`Err`/`Some` payload; unqualified variant; refined-before-unrefined ordering; exhaustive by refined coverage | `corelib/test/result` (`why`, `io_why`) — no `tests/` fixture, see the note below |

### §15 Functions

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §15.1 | declaration; duplicate/no-main rejected | `tests/early_return_main`, `reject/dup_fn`, `reject/no_main` |
| §15.1 | at most 16 parameters (`fn` and `extern fn`) | `tests/params_16_max`, `reject/params_17`, `reject/extern_params_17` |
| §15.2 | parameter passing modes | `tests/inout_string`, `tests/sink`, `reject/mut_arg_no_amp` |
| §15.3 | variadic parameters | `tests/variadic` |
| §15.5 | methods (UFCS) | `tests/methods`, `tests/pkg/methods`, `tests/pkg/methodscalar` |
| §15.6 | subscripts (yielding projections) | `tests/subscript`, `reject/subscript_dangling`, `reject/subscript_not_place`, `reject/subscript_param_twice`, `reject/subscript_type_mismatch` |

### §16–19 Aggregates

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §16.2 | indexing & bounds; OOB aborts | `tests/abort/index_oob`, `tests/bounds_elision` |
| §16.4 | growth `push`; in-place append; alias guard | `tests/push_fusion`, `tests/append_alias`, `reject/push_scalar` |
| §16.6 | slices | `tests/slices`, `tests/string_slice`, `reject/slice_inout_alias` |
| §17.1 | struct construction & fields | `tests/named_fields`, `tests/recursive_structs` |
| §17.3 | recursion only through a container | `tests/recursive_structs`, `tests/recursive_enum_array` |
| §17.4 | tuples; index-assign/range rejected | `tests/tuples`, `tests/tuple_assign`, `reject/tuple_elem_index_assign`, `reject/tuple_index_range` |
| §17.5 | destructuring | `tests/multiassign_scope`, `tests/tuple_assign` |
| §18.2–18.6 | `m[k]` place/rvalue, `delete`, `keys`, `m.get` | `tests/map_mutation`, `tests/map_delete`, `tests/map_get_method`, `tests/map_insorder`, `reject/map_del_removed`, `reject/map_key_wrong_read` |
| §18.7 | user-defined subscripts | `tests/subscript` |
| §19.1–19.3 | enums, `Option`, `Result`; construction | `tests/enums`, `tests/options`, `tests/results`, `reject/genenum_bare_nullary`, `reject/sum_ctor_payload_mismatch` |
| §19.x | `or_return` propagation | `tests/or_return`, `tests/or_return_option`, `tests/or_return_frees` |

### §23–24 Concurrency & FFI

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §23.x | spawn / Task / wait (affine, implicit join) | `tests/conc/basic`, `tests/conc/implicit`, `reject/task_copy` |
| §23.1 | channels (Vyukov, capacity, select) | `tests/conc/chan`, `tests/conc/chancap1`, `tests/conc/select`, `reject/send_wrong_type`, `reject/chan_reassign` |
| §23.x | parallel-for; channel-drain | `tests/conc/parfor`, `tests/conc/parfor_chan`, `tests/conc/select_parfor`, `parforparity` lane |
| §24.1 | FFI crossable types (scalars/str/bytes/handles/sized) | `tests/ffi`, `examples/sqlite/demo.ty` |
| §24.2 | linking / cc invocation | `tests/ffi/run.sh`, `examples/sqlite` |
| §25 | typed handle decl: name must not collide with a struct/enum/newtype/handle | `tests/reject/handle_dup_name` |

### §27–28 Program & packages

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §27.1 | entry point `main`; missing rejected | `tests/reject/no_main`, `tests/early_return_main` |
| §28.1–28.5 | packages, import, multi-file merge | `tests/pkg/multifile`, `tests/pkg/alias`, `tests/pkg/variant`, `tests/pkg/shapes` |
| §28.3 | visibility / privacy | `tests/pkg/privacy` |

### §29–30 Builtins & runtime

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §29.3 | I/O & process builtins | `tests/io_builtins`, `tests/println`, `tests/write_file` |
| §29.5 | string builtins | `tests/strbuild`, `tests/str_fuse`, `tests/str_index`, `tests/strbytes` |
| §29.5 | `char_at` yields `char`; `s[i]` still yields `int`; OOB aborts | `tests/char_at`, `tests/abort/char_at_oob`, `reject/char_at_arg_index_type`, `reject/char_at_arg_recv_type` |
| §29.6 | array builtins; `len` on scalar rejected | `tests/pop`, `tests/reservecomposite`, `reject/len_scalar` |
| §29.12 | abnormal termination (`die`) | `tests/die` |
| §29.12 | `exit(code)` terminates with an explicit status (`0` included) | `server/main.ty` (`--help`) — no `tests/` fixture, see the note below |
| §29.12.1 | `die`/`exit` are diverging: legal as a value `if`/`match` tail; all-diverging rejected | `server/main.ty` (`srv := match net.listen(...)`) — no `tests/` fixture, see the note below |
| §30.1 | defined two's-complement wraparound | `tests/int_overflow` |
| §30.2 | the abort set (div0, bounds, empty pop, …) | `tests/abort/div_zero`, `abort/div_overflow`, `abort/mod_zero`, `abort/index_oob`, `abort/chr_oob`, `abort/pop_empty`, `abort/reserve_range` |
| §30.4 | defined string/map behavior (byte-safe, insertion order) | `tests/string_nul`, `tests/map_insorder`, `tests/maparraykey` |

### E.2.1 Clauses without a dedicated fixture (flagged)

These are covered by construction or by design rather than a single fixture, and
are flagged here so the gap is explicit rather than hidden:

- **§5.1 identity, §9.4 uniqueness, §9.5 transparent optimizations, §10.4
  soundness** — properties of the model exercised by the whole corpus rather than
  by one fixture: every golden fixture is built native *and* under ASan/UBSan and
  the two outputs must agree, and `make fuzz` applies the same differential to
  randomly generated programs. (Through 2026-07-25 the `eqparity`/`typeparity`
  lanes and the byte-identical `make fixpoint` were also cited here; both are gone
  with the `tychoc0` freeze — see E.1.)
- **§6.6 non-goals of inference (no Hindley-Milner)** — a design boundary; no
  program can exercise the absence of a feature. Asserted, not tested.
- **§10.2 escape rule** — enforced structurally (re-home on escape); the closest
  behavioral witnesses are the closure fixtures above and the memory dogfood
  benches, but there is no single reject fixture for "a value escaped its arena."
- **§3.9.4 `\r` / adjacent-literal join and §12.2's string fold** — all three are
  covered by committed, golden-validated programs (`corelib/test/csv` and
  `corelib/test/httpd` under `make corelib`; `server/main.ty`'s `error_body` and
  `usage` under `make server`), but deliberately **not** by a `tests/` fixture. The
  reason is mechanical, not an oversight: `compiler/fixpoint.sh:24` and
  `scripts/frontparity.sh:127` feed every `tests/*.ty`, `tests/pkg/*/main.ty`,
  `examples/*.ty` and `tools/*.ty` to the **frozen** `tychoc0`, whose lexer rejects
  `\r` (`compiler/tychoc0.ty:195`) and knows no adjacent-literal join. A fixture in
  `tests/` would therefore redden two runners at a file that must not be edited
  (see E.1's `tychoc0` freeze). The same constraint is why `core:httpd` keeps
  `crlf()` instead of writing the literal.
- **§14.3.1 nested patterns and §6.2(7)'s tuple-element checking** — same
  mechanism, same conclusion: both are covered by `corelib/test/result`, which
  `corelib/run.sh` golden-validates and no runner feeds to the frozen `tychoc0`,
  and deliberately **not** by a `tests/` fixture, because `tests/*.ty` and
  `tests/pkg/*/main.ty` go to `tychoc0` and its grammar has neither form. Measured,
  not assumed: rewriting `httpd.read_request_capped` to `return (Err(why), buf)`
  makes `examples/webserver/run.sh` report `returning
  (Result(,httpd__ReqErr),str) but this function returns
  (Result(httpd__Request,httpd__ReqErr),str)`, which is why that function keeps its
  typed local. Note that §6.1 already listed "a tuple or array literal's element
  type" as a checking context: for §6.2(7) the **implementation**, not the
  specification, was the thing out of conformance.
- **§29.12's `exit` and §29.12.1's divergence rule** — third time this mechanism
  bites, same conclusion. `exit` is a **new builtin** and divergence is a **new
  acceptance**, so a `tests/` fixture for either would be a program the live
  compiler accepts and the frozen `tychoc0` refuses — which is exactly what
  `scripts/frontparity.sh:127` reports as a divergence, and `compiler/fixpoint.sh:24`
  as a build failure. The witness is `server/main.ty`, which no runner feeds to
  `tychoc0`: it calls `exit(0)` for `--help` (status verified with `echo $?`) and
  binds `srv := match net.listen(...)` with `Err(e): die(...)` as the failure arm.
  For the same reason **no corelib package can use either form** while
  `examples/webserver/run.sh` asserts `tychoc == tychoc0 == golden` over
  `core:httpd`, `core:net` and `core:io`.
- **§5.2.6's `bytes` operators** — **fourth** time this mechanism bites, same
  conclusion. All three are **new acceptances**, so a `tests/` fixture would be a
  program `tychoc` accepts and the frozen `tychoc0` refuses. Measured, not assumed:
  `println(str(b[2]))` on a `bytes` gives `line 3: str(x) can't stringify a yte`
  from a `tychoc0` built at this commit, which `scripts/frontparity.sh:127` would
  report as a divergence and `compiler/fixpoint.sh:24` as a build failure. The
  covering fixtures are therefore `corelib/test/io` (golden-validated by
  `corelib/run.sh`, whose `tychoc0` leg was cut on 2026-07-26) and the §5.2.6
  specification example, which `scripts/spec_check.sh` compiles and runs. The
  application witness is `server/main.ty`'s `log_safe`, which no runner feeds to
  `tychoc0`. Unlike the three notes above, the blocked set was **enumerated** this
  time rather than assumed: closing the import graph from every file a `tychoc0`
  runner compiles (`examples/*.ty`, `tools/*.ty`, `tests/*.ty`,
  `tests/pkg/*/main.ty`, `compiler/tychoc0.ty`, plus the four per-example runners
  at `examples/webserver/run.sh:24`, `examples/weblog/run.sh:24`,
  `examples/fetch/run.sh:35` and `examples/sqlite/run.sh:31`) reaches **13** corelib
  packages — `cli`, `datetime`, `http`, `httpd`, `io`, `json`, `markdown`, `net`,
  `path`, `result`, `sha256`, `sort`, `strings` — which may **not** use a `bytes`
  operator. The other **24**, including `base64`, `compress`, `crypto`, `hash`,
  `hex`, `image`, `md5`, `raster` and `tls` — the packages that would most want
  them — are outside every `tychoc0` runner and are free to adopt them. Note that
  `core:cli` is in the blocked set via `examples/weblog/run.sh:24`. **Since
  2026-07-26 that is checkable rather than argued:** `scripts/frontparity.sh` fed
  `examples/*.ty` and never `examples/<dir>/main.ty`, so the 13-blocked set ran
  through packages no runner in `scripts/` could see; it now also feeds the four
  per-example entry points those runners use, and the enumeration above is what it
  enforces. Measured both ways on one tree: giving `corelib/cli/cli.ty` a `\r`
  escape leaves the old script at `agreed: 288  diverged: 0` and makes the current
  one report `FAIL examples/weblog/main.ty ... lex: unsupported string escape`.
  `server/` and `examples/corelib/{result,httpd}` are deliberately **excluded**
  from that lane — they are the witnesses written outside the freeze, so including
  them would redden it at intended state.
- **§30.3 clamp conditions and §30.5 unspecified behavior** — clamp behavior is
  exercised incidentally by the slice fixtures; the unspecified set is, by
  definition, not pinned (it is enumerated in [Appendix F](appendix-f-impl-defined.md)).

The `deps`-tier corelib clauses (§31–33, the `http`/`crypto`/`compress`/`image`/
`tls` packages) map to `corelib/test/<pkg>` and are **extended-tier** only.

## E.3 The `make spec-check` gate

The gate exists (`scripts/spec_check.sh`, CI step 17) and grows in tiers:

- **Tier 1 — grammar + citation consistency (landed).** Two checks: (a) the
  collected grammar of [Appendix A](appendix-a-grammar.md) is regenerated from
  the defining chapters §3/§4 by `scripts/gen_grammar.sh` and diffed against the
  committed listing, so the appendix cannot become a stale second copy of the
  grammar; (b) every fixture cited in the E.2 coverage matrix is asserted to
  exist, so a renamed or removed fixture breaks the build instead of leaving a
  dangling citation.
- **Tier 2 — example execution on both compilers (landed).**
  `scripts/spec_examples.sh` extracts every runnable example — a ` ```tycho `
  block immediately followed by a ` ```output ` block
  ([§2.3](00-conventions.md#23-examples-and-code-fences)) — and builds it with
  **both** the reference `tychoc` and the self-hosted `tychoc0`, runs each, and
  asserts both produce stdout equal to the `output` block. This is the
  two-compiler oracle of E.1 applied to the spec's own examples: a divergence
  between the compilers, or between either compiler and the shown output, is a
  defect that blocks the build. Most spec code blocks are illustrative fragments
  or grammar and are correctly skipped; new complete programs added with an
  `output` block are gated automatically. (Building `tychoc0` from source each
  run is why this check dominates `make spec-check`'s wall time.)
