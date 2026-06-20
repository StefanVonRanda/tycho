# Packages & modules

Hier has **Odin-style packages**: a package is a *directory* of `.hi` files that
share one flat namespace. You `import` a package and reference its symbols with a
qualified `pkg.symbol` name. There is no privacy — every top-level symbol in a
package is visible to importers — and no separate compilation: the driver follows
the import graph, merges everything reachable into one program, and emits a
single `.c`. The README's [Packages section](../README.md#packages) is the short
version; this is the full design.

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

- **Whole-program transpile.** The driver reads the entry package, follows every
  `import`, merges all reachable definitions into one AST, and emits one `.c`.
  There is no linker step and no per-package object file.
- **A file with no `package` declaration is a standalone single-file program** —
  exactly today's behavior. The `package` keyword is what switches on
  directory-package mode, so every existing single-file program is unaffected.
- **Main-package symbols are never mangled** — they keep their plain names.
  Only imported packages get a `pkg__` prefix, applied uniformly to every
  definition and reference, including the implicit monomorphized families
  (`Arr_geom__Point` for `[geom.Point]`, the `_copy`/`_eq` helpers, and so on).
  Cross-package structs, enums, arrays, tuples, and `Result` element types all
  work, keyed on the mangled type name end to end.

## The `core:` collection

The standard library is a **collection** — a named root for imports that resolve
outside the local directory tree. `import "core:strings"` pulls in the bundled
corelib package `strings`, located via the `HIER_CORELIB` environment variable
rather than relative to the importer. The corelib and its three-way gating are
documented in [corelib.md](corelib.md). (Odin's wider `collection:` mechanism —
arbitrary named roots beyond `core:` — is not exposed yet.)

## Verification

Packages work in both compilers. A package fixture lives in
`tests/pkg/<name>/` (entry `main.hi`, golden `tests/pkg/<name>.out`); `make test`
compiles the entry with `hierc` under the usual native-vs-ASan + golden
discipline, and `make fixpoint` additionally builds each fixture with the
self-hosted compiler and asserts byte-identical output against the C compiler.

---

## Appendix: implementation history

For contributors. The feature was built in `hierc` first — fixpoint green at
every step — then ported to `hierc0`, then dogfooded by splitting the compiler
itself.

**Staged build.** (A) package declaration + multi-file single-package merge, no
imports acted on; (B) imports, qualified references, and the one new resolver
step — disambiguate `a.b` after all packages are parsed and symbol tables built;
(C) cross-package aggregates (qualified types in fields/params/returns/elements,
monomorphized families keyed on the mangled name).

**Self-hosting via a bundle stream.** `hierc0` reads source on stdin and the
language has no globals, so the C compiler's directory-walking driver and global
mangling prefix can't be replicated directly. Instead, `hierc --bundle <entry>`
emits the whole package program as one post-order source stream (imports first;
the entry package's header rewritten to `package main` so it keeps the empty
prefix), and `hierc0` mangles in a post-parse pass that is **dormant unless a
`package` declaration was seen** — so a single-file compile (including
`hierc0.hi` itself) is byte-identical and the fixpoint stays green by
construction.

**Dogfood.** `compiler/pkg-split.sh` splits the self-hosted compiler into a
two-package program — `rt` (the leaf C-runtime/string emitters) and `main`
(`import "rt"`) — generated from `hierc0.hi` by function name, so there is no
duplicate source to maintain. `make fixpoint` proves the split compiler is itself
a fixed point and that the multi-package build emits byte-identical C to the
single-file build. A finer lexer/parser/typecheck/codegen decomposition is
mechanical (move the shared `Tok`/AST vocabulary into a common package, qualify
it at the use sites) and needs no new capability.

**Risk that shaped the design.** Mangling has to be applied at *every*
definition and reference, implicit families included — a missed site is a loud C
link error, never silent corruption — and main-package non-mangling is the
invariant that keeps the goldens and fixpoint stable, so it is guarded
explicitly.
