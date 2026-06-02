# Packages & modules — design + implementation plan

Hier's next major language feature: **Odin-style packages**. A package is a
**directory** of `.hi` files sharing one flat namespace; you `import "path"` and
reference `pkg.symbol`. Decided constraints:

- **No privacy, ever.** Every top-level symbol in a package is visible to importers.
- **No generics** (separate decision; unrelated, but the type surface stays fixed).
- **Whole-program transpile.** The driver follows the import graph, merges all
  reachable packages into one AST, and emits one `.c` — no linker, no separate
  compilation.

## Surface syntax

```
file         := package_decl? import_decl* top_def*
package_decl := "package" IDENT NEWLINE          # first non-comment line
import_decl  := "import" IDENT? STRING NEWLINE   # optional alias, then the path
```

- **Package = directory.** Every `.hi` in a directory declares the same
  `package <name>`, and `<name>` must equal the directory name.
- **`import "math/geom"`** binds the package under its last path component
  (`geom`); **`import g "math/geom"`** aliases it to `g`.
- **`pkg.symbol`** — qualified use for types, functions, struct/enum constructors,
  and enum variants (`geom.Point`, `geom.add`, `geom.Red`).
- Import paths resolve **relative to the importing package's directory**
  (Odin-style `collection:` roots, e.g. `std:`, come later with a stdlib).
- **Cycles are an error.** **An imported package name is reserved in the file** —
  a local/param with that name is a compile error (keeps `a.b` unambiguous).

### Two-package example

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
`proj/geom/`, parses both geom files, and emits one `.c` with package-prefixed
symbols (`geom__Point`, `geom__add`).

## Invariants (the safety rails)

1. **Existing single-file programs stay byte-identical.** A file with **no
   `package` decl** is a standalone single-file program (today's behavior —
   compile just that file). A file **with** `package` triggers directory-package
   mode. Main-package symbols are **never mangled** (they keep today's naming);
   only imported packages get a `pkg__` prefix. So every current test, golden, and
   the whole self-host fixpoint are untouched until something imports.
2. **Build in `hierc` first**, fixpoint green at every step, then port to
   `hierc0`, then dogfood by splitting `hierc0.hi`.

## Stages

### Stage A — package decl + multi-file single-package merge (no imports acted on)
- AST: a `package` field on each top-level def (`Proc`/`StructDef`/`EnumDef`/newtype).
- Lexer: `package`/`import` keywords.
- Parser: parse the optional `package <name>` and `import [alias] "path"` headers
  (record imports; don't resolve them yet).
- Driver: if the entry file declares `package`, compile a package = read **all**
  `.hi` in the entry's directory, assert they agree on the package name and it
  matches the directory, and merge their defs into one program. No `package` decl
  → single-file mode (unchanged). No mangling yet (single package, names unique).
- **Gate:** all current tests + `make fixpoint` byte-identical; a two-file
  single-package program compiles and runs.

### Stage B — imports + qualified references + mangling
- Parser: allow `pkg.func(args)` (qualified call), `pkg.Type` (qualified type),
  `pkg.Red` (qualified value); value-position `a.b` still parses as field access.
- Driver: resolve `import` paths relative to the importing package's dir; read the
  imported packages; build the import graph; **reject cycles**.
- Resolver (the one new thing): after all packages are parsed and symbol/import
  tables built, disambiguate `a.b` — if `a` is an imported package → qualified
  reference (look `b` up there, error if absent), else field access. Reserved
  package names (local can't shadow an import) → error.
- Codegen: a single `mangle(pkg, name) → "pkg__name"` used by **every** definition
  and reference of an imported-package symbol — including the implicit families
  (`Arr_`, `mk_`, `_copy`, `_eq`). Main-package symbols unmangled.
- **Gate:** the `geom` example outputs `(4,6)`; single-file tests byte-identical.

### Stage C — cross-package aggregates
- Qualified types in fields/params/returns/array-elements (`p: geom.Point`,
  `xs: [geom.Point]`); cross-package struct construction and enum variants; the
  monomorphized families keyed on the mangled name (`Arr_geom__Point`,
  `Option(geom.Point)`).
- **Gate:** an array of a cross-package struct + a cross-package enum matched in
  `main`, clean under ASan/UBSan/LeakSanitizer.

### Stage D — port to `hierc0`
- Replicate A–C in `compiler/hierc0.hi`. The feature is additive (`hierc0.hi` is
  still one file → its self-compilation is unchanged), so **fixpoint stays green
  by construction**; extend the differential to cover the package examples.

### Stage E — dogfood: split `hierc0.hi` into packages
- Split the ~3.5k-line compiler into `lexer`/`parser`/`typecheck`/`codegen`/`main`.
- The real proof: hierc0 (now multi-package) self-compiles the multi-package source
  **byte-identical** (B≡C still holds).

## Test-harness change (needed from B on)
`make test` compiles single files; packages need a multi-file entry. Add a
`tests/pkg/<name>/` convention (a directory = one package program, entry
`main.hi`) and a harness branch that compiles the entry and checks output against
a golden — same native-vs-ASan + golden discipline; the differential/fixpoint
extends to these.

## Top implementation risks
- **The `a.b` disambiguation** must run after all imports are known: parse-all →
  build symbol/import tables → resolve expressions.
- **Mangling consistency** — one `mangle()` for every def *and* reference,
  including the implicit families. A missed site is a loud C link error, never
  silent.
- **Main-package non-mangling** is the invariant that keeps the fixpoint/goldens
  stable — guard it explicitly.
- **Cross-package type families** — verify the monomorphization key is the mangled
  type name end to end.
