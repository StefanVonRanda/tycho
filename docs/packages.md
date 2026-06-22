# Packages & modules

Hier organizes code into **packages**. A package is a *directory* of
`.hi` files that share one flat namespace. You `import` a package and reference
its symbols with a qualified `pkg.symbol` name.

Three properties define the model:

- **A package is a directory**, not a file. Every `.hi` file in the directory
  belongs to the same package and contributes to one shared namespace.
- **There is no privacy.** Every top-level symbol in a package is visible to
  importers; there is no `public`/`private` distinction.
- **There is no separate compilation.** The compiler follows the import graph,
  merges everything reachable into one program, and emits a single `.c`.

The README's [Packages section](../README.md#packages) is the short version; this
is the full reference.

## Surface syntax

```
file         := package_decl? import_decl* top_def*
package_decl := "package" IDENT NEWLINE          # first non-comment line
import_decl  := "import" IDENT? STRING NEWLINE   # optional alias, then the path
```

- **A package is a directory.** Every `.hi` file in it declares the same
  `package <name>`, and `<name>` must equal the directory name.
- **`import "math/geom"`** binds the package under its last path component
  (`geom`); **`import g "math/geom"`** aliases it to `g`. Paths resolve relative
  to the importing package's directory.
- **`pkg.symbol`** is the qualified form for types, functions, struct/enum
  constructors, and enum variants — `geom.Point`, `geom.add`, `geom.Red`.
- An imported package name is **reserved** in the file: a local or parameter
  with that name is a compile error, so `a.b` is never ambiguous. Import cycles
  are an error.

## Example

```
proj/
  main.hi            # package main
  geom/
    point.hi         # package geom
    vec.hi           # package geom (same package, second file)
```

`geom/point.hi`

```
package geom
struct Point:
    x: int
    y: int
fn add(a: Point, b: Point) -> Point:
    return Point(a.x + b.x, a.y + b.y)
```

`main.hi`

```
package main
import "geom"
fn main():
    r := geom.add(geom.Point(3, 4), geom.Point(1, 2))
    print("sum = (" + str(r.x) + "," + str(r.y) + ")\n")   # (4,6)
```

`./hierc proj/main.hi` reads the `main` package, follows `import "geom"` to
`proj/geom/`, parses both `geom` files, and emits one `.c` with package-prefixed
symbols (`geom__Point`, `geom__add`).

## How it builds

The package system is a whole-program transpile, not a linker pipeline:

- The compiler reads the entry package, follows every `import`, merges all
  reachable definitions into one AST, and emits one `.c`. There is no linker step
  and no per-package object file.
- **A file with no `package` declaration is a standalone single-file program.**
  The `package` keyword is what switches on directory-package mode, so every
  single-file program compiles unchanged.
- **Main-package symbols keep their plain names.** Only imported packages get a
  `pkg__` prefix, applied uniformly to every definition and reference. This
  includes the generated type families — `Arr_geom__Point` for `[geom.Point]`,
  the `_copy`/`_eq` helpers, and so on — so cross-package structs, enums, arrays,
  tuples, and `Result` element types all work, keyed on the mangled type name end
  to end.

You rarely need to think about the mangling; it matters only when reading the
generated C.

## The `core:` collection

The standard library is reached through a **collection** — a named root for
imports that resolve outside the local directory tree. `import "core:strings"`
pulls in the bundled corelib package `strings`, located via the `HIER_CORELIB`
environment variable rather than relative to the importer. The corelib is
documented in [corelib.md](corelib.md).

Only the `core:` collection is exposed today; arbitrary named roots (Odin's wider
`collection:` mechanism) are not.

## Both compilers

Packages work identically in the C reference compiler (`hierc`) and the
self-hosted compiler (`hierc0`).

`hierc0` can compile a package directly — `hierc0 path/main.hi` walks the
directory and follows its imports through the same filesystem builtins — or read a
pre-bundled, post-order source stream on stdin, which the C compiler produces with `hierc --bundle <entry>`: it emits imports first, with
the entry package's header rewritten to `package main`. `hierc0` then applies the
same package mangling in a post-parse pass — dormant unless a `package`
declaration was seen, so a single-file compile stays identical.

Package fixtures live in `tests/pkg/<name>/` (entry `main.hi`, golden
`tests/pkg/<name>.out`). `make test` compiles the entry with `hierc`, and
`make fixpoint` additionally builds each fixture with `hierc0` and checks that its
output is byte-identical to the C compiler's.

The compiler dogfoods its own package system: `compiler/pkg-split.sh` splits the
self-hosted compiler into a two-package program — `rt` (the leaf
C-runtime/string emitters) and `main` (`import "rt"`) — derived from `hierc0.hi`
by function name, with no duplicate source to maintain. This serves as a
real-world test that a multi-package build emits the same C as the single-file
build.
