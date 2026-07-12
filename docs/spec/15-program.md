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
the C boundary, and how — is [§24](14-ffi.md) (forthcoming). This chapter
specifies only *program- and package-level* structure and does not restate
those rules.

> Provenance: entry point `src/tychoc.c:6354-6355`, `:6379-6380`; compilation
> unit `compile_package` `:10355-10360`, driver `:10566-10570`; `extern`
> `parse_extern_fn` `:3212-3282`; the C compiler invocation `:10592-10606`.

## 27. Program structure

### 27.1 The entry point

A program MUST define exactly one procedure named `main`. It MUST take no
parameters and MUST return `void`; the declaration form is `fn main():`. An
implementation MUST reject a program that defines no `main` (the reference
diagnoses `no 'main' procedure`) and MUST reject a `main` that declares any
parameter or a non-`void` return type (`src/tychoc.c:6354-6355`,
`:6379-6380`).

`main` is the sole entry point: execution begins by calling it and the program
terminates when it returns (the runtime allocates the program's root arena
around this call — [§10](07-memory-model.md), forthcoming). `main` is an
ordinary procedure in every other respect; it MAY be called recursively and MAY
appear in any file of the entry package.

The program's command-line arguments are obtained through the builtin `args()`,
which returns a `[string]` ([§29](16-builtins.md), forthcoming). The array
mirrors the process `argv` exactly, so `args()[0]` is the **program name** (the
path by which the executable was invoked) and the operands follow at
`args()[1]` onward (`runtime/tycho_rt.c:1351-1356`, wired from the generated
`main` at `src/tychoc.c:10162`). `args()` is never empty for a normally-launched
program.

### 27.2 The compilation unit

A Tycho program is compiled as **one whole-program unit**. The compiler reads
the entry file, follows every `import` transitively, merges all reachable
definitions into a **single** AST, and emits a **single** C translation unit,
which is compiled and linked into a **single** executable. There is **no**
separate compilation, **no** per-package object file, and **no** linker step
that joins independently-compiled packages (`compile_package`
`src/tychoc.c:10355-10360`; [packages.md](../packages.md) §"How it builds").

