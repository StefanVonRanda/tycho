# Strings v2: `{ptr,len}`, inline small strings, and views (phase 4.1)

Status: design + probe, 2026-11-17. Verdicts at the end. The probe is
`bench/strings_v2/probe.c`, driven by `bench/strings_v2/run.sh`.

## What a string is today

`tycho_str_alloc` (`runtime/tycho_rt.c`, ~line 1355) allocates `8 + n + 1`
bytes from the arena: an 8-byte length word, the data, a NUL. The value is the
`char *` to the data, so a string is 8 bytes wherever it is stored and is a
valid C string at every boundary. A negative header marks an immortal literal
(`tycho_str_len` returns `~n`). The arena rounds every request up to 8 bytes,
so the smallest string costs 16 bytes of arena plus its 8-byte value: a
10-byte SKU costs 24 + 8 = 32 bytes.

A view already exists. O4 (`TychoStrView {data, len}`, low pointer bit set,
`tycho_str_is_view` / `tycho_str_data`) lets the emitter pass `s[a:b]` to a
call argument without copying (`cs.slice_view`, `compiler/emit/emit.ty`
~2947 and ~5924, control `TYCHOC_NO_SLICE_VIEW=1`). Every runtime string
function goes through `tycho_str_data` / `tycho_str_len`, so it accepts either
form. A view can't be stored: every retention path calls `tycho_str_copy`.
The "there is no zero-copy string view" sentence in
`docs/reference/arrays-slices.md` (~295) and `docs/spec/12-aggregates.md`
(~186) describes the semantics, which are still copy semantics. It does not
describe the codegen.

What depends on `char *`:

| Where | What | Count |
|---|---|---|
| `runtime/tycho_rt.c` | `char *` occurrences | 277 |
| | `tycho_str_alloc` / `tycho_str_len` / `tycho_str_data` uses | 24 / 24 / 20 |
| | `TychoArrStr` (`[string]` as `char **data`) | 29 |
| | string-keyed maps hash and compare `char *` keys | one map family |
| `compiler/emit/emit.ty` | the C spelling of `string` (`"char *"` at ~617, ~1531, ~3303, ~5259, ~5269, ~11878) | 6 |
| | `tycho_str_*` call sites | 101 (30 of them `tycho_str_from_c`) |
| corelib FFI | `extern` declarations with a `string` parameter or result, across 35 `.ty` files | 137 |
| corelib C shims | `char *` / `tycho_str` lines across 28 `*_shim.c` | 539 |
| bootstrap | `bootstrap/tycho_compiler.c` embeds the runtime verbatim and is linked with 3 shims (`strings`, `os`, `io`: 122 lines) | 6566 `char *` |

FFI (`docs/reference/ffi.md`): a `string` goes to C as `char *`, and a
C-returned `char *` is copied in with `tycho_str_from_c`.

## Design

```c
typedef union {
    struct { const char *p; tycho_int n; } h;   /* heap or view: n < 2^56 */
    unsigned char b[16];                        /* inline: b[0..14], b[15] = 0x80|len */
} TychoStr;
```

- 16 bytes, passed and stored by value (two registers on SysV and AAPCS64).
- On a little-endian host the inline flag is byte 15's top bit, which is the
  top byte of `n`, always 0 for a heap string. ILP32 (`make ilp32`) needs its
  own packing (a 4-byte pointer and a 4-byte length leave 8 bytes inline).
  Plan for SSO to be off there.
- Owned heap buffers keep a trailing NUL. `n + 1` costs nothing after the
  8-byte rounding unless `n % 8 == 0`, and it makes the common FFI crossing
  (owned heap string to C) free. Only inline strings and views need a
  NUL-terminated scratch copy, which `tycho_str_cstr` already does for views.
- Views are `{p, n}` heap-form values that point into the parent. A view is
  never copied into inline form, because that costs 26% on the split loop
  (row `sso+view` against `ptrlen+view`).
- The length header and the immortal-literal sign trick go away. Literals
  become `{"...", n}` constants or inline values.

