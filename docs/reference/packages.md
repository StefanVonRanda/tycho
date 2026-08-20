# Packages

> **One program per directory.** `tychoc` compiles every `.ty` file beside the
> entry file as one package, so two programs cannot share a directory — the
> second one's declarations collide with the first's and the error names a file
> you did not compile (`'Index' is already defined`). A file with an `import` is
> a package: give it its own directory. Four separate first-time readers hit
> this in one day.


> **Memory:** A multi-package build merges all reachable files into one AST and emits
> one C file, so the same per-scope arena codegen applies regardless of which package
> a definition came from. There is no separate compilation and no cross-package
> pointer escape.

A package is a directory of `.ty` files sharing one namespace. You name it with a `package`
declaration, pull it in with `import`, and use its qualified `pkg.symbol` names — for
functions, types, and enum variants. An alias renames the prefix.

<!-- fence-skip: one file of a multi-file package; needs the sibling files to build -->
```tycho
package main
import g "geom"          # alias; plain `import "geom"` uses the package name

fn main():
    r := g.add(g.Point(3, 4), g.Point(1, 2))
```

`./tychoc pkg/main.ty` follows the imports and emits one binary. `import "core:strings"`
resolves the standard library found next to the transpiler binary, or at `TYCHO_CORELIB` if set
(see [the corelib catalog](../guides/corelib.md)). Privacy is by leading underscore: a top-level
name beginning with `_` is package-private and cannot be reached through a `pkg.` qualifier
from another package; every other name is visible to importers.

A file with an `import` is a package, so give it its own directory. Fixtures live in
`tests/pkg/`; the resolution and mangling details are in [the packages design note](../guides/packages.md).
