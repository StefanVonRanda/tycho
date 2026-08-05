# What comes next

> 2026-08-04: the three-phase optimization chain (housekeeping, deterministic
> hash, map memory) is complete and the closing `make ci` is green. New
> owner-directed agenda for making Tycho day-to-day usable, in this order:
> (1) a batteries-included corelib, (2) a vendor-based dependency story
> (Go/Odin style — vendor your dependencies), (3) a debugger, (4) 1.0
> promotion. Explicitly NOT on the agenda: a REPL (the owner never reaches for
> one) and native Windows (deferred, WSL suffices). The corelib demand-gate is
> waived for Phase 1 by the owner's explicit batteries-included instruction;
> every package still ships with its test + golden + corelib guide entry per
> the established convention.

## Phase 1 — Batteries-included corelib

Sub-phases, each verified and committed on its own, in this order. Probed
against a Go/Odin stdlib baseline: these are the missing pieces real programs
touch every day. The corelib structure convention holds for each: a
`corelib/<name>/<name>.ty` package (plus `<name>_shim.c` when FFI is needed,
with a pkg-config `deps` manifest and the skip-if-absent convention),
`corelib/test/<name>/main.ty` + golden (re-record with
`RECORD=1 sh corelib/run.sh`), and a `docs/guides/corelib.md` entry.

Gate per sub-phase: `make corelib` + `make goldens-check` + doc gates. NOT
`make test` (tests/run.sh never descends into corelib — it cannot redden).
A new shim adds `make shim-check`. Expected per sub-phase: all existing
corelib tests green + the new one; the new golden locked.

### 1.1 `core:testing` — unit-test framework (the daily-dev gap)