## Probe

The probe models the three layouts in plain C on the same bump arena (64 KiB
blocks, 8-byte rounding, per-line `reset` for the loop scratch). It covers
only what the layouts change: value size, header, NUL, and inline storage. It
leaves out recycling and interning. Workloads:

- `orders`: Goose's example, `Order{id, customer: string, items: [Item{sku:
  string, qty, cents}]}` × 1M. Customer names are 4..24 bytes, 1..4 items per
  order, and each SKU is the 10-byte `SKU-NNNNNN`. All records are built, then
  hashed, summed and compared against a key.
- `split`: 200k CSV lines of 8 fields (1..20 bytes each), 5 passes. Each line
  is read into the scratch arena, then `split(line, ",")`, `part[1:4]` hashed,
  and `part == key` counted, with the scratch reset per line. `+view` rows
  make `split` and `substr` return views (layouts 1 and 2 only), which is the
  ceiling for 4.3.

Measured on darwin-arm64 (Apple clang 21, `-O2`) with `bench/peakrss.c`,
median of 5. All builds print the same checksum line, which `run.sh` checks
with `cmp`. Between two whole runs the times moved by about ±5%. RSS was
stable to 0.1 MiB.

| work   | layout       | RSS MiB  | ms     |
|--------|--------------|----------|--------|
| orders | today        |    196.8 |    189 |
| orders | ptrlen       |    196.7 |    191 |
| orders | sso          |    139.9 |    185 |
| split  | today        |     19.0 |    220 |
| split  | ptrlen       |     19.0 |    213 |
| split  | sso          |     19.0 |    225 |
| split  | ptrlen+view  |     19.0 |    167 |
| split  | sso+view     |     19.0 |    211 |

The split RSS is the 17 MiB input text plus one scratch block. That workload
measures time, not memory.

Why `{ptr,len}` alone wins nothing: it saves the 8-byte header and adds 8
bytes to every stored value, and on short strings the arena's 8-byte rounding
cancels the rest. A SKU costs 32 bytes in both layouts (24 + 8 against 16 +
16). SSO makes the SKU 16 bytes with no arena allocation, which is where the
29% on `orders` comes from: 196.8 to 139.9 MiB, with time in the noise.

## Verdicts

- **4.2 `{ptr,len}` alone: NO-GO.** RSS was 0.0% different on `orders` and
  the time was within noise on both workloads. It is the largest change in the
  plan, so it ships only as the carrier for 4.4. Tick 4.2 with "folded into
  4.4".
- **4.3 zero-copy views: GO, on today's layout.** Call-argument slices
  are already views (O4, `TYCHOC_NO_SLICE_VIEW`). What remains is
  `for part in split(s, sep)` with read-only parts: lower it to a loop that
  hands each part to the body as a stack `TychoStrView` (the same tagged
  pointer O4 uses), with no `[string]` and no per-part copy. The ceiling
  measured here is 213 to 167 ms (-22%; -24% against today) on the split
  loop. It needs no representation change. Control
  `TYCHOC_NO_STR_VIEW=1`.
  *Shipped (phase 4.3):* `_split_view` in `compiler/emit/emit.ty` and
  `tycho_str_split_next` in the runtime. Real compiler, darwin-arm64,
  `bench/peakrss.c` median of 5, an 8-field 52-byte line split 2M times,
  each part's `len` summed and compared: 34 ms against 108 ms with the
  control (-69%), RSS 1.5 MiB both, same output. The loop body's scratch
  is reset per line, so the win is time, not memory. Fixture
  `tests/split_view.ty`.
- **4.4 inline small strings (with the 4.2 switch underneath): GO.** -29%
  RSS on the records workload, time flat. The split loop costs about +5%
  (225 against 213 ms), which is the inline-flag branch on every access. The
  gain is in memory, and the cost is the implementation size below.

## Migration order (4.4, which carries 4.2)

1. **Runtime with two ABIs.** `runtime/tycho_rt.c` and the 28 shims get
   `#ifdef TYCHO_STR_V2` so they compile both ways. The old ABI stays the
   default because the checked-in bootstrap embeds the old runtime and its
   emitter spells `char *`. Stage 1 is that emitter reading the current
   `runtime/tycho_rt.c`, so it must still compile.
