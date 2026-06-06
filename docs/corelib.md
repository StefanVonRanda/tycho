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
- **`strings`** — `to_upper`, `to_lower`, `starts_with`, `ends_with`, `contains`,
  `repeat(s, n)`, `trim` (ASCII whitespace), `parse_int`, `is_space`. (`split`/`find`/
  `substr`/`len`/`chr` are builtins.)
- **`arrays`** — `[int]` utilities: `contains`, `index_of` (−1 if absent), `count`, `sum`,
  `imin`, `imax`, `reverse`, `is_sorted`, `sort` (ascending). `reverse`/`sort` return a new
  array — value semantics, the input is never mutated. (`push`/`pop`/`len`/`range` are
  builtins.)
- **`arrays_str`** — the same over `[string]` (no overloading in hier, so per-type variants
  are sibling packages): `contains`, `index_of`, `count`, `join(xs, sep)`, `smin`, `smax`
  (lexicographic), `reverse`, `is_sorted`, `sort`.
- **`arrays_float`** — the same over `[float]`: `contains`, `index_of`, `count`, `sum`,
  `fmin`, `fmax`, `reverse`, `is_sorted`, `sort`. (Equality/`contains` use exact float `==`.)
- **`iter`** — higher-order over `[int]`, each taking a `fn`/closure: `map`, `filter`,
  `reduce`, `count`, `any`.

## Testing

`make corelib` (→ `corelib/run.sh`): every `corelib/test/<name>/main.hi` is compiled three
ways — by the C compiler, via `hierc --bundle | hierc0`, and via **standalone** `hierc0`
(which resolves `core:` itself) — and all three must produce the golden
`corelib/test/<name>.out`. Re-record goldens with `RECORD=1 sh corelib/run.sh`. Part of
`make ci`.
