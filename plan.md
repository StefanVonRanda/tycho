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

## Phase 2 — Vendored dependencies (Go/Odin style)

Vendor your dependencies — no central registry, no version resolution. The
mechanism is ALREADY COMPLETE and verified end-to-end: `import "PATH"`
resolves relative to the importing package's own directory (cwd-independent),
so `import "vendor/foo"` from the entry and `import "../bar"` from inside a
vendored package compile and run as-is, mixed with `core:` imports;
multi-package merge, name mangling, visibility and cycle detection are all
spec'd and gated. tycho-build needs nothing — its recipes are shell lines; the
compiler resolves the imports. So the phase is documentation + one
convenience command, NOT compiler surface:

- **Document the vendoring convention** in `docs/guides/packages.md`: the
  `vendor/` directory pattern, the importer-relative resolution rule, the
  flat-vendor convention that keeps `../` chains at depth one, and the
  current "only the `core:` collection is exposed today" note corrected (the
  relative-import story works today, undocumented).
- **`tycho fetch <url> <name>`**: download a package tree and unpack it into
  `vendor/<name>`, with checksum verification. The HTTP + io + path + sha256
  pieces are proven by examples/fetch; the tool is a small addition to the
  `tycho` dispatcher, dogfooding core:http/core:io/core:path/core:sha256.
- **A tycho-build leg**: build-check gains a vendored-project fixture (entry
  + two vendored packages, one importing the other) proving the whole story
  builds through the build tool.

Explicitly NOT in scope, deferred until a real caller: a `vendor:` named
collection root (Odin's `collection:` mechanism). Relative imports cover the
story; the demand-gate says a second resolution rule waits for a real
multi-entry or nested-layout project to feel the `../` pain.

Gate: `make build-check` (the new leg) + doc gates. No compiler change, so no
`make test` / `make selfhost-check` / citation shift.

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
  validation/iteration, image codecs and timezone until a caller appears,
  and a `vendor:` named collection root (Odin-style) until a real multi-entry
  or nested-layout project needs it.

> Phase 1 evidence — 2026-08-04: all six packages shipped, each verified and
> committed on its own: 1.1 core:testing (check/eq/report + exit-code
> contract), 1.2 core:toml (recursive Value enum, tables as parallel
> key/value arrays; dotted keys, [table], [[array-of-tables]], escapes incl
> \uXXXX, int/float/bool, arrays, ISO datetimes as strings; fail-closed),
> 1.3 core:log (leveled, threaded state, stdout/stderr), 1.4 core:sqlite
> (direct FFI over libsqlite3, no shim — the dbquery pattern; open/close/
> exec/query/params, rows as [[string]], sqlite's own messages), 1.5
> core:utf8 (valid/decode/encode/count, strict rejection of overlong/
> surrogate/truncated), 1.6 core:zip (reader/writer over core:compress's new
> raw-deflate path; CRC-32 in-package; the writer's output verified
> byte-exactly by Python's zipfile during development). Each package: test +
> golden + guide entry. Gates: make corelib all green (46 packages), goldens-
> check ok, shim-check 12 ok / 1 skipped (the compress shim grew raw
> deflate), doc gates ok.
>
> Learnings recorded (several are real language sharp edges a package author
> hits daily): a package-level `len` SHADOWS the builtin inside its own
> package (utf8.decode called utf8.len recursively until the stack guard
> fired — the API became `count`); two `_` bindings in one multi-payload
> pattern collide in the emitted C (compiler bug surfaced — use named
> bindings); there is no bare `pass` and no `defer`; arithmetic refuses
> int/float mixing (use to_float); `or_return` requires a Result-returning
> fn (main() can't); a newtype-of-array blocks push (dropped the Row
> aliases — query returns [[string]]); the null pointer is `null`, not
> ptr(0); the typed empty of [[string]] is [][string]; FFI out-params must
> be `inout ptr`; lines can't continue with a trailing `+`; consts don't
> cross package boundaries (levels exposed as functions). The two-`_`
> compiler bug is the one finding worth a follow-up fix in src/tychoc.c.
>
> History note: on the owner's instruction the Co-Authored-By trailer was
> stripped from all 111 commits that carried it (including this session's);
> the pre-rewrite state is preserved in refs/original.

> Phase 2 evidence — 2026-08-04: all three pieces landed, gates green
> (make corelib incl. the http test, shim-check, goldens-check, doc gates,
> tools-check, and make build-check now 7 legs).
>
> The re-scoped plan held: the compiler needed NO change (verified again —
> the vendored project compiles and runs). (1) packages.md gains a
> "Vendoring" section documenting the vendor/ convention, the
> importer-relative rule (cwd-independent), the flat-vendor shape that keeps
> ../ chains at depth one, mixed core:+vendor, and the corrected "only the
> core: collection" note — a named vendor: root explicitly deferred. (2)
> `tools/tycho-fetch/main.ty` — a standalone tool (its own directory, like
> tycho-ar/tycho-build, because importing core:* needs a package header which
> would pull every tools/*.ty into the compile — the repo documents that
> trap); `tycho-fetch <url> <name>` downloads via core:http, prints the
> sha256, unpacks the tarball into vendor/<name> (tar does the extraction —
> a shell step like the build tool's recipes; the tarball must have a single
> top-level directory), fail-closed on HTTP errors, unsafe names (no ../),
> existing dest, and malformed tarballs. (3) build-check gains leg [7]: the
> whole story end to end — fetch two packages from local file:// tarballs,
> build a project whose entry imports "vendor/greet" which imports "../util",
> run the binary. The fetch test is fully offline.
>
> One real corelib gap found and fixed en route: `http.body` truncates at the
> first NUL (the FFI string return), so binary downloads were impossible —
> core:http gains `body_bytes(r) -> bytes` (a shim that hands over a
> malloc'd copy, matching the bytes-from-C convention where the wrapper
> frees; returning the Resp's own buffer double-freed — found and fixed).
> tycho-fetch uses it, which is its integration test.
