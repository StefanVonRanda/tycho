# Appendix D — Builtin index

Normative definitions and semantics: [§29](16-builtins.md). Conversion signatures:
[§8.2](06-conversions.md#82-explicit-conversion-builtins). This is an
alphabetical locator only.

| Builtin | Category | Chapter |
|---|---|---|
| `args` | process | §29 |
| `channel` | concurrency | §29 / [§23](13-concurrency.md#231-channels) |
| `chr` | conversion | §29 |
| `clock` | time | §29 |
| `close` | concurrency / handle | §29 / [§25](14-ffi.md#25-typed-handles) |
| `die` | termination | §29 / [§14.8](10-statements.md#148-die-and-termination) |
| `eprint` | I/O | §29 |
| `find` | string | §29 |
| `floor`, `fabs` | math (libm) | §29 |
| `getenv` | process | §29 |
| `input` | I/O | §29 |
| `is_null` | FFI | §29 / [§24](14-ffi.md) |
| `keys` | map | §29 / [§18](12-aggregates.md) |
| `len` | string / array / map | §29 |
| `list_dir` | filesystem | §29 |
| `now` | time | §29 |
| `ncpu` | concurrency | §29 |
| `pop`, `push`, `reserve` | array | §29 / [§16](12-aggregates.md) |
| `pow`, `sqrt` | math (libm) | §29 |
| `print`, `println` | I/O | §29 |
| `read_all`, `read_file` | I/O / filesystem | §29 |
| `recv`, `send`, `wait` | concurrency | §29 / [§21](13-concurrency.md)–[§23](13-concurrency.md) |
| `split`, `substr` | string | §29 |
| `str` | conversion | §29 / §8.2 |
| `to_bool`, `to_bytes`, `to_float`, `to_i8`, `to_i16`, `to_i32`, `to_i64`, `to_int`, `to_ptr`, `to_str`, `to_u8`, `to_u16`, `to_u32`, `to_u64`, `to_f32`, `to_under` | conversion | §29 / §8.2 |
| `write_file` | filesystem | §29 |
| `zero$` | type / generics | §29 / [§7.5](05-generics.md#75-zerot-and-namet-) |

Map/element operators that are not function-named builtins — `m[k]`, `k in m`,
`delete m[k]`, `m.get(k[, d])` — are defined in [§18](12-aggregates.md). `map_get`,
`map_set`, `map_has`, `map_del` are **not** callable (a hard parse error).