2. **Emitter switch.** The emitter changes the 6 C-type spellings, `char **` in
   `[string]`, the 101 `tycho_str_*` sites (most only change argument type),
   literal emission (`{ptr,len}` constant or inline), and string-keyed map
   calls. It emits `#define TYCHO_STR_V2` at the top of every unit, and
   `TYCHOC_NO_SSO=1` keeps `{ptr,len}` with inline storage off.
3. **FFI.** At `extern` call sites, a `string` argument is lowered to
   `tycho_str_cstr(arena, s)`, which is free for an owned heap string and a
   scratch copy for an inline string or a view. Results keep
   `tycho_str_from_c`. The 137 corelib `extern` declarations don't change,
   because the C side still sees `char *`. Shims that call `tycho_str_*`
   internally do change.
4. **Bootstrap regeneration.** `make tychoc1` gives a stage 2 that emits V2.
   Regenerate `bootstrap/tycho_compiler.c` from it, then prove
   `make bootstrap-check` and `make fixpoint-check`. After that, delete the
   old-ABI branches and regenerate once more so the bootstrap carries no
   `#ifdef`.
5. **Embedded runtime.** `build/tycho_rt_embed.h` is generated from
   `runtime/tycho_rt.c`, so a rebuild picks it up.

## Size estimate

| Piece | Files | Size |
|---|---|---|
| 4.3 split views | `compiler/emit/emit.ty` (one `for ... in split(...)` lowering beside the O4 code), 1 runtime iterator helper, 2-3 fixtures, 2 doc sentences | ~150 lines, no ABI change, no bootstrap regeneration |
| 4.4 (with 4.2) runtime | `runtime/tycho_rt.c`: every function that takes or returns a string, `TychoArrStr`, the string maps, intern, recycling | ~277 `char *` sites, ~800-1200 changed lines |
| 4.4 emitter | `compiler/emit/emit.ty` | 6 type spellings, 101 `tycho_str_*` sites, literals, FFI lowering: ~300 lines |
| 4.4 shims | 28 `corelib/*/*_shim.c` | 539 string lines to audit, most mechanical |
| 4.4 bootstrap | `bootstrap/tycho_compiler.c` | two regenerations (dual ABI, then old ABI removed) |
| 4.4 tests | FFI fixtures in `tests/` that call `tycho_str_*` from C | 2 files |

## Risks

- **Silent ABI mismatch.** A C shim that still treats the value as `char *`
  compiles, through a cast or `void *`, and reads garbage. Mitigation:
  `TychoStr` is a struct, so a raw `char *` mix is a compile error. Grep for
  casts.
- **Lifetime of `&s` in the inline form.** An inline string's data address
  is the address of the value, so `tycho_str_data(&tmp)` on a temporary
  dangles once `tmp` dies. The runtime must take `TychoStr` by value and use
  the pointer only locally, as the probe does.
- **Struct-size growth.** Every `string` field and `[string]` element doubles
  from 8 to 16 bytes. For records with long strings (over 15 bytes) the net is
  a loss of up to 8 bytes per string. The probe's 4..24-byte customer names
  still came out ahead.
- **ILP32.** A 4-byte pointer and a 4-byte length leave 8 bytes inline, so
  SSO stays off there in the first cut.
- **Interning and immortality.** Map keys go through `tycho_str_intern` /
  `tycho_str_intern_copy`. Both read the negative header to tell an immortal
  (literal or interned) string from an arena one, and V2 has no header.
  Inline keys need no interning at all. For heap keys the "immortal" bit has
  to move somewhere else, for example a pointer-range check against the
  intern pool or a spare bit in `n`. The SipHash path (`tycho_si_hash`) hashes
  `data, len` and only changes its argument type.
