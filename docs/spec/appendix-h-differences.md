# Appendix H — Differences from the reference documentation

Writing this specification against the source of record surfaced points where the
reader-facing reference (`docs/reference/*`, `docs/corelib.md`) is stale,
incomplete, or contradicts the implementation. On every such point **this
specification governs** ([§1](00-conventions.md)); each is logged here so the
reference pages can be corrected in the same pass.

These are documentation drifts. A separate class — a divergence *between the two
compilers* — is a compiler bug, not a documentation correction. One was found
while drafting this specification (the `tychoc0` `inout`-exclusivity fail-open)
and has been **fixed** (`compiler/tychoc0.ty` `check_call_args`; locked by
`tests/reject/inout_alias.ty`); it is recorded in `docs/internals/spec-plan.md §6a`.

| # | Reference says | Actual / spec says | Where | Source |
|---|---|---|---|---|
| H1 | `reference/packages.md:23`: packages have **"no privacy"** — everything visible. | Cross-package access to a `_`-prefixed name is **rejected** (leading-underscore package privacy). | [§28](15-program.md) | `check_pkg_private`, `src/tychoc.c:3580-3587`; matches `docs/packages.md:17-20` |
| H2 | `reference/maps.md` / the literal-map error: "int-keyed maps support only int/float values." | A map's **value type is unrestricted**; only the *key* type is constrained. | [§5.3.5](03-types.md#535-maps-k-v) | `map_of`, `src/tychoc.c:1037-1065`; stale message `:1684`,`:4247` |
| H3 | `reference/types.md:77`: f-string holes must be int/float/bool/string. | A hole may also be `u32`/`u64`/`f32` (it desugars to `str`, which accepts them). | [§8.2](06-conversions.md#82-explicit-conversion-builtins) | `str` resolve `src/tychoc.c:4722-4724` |
| H4 | `reference/types.md:50`: `char ± int` "stays within a byte." | The result has type `char` but its value is **not** reduced to `0..255` (`'a' + 300` → `char` holding `397`). | [§5.2.4](03-types.md#524-char) | probed both compilers (spec-plan §6a) |
| H5 | `docs/generics.md`: discusses `empty$` as though it were a builtin. | `empty$` is **not** a builtin; only `zero$(T)` is special-cased. `name$(…)` is the generic explicit-type-argument form. | [§7.5](05-generics.md#75-zerot-and-namet-) | resolve at `src/tychoc.c:4355-4371` |
| H6 | `reference/builtins.md` — the builtin catalog. | It is **incomplete**: it omits `eprint`, `is_null`, `to_ptr`, `to_i32`, `to_u32`, `to_u64`, `to_f32`, `to_under`, and `keys`. | [§29](16-builtins.md) | `register_builtins` + magic cases, `src/tychoc.c:3818-3849`,`:4716-4906` |
| H7 | `docs/corelib.md` — the corelib package list. | Six packages exist in the tree but are **absent** from the doc: `bignum`, `decimal`, `net`, `compress`, `image`, `tls`. | [§32–§33](18-library.md) | `corelib/` tree |
| H8 | `docs/corelib.md` datetime entry. | The `datetime` libc shim exposes undocumented `dtx_local_offset` / `dtx_offset_at`. | [§32](18-library.md) | `corelib/datetime/datetime_shim.c` |

## Notes

- **H1 is a genuine behavioral correction, not just wording:** an implementation
  that allowed cross-package `_`-name access would be non-conforming. The
  reference page understates the language.
- **H4 was resolved by differential probing**, not by reading either doc; both
  compilers agree on the un-masked value.
- Corrections H2–H8 do not change any accept/reject or output behavior of a
  *conforming* program; they align the reference prose with what the compilers
  already do. H1 changes what a program may write.
