# 27. Program structure · 28. Packages and modules

A Tycho **program** is the whole set of definitions reachable from a single
entry point, gathered across one or more **packages** and compiled as one unit.
This chapter covers program- and package-level structure only; declarations are
typed in [§5](03-types.md), scoped in [§12](08-declarations.md), and the types an
`extern` may carry across the C boundary are [§24](14-ffi.md).


## 27. Program structure

### 27.1 The entry point

A program MUST define exactly one procedure named `main`, and it MUST take no
parameters — the command line arrives through the `args()` builtin
([§29](16-builtins.md)). `main` has **two** legal return shapes:

- `fn main():` — returns nothing. The process exits `0`.
- `fn main() -> Result(void, string):` — so `or_return`
  ([§19](../spec/10-statements.md)) works at the entry point. On `Ok` the process
  exits `0`; on `Err(msg)` the implementation MUST write `msg` followed by a
  newline to standard error and exit with a **non-zero** status (the reference
  exits `1`).

The error type MUST be `string`. An `Err` reaching the entry point is *printed*,
and `has_str` admits no enum, so the message is the error itself. The ok payload
MUST be `void`: a value there would make the exit status ambiguous.

The message is written **bare**, with no implementation-supplied prefix, as
`die(msg)` writes a user message ([§29](16-builtins.md)).

A minimal complete program is therefore:

```tycho
fn main():
    println("hello from tycho")
```

```output
hello from tycho
```

`main` is the sole entry point: execution begins by calling it and the program
terminates when it returns (the runtime allocates the program's root arena
around this call — [§10](07-memory-model.md)). `main` is an
ordinary procedure in every other respect; it MAY be called recursively and MAY
appear in any file of the entry package.

The program's command-line arguments are obtained through the builtin `args()`,
which returns a `[string]` ([§29](16-builtins.md)). The array
mirrors the process `argv` exactly, so `args()[0]` is the **program name** (the
path by which the executable was invoked) and the operands follow at
`args()[1]` onward. `args()` is never empty for a normally-launched
program.

### 27.2 The compilation unit

A Tycho program is compiled as **one whole-program unit**. The compiler reads
the entry file, follows every `import` transitively, merges all reachable
definitions into a **single** AST, and emits a **single** C translation unit,
which is compiled and linked into a **single** executable. There is **no**
separate compilation, **no** per-package object file, and **no** linker step
that joins independently-compiled packages ([packages.md](../reference/packages.md) §"How it builds").

