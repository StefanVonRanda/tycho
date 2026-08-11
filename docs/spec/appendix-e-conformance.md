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
>
> **2026-07-29 — the lanes themselves are retired.** The 2026-07-26 freeze took
> `tychoc0` out of `make ci`, but `compiler/fixpoint.sh` and
> `scripts/frontparity.sh` were still hand-run, and fourteen other non-gated
> runners still built a `tychoc0`. As of 2026-07-29 **nothing builds it**: the
> three-clause `for` and bare `for:` replaced `for i in range(...)` and the frozen
> compiler cannot parse the corpus. See `ROADMAP.md` and
> [docs/architecture.md](../architecture.md) for what that costs. Every rationale
> below that explains a fixture's *location* by "the frozen compiler would refuse
> it" — the `corelib/test/` and `examples/corelib/` carve-outs, the `\r` and
> adjacent-literal and nested-pattern constraints — describes a constraint **that
> no longer binds**. The postfreeze lane (deleted 2026-07-29) was the first to go:
> it was folded back into `tests/` and `tests/abort/` on 2026-07-29 and no longer
> exists.
>
> **2026-07-30 — the placement convention, settled.** The two remaining
> freeze-displaced clauses now have `tests/` fixtures of their own:
> `tests/crlf_adjacent` for §3.9.4's `\r` and adjacent-literal join, and
> `tests/result_tuple` for §6.2(7)'s `Ok`/`Err` in a tuple literal. Note what did
> **not** happen: nothing was *moved*. `corelib/test/csv`, `corelib/test/httpd`,
> `corelib/test/result` and `server/main.ty` keep their coverage, because their
> placement was over-determined — each is the lane for its own package and would
> belong there with or without a freeze. What the freeze actually cost was the
> `tests/` witness, not the corelib one, and that is what has been restored. The
> convention from here is therefore the plain one, with no carve-out: **a clause
> gets a `tests/` fixture; a package's own lane covers the package.** A rationale
> below that still explains a *location* by the frozen compiler is history, kept
> because it is why the gap existed, and each is marked. The clauses they back are
> normative either way.

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
| §3.9.3 | `\xNN`: exactly two hex digits, either case; char literals only | `tests/char_hex_escape`, `reject/hex_escape_one_digit`, `reject/hex_escape_in_string` |
| §3.9.4 | string literals; escapes; interior NUL | `tests/multiline_literals`, `tests/string_nul`, `reject/string_escape` |
| §3.9.4 | `\r` escape; adjacent-literal join (multi-line string) | `tests/crlf_adjacent`, plus `corelib/test/csv`, `corelib/test/httpd` (`\r`) and `server/main.ty`'s `error_body`/`usage` (join) |
| §3.9.5 | f-string escape rule | `tests/reject/fstring_escape` |

### §5 Types

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §5.1 | distinct type identity (no erasure) | `tests/reject/char_as_type`, `reject/newtype_key_mix`, `reject/newtype_agg_mix` |
| §5.2.1 | `int` = required 64-bit two's-complement (range, defined wrap) | `tests/int_overflow` |
| §5.2.1 | `int` stays 64-bit under a non-LP64 C data model (no truncation of values, literal arithmetic or length headers) | `tests/int64_width`, the `make ilp32` lane (whole suite rebuilt `gcc -m32`, 64-bit goldens unchanged) |
| §5.2.3 | `char` is not `int` | `tests/char_ops`, `reject/char_int_eq`, `reject/char_int_mul`, `reject/char_int_ord` |
| §5.2.4 | `to_char(n)`: `int -> char`, aborting outside `0..255` | `tests/char_to_char`, `tests/abort/chr_oob` |
| §16.8 | the `char` row of the element-wise table (`+ -` only), reached by inference since `char` has no keyword | `tests/char_elem_ops`, `reject/char_elem_mul`, `reject/char_elem_div`, `reject/char_elem_mod` |
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
| §6.2(7) | a tuple literal is checked element-wise (a `Result` element grounds) | `tests/result_tuple`, `corelib/test/result` (`outcome`) |
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
| §12.2 | `const` folds `+` over two string literals | `tests/crlf_adjacent` (`GREET`) |
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
| §13.4 | f-string holes are sequenced left-to-right | `tests/fstring_eval_order` |
| §13.5 | expression-valued `if`/`match` (tail) | `tests/if_expr`, `tests/match_expr`, `tests/match_inline`, `reject/if_expr_no_else`, `reject/match_expr_nonexhaustive` |
| §13.5 | multi-statement value arms (block ending in a value expr) | `tests/if_expr_block`, `tests/match_expr_block`, `reject/value_arm_no_tail` |
| §13.6 | closures & function values | `tests/closures`, `tests/funcvalues`, `tests/combinator`, `reject/infer_lambda_param` |

