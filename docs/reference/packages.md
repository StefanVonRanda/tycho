# Packages

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
(see [the corelib catalog](../corelib.md)). There's no privacy: everything in a package is
visible to its importers.

A file with an `import` is a package, so give it its own directory. Fixtures live in
`tests/pkg/`; the resolution and mangling details are in [the packages design note](../packages.md).
