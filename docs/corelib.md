# corelib — Hier's standard library

A growing set of packages under `corelib/`, imported with the `core:` collection root:

```
import "core:math"

fn main():
    print(f"gcd={math.gcd(12, 18)}\n")
```

## Resolution

`import "core:<pkg>"` resolves to `$HIER_CORELIB/<pkg>` (the corelib directory) and binds
the name `<pkg>`. Set `HIER_CORELIB` to point at the corelib dir — the Makefile and CI
export `HIER_CORELIB=corelib` (repo-relative). Non-`core:` imports stay relative to the
importing package, unchanged. Implemented in the C compiler's package walker
(`resolve_pkg_dir`/`pkg_basename` in `src/hierc.c`, used by both `compile_package` and
`--bundle`).

The self-hosted **hierc0** sees corelib two ways, both checked by `corelib/run.sh`:
(1) fed `hierc --bundle` (which resolves `core:` and emits one merged source stream) — the
path the fixpoint exercises; and (2) **standalone** `hierc0 <path>`, which now resolves
`import "core:X"` itself: its dir-walker reads `HIER_CORELIB` via the `getenv` builtin and
routes `core:X` to `$HIER_CORELIB/X` (`resolve_import_dir`, mirroring hierc's
`resolve_pkg_dir`). So hierc0 is a fully standalone corelib-aware compiler — no `--bundle`
middleman needed.

## Shape (constrained by the language)

No generics, no methods — so corelib is **concrete free functions over concrete types**;
array utilities are per-element-type (currently `[int]`). Higher-order helpers
(map/filter/reduce taking a function) **are** expressible since closures shipped — they
live in `core:iter` and take a first-class `fn`/closure argument. This shape is a
deliberate consequence of the language's minimalism, not a corelib limitation.

## Packages

- **`math`** — integer utilities: `abs`, `imin`, `imax`, `clamp(x, lo, hi)`, `sign`,
  `gcd`, `ipow(base, exp)` (exp ≥ 0). (`sqrt`/`pow`/`floor`/`fabs` are float builtins.)
- **`fmath`** — float utilities over the float builtins: `pi`, `e`, `min`, `max`,
  `clamp`, `sign`, `round` (half away from zero), `trunc`, `lerp(a, b, t)`,
  `approx_eq(a, b, eps)`. (No trig — the language has no libm sin/cos builtin.)
- **`char`** — byte/char classification & conversion over int byte values (what
  `s[i]`/`chr` use): `is_digit`, `is_alpha`, `is_alnum`, `is_upper`, `is_lower`,
  `is_space`, `is_hex`, `to_upper`/`to_lower` (one byte), `digit_val` (−1 if not a
  digit), `hex_val` (0..15 or −1). The lexer/parser workhorse.
- **`strings`** — `to_upper`, `to_lower`, `starts_with`, `ends_with`, `contains`,
  `repeat(s, n)`, `trim` (ASCII whitespace), `parse_int`, `is_space`, `lines` (splits on
  `\n`, drops one trailing `\r` per line, trailing newline adds no empty line), `replace`
  (non-overlapping, left to right; empty `old` returns the input unchanged), `count(s, sub)`,
  `strip_prefix`/`strip_suffix`, `pad_left`/`pad_right(s, width, pad)` (pad is one byte),
  `reverse`, `capitalize`, `split_once(s, sep) -> (before, after)` (`(s, "")` if absent).
  (`split`/`find`/`substr`/`len`/`chr` are builtins.)
- **`arrays`** — `[int]` utilities: `contains`, `index_of` (−1 if absent), `count`, `sum`,
  `product`, `imin`, `imax`, `reverse`, `is_sorted`, `sort` (ascending), `take(xs, n)`,
  `drop(xs, n)`, `concat(a, b)`, `fill(n, v)`, `dedup` (consecutive — sort first for a full
  dedup). All return a new array — value semantics, the input is never mutated. (`push`/
  `pop`/`len`/`range` are builtins.)
- **`arrays_str`** — the same over `[string]` (no overloading in hier, so per-type variants
  are sibling packages): `contains`, `index_of`, `count`, `join(xs, sep)`, `smin`, `smax`
  (lexicographic), `reverse`, `is_sorted`, `sort`.
- **`arrays_float`** — the same over `[float]`: `contains`, `index_of`, `count`, `sum`,
  `fmin`, `fmax`, `reverse`, `is_sorted`, `sort`. (Equality/`contains` use exact float `==`.)
- **`iter`** — higher-order over `[int]`, each taking a `fn`/closure: `map`, `filter`,
  `reduce`, `count`, `any`. (Lambdas in hier are expression-bodied; pass a named fn for a
  multi-line predicate.)
- **`iter_str`**, **`iter_float`** — the same five over `[string]` / `[float]`.
- **`sort`** — `argsort(keys)` / `argsort_desc(keys)` (`[int]`) / `argsort_str(keys)`:
  return the index permutation that orders the keys — the no-generics way to sort
  anything (keep data in parallel arrays, argsort one, walk all through the permutation).
  All stable. Plus `by_key(xs, key)`: sort `[int]` by a derived key fn/closure.
- **`rand`** — deterministic xorshift32 (not cryptographic). No globals in hier, so the
  state is an explicit int threaded via `inout`: `st := rand.seed(42)`,
  `rand.next(&st)` ([1, 2³²)), `rand.below(&st, n)` ([0, n)), `rand.shuffle(&st, xs)`
  (Fisher-Yates, returns a new array). Every left shift is masked to 32 bits inside the
  signed 64-bit int, so the generator is UB-free by construction.
- **`time`** — wraps the `clock()` (monotonic ns) and `now()` (UNIX seconds) builtins.
  Stopwatch (value-semantic, no inout — a reading is just an int): `sw := time.start()`,
  then `time.elapsed_ns(sw)` / `elapsed_us` / `elapsed_ms`. Duration conversions
  `ns_to_us` / `ns_to_ms` / `ns_to_s`. Wall clock `unix_secs()` (named so, not `now`,
  to avoid shadowing the builtin and recursing).
- **`regex`** — POSIX extended regular expressions (ERE), the first **C-shim-backed**
  core module (FFI over `<regex.h>`, libc). `compile(pat) -> ptr` (opaque handle;
  `ok`/`is_null` to check), `is_match`, `find` / `find_end` (offset or −1), `matched`
  (first match substring), `release` (free the C-owned handle). The compiled pattern
  is C-malloc'd, **not** arena-managed, so call `release` when done.

## C-shim (FFI-backed) modules

A core module can wrap a C library via FFI. Drop a `<module>/<module>_shim.c` next to
the `.hi`; the compiler auto-compiles and links it on `import "core:<module>"` (no
`--shim` needed). The `.hi` declares the shim's functions with `extern fn` (and
`extern "Lib" fn` auto-adds `-lLib` for an external library; `core:regex` needs none —
POSIX regex is in libc). Opaque handles cross as `ptr` (carried by value, never
dereferenced or arena-managed — `null` / `is_null`). The corelib harness links a
module's shim on the hierc0 paths too, so all three (hierc, hierc0 `--bundle`,
standalone hierc0) stay in agreement.

## Testing

`make corelib` (→ `corelib/run.sh`): every `corelib/test/<name>/main.hi` is compiled three
ways — by the C compiler, via `hierc --bundle | hierc0`, and via **standalone** `hierc0`
(which resolves `core:` itself) — and all three must produce the golden
`corelib/test/<name>.out`. Re-record goldens with `RECORD=1 sh corelib/run.sh`. Part of
`make ci`.