### §14 Statements & control flow

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §14.4 | the four loop shapes: condition, infinite, three-clause (ascending and descending), foreach | `tests/foreach`, `tests/while_loop`, `tests/for3`, `tests/for_bare`, `tests/range_negative_step` |
| §14.4 | all three clauses required; `range()` removed; `0..<N` refused outside `parallel for` | `reject/for3_empty_clause`, `tests/diag/range_removed`, `reject/dotlt_sequential`, `tests/diag/dotlt_sequential` — diagnostics are byte-for-byte, see the note below |
| §14.4 | `break` / `continue`; `continue` runs the three-clause post clause | `tests/break_continue`, `tests/loop_return`, `tests/for3` |
| §19.4 | `match` statement; exhaustive; wildcard-last | `tests/enums`, `tests/matchwild`, `reject/match_non_exhaustive`, `reject/match_dup_arm`, `reject/match_wildcard_not_last` |
| §14.3.1 | nested patterns on an `Ok`/`Err`/`Some` payload; unqualified variant; refined-before-unrefined ordering; exhaustive by refined coverage | `tests/nested_pattern`, `corelib/test/result` (`why`, `io_why`) — see the note below |

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
| §16.7 | element-type restriction: `void` rejected everywhere; `bool` legal as a dynamic element, rejected in the inline fixed forms | `tests/bool_array`, `tests/cond_stmt_expr`, `reject/fixarr_elem_bool`, `reject/bounded_elem_bool` |
| §16.8 | element-wise arithmetic; broadcast (order kept, literal adapts); fresh result; `[N]T` mismatch rejected, `[T]` mismatch aborts | `tests/array_arith`, `tests/array_bcast`, `tests/array_arith_fresh`, `tests/array_bcast_fresh`, `tests/abort/array_arith_len`, `reject/array_arith_fixlen`, `tests/diag/array_arith_fixlen`, `tests/diag/array_bcast_widen` — post-freeze, see the note below |
| §17.1 | struct construction & fields | `tests/named_fields`, `tests/recursive_structs` |
| §17.3 | recursion only through a container | `tests/recursive_structs`, `tests/recursive_enum_array` |
| §17.4 | tuples; index-assign/range rejected | `tests/tuples`, `tests/tuple_assign`, `reject/tuple_elem_index_assign`, `reject/tuple_index_range` |
| §17.5 | destructuring | `tests/multiassign_scope`, `tests/tuple_assign` |
| §18.2–18.6 | `m[k]` place/rvalue, `delete`, `keys`, `m.get` | `tests/map_mutation`, `tests/map_delete`, `tests/map_get_method`, `tests/map_insorder`, `reject/map_del_removed`, `reject/map_key_wrong_read` |
| §18.7 | user-defined subscripts | `tests/subscript` |
| §19.1–19.3 | enums, `Option`, `Result`; construction | `tests/enums`, `tests/options`, `tests/results`, `reject/genenum_bare_nullary`, `reject/sum_ctor_payload_mismatch` |
| §14.1 | `pass`, the no-op statement; contextual, so the name still binds | `tests/pass_stmt`, `reject/pass_as_value` |
| §28.3 | a top-level `const` is exported and folded at the use site; `_name` stays private | `tests/pkg/const_export`, `reject/pkg/const_private` |
| §15 | `main`'s two shapes; `Err` out of the entry point prints bare and exits non-zero | `tests/main_result`, `tests/abort/main_result_err`, `reject/main_result_int`, `reject/main_result_enum_err`, `reject/main_params` |
| §19.x | `or_return` propagation | `tests/or_return`, `tests/or_return_option`, `tests/or_return_frees` |
| §5.3.6 | `Result(void, E)`: `Ok()`, a bare `Ok:` arm, the `or_return` statement form | `tests/result_void`, `reject/void_not_a_type`, `reject/option_void`, `reject/result_void_err`, `reject/result_void_ok_arg`, `reject/ok_empty_nonvoid`, `reject/err_empty`, `reject/result_void_bind`, `reject/result_void_arm_binds`, `reject/orreturn_stmt_value` |