No test framework exists: the harness is golden-output-based, and a unit-test
package is what day-to-day development actually uses. Library-first (no new
syntax): an `assert(cond, msg)` / `assert_eq(a, b)` family collecting failures
into a `Test` state (threaded like core:rand's state — no globals), and a
`run()`/`report()` that prints pass/fail per test and exits non-zero on any
failure. Design freedom on the exact shapes (e.g. a `test_case(name, fn())`
registration vs a returned list) — decided in the phase.

### 1.2 `core:toml` — config parsing

The common config format for real programs; nothing exists. Pure Tycho (like
core:json/csv): `toml.parse(s) -> Result(Table, string)` covering tables,
dotted keys, arrays, arrays-of-tables, and the scalar set (string/int/float/
bool/datetime-iso), fail-closed on malformed input, plus the basic
`get`/`keys` accessors. Exact surface mirrors core:json's shape where
sensible.

### 1.3 `core:log` — leveled logging

A leveled logger (debug/info/warn/error), state threaded explicitly (no
globals): `log.init(&st, level, dest)`, `log.info(&st, ...)`, timestamps via
the existing clock, stderr/stdout destinations. Pure Tycho over core:io + the
clock builtin.

### 1.4 `core:sqlite` — SQL database

The dbquery bench proves the FFI pattern; package it. `corelib/sqlite/` with
`sqlite_shim.c` over libsqlite3 (deps manifest names `sqlite3`, pkg-config,
skip-if-absent). Scope: `open`/`close`, `exec` (no results), `query` (SELECT →
rows as `[[string]]` or a typed row struct — decided in the phase), error
reporting (the sqlite error message), parameter binding for the common cases.
A small real fixture (create/insert/select/join) in the test.

### 1.5 `core:utf8` — Unicode validation and iteration

Strings are bytes today; text processing needs a codepoint view. Pure Tycho:
`utf8.valid(s)`, `utf8.decode(s, at) -> (cp, width)` (fail-closed on a
truncated/invalid sequence), `utf8.encode(cp) -> string`, `utf8.len(s)`
(codepoint count), and a rune-wise iteration idiom. Full Unicode tables
(case mapping, normalization) deferred — validation + iteration is the
batteries-included core.

### 1.6 `core:zip` — archive reader/writer

Deployment needs archives; only gzip/zlib exists. Pure Tycho over
core:compress: `zip.list(bytes) -> [Entry]`, `zip.extract(bytes, name) ->
bytes` (fail-closed on a corrupt/truncated archive, no path traversal —
entries are names, not paths), `zip.create(entries) -> bytes`. Reader first,
writer second, inside the same sub-phase.

### 1.7 `core:timezone` (rich datetime) — deferred

The roadmap names "richer date/time"; a timezone database is a large,
data-heavy package. Held until a real program needs it (the one item where
the demand-gate is NOT waived — it is the roadmap's own example of a gated
add).

### 1.8 image codecs (JPEG/GIF) — deferred

core:image is PNG (libpng), core:raster is BMP/QOI. JPEG via libjpeg FFI and
a pure-Tycho GIF are real, but no program in-tree needs them yet; attach to
this phase only if a caller appears.

## Phase 2 — `vendor:` dependency story (Go/Odin style)

Vendor your dependencies — no central registry, no version resolution. The
compiler already resolves `core:X` via `TYCHO_CORELIB` and everything else
relative (`src/tychoc.c:13178`); the new surface:

- **`import "vendor:pkg"`** resolution: a `vendor/` directory convention
  (TYCHO_VENDOR-style env override mirroring TYCHO_CORELIB, defaulting to a
  `vendor/` dir beside the entry program), so a vendored package is imported
  by name and compiled exactly like a corelib package.
- **The fetch tool**: `tycho fetch <url-or-name> <dest>` — download a
  vendorable package tree into `vendor/` (the fetch dogfood exists at
  examples/fetch), with checksum verification.
- **tycho-build integration**: the build tool resolves `vendor:` deps the way
  it resolves file deps, and a `vendor` manifest (a buildfile target or a
  `deps` line — decided in the phase) pins what a project vendors.

Gate: `make test` (compiler import-resolution change) + `make selfhost-check`
(the import path table changes) + `make build-check` (tycho-build integration)
+ doc gates. Citations re-point (the resolution code shifts).

## Phase 3 — Debugger

`-g` already emits `#line` mapping and compiles (verified by tools-check's
line-info leg). The pragmatic route: a **gdb adapter** — the compiler's `-g`
output drives a scripted gdb session (breakpoints by source line, `next`,
locals via the emitted C names), wrapped in a `tycho debug <program>` command
in the dispatcher — rather than a from-scratch debugger. A minimal built-in
(breakpoint/step/locals over the runtime) is the alternative; decided in the
phase. The debugger itself is a Tycho tool (tools/tycho-debug/), dogfooding
core:os + core:signal.

Gate: `make tools-check` (the tool's lane) + `make test` (if any `-g`
emission changes) + `make build-check` if the dispatcher changes + doc gates.

## Phase 4 — 1.0 promotion

The last bit of prep, after 1–3 settle:

- **Versioning**: a version constant in the compiler, `--version`, and the
  changelog discipline (each phase's changes recorded against it).
- **Stability statement**: replace the README's "pre-1.0: no stability
  guarantees" with the 1.0 contract — what is stable (the language surface,
  the spec), what is not (performance tuning, benches).
- **Security audit**: the README names "no security audit" as a pre-1.0
  caveat; close it — a review of the FFI shims (string ownership, the
  shell-out paths in core:os, the TLS wrapper) with the findings recorded.
- **API freeze decision**: which corelib packages are in the 1.0 surface and
  what the deprecation path is.

Gate: the full `make ci` as the closing sweep, plus the doc gates.
Expected: the README's status banner flips from research prototype to 1.0.

## Not in scope

- A REPL (owner decision), native Windows (deferred; WSL is the supported
  path), a package registry (vendoring is the model), Unicode tables beyond
  validation/iteration, and image codecs/timezone until a caller appears.
