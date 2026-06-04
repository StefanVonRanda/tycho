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

The self-hosted **hierc0** sees corelib via `hierc --bundle` (which resolves `core:` and
emits one merged source stream that hierc0 compiles) — this is the path the fixpoint
exercises. Standalone `hierc0 <path>` does **not** yet resolve `core:` itself (that needs
a `getenv` builtin in Hier so the dir-walker can read `HIER_CORELIB`); deferred.

## Shape (constrained by the language)

No generics, no closures, no methods — so corelib is **concrete free functions over
concrete types**. Higher-order helpers (map/filter/reduce taking a function) aren't
expressible; array utilities are per-element-type. This is a deliberate consequence of
the language's minimalism, not a corelib limitation.

## Packages

- **`math`** — integer utilities: `abs`, `imin`, `imax`, `clamp(x, lo, hi)`, `sign`,
  `gcd`, `ipow(base, exp)` (exp ≥ 0). (`sqrt`/`pow`/`floor`/`fabs` are float builtins.)
- **`strings`** — `to_upper`, `to_lower`, `starts_with`, `ends_with`, `contains`,
  `repeat(s, n)`, `trim` (ASCII whitespace), `parse_int`, `is_space`. (`split`/`find`/
  `substr`/`len`/`chr` are builtins.)

More to come (arrays).

> **f-string gotcha:** an interpolation hole `{…}` cannot contain a string literal — the
> lexer stops the f-string at the inner `"`. Bind it to a variable first:
> `p := f("x"); print(f"got {p}")`. (A brace/quote-aware f-string lexer would lift this.)

## Testing

`make corelib` (→ `corelib/run.sh`): every `corelib/test/<name>/main.hi` is compiled by
the C compiler **and** via `hierc --bundle | hierc0`, and both must produce the golden
`corelib/test/<name>.out`. Re-record goldens with `RECORD=1 sh corelib/run.sh`. Part of
`make ci`.
