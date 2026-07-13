# Appendix E — Conformance test map

This appendix maps every normative clause of the specification to at least one
test that exercises it, so that "conforming" is a checkable claim rather than a
prose assertion. It is a **living artifact**: the matrix in E.2 is populated and
every cited fixture is verified to exist; clauses with no dedicated fixture are
flagged in E.2.1.

## E.1 The conformance oracle

Conformance is defined against the **two-implementation oracle**
([§1.3](00-conventions.md#13-conformance)): across the conformance suite an
implementation MUST accept exactly the programs `tychoc` accepts, reject exactly
those it rejects, and produce byte-identical output. The suite is drawn from:

- the existing golden fixtures under `tests/` (behavioral) and `tests/reject/`
  (must-fail);
- the corelib fixtures under `corelib/test/`;
- the differential-fuzz and accept/reject parity corpora (`fuzz/`, the
  `typeparity`/`eqparity`/`unaryparity`/`parforparity` lanes);
- new probe fixtures written to pin previously-untested corners (the resolved
  items in `spec-plan.md §6a` each become a fixture).

## E.2 The coverage matrix

Each row binds a normative clause (section + requirement) to one or more fixtures
in the suite of E.1. A behavioral fixture is `tests/<name>` (golden output); a
must-fail fixture is `tests/reject/<name>`; parity **lanes** are the differential
gates (`typeparity`/`eqparity`/`unaryparity`/`parforparity`). A clause with no
dedicated fixture is flagged in E.2.1, exactly as an untested branch is.

### §3 Lexical structure

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §3.2 | one logical statement per line | `tests/reject/two_stmts_one_line`, `reject/bare_expr_stmt` |
| §3.4 | indentation; mixed tabs/spaces rejected | `tests/tab_indent`, `reject/tab_mix`, `reject/tab_mix2` |
| §3.9.1 | integer literals; overflow rejected | `tests/float_int_lit`, `reject/int_literal_overflow` |
| §3.9.2 | float literals (exp / leading-dot forms) | `tests/float_exp`, `tests/float_dot`, `reject/float_exp_bad` |
| §3.9.3 | character literals | `tests/char_basic`, `tests/char_byte` |
| §3.9.4 | string literals; escapes; interior NUL | `tests/multiline_literals`, `tests/string_nul`, `reject/string_escape` |
| §3.9.5 | f-string escape rule | `tests/reject/fstring_escape` |

### §5 Types

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §5.1 | distinct type identity (no erasure) | `tests/reject/char_as_type`, `reject/newtype_key_mix`, `reject/newtype_agg_mix` |
| §5.2.3 | `char` is not `int` | `tests/char_ops`, `reject/char_int_eq`, `reject/char_int_mul`, `reject/char_int_ord` |
| §5.2.7 | fixed-width `u32`/`u64`/`f32` | `tests/sized_ints`, `tests/sized_family`, `corelib/test/sha256` |
| §5.3.2 | fixed-size arrays `[N]T` | `tests/fixed_array`, `reject/fixed_array_bad_length`, `reject/fixed_array_zero_size`, `reject/fixed_array_nonconst_size` |
| §5.3.5 | maps; composite keys | `tests/maps`, `tests/map_literal_composite_key`, `tests/mapstructkey` |
| §5.3.9 | typed handles (affine, RAII free) | `tests/ffi` (`use_res_close`), `reject/close_handle_nonvar` |
| §5.4 | newtypes; unwrap; key/agg mixing rejected | `tests/newtypes`, `tests/newtype_key`, `reject/newtype_key_mix` |
| §5.5 | structural `==`; functions not comparable | `tests/map_eq`, `tests/option_eq`, `eqparity` lane, `reject/fn_eq` |

### §6 Type inference

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §6.3 | `:=` / typed-decl synthesis | `tests/inference`, `reject/result_bare_decl` |
| §6.4 | pending (ungrounded) types rejected | `tests/reject/infer_bare_empty`, `reject/infer_use_before_ground` |
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

### §12 Declarations & scoping

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §12.2 | `const` (literal/folded); reassign/nonliteral rejected | `tests/const_toplevel`, `tests/const_expr`, `reject/const_reassign`, `reject/const_expr_divzero`, `reject/const_expr_localref` |
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
| §13.5 | expression-valued `if`/`match` (tail) | `tests/if_expr`, `tests/match_expr`, `tests/match_inline`, `reject/if_expr_no_else`, `reject/if_expr_multistatement`, `reject/match_expr_nonexhaustive` |
| §13.6 | closures & function values | `tests/closures`, `tests/funcvalues`, `tests/combinator`, `reject/infer_lambda_param` |

### §14 Statements & control flow

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §14.4 | loops; `range` step 0 reject/abort | `tests/foreach`, `tests/while_loop`, `tests/range_negative_step`, `reject/range_step_zero_lit`, `tests/abort/range_step_zero` |
| §14.4 | `break` / `continue` | `tests/break_continue`, `tests/loop_return` |
| §19.4 | `match` statement; exhaustive; wildcard-last | `tests/enums`, `tests/matchwild`, `reject/match_non_exhaustive`, `reject/match_dup_arm`, `reject/match_wildcard_not_last` |

### §15 Functions

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §15.1 | declaration; duplicate/no-main rejected | `tests/early_return_main`, `reject/dup_fn`, `reject/no_main` |
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
| §29.6 | array builtins; `len` on scalar rejected | `tests/pop`, `tests/reservecomposite`, `reject/len_scalar` |
| §29.12 | abnormal termination (`die`) | `tests/die` |
| §30.1 | defined two's-complement wraparound | `tests/int_overflow` |
| §30.2 | the abort set (div0, bounds, empty pop, …) | `tests/abort/div_zero`, `abort/div_overflow`, `abort/mod_zero`, `abort/index_oob`, `abort/chr_oob`, `abort/pop_empty`, `abort/reserve_range` |
| §30.4 | defined string/map behavior (byte-safe, insertion order) | `tests/string_nul`, `tests/map_insorder`, `tests/maparraykey` |

### E.2.1 Clauses without a dedicated fixture (flagged)

These are covered by construction or by design rather than a single fixture, and
are flagged here so the gap is explicit rather than hidden:

- **§5.1 identity, §9.4 uniqueness, §9.5 transparent optimizations, §10.4
  soundness** — properties of the model proven by the whole differential suite +
  byte-identical fixpoint, not one fixture. The `eqparity`/`typeparity` lanes and
  `make fixpoint` are the evidence.
- **§6.6 non-goals of inference (no Hindley-Milner)** — a design boundary; no
  program can exercise the absence of a feature. Asserted, not tested.
- **§10.2 escape rule** — enforced structurally (re-home on escape); the closest
  behavioral witnesses are the closure fixtures above and the memory dogfood
  benches, but there is no single reject fixture for "a value escaped its arena."
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
