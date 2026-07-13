# Appendix H — Differences from the reference documentation

Writing this specification against the source of record surfaced points where the
reader-facing reference (`docs/reference/*`, `docs/corelib.md`) was stale,
incomplete, or contradicted the implementation. On every such point **this
specification governs** ([§1](00-conventions.md)). Each was logged here and has
since been **reconciled** — the reference-doc corrections landed in `cf51e09`
("back-port spec-audit findings into the reference docs"), the `char`-arithmetic
fix (H4) in the tightening campaign, and the stale compiler message (H2) was
removed from source. This appendix is retained as the audit trail; the "Fixed
in" column and the verified reference locations below show each is closed.

These are documentation drifts. A separate class — a divergence *between the two
compilers* — is a compiler bug, not a documentation correction. One was found
while drafting this specification (the `tychoc0` `inout`-exclusivity fail-open)
and has been **fixed** (`compiler/tychoc0.ty` `check_call_args`; locked by
`tests/reject/inout_alias.ty`); it is recorded in `docs/internals/spec-plan.md §6a`.

| # | Reference said | Correction (spec governs) | Reference location (now correct) | Fixed in |
|---|---|---|---|---|
| H1 | `reference/packages.md`: packages have **"no privacy"** — everything visible. | Cross-package access to a `_`-prefixed name is **rejected** (leading-underscore package privacy). | `reference/packages.md:22-24` | `cf51e09` |
| H2 | A stale **compiler diagnostic** ("int-keyed maps support only int/float values"). *(Not a reference-doc drift — `reference/maps.md` already states V is any type.)* | A map's **value type is unrestricted**; only the *key* type is constrained. The false message was removed. | `reference/maps.md` (already correct); message absent from `src/tychoc.c` | source cleanup |
| H3 | `reference/types.md`: f-string holes must be int/float/bool/string. | A hole may also be `u32`/`u64`/`f32` (it desugars to `str`, which accepts them). | `reference/types.md:78-79` | `cf51e09` |
| H4 | `reference/types.md`: `char ± int` "stays within a byte." | **Correct**: `char` arithmetic wraps to a byte (`0..255`, like `u8`), so the reference matches the language. | `reference/types.md` (§ char) | tightening campaign; `tests/char_byte` |
| H5 | `docs/generics.md`: appeared to discuss `empty$` as though it were a builtin. | `empty$` is **not** a builtin; `empty$(int)` is the *explicit call-site type-argument* form (`name$(…)`) applied to the generic `empty()`. Only `zero$(T)` is special-cased. | `docs/generics.md:11`, `:205-208` (framed as explicit type args) | already correct |
| H6 | `reference/builtins.md` — the builtin catalog was incomplete. | Now lists `eprint`, `is_null`, `to_ptr`, `to_i32`, `to_u32`, `to_u64`, `to_f32` (`to_under` and `keys` are cross-referenced on the newtypes/maps pages). | `reference/builtins.md` | `cf51e09` |
| H7 | `docs/corelib.md` — six packages were absent from the list. | `bignum`, `decimal`, `net`, `compress`, `image`, `tls` are now documented. | `docs/corelib.md:194-257` | `cf51e09` |
| H8 | `docs/corelib.md` datetime entry. | The `datetime` timezone offset functions (`local_offset`, `offset_at`, `now_local`; fixed-offset `from_unix_at`/`to_unix_at`/`format_iso_tz`) are now documented. | `docs/corelib.md:116-119` | `cf51e09` |

## Notes

- **H1 was a genuine behavioral correction, not just wording:** an implementation
  that allowed cross-package `_`-name access would be non-conforming. The
  reference page understated the language; it now states the rule.
- **H4 is resolved**: `char` arithmetic was changed to wrap to a byte (tightening
  campaign), so the reference's "stays within a byte" is accurate; both compilers
  agree.
- Corrections H2–H8 do not change any accept/reject or output behavior of a
  *conforming* program; they aligned the reference prose with what the compilers
  already do. H1 changes what a program may write.
- **Status: all rows closed.** Each correction has been verified against the
  current reference location cited above. New drifts, if any surface, are appended
  here and reconciled in the same manner.
