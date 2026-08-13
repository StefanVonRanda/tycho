# 27. Program structure · 28. Packages and modules

A Tycho **program** is the whole set of definitions reachable from a single
entry point, gathered across one or more **packages** and compiled as one unit.
This chapter defines the program's shape — its entry point, its compilation
unit, its foreign `extern` declarations, and how it is linked into an
executable ([§27](#27-program-structure)) — and then the package system that
supplies its definitions: package identity, `import`, name resolution and
visibility, import resolution, the multi-file merge, and per-package foreign
dependencies ([§28](#28-packages-and-modules)).

The type-level meaning of the declarations a program contains is given in
[§5](03-types.md); their scoping within a package is [§12](08-declarations.md);
the FFI contract an `extern` declaration participates in — which types may cross
the C boundary, and how — is [§24](14-ffi.md). This chapter
specifies only *program- and package-level* structure and does not restate
those rules.

> Provenance: entry point `src/tychoc.c:9092@no 'main' procedure` (no `main`),
> `:9123@takes no parameters` and `:9126@returns nothing or` (the two signature
> rules, diagnosed separately); compilation unit
> `compile_package` `:14104-14109@compile_package`, driver `:14222-14326@int main(`;
> `extern` `parse_extern_fn` `:4511-4589@parse_extern_fn`; the C compiler
> invocation `:14446@system(cmd)`.

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

The message is written **bare**, with no implementation-supplied prefix — the
same treatment `die(msg)` gives a user message ([§29](16-builtins.md)), because
it is the program's error and not a runtime fault.

An implementation MUST reject a program that defines no `main` (the reference
diagnoses `no 'main' procedure`, `src/tychoc.c:9092@no 'main' procedure`), a `main`
that declares any parameter, and a `main` whose return type is neither of the two
shapes above. The reference enforces the last two separately, so each diagnostic
names the fault it found; `compiler/tychoc0.ty` is frozen and predates the
`Result` shape.

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
`args()[1]` onward (`runtime/tycho_rt.c@tycho_args`, wired from the generated
`main` at `src/tychoc.c:13866@tycho_argc`). `args()` is never empty for a normally-launched
program.

### 27.2 The compilation unit

A Tycho program is compiled as **one whole-program unit**. The compiler reads
the entry file, follows every `import` transitively, merges all reachable
definitions into a **single** AST, and emits a **single** C translation unit,
which is compiled and linked into a **single** executable. There is **no**
separate compilation, **no** per-package object file, and **no** linker step
that joins independently-compiled packages (`src/tychoc.c@compile_package`; [packages.md](../guides/packages.md) §"How it builds").

A conforming implementation is not required to transpile to C; the single-unit
merge, however, is observable and normative — it determines name resolution and
mangling (§28.3), cross-package type identity ([§5.1](03-types.md#51-the-type-identity-model)),
and the fact that a definition's behavior does not depend on which package it
came from.

Whether a source file participates in the package system is decided by the
presence of a `package` declaration. The entry file is compiled in **package
mode** (the whole directory plus its import graph, §28.5) iff it begins with a
`package` declaration; a file with no `package` declaration is a **single-file
program** compiled alone (`src/tychoc.c:14360@compile_package`). A single-file program has
the same entry-point rule (§27.1) and is a degenerate one-package unit.

### 27.3 `extern` declarations

An `extern` declaration introduces a procedure that is **bodyless** and bound to
a foreign **C symbol** rather than to Tycho code:

```text
extern [ "Lib" ] fn name(p: T, …) [ -> T ]
```

The following rules are normative (`src/tychoc.c@parse_extern_fn`):

- An `extern` declaration has **no body**: it ends at the newline after its
  signature (`src/tychoc.c:4657@an extern fn has no body`), so a following indented
  block is a parse error.
- Its `name` is the **literal C symbol** and is **not** package-mangled (§28.3),
  because a C symbol is global (`src/tychoc.c:4599@never mangled`). Two `extern` declarations naming the
  same C symbol therefore refer to the same foreign function regardless of the
  Tycho packages they appear in.
- An `extern` procedure is invoked **without** the implicit arena argument that
  every Tycho procedure receives ([§10](07-memory-model.md)); it
  calls the C symbol directly.
- The optional leading string literal names a **link library** (`extern "Lib"
  fn …`), which the implementation adds to the link line as `-lLib` (§27.4).

The set of types an `extern` parameter or return may name — including the
fixed-width integer types (`u8`, `u16`, `i8`, `i16`, `i32`, `i64`),
`inout` out-parameters, and the `Option(string)` nullable return — is defined by
the FFI chapter ([§24](14-ffi.md)), which is the single normative
home for the crossable-type rules. This section governs only the *declaration
form* and its role in program structure.

### 27.4 Linking and the C compiler invocation

For a program that is not stopped early (e.g. `--emit-c`, `--symbols`), the
reference implementation compiles the emitted C and links it into the output
executable with a single C-compiler invocation (`src/tychoc.c:14445@-fwrapv`,
run at `src/tychoc.c:14446@system(cmd)`).
The invocation has the shape:

```text
cc {-O3 | -O0 -g} -fwrapv [-march=native] -pthread -o OUT gen.c \
   <auto shims> -lm <-lLib per extern lib> \
   <-L/-I/--link/--pkg passthrough> <pkg-config flags from deps>
```

with these normative properties:

- **`-fwrapv` is required semantics, not an optimization flag.** It makes signed
  integer overflow **defined** as two's-complement wrapping rather than C
  undefined behavior, which is precisely Tycho's integer-overflow contract
  ([§5.2.1](03-types.md#521-int)): a conforming realization on C MUST compile
  such that signed overflow wraps and never traps or miscompiles
  (`src/tychoc.c:14445@-fwrapv`).
- **`-lm` is always passed**, so bare libc math externs (e.g. `extern fn sqrt`)
  link with no `"m"` annotation (`src/tychoc.c:5206@-lm is always passed`).
- **`-pthread` is always passed**, supporting the concurrency runtime
  ([§20](13-concurrency.md)).
- **Optimization / debug:** `-O3` is the portable default; `-g` selects `-O0 -g`
  (unoptimized with debug info) instead (`src/tychoc.c:14442@optdbg`).
- **`-march=native` is opt-in** via `--native`. It is host-CPU-specific and MUST
  NOT be assumed portable across machines (`src/tychoc.c:14441@march`).
- The default C compiler is `cc` (`src/tychoc.c:14283@"cc"`); `--cc <compiler>`
  overrides it (`src/tychoc.c:14304@--cc`).

Three CLI options let a program splice additional flags onto this line for FFI
(`src/tychoc.c:14311-14313`):

- **`--link <lib>`** appends a raw `-l<lib>`.
- **`--pkg <name>`** runs `pkg-config --cflags --libs <name>` and appends the
  result.
- **`--shim <file.c>`** adds a companion C source that is compiled and linked
  alongside the generated C.

A build that stops early at `--emit-c` and links the generated C itself needs the
same auto-discovered shim set that §27.4's line would have received. **`--print-shims`**
prints it: the *transitive* closure of companion `<pkg>_shim.c` sources (§28.6),
one absolute path per line on stdout, then exits without code generation. The
closure is not recomputed for the flag — it is the `g_shims` set that `merge_pkg`
fills as it walks the import graph, so a shim reached only through an indirect
import is listed on the same terms as a directly imported one. A program with no
package prints nothing and exits 0; an empty closure is an answer, not an error.

**`--print-deps`** reads the same walk out the other way: the *transitive* closure
of pkg-config package **names** declared by each package's `deps` file (§28.6),
one per line on stdout, then exits without code generation. It answers a different
question from `--print-shims`, for a caller that does not link the C itself — a
harness that lets the reference compiler build, and only needs to know whether the
link line *can* resolve on this host, so that a missing library is a skip rather
than a failure. The names are recorded as the walk enters each package and
**before** resolution is attempted, so they are printed whether or not pkg-config
can resolve them here; the resolved `--cflags --libs` form that reaches the cc line
is empty on exactly the host where the library is absent, which makes it
indistinguishable from a package that declared no dependencies at all. Duplicates
are collapsed. A program whose closure declares no `deps` prints nothing and exits
0.

`-L<dir>` and `-I<dir>` (attached or separated) also accumulate onto the line
(`src/tychoc.c:14307@-L`). Every library/package name that reaches the shell — from
`extern "Lib"`, `--link`, or `--pkg` — MUST be restricted to a conservative
character set (`[A-Za-z0-9._+-]`) and rejected otherwise, so compiling an
untrusted source cannot inject a shell command (`src/tychoc.c@cc_safe_name`). The `extern "Lib"` libraries, the auto-discovered
shims, and the `deps` flags are contributed automatically by the sources they
belong to (§27.3, §28.6); the CLI options above are the manual escape hatch.

The exact command line — flag spelling and order, the choice of `cc`, and the
`--native`/`-g` couplings — belongs to the reference implementation and is outside
the language proper ([§1.1](00-conventions.md#11-scope) excludes the CLI and the
generated-C layout). The **normative** content of this section is narrow:
`-fwrapv` two's-complement wrapping is the language's integer contract, and a
program that names an external link library (via `extern "Lib"`, a `deps` entry,
or `--link`/`--pkg`) depends on that library being present at link time. The
precise realization is implementation-defined
([Appendix F](appendix-f-impl-defined.md)).

## 28. Packages and modules

### 28.1 Packages and namespaces

A **package** is a *directory* of `.ty` files that all declare the same
`package NAME` and share **one flat namespace**. Every top-level definition in
any file of the directory — function, `struct`, `enum`, `handle`, newtype,
`const`, and enum variant — belongs to that one namespace; the split into files
is not semantically significant ([packages.md](../guides/packages.md) §"Surface
syntax"; merge at `src/tychoc.c@merge_pkg`).

A package's symbols are reached from another package by a **qualified name**
`pkg.symbol`, where `pkg` is the importing binding (§28.2). The qualified form
applies to types, functions, struct/enum constructors, and enum variants alike
— `geom.Point`, `geom.add`, `geom.Red` (`src/tychoc.c:6053@payload-less` handles a
payload-less `pkg.Variant`).

Every non-entry package's `package NAME` MUST equal the final component of the
import path that reached it — which, for a relative import, is its directory
name (`src/tychoc.c:14086@declares`, checked against `src/tychoc.c@pkg_basename` of
the import path). The **entry package may be named anything**: its name is taken from the
entry file's own `package` declaration and is not constrained to the directory
name (`src/tychoc.c@detect_package`, called at `src/tychoc.c:14359@detect_package`;
`compile_package` starts the merge at that name with an empty prefix,
`src/tychoc.c:14111@merge_pkg`).

### 28.2 Import declarations

An import is a top-level declaration in a package's file:

```tycho
import "geom"          # binds the package's own name  ->  geom.symbol
import g "geom"         # aliases the prefix            ->  g.symbol
```

The grammar is `"import" IDENT? STRING NEWLINE` (`src/tychoc.c@parse_import_decl`). The plain form `import "PATH"` binds the package
under the **final component** of `PATH` (`import "math/geom"` binds `geom`); the
aliased form `import ALIAS "PATH"` binds the package under `ALIAS` instead
(`src/tychoc.c@pkg_prefix_for`, `src/tychoc.c@is_imported_pkg`).

`package` and `import` are **contextual**: they are special only as the leading
identifier of a top-level item and remain ordinary identifiers elsewhere, so
they are not reserved words (`src/tychoc.c:5168-5169`). A bound package name,
however, is effectively reserved within the file: because a bare `pkg.symbol`
is disambiguated by treating `pkg` as an imported package first, a program
SHOULD NOT shadow an imported binding with a local of the same name
([packages.md](../guides/packages.md) §"Surface syntax"). **Import cycles are an
error** (§28.5).

### 28.3 Name resolution and visibility

Within the single merged unit (§27.2), every definition carries a
**per-package prefix**. The entry package uses the **empty** prefix, so its
symbols keep their plain names; every imported package `P` uses the prefix
`P__`, applied uniformly to every definition and every reference — including the
generated type families for its arrays, tuples, maps, and helper functions
(`src/tychoc.c@pkg_mangle`; `g_cur_pkg_prefix` set per package at
`src/tychoc.c:14079@g_cur_pkg_prefix`, reset at `src/tychoc.c:14100@g_cur_pkg_prefix`; [packages.md](../guides/packages.md) §"How it builds").
Two identically-named symbols in different packages are therefore distinct after
mangling. `extern` C symbols are the sole exception and are never prefixed
(§27.3).

**Visibility is by a leading underscore.** A top-level symbol whose name begins
with `_` is **private to its own package**: it is usable from any file *within*
that package, but a qualified `pkg._name` from another package MUST be rejected
with a diagnostic (`src/tychoc.c@check_pkg_private`; enforced at the
qualified-reference sites `src/tychoc.c:2535@check_pkg_private`,
`src/tychoc.c:3182@check_pkg_private`, `src/tychoc.c:6056@check_pkg_private`,
`src/tychoc.c:6371@check_pkg_private`). Every other top-level
symbol is exported and visible to importers. There is no visibility keyword;
the underscore convention is the whole of the rule.

That includes **top-level `const`s**: `pkg.NAME` reads an exported constant and
is **folded to its literal at the use site**, exactly as an unqualified const
is, so no runtime global is emitted and the reference costs nothing. A
`pkg._NAME` is refused by the same underscore rule. A qualified const is
accepted wherever an expression is; it is **not** accepted as a fixed-array
length (`[pkg.CAP]int`), because an array length is resolved while parsing and
the imported package may not be parsed yet — an unqualified `const` still works
there.

### 28.4 Import resolution

An import `PATH` resolves to a package directory as follows (`src/tychoc.c@resolve_pkg_dir`):

- **`core:` collection.** If `PATH` begins with `core:`, it resolves to
  `<corelib_root>/<rest>` and binds the name `<rest>`'s final component — e.g.
  `import "core:strings"` resolves to `<corelib_root>/strings` and binds
  `strings`; `import "core:text/utf8"` resolves to `<corelib_root>/text/utf8`
  and binds `utf8` (`src/tychoc.c:5010@core:`, `src/tychoc.c@pkg_basename`).
- **Relative imports.** Any other `PATH` resolves relative to the **importing
  package's own directory** (`src/tychoc.c:5023@importer_dir`), independent of the current working
  directory or the entry file's location.

The **corelib root** is determined in order (`src/tychoc.c@corelib_root`): the environment variable `TYCHO_CORELIB` if set and
non-empty; otherwise `<exe_dir>/corelib` (next to the compiler binary, so an
in-tree build needs no configuration); otherwise `<exe_dir>/../share/tycho/corelib`
(the installed layout); otherwise resolution fails with a diagnostic
(`src/tychoc.c:5015@cannot find the corelib`). `core:` is currently the only collection root
([packages.md](../guides/packages.md) §"The `core:` collection").

### 28.5 The multi-file merge

Package mode assembles the program by a **post-order depth-first traversal** of
the import graph, merging every reachable package's definitions into one AST
(`src/tychoc.c@merge_pkg`). For each package directory the
implementation MUST:

1. **Enter with cycle and depth checks.** If the directory is already on the
   active DFS path, it is an **import cycle** and MUST be rejected; if it was
   already merged (a shared dependency), it is skipped; the active path is
   **capped at 64** and deeper nesting MUST be rejected
   (`src/tychoc.c@pkg_walk_enter`).
2. **Scan its `.ty` files in sorted order.** All `*.ty` files in the directory
   are collected and sorted by name, so the merge order is deterministic and
   independent of directory-listing order (`src/tychoc.c@scan_pkg_files`; the
   sort at `src/tychoc.c:14007@qsort`). A directory with no `.ty` files is an
   error (`src/tychoc.c:14006@has no .ty files`).
3. **Load imports first (post-order).** Every import found in the package's file
   headers is resolved (§28.4) and merged **before** this package's own
   definitions, so a definition's dependencies are already registered when it is
   parsed (`src/tychoc.c:14064@load imported packages first`).
4. **Require the declared package name.** Every file in the directory MUST carry
   a `package` declaration, and it MUST equal the package name expected for this
   directory (§28.1); a missing declaration or a mismatch MUST be rejected
   (`src/tychoc.c:14082@but has no`).
5. **Append into the one shared AST**, mangling each package's names by its
   `P__` prefix (§28.3) into the single flat namespace (`src/tychoc.c:14096@prog->v`).

The resulting program is then finiteness-checked, resolved, and code-generated
as a whole ([§5](03-types.md), [§6](04-inference.md);
`src/tychoc.c@check_finite_types`, `src/tychoc.c@resolve_program`,
`src/tychoc.c@gen_program`).

### 28.6 Package foreign dependencies (`deps` and shims)

A package MAY carry C-backed machinery that is built and linked automatically
when the package is imported (`src/tychoc.c:14043@add_pkg_deps`):

- **Companion shim.** A co-located `<pkg>/<pkg>_shim.c` is auto-added to the
  link line as a compiled companion source (`src/tychoc.c@add_shim`), so a
  C-backed corelib package (e.g. `core:regex` over `<regex.h>`) is turnkey with
  no manual `--shim`. One shim per package; it is deduplicated.
- **`deps` file (pkg-config).** When a shim is present, the package's sibling
  `<pkg>/deps` file is read: each line names a **pkg-config** package (blank
  lines and lines beginning with `#` are ignored); for each name the
  implementation runs `pkg-config --cflags --libs <name>` and splices the result
  onto the C compiler line (§27.4), so a shim that `#include`s a system header
  builds against the right flags (`src/tychoc.c@add_pkg_deps`). A `deps` name
  that pkg-config cannot resolve prints a diagnostic
  (`src/tychoc.c:5285@could not resolve dependency`); the missing library
  then surfaces as a link error at the `cc` stage. Note that
  `deps` is consulted **only alongside a shim** (`src/tychoc.c:14043@file_exists`); a `deps` file with
  no companion shim contributes nothing.

`deps`-backed packages are the boundary between the two conformance tiers
([§1.3](00-conventions.md#13-conformance)). They belong to the **extended
tier**: a conforming implementation **MAY** omit them and still conform at the
**core tier**, but a program that imports an absent extended package **MUST** be
diagnosed rather than silently mis-linked. The reference test harness embodies
this split — it probes the same `deps` and **skips** (rather than fails) a
package's tests when the external library is unavailable (`corelib/run.sh`, per
the note at `src/tychoc.c:5234@SKIPS`).