### §23–24 Concurrency & FFI

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §23.x | spawn / Task / wait (affine, implicit join) | `tests/conc/basic`, `tests/conc/implicit`, `reject/task_copy` |
| §23.1 | channels (Vyukov, capacity, select) | `tests/conc/chan`, `tests/conc/chancap1`, `tests/conc/select`, `reject/send_wrong_type`, `reject/chan_reassign` |
| §23.x | parallel-for (`0..<N` counting form and foreach); channel-drain | `tests/conc/parfor`, `tests/conc/parfor_dotlt`, `tests/conc/parfor_chan`, `tests/conc/select_parfor`, `parforparity` lane |
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
  reason is mechanical, not an oversight: `compiler/fixpoint.sh` and
  `scripts/frontparity.sh` feed every `tests/*.ty`, `tests/pkg/*/main.ty`,
  `examples/*.ty` and `tools/*.ty` to the **frozen** `tychoc0`, whose lexer rejects
  `\r` (`compiler/tychoc0.ty:195`) and knows no adjacent-literal join. A fixture in
  `tests/` would therefore redden two runners at a file that must not be edited
  (see E.1's `tychoc0` freeze). The same constraint is why `core:httpd` keeps
  `crlf()` instead of writing the literal.
  **Closed 2026-07-30, and this clause is no longer flagged.** Both lanes were
  retired on 2026-07-29 and nothing builds `tychoc0`, so the paragraph above is
  history — kept because it is why the gap existed for as long as it did.
  `tests/crlf_adjacent` now covers all three under the main golden loop: `\r` as
  byte 13 (`term_bytes = 13,10,13,10`, and the CR/LF counts over a joined header
  block), the adjacent join including a raw piece beside a cooked one, and
  §12.2's `+` fold (`GREET`). The corelib and `server/` witnesses stay where they
  are — they cover these bytes as a side effect of being their packages' lanes.
  `core:httpd` keeping `crlf()` is now a style choice, not a constraint.
- **§14.3.1 nested patterns and §6.2(7)'s tuple-element checking** — same
  mechanism, same conclusion: both are covered by `corelib/test/result`, which
  `corelib/run.sh` golden-validates and no runner feeds to the frozen `tychoc0`,
  and deliberately **not** by a `tests/*.ty` fixture, because `tests/*.ty` and
  `tests/pkg/*/main.ty` go to `tychoc0` and its grammar has neither form.
  **Amended 2026-07-29:** nested patterns now DO have a `tests/` fixture —
  `tests/nested_pattern`. It was first written into the postfreeze lane (deleted 2026-07-29), a
  directory held outside the `tychoc0` lanes on purpose, which is what closed the
  open `docs/internals/FRICTION.md` item "new language syntax can no longer be given a `tests/`
  fixture". Later the same day the `tychoc0` lanes were retired outright, the
  constraint that made the directory necessary went with them, and the fixture
  moved to `tests/`, where the main golden loop scores it with the full
  native-vs-ASan + golden discipline. Measured, not assumed — this is what the
  directory was protecting against: a `tychoc0` built on 2026-07-29 reported
  `parse: line 34: unexpected token` on the fixture (line 34 was the
  `Err(TooBig(n))` arm; the payload-free `Err(NotFound)` before it does not redden
  it, because that grammar reads the name as a payload binding).
  **Amended again 2026-07-30, and this clause is no longer flagged:** §6.2(7) has
  caught up — `tests/result_tuple` builds `Ok`/`Err` directly in a tuple literal
  in a `return`, at a call site, with a payload-carrying `Err` variant and with a
  heap `[int]` `Ok` payload. `corelib/test/result` keeps its `outcome` copy; it is
  the `core:result` lane and exercises the combinators over it. Measured,
  not assumed: rewriting `httpd.read_request_capped` to `return (Err(why), buf)`
  makes `examples/webserver/run.sh` report `returning
  (Result(,httpd__ReqErr),str) but this function returns
  (Result(httpd__Request,httpd__ReqErr),str)`, which is why that function keeps its
  typed local. Note that §6.1 already listed "a tuple or array literal's element
  type" as a checking context: for §6.2(7) the **implementation**, not the
  specification, was the thing out of conformance.
- **§29.12's `exit` and §29.12.1's divergence rule** — third time this mechanism
  bit, same conclusion at the time. The witness is `server/main.ty`, which calls
  `exit(0)` for `--help` (status verified with `echo $?`) and binds
  `srv := match net.listen(...)` with `Err(e): die(...)` as the failure arm.
  **Historical, and no longer a live constraint:** `exit` was a **new builtin** and
  divergence a **new acceptance**, so through 2026-07-29 a `tests/` fixture for
  either would have been a program the live compiler accepted and the frozen
  `tychoc0` refused — `scripts/frontparity.sh` reported that as a divergence and
  `compiler/fixpoint.sh` as a build failure, and for the same reason **no corelib
  package could use either form** while `examples/webserver/run.sh` asserted
  `tychoc == tychoc0 == golden` over `core:httpd`, `core:net` and `core:io`. Both
  lanes were retired on 2026-07-29 (see E.1) and that prohibition went with them;
  the clause stays flagged only because `server/main.ty` remains its sole witness,
  which is a fixture decision, not a freeze one.
- **§5.2.6's `bytes` operators** — **fourth** time this mechanism bit, same
  conclusion at the time. **Closed 2026-07-30, and this clause is no longer
  flagged for a freeze reason** — see the historical paragraph below for what the
  reason was. What covers the clause today: `corelib/test/io` (`byte_index`,
  `byte_slice`, `byte_cat`, `byte_rebuild`), golden-validated by `corelib/run.sh`;
  the §5.2.6 specification example, which `scripts/spec_check.sh` compiles and
  runs; and `server/main.ty`'s `log_safe` as the application witness. **No corelib
  package is blocked from a `bytes` operator any more** — all 37 may use one.
  **The decision on a `tests/` fixture, taken 2026-07-30: §5.2.6 does not get one.**
  The block that kept it out expired, but E.1's convention is that a fixture does
  not move merely because it now could, and the three lanes above already assert
  every operator in the clause. Recorded so the next reader does not re-open it.

  *Historical, 2026-07-25 to 2026-07-29 — kept because it is why the gap existed
  for as long as it did, and because the enumeration below was a real measurement.*
  All three operators were **new acceptances**, so a `tests/` fixture would have
  been a program `tychoc` accepted and the frozen `tychoc0` refused. Measured, not
  assumed: `println(str(b[2]))` on a `bytes` gave `line 3: str(x) can't stringify a
  yte` from a `tychoc0` built at that commit, which `scripts/frontparity.sh` then
  reported as a divergence and `compiler/fixpoint.sh` as a build failure. Unlike
  the three notes above, the blocked set was **enumerated** rather than assumed:
  closing the import graph from every file a `tychoc0` runner compiled
  (`examples/*.ty`, `tools/*.ty`, `tests/*.ty`, `tests/pkg/*/main.ty`,
  `compiler/tychoc0.ty`, plus the four per-example runners at
  `examples/webserver/run.sh:24`, `examples/weblog/run.sh:24`,
  `examples/fetch/run.sh:35` and `examples/sqlite/run.sh:31`) reached **13**
  corelib packages — `cli`, `datetime`, `http`, `httpd`, `io`, `json`, `markdown`,
  `net`, `path`, `result`, `sha256`, `sort`, `strings` — which could **not** use a
  `bytes` operator. The other **24**, including `base64`, `compress`, `crypto`,
  `hash`, `hex`, `image`, `md5`, `raster` and `tls` — the packages that would most
  want them — sat outside every `tychoc0` runner and were free to adopt them.
  `core:cli` was in the blocked set via `examples/weblog/run.sh:24`. From
  2026-07-26 that was checkable rather than argued: `scripts/frontparity.sh` had
  fed `examples/*.ty` and never `examples/<dir>/main.ty`, so the 13-blocked set ran
  through packages no runner in `scripts/` could see; it was then made to feed the
  four per-example entry points too, and the enumeration above is what it enforced.
  Measured both ways on one tree: giving `corelib/cli/cli.ty` a `\r` escape left
  the old script at `agreed: 288  diverged: 0` and made the current one report
  `FAIL examples/weblog/main.ty ... lex: unsupported string escape`. `server/` and
  `examples/corelib/{result,httpd}` were deliberately **excluded** from that lane —
  they are the witnesses written outside the freeze, so including them would have
  reddened it at intended state. Both lanes were retired on 2026-07-29 (see E.1);
  nothing builds `tychoc0`, so none of the constraints in this paragraph is live.
- **§16.8 element-wise arithmetic on arrays** — **fifth** time the freeze
  mechanism bites, and the first time it costs nothing, because the door opened
  for §14.3.1 was already there. Every §16.8 program is a **new acceptance**:
  `a * b` on two arrays is a type error to the frozen `tychoc0`, so a fixture in
  `tests/*.ty` would be a program `tychoc` compiles and `tychoc0` refuses —
  `scripts/frontparity.sh` reported that as a divergence. The whole fixture
  set therefore went into the postfreeze lane (deleted 2026-07-29), which no `tychoc0`-derived binary
  was fed, and its `[T]` length-mismatch **abort** fixture into
  the postfreeze abort lane (deleted 2026-07-29) rather than `tests/abort/`, because
  `scripts/frontparity.sh` globbed `tests/abort/*.ty` and an abort fixture is by
  construction a program tychoc accepts. **Amended 2026-07-29:** the `tychoc0`
  lanes were retired, so both placements were undone the same day — the set is now
  `tests/array_arith`, `tests/array_bcast`, `tests/array_arith_fresh`,
  `tests/array_bcast_fresh` under the main golden loop and
  `tests/abort/array_arith_len` under the abort lane, which requires a nonzero
  exit and a `tycho:` message. `tests/reject/` needed no such move (it appears in no frontparity
  glob, and a program tychoc refuses is skipped there anyway), and
  `tests/diag/array_arith_fixlen` / `array_bcast_widen` carry the byte-for-byte
  diagnostics, because the reject lane asserts only that a diagnostic is
  non-empty.
- **§14.4 loop refusals** — `tests/reject/` asserts only a nonzero exit and a
  non-empty diagnostic, so the two messages a user is most likely to meet are
  locked byte-for-byte by the `tests/diag/` lane: `tests/diag/range_removed` (the
  removed counting form, whose message names both replacements) and
  `tests/diag/dotlt_sequential` (a sequential `0..<N`, whose message names the
  three-clause form with the user's own loop variable substituted in).
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
- **Tier 2 — example execution on the reference compiler (landed).**
  `scripts/spec_examples.sh` extracts every runnable example — a ` ```tycho `
  block immediately followed by a ` ```output ` block
  ([§2.3](00-conventions.md#23-examples-and-code-fences)) — and builds it with
  the reference `tychoc`, runs it, and asserts its stdout equals the `output`
  block. `tychoc` is the reference implementation
  ([§1.3](00-conventions.md#13-conformance)), so a spec example is conformant
  iff `tychoc` produces the documented output: a divergence between the compiler
  and the shown output is a defect that blocks the build. Every example the gate
  runs is reported `(tychoc)` — one compiler, one leg. Most spec code blocks are
  illustrative fragments or grammar and are correctly skipped; new complete
  programs added with an `output` block are gated automatically.
  (**Historical, through 2026-07-26:** each example was also built with the
  self-hosted `tychoc0` and run, and both outputs had to agree — the
  two-compiler oracle of E.1 applied to the spec's own examples, with a
  divergence *between the compilers* likewise blocking the build. Building
  `tychoc0` from source each run was then what dominated `make spec-check`'s
  wall time. That leg was cut when `tychoc0` was frozen; as of 2026-07-29
  nothing builds it at all — see the historical note in E.1, and
  `scripts/spec_examples.sh:13-16`, which records the same.)
