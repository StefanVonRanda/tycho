# Packages

> **Thesis context:** Packages test that the arena model works across compilation units.
> A multi-package build merges all reachable files into one AST and emits one C file — the
> same per-scope arena codegen applies regardless of which package a definition came from.
> No separate compilation, no cross-package pointer escape.

A package is a directory of `.ty` files sharing one namespace. You name it with a `package`
declaration, pull it in with `import`, and use its qualified `pkg.symbol` names — for
functions, types, and enum variants. An alias renames the prefix.

```
package main
import g "geom"          # alias; plain `import "geom"` uses the package name

fn main():
    r := g.add(g.Point(3, 4), g.Point(1, 2))
```

`./tychoc pkg/main.ty` follows the imports and emits one binary. `import "core:strings"`
resolves the standard library found next to the transpiler binary, or at `TYCHO_CORELIB` if set
(see [the corelib catalog](../corelib.md)). Privacy is by leading underscore: a top-level
name beginning with `_` is package-private and cannot be reached through a `pkg.` qualifier
from another package; every other name is visible to importers.

A file with an `import` is a package, so give it its own directory. Fixtures live in
`tests/pkg/`; the resolution and mangling details are in [the packages design note](../packages.md).
