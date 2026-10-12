# Packages

> **One program per directory.** `tychoc` compiles every `.ty` file beside the
> entry file as one package. A file with an `import` is a package: give it
> its own directory, or its declarations collide with whatever else is there.


> **Memory:** packages don't change memory rules. All files build as one
> program, so the same per-scope freeing applies everywhere.

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
(see [the corelib catalog](../reference/corelib.md)). Privacy is by leading underscore: a top-level
name beginning with `_` is package-private and cannot be reached through a `pkg.` qualifier
from another package; every other name is visible to importers.

A file with an `import` is a package, so give it its own directory.

## How it works

- **A package is a directory**, not a file. Every `.ty` file in the directory
  belongs to the same package and adds to one shared namespace.
- **Privacy is by leading underscore.** A top-level symbol whose name starts
  with `_` is private to its own package: you can use it from any file *within*
  the package, but a qualified `pkg._name` from another package is rejected. Every other top-level symbol is exported.
- **There's no separate compilation.** The transpiler follows the import graph
  and emits a single `.c`.

### Surface syntax

```ebnf
file         := package_decl? import_decl* top_def*
package_decl := "package" IDENT NEWLINE          # first non-comment line
import_decl  := "import" IDENT? STRING NEWLINE   # optional alias, then the path
```

- **A package is a directory.** Every `.ty` file in it declares the same
  `package <name>`, and `<name>` must equal the directory name.
- **`import "math/geom"`** binds the package under its last path component
  (`geom`); **`import g "math/geom"`** aliases it to `g`. Paths resolve relative
  to the importing package's directory.
- **`pkg.symbol`** is the qualified form for types, functions, struct/enum
  constructors, and enum variants — `geom.Point`, `geom.add`, `geom.Red`.
- An imported package name is **reserved** in the file: a local or parameter
  with that name is a compile error, so `a.b` is never ambiguous. Import cycles
  are an error too.

### Example

```text
proj/
  main.ty            # package main
  geom/
    point.ty         # package geom
    vec.ty           # package geom (same package, second file)
```

`geom/point.ty`

<!-- fence-skip: one file of a multi-file package; needs the sibling files to build -->
```tycho
package geom
struct Point:
    x: int
    y: int
fn add(a: Point, b: Point) -> Point:
    return Point(a.x + b.x, a.y + b.y)
```

`main.ty`

<!-- fence-skip: one file of a multi-file package; needs the sibling files to build -->
```tycho
package main
import "geom"
fn main():
    r := geom.add(geom.Point(3, 4), geom.Point(1, 2))
    println("sum = (" + str(r.x) + "," + str(r.y) + ")")   # (4,6)
```

`./tychoc proj/main.ty` reads the `main` package, follows `import "geom"` to
`proj/geom/`, and builds one binary from both files.

### How it builds

- The transpiler reads the entry package, follows every `import`, and emits
  one `.c`. There's no linker step and no per-package object file.
- **A file with no `package` declaration is a standalone single-file program** —
  provided it imports nothing. An `import` switches on directory-package mode:
  the file must then declare a package and live in its own directory.
- **Main-package symbols keep their plain names.** Imported packages get a
  `pkg__` prefix. You only meet it when reading the generated C.

### The `core:` collection

The standard library is reached through a **collection** — a named root for
imports that resolve outside the local directory tree. `import "core:strings"`
pulls in the corelib package `strings`, which sits next to the transpiler binary by
default (or at `TYCHO_CORELIB` if set) rather than relative to the importer. The
corelib is documented in [corelib.md](corelib.md).

Only the `core:` collection exists today. Vendoring needs no more — see below.

### Vendoring

Third-party packages are **vendored**: you keep a `vendor/` directory in your
project, and the compiler resolves `import "vendor/name"` the same way it
resolves any non-`core:` path — **relative to the importing package's own
directory**, independent of the current working directory. No registry, no version resolution:
`vendor/` is just a directory, and the code in it is the code that builds.

The shape that keeps the `../` chains at depth one:

```
myproj/
  main.ty            # import "vendor/greet"
  vendor/
    greet/greet.ty   # import "../util"  -- relative to greet's own dir
    util/util.ty
```

A vendored package imports its siblings with a relative path (`../util`),
resolved against **its own** directory — so the vendor tree stays flat (one
package per top-level entry) and moves as a unit. Vendored packages mix
freely with `core:` imports. `tycho-fetch <url> <name>` downloads a package
tarball (the single-top-directory shape) into `vendor/<name>` and prints the
sha256 of the downloaded bytes; a tarball already on disk is `tycho-fetch
--local <path> <name>`, since `file://` is refused rather than fetched over
HTTP. `git clone` / `cp` into `vendor/` work just as well — the compiler never
cares how the files got there.

### Imports are per-file

An `import` is visible only in the file that writes it. A second file in the same
package that names that package must import it as well — using a package this
file did not import is an error, even when a sibling file already imported it and
the build therefore resolved it. An import that no file in the build uses is an
error too.