A conforming implementation is not required to transpile to C; the single-unit
merge, however, is observable and normative — it determines name resolution and
mangling (§28.3), cross-package type identity ([§5.1](03-types.md#51-the-type-identity-model)),
and the fact that a definition's behavior does not depend on which package it
came from.

Whether a source file participates in the package system is decided by the
presence of a `package` declaration. The entry file is compiled in **package
mode** (the whole directory plus its import graph, §28.5) iff it begins with a
`package` declaration; a file with no `package` declaration is a **single-file
program** compiled alone (`src/tychoc.c:10566-10570`). A single-file program has
the same entry-point rule (§27.1) and is a degenerate one-package unit.

### 27.3 `extern` declarations

An `extern` declaration introduces a procedure that is **bodyless** and bound to
a foreign **C symbol** rather than to Tycho code:

```
extern [ "Lib" ] fn name(p: T, …) [ -> T ]
```

The following rules are normative (`parse_extern_fn`
`src/tychoc.c:3212-3282`):

- An `extern` declaration has **no body**: it ends at the newline after its
  signature (`:3280`), so a following indented block is a parse error.
- Its `name` is the **literal C symbol** and is **not** package-mangled (§28.3),
  because a C symbol is global (`:3222`). Two `extern` declarations naming the
  same C symbol therefore refer to the same foreign function regardless of the
  Tycho packages they appear in.
- An `extern` procedure is invoked **without** the implicit arena argument that
  every Tycho procedure receives ([§10](07-memory-model.md), forthcoming); it
  calls the C symbol directly.
- The optional leading string literal names a **link library** (`extern "Lib"
  fn …`), which the implementation adds to the link line as `-lLib` (§27.4).

The set of types an `extern` parameter or return may name — including the
FFI-only sized-integer spellings (`u8`, `u16`, `i8`, `i16`, `i32`, `i64`),
`inout` out-parameters, and the `Option(string)` nullable return — is defined by
the FFI chapter ([§24](14-ffi.md), forthcoming), which is the single normative
home for the crossable-type rules. This section governs only the *declaration
form* and its role in program structure.

### 27.4 Linking and the C compiler invocation

For a program that is not stopped early (e.g. `--emit-c`, `--symbols`), the
reference implementation compiles the emitted C and links it into the output
executable with a single C-compiler invocation (`src/tychoc.c:10592-10606`).
The invocation has the shape:

```
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
  (`src/tychoc.c:10597-10599`).
- **`-lm` is always passed**, so bare libc math externs (e.g. `extern fn sqrt`)
  link with no `"m"` annotation (`:10604`, `:3728`).
- **`-pthread` is always passed**, supporting the concurrency runtime
  ([§20](13-concurrency.md), forthcoming).
- **Optimization / debug:** `-O3` is the portable default; `-g` selects `-O0 -g`
  (unoptimized with debug info) instead (`:10601`).
- **`-march=native` is opt-in** via `--native`. It is host-CPU-specific and MUST
  NOT be assumed portable across machines (`:10600`).
- The default C compiler is `cc`; `--cc <compiler>` overrides it (`:10518`,
  `:10534`).

Three CLI options let a program splice additional flags onto this line for FFI
(`src/tychoc.c:10541-10547`):

- **`--link <lib>`** appends a raw `-l<lib>`.
- **`--pkg <name>`** runs `pkg-config --cflags --libs <name>` and appends the
  result.
- **`--shim <file.c>`** adds a companion C source that is compiled and linked
  alongside the generated C.

`-L<dir>` and `-I<dir>` (attached or separated) also accumulate onto the line
(`:10537-10540`). Every library/package name that reaches the shell — from
`extern "Lib"`, `--link`, or `--pkg` — MUST be restricted to a conservative
character set (`[A-Za-z0-9._+-]`) and rejected otherwise, so compiling an
untrusted source cannot inject a shell command (`cc_safe_name`
`src/tychoc.c:10415-10424`). The `extern "Lib"` libraries, the auto-discovered
shims, and the `deps` flags are contributed automatically by the sources they
belong to (§27.3, §28.6); the CLI options above are the manual escape hatch.

> Editor's note: the exact command line — flag spelling and order, the choice of
> `cc`, and the `--native`/`-g` couplings — is the reference implementation's
> and is expressly outside the language proper
> ([§1.1](00-conventions.md#11-scope) excludes the CLI and generated-C layout).
> The **normative** content of this section is narrow: `-fwrapv` two's-complement
> wrapping is the language's integer contract, and a program that names an
> external link library (via `extern "Lib"`, a `deps` entry, or `--link`/`--pkg`)
> depends on that library being present at link time. The precise realization is
> implementation-defined ([Appendix F](appendix-f-impl-defined.md)).

## 28. Packages and modules

### 28.1 Packages and namespaces

A **package** is a *directory* of `.ty` files that all declare the same
`package NAME` and share **one flat namespace**. Every top-level definition in
any file of the directory — function, `struct`, `enum`, `handle`, newtype,
`const`, and enum variant — belongs to that one namespace; the split into files
is not semantically significant ([packages.md](../packages.md) §"Surface
syntax"; merge at `src/tychoc.c:10328-10347`).

A package's symbols are reached from another package by a **qualified name**
`pkg.symbol`, where `pkg` is the importing binding (§28.2). The qualified form
applies to types, functions, struct/enum constructors, and enum variants alike
— `geom.Point`, `geom.add`, `geom.Red` (`src/tychoc.c:4319-4331` handles a
payload-less `pkg.Variant`).

Every non-entry package's `package NAME` MUST equal the final component of the
import path that reached it — which, for a relative import, is its directory
name (`src/tychoc.c:10336-10339`, checked against `pkg_basename` of the import
path). The **entry package may be named anything**: its name is taken from the
entry file's own `package` declaration and is not constrained to the directory
name (`detect_package` `:10197`, `:10568`; `compile_package` starts the merge at
that name with an empty prefix, `:10355-10358`).

### 28.2 Import declarations

An import is a top-level declaration in a package's file:

```
import "geom"          # binds the package's own name  ->  geom.symbol
import g "geom"         # aliases the prefix            ->  g.symbol
```

The grammar is `"import" IDENT? STRING NEWLINE` (`parse_import_decl`
`src/tychoc.c:3466-3478`). The plain form `import "PATH"` binds the package
under the **final component** of `PATH` (`import "math/geom"` binds `geom`); the
aliased form `import ALIAS "PATH"` binds the package under `ALIAS` instead
(`pkg_prefix_for` `:3559-3568`, `is_imported_pkg` `:3572-3578`).

`package` and `import` are **contextual**: they are special only as the leading
identifier of a top-level item and remain ordinary identifiers elsewhere, so
they are not reserved words (`src/tychoc.c:3448-3452`). A bound package name,
however, is effectively reserved within the file: because a bare `pkg.symbol`
is disambiguated by treating `pkg` as an imported package first, a program
SHOULD NOT shadow an imported binding with a local of the same name
([packages.md](../packages.md) §"Surface syntax"). **Import cycles are an
error** (§28.5).

### 28.3 Name resolution and visibility

Within the single merged unit (§27.2), every definition carries a
**per-package prefix**. The entry package uses the **empty** prefix, so its
symbols keep their plain names; every imported package `P` uses the prefix
`P__`, applied uniformly to every definition and every reference — including the
generated type families for its arrays, tuples, maps, and helper functions
(`pkg_mangle` `src/tychoc.c:1343-1345`; `g_cur_pkg_prefix` set per file at
`:10330`, reset at `:10348`; [packages.md](../packages.md) §"How it builds").
Two identically-named symbols in different packages are therefore distinct after
mangling. `extern` C symbols are the sole exception and are never prefixed
(§27.3).

**Visibility is by a leading underscore.** A top-level symbol whose name begins
with `_` is **private to its own package**: it is usable from any file *within*
that package, but a qualified `pkg._name` from another package MUST be rejected
with a diagnostic (`check_pkg_private` `src/tychoc.c:3580-3587`; enforced at the
qualified-reference sites `:1724`, `:2427`, `:4323`, `:4562`). Every other top-level
symbol is exported and visible to importers. There is no visibility keyword;
the underscore convention is the whole of the rule.

> Editor's note (reference-doc conflict): the reference page
> [reference/packages.md](../reference/packages.md) states "There's no privacy:
> everything in a package is visible to its importers." That sentence is
> **stale** — it contradicts both the reference transpiler
> (`check_pkg_private`, `src/tychoc.c:3580-3587`, which *rejects* a
> cross-package `pkg._name`) and the longer design note
> [docs/packages.md](../packages.md) (its "Privacy is by leading underscore"
> rule), which documents leading-underscore privacy. This specification follows
> the source and the design note: a
> leading-underscore top-level name is package-private. The reference page is to
> be corrected; the divergence is logged in
> [Appendix H](appendix-h-differences.md). The precise surface still to pin: the
> rule keys on the **name text at the qualified reference**, so it governs
> qualified access from another package and does not add any in-package
> restriction.

### 28.4 Import resolution

An import `PATH` resolves to a package directory as follows (`resolve_pkg_dir`
`src/tychoc.c:3542-3557`):

- **`core:` collection.** If `PATH` begins with `core:`, it resolves to
  `<corelib_root>/<rest>` and binds the name `<rest>`'s final component — e.g.
  `import "core:strings"` resolves to `<corelib_root>/strings` and binds
  `strings`; `import "core:text/utf8"` resolves to `<corelib_root>/text/utf8`
  and binds `utf8` (`:3543-3554`, `pkg_basename` `:3489-3493`).
- **Relative imports.** Any other `PATH` resolves relative to the **importing
  package's own directory** (`:3556`), independent of the current working
  directory or the entry file's location.

The **corelib root** is determined in order (`corelib_root`
`src/tychoc.c:3525-3540`): the environment variable `TYCHO_CORELIB` if set and
non-empty; otherwise `<exe_dir>/corelib` (next to the compiler binary, so an
in-tree build needs no configuration); otherwise `<exe_dir>/../share/tycho/corelib`
(the installed layout); otherwise resolution fails with a diagnostic
(`:3544-3553`). `core:` is currently the only collection root
([packages.md](../packages.md) §"The `core:` collection").

### 28.5 The multi-file merge

Package mode assembles the program by a **post-order depth-first traversal** of
the import graph, merging every reachable package's definitions into one AST
(`merge_pkg` `src/tychoc.c:10296-10351`). For each package directory the
implementation MUST:

1. **Enter with cycle and depth checks.** If the directory is already on the
   active DFS path, it is an **import cycle** and MUST be rejected; if it was
   already merged (a shared dependency), it is skipped; the active path is
   **capped at 64** and deeper nesting MUST be rejected (`pkg_walk_enter`
   `:10276-10289`).
2. **Scan its `.ty` files in sorted order.** All `*.ty` files in the directory
   are collected and sorted by name, so the merge order is deterministic and
   independent of directory-listing order (`scan_pkg_files` `:10252-10269`; the
   sort at `:10267`). A directory with no `.ty` files is an error (`:10266`).
3. **Load imports first (post-order).** Every import found in the package's file
   headers is resolved (§28.4) and merged **before** this package's own
   definitions, so a definition's dependencies are already registered when it is
   parsed (`:10308-10325`).
4. **Require the declared package name.** Every file in the directory MUST carry
   a `package` declaration, and it MUST equal the package name expected for this
   directory (§28.1); a missing declaration or a mismatch MUST be rejected
   (`:10331-10339`).
5. **Append into the one shared AST**, mangling each package's names by its
   `P__` prefix (§28.3) into the single flat namespace (`:10328-10347`).

The resulting program is then finiteness-checked, resolved, and code-generated
as a whole ([§5](03-types.md), [§6](04-inference.md); driver
`src/tychoc.c:10577-10584`).

### 28.6 Package foreign dependencies (`deps` and shims)

A package MAY carry C-backed machinery that is built and linked automatically
when the package is imported (`merge_pkg` `src/tychoc.c:10300-10303`):

- **Companion shim.** A co-located `<pkg>/<pkg>_shim.c` is auto-added to the
  link line as a compiled companion source (`add_shim` `:3744-3748`), so a
  C-backed corelib package (e.g. `core:regex` over `<regex.h>`) is turnkey with
  no manual `--shim`. One shim per package; it is deduplicated.
- **`deps` file (pkg-config).** When a shim is present, the package's sibling
  `<pkg>/deps` file is read: each line names a **pkg-config** package (blank
  lines and lines beginning with `#` are ignored); for each name the
  implementation runs `pkg-config --cflags --libs <name>` and splices the result
  onto the C compiler line (§27.4), so a shim that `#include`s a system header
  builds against the right flags (`add_pkg_deps` `:3759-3775`). A `deps` name
  that pkg-config cannot resolve prints a diagnostic (`:3772`); the missing library
  then surfaces as a link error at the `cc` stage. Note that
  `deps` is consulted **only alongside a shim** (`:10303`); a `deps` file with
  no companion shim contributes nothing.

`deps`-backed packages are the boundary between the two conformance tiers
([§1.3](00-conventions.md#13-conformance)). They belong to the **extended
tier**: a conforming implementation **MAY** omit them and still conform at the
**core tier**, but a program that imports an absent extended package **MUST** be
diagnosed rather than silently mis-linked. The reference test harness embodies
this split — it probes the same `deps` and **skips** (rather than fails) a
package's tests when the external library is unavailable (`corelib/run.sh`, per
the note at `src/tychoc.c:3754-3756`).