A conforming implementation is not required to transpile to C; the single-unit
merge, however, is observable and normative — it determines name resolution and
mangling (§28.3), cross-package type identity ([§5.1](03-types.md#51-the-type-identity-model)),
and the fact that a definition's behavior does not depend on which package it
came from.

Whether a source file participates in the package system is decided by the
presence of a `package` declaration. The entry file is compiled in **package
mode** (the whole directory plus its import graph, §28.5) iff it begins with a
`package` declaration; a file with no `package` declaration is a **single-file
program** compiled alone. A single-file program has
the same entry-point rule (§27.1) and is a degenerate one-package unit.

### 27.3 `extern` declarations

An `extern` declaration introduces a procedure that is **bodyless** and bound to
a foreign **C symbol** rather than to Tycho code:

```text
extern [ "Lib" ] fn name(p: T, …) [ -> T ]
```

The following rules are normative:

- An `extern` declaration has **no body**: it ends at the newline after its
  signature, so a following indented block is a parse error.
- Its `name` is the **literal C symbol** and is **not** package-mangled (§28.3),
  because a C symbol is global. Two `extern` declarations naming the
  same C symbol therefore refer to the same foreign function regardless of the
  Tycho packages they appear in.
- An `extern` procedure is invoked **without** the implicit arena argument that
  every Tycho procedure receives ([§10](07-memory-model.md)); it
  calls the C symbol directly.
- The optional leading string literal names a **link library** (`extern "Lib"
  fn …`), which the implementation adds to the link line as `-lLib` (§27.4).

The types an `extern` parameter or return may name are defined in
[§24](14-ffi.md).

### 27.4 Linking and the C compiler invocation

For a program that is not stopped early (e.g. `--emit-c`, `--symbols`), the
reference implementation compiles the emitted C and links it into the output
executable with a single C-compiler invocation of the shape:

```text
cc {-O3 [-flto] | -O0 -g} -fwrapv -ffp-contract=off -pthread \
   [-march=native | -march=<level>] [$TYCHO_CFLAGS] -o OUT gen.c <auto shims> \
   -lm <-lLib per extern lib> <-L/-I/--link/--pkg passthrough> <pkg-config flags from deps>
```

with these normative properties:

- **`-fwrapv` is required semantics, not an optimization flag.** It makes signed
  integer overflow **defined** as two's-complement wrapping rather than C
  undefined behavior, which is precisely Tycho's integer-overflow contract
  ([§5.2.1](03-types.md#521-int)): a conforming realization on C MUST compile
  such that signed overflow wraps and never traps or miscompiles.
- **`-lm` is always passed**, so bare libc math externs (e.g. `extern fn sqrt`)
  link with no `"m"` annotation.
- **`-pthread` is always passed**, supporting the concurrency runtime
  ([§20](13-concurrency.md)).
- **Optimization / debug:** `-O3` is the portable default; `-g` selects `-O0 -g`
  (unoptimized with debug info) instead.
- **`-march=native` is opt-in** via `--native`. It is host-CPU-specific and MUST
  NOT be assumed portable across machines.
- **A raised ISA baseline is opt-in** via `--target <level>`, whose levels are the
  x86-64 microarchitecture levels `x86-64-v2`, `x86-64-v3` and `x86-64-v4`, plus
  `baseline` for the default. `baseline` and the absence of the flag add nothing
  to this line; any other level adds `-march=<level>` and the binary MUST NOT be
  assumed to run on a machine below it. An unknown level, and a level the C
  compiler does not accept, are both hard errors.
- The default C compiler is `cc`; `--cc <compiler>`
  overrides it.

Three CLI options splice additional flags onto this line for FFI:

- **`--link <lib>`** appends a raw `-l<lib>`.
- **`--pkg <name>`** runs `pkg-config --cflags --libs <name>` and appends the
  result.
- **`--shim <file.c>`** adds a companion C source that is compiled and linked
  alongside the generated C.

A build that stops early at `--emit-c` and links the generated C itself needs the
same auto-discovered shim set that §27.4's line would have received. **`--print-shims`**
prints it: the *transitive* closure of companion `<pkg>_shim.c` sources (§28.6),
including shims reached only through an indirect import, one absolute path per
line on stdout, then exits without code generation. A program with no package
prints nothing and exits 0.

**`--print-deps`** prints the *transitive* closure of pkg-config package
**names** declared by each package's `deps` file (§28.6), one per line,
duplicates collapsed, then exits without code generation. The names are printed
whether or not pkg-config can resolve them on this host, so a harness can tell a
missing library (a skip) from a package with no dependencies. A program whose
closure declares no `deps` prints nothing and exits 0.

`-L<dir>` and `-I<dir>` (attached or separated) also accumulate onto the line.
Every library/package name that reaches the shell — from
`extern "Lib"`, `--link`, or `--pkg` — MUST be restricted to a conservative
character set (`[A-Za-z0-9._+-]`) and rejected otherwise, so compiling an
untrusted source cannot inject a shell command. The `extern "Lib"` libraries, the auto-discovered
shims, and the `deps` flags are contributed automatically by the sources they
belong to (§27.3, §28.6); the CLI options above are the manual escape hatch.

The exact command line belongs to the reference implementation, not the
language ([§1.1](00-conventions.md#11-scope)); it is implementation-defined
([Appendix F](appendix-f-impl-defined.md)). Normative here are only `-fwrapv`
wrapping, the language's integer contract, and that a program naming a link
library (via `extern "Lib"`, a `deps` entry, or `--link`/`--pkg`) needs that
library at link time.

## 28. Packages and modules

### 28.1 Packages and namespaces

A **package** is a *directory* of `.ty` files that all declare the same
`package NAME` and share **one flat namespace**. Every top-level definition in
any file of the directory — function, `struct`, `enum`, `handle`, newtype,
`const`, and enum variant — belongs to that one namespace; the split into files
is not semantically significant.

A package's symbols are reached from another package by a **qualified name**
`pkg.symbol`, where `pkg` is the importing binding (§28.2). The qualified form
applies to types, functions, struct/enum constructors, and enum variants alike
— `geom.Point`, `geom.add`, `geom.Red` (a payload-less variant included).

Every non-entry package's `package NAME` MUST equal the final component of the
import path that reached it — which, for a relative import, is its directory
name. The **entry package may be named anything**: its name is taken from the
entry file's own `package` declaration and is not constrained to the directory
name.

### 28.2 Import declarations

An import is a top-level declaration. The plain form binds the package under the final component of its path:

```tycho
package main
import "core:math"

fn main():
    println(str(math.abs(-7)))
```
```output
7
```

The aliased form binds it under the alias instead, and the original name is then
not in scope:

```tycho
package main
import m "core:math"

fn main():
    println(str(m.abs(-7)))
```
```output
7
```

The grammar is `"import" IDENT? STRING NEWLINE`. A nested path binds its
**final component** (`import "math/geom"` binds `geom`).

`package` and `import` are **contextual**: they are special only as the leading
identifier of a top-level item and remain ordinary identifiers elsewhere, so
they are not reserved words. A bound package name,
however, is effectively reserved within the file: because a bare `pkg.symbol`
is disambiguated by treating `pkg` as an imported package first, a program
SHOULD NOT shadow an imported binding with a local of the same name. **Import cycles are an
error** (§28.5).

### 28.3 Name resolution and visibility

Within the single merged unit (§27.2), every definition carries a
**per-package prefix**. The entry package uses the **empty** prefix, so its
symbols keep their plain names; every imported package `P` uses the prefix
`P__`, applied uniformly to every definition and every reference — including the
generated type families for its arrays, tuples, maps, and helper functions.
Two identically-named symbols in different packages are therefore distinct after
mangling. `extern` C symbols are the sole exception and are never prefixed
(§27.3).

**Visibility is by a leading underscore.** A top-level symbol whose name begins
with `_` is **private to its own package**: it is usable from any file *within*
that package, but a qualified `pkg._name` from another package MUST be rejected
with a diagnostic. Every other top-level symbol is exported. There is no
visibility keyword.

That includes **top-level `const`s**: `pkg.NAME` reads an exported constant and
is **folded to its literal at the use site**, exactly as an unqualified const
is, so no runtime global is emitted and the reference costs nothing. A
`pkg._NAME` is refused by the same underscore rule. A qualified const is
accepted wherever an expression is; it is **not** accepted as a fixed-array
length (`[pkg.CAP]int`), because an array length is resolved while parsing and
the imported package may not be parsed yet — an unqualified `const` still works
there. A `const` initializer is also literal-only: it folds literals and
backward **unqualified** constants, not `pkg.NAME`; use the qualified constant
at the use site or a local literal.

### 28.4 Import resolution

An import `PATH` resolves to a package directory as follows:

- **`core:` collection.** If `PATH` begins with `core:`, it resolves to
  `<corelib_root>/<rest>` and binds the name `<rest>`'s final component — e.g.
  `import "core:strings"` resolves to `<corelib_root>/strings` and binds
  `strings`; a nested `core:a/b` resolves to `<corelib_root>/a/b` and binds
  `b`.
- **Relative imports.** Any other `PATH` resolves relative to the **importing
  package's own directory**, independent of the current working
  directory or the entry file's location.

The **corelib root** is determined in order: the environment variable `TYCHO_CORELIB` if set and
non-empty; otherwise `<exe_dir>/corelib` (next to the compiler binary, so an
in-tree build needs no configuration); otherwise `<exe_dir>/../share/tycho/corelib`
(the installed layout); otherwise resolution fails with a diagnostic. `core:`
is the only collection root.

### 28.5 The multi-file merge

Package mode assembles the program by a **post-order depth-first traversal** of
the import graph, merging every reachable package's definitions into one AST.
For each package directory the
implementation MUST:

1. **Enter with cycle and depth checks.** If the directory is already on the
   active DFS path, it is an **import cycle** and MUST be rejected; if it was
   already merged (a shared dependency), it is skipped; the active path is
   **capped at 64** and deeper nesting MUST be rejected.
2. **Scan its `.ty` files in sorted order.** All `*.ty` files in the directory
   are collected and sorted by name, so the merge order is deterministic and
   independent of directory-listing order. A directory with no `.ty` files is an
   error.
3. **Load imports first (post-order).** Every import found in the package's file
   headers is resolved (§28.4) and merged **before** this package's own
   definitions, so a definition's dependencies are already registered when it is
   resolved.
4. **Require the declared package name.** Every file in the directory MUST carry
   a `package` declaration, and it MUST equal the package name expected for this
   directory (§28.1); a missing declaration or a mismatch MUST be rejected.
5. **Append into the one shared AST**, mangling each package's names by its
   `P__` prefix (§28.3) into the single flat namespace.

The resulting program is then finiteness-checked, resolved, and code-generated
as a whole ([§5](03-types.md), [§6](04-inference.md)).

### 28.6 Package foreign dependencies (`deps` and shims)

A package MAY carry C-backed machinery that is built and linked automatically
when the package is imported:

- **Companion shim.** A co-located `<pkg>/<pkg>_shim.c` is auto-added to the
  link line as a compiled companion source, so a C-backed corelib package (e.g. `core:regex` over `<regex.h>`) is turnkey with
  no manual `--shim`. One shim per package; it is deduplicated.
- **`deps` file (pkg-config).** The package's sibling
  `<pkg>/deps` file is read: each line names a **pkg-config** package (blank
  lines and lines beginning with `#` are ignored). Lines after a `_WIN32:` line
  are raw linker flags for a Windows build (e.g. `-lws2_32`) and are skipped on
  every other target. For each name the
  implementation runs `pkg-config --cflags --libs <name>` and splices the result
  onto the C compiler line (§27.4), so a shim that `#include`s a system header
  builds against the right flags. A `deps` name that pkg-config cannot resolve prints a diagnostic; the missing library
  then surfaces as a link error at the `cc` stage. A `deps` file is read
  **whether or not the package has a shim**: `core:sqlite` has none — it binds
  libsqlite3 through `extern "sqlite3"` — and its dependency is reported and
  linked on the same terms as a shim-backed one.

`deps`-backed packages are the boundary between the two conformance tiers
([§1.3](00-conventions.md#13-conformance)). They belong to the **extended
tier**: a conforming implementation **MAY** omit them and still conform at the
**core tier**, but a program that imports an absent extended package **MUST** be
diagnosed rather than silently mis-linked. The reference test harness skips
(rather than fails) a package's tests when its external library is unavailable
(`corelib/run.sh`).
