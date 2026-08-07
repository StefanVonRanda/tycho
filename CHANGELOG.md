# Changelog

All notable changes are recorded here against the version that shipped them.
The version constant lives in `src/tychoc.c` (`TYCHO_VERSION`, printed by
`tychoc --version`); bump both together. Per-release publishing notes stay in
`RELEASE_NOTES.md`; this file is the accumulating record.

## [Unreleased]

Landed since 1.0.0, against no version constant yet — `TYCHO_VERSION` still
says `1.0.0`, so these ship the next time it is bumped.

- **Native Windows support (MSYS2 + mingw-w64).** The port ran as its own
  track: the compiler and runtime (SEH stack guard, binary stdio,
  winpthreads), the corelib shims (`core:io`'s directory classification,
  `core:signal`'s `SetConsoleCtrlHandler` branch, `core:datetime`'s
  `localtime_s`/`mktime`-difference, `core:regex` on `pcre2posix.h`,
  `core:net` and `core:tls` on winsock), a `_WIN32:` section in the `deps`
  manifest for Windows-only linker flags, the tools, and the harness itself
  (loud platform skips in `scripts/ci.sh`, `tests/run.sh`, `tests/conc`,
  `tests/ffi`, `tests/recursion`, `corelib/run.sh`; `check_goldens.py` moved
  to `posixpath` so goldens resolve against git's forward-slash paths).
  MSVC is not a supported C target; WSL2 remains the zero-setup path.
- **The recorded Windows run — 2026-08-07, ARM64 Windows 11 under Prism
  emulation (MSYS2/mingw x86-64).** Green: `goldens-check`, `entrypoints`,
  `spec-check`, `docs-fences`, `check-links`, `shim-check`. Residual, each
  classified rather than fixed: `make test` 589/591 and `make conc` 34/38
  (the 2 + 4 are one class — the emulator's startup heap-corruption race,
  `0xC0000374`, which the runner retries with backoff), and `make corelib` 43
  ok / 2 fail / 1 skip (`datetime` and `time` — the POSIX-`TZ` sign
  convention and a timing bound; `signal`'s test needs a POSIX `kill`). **No
  full-green `make ci` on Windows has been recorded**, and none on a
  non-emulated x86-64 Windows box.
- **`scripts/release.sh --mingw` now ships the tools.** The Windows tarball
  carries `tychofmt.exe`, `tycho-lsp.exe` and `tycho-debug.exe` beside
  `tychoc.exe` (parity with the native leg), and the staged layout is
  smoke-tested under Wine — the packaged compiler must find `corelib/` beside
  itself and emit C — skipping loudly when Wine is absent.
- **Compiler: a pattern discard binds nothing.** `Sized(_, _)` emitted one C
  local per binding named `h__`, so two discards in one pattern declared the
  same name twice and the generated C failed to compile. `_` in a pattern is
  now a discard everywhere: it fills its payload slot for the arity check,
  binds nothing, is not referenceable in the arm body, and is not emitted —
  while the arm's body still runs (`Err(_)`). Fixtures
  `tests/match_underscore.ty` and `tests/reject/match_underscore_use.ty`.
- **Diagnostic: bare `Ok`/`Err`/`None` in a `:=` or typed value-branch.** The
  old message suggested annotating the binding (`x : T = if/match ...`),
  which rejects the same program — neither form flows a destination type into
  branch unification. The message now states the rule and names the tails
  that do flow one in (an assignment or a return).

## [1.0.0] — 2026-08-05

The 1.0 promotion. The language surface and the spec are now the stability
contract (see the README's status banner); what follows is the last chain of
work that landed ahead of it.

- **Batteries-included corelib (phase 1).** Six new packages, each with a test
  + golden + guide entry: `core:testing` (assert/eq/report unit-test state),
  `core:toml` (recursive table/array parser, fail-closed), `core:log`
  (leveled logging, threaded state), `core:sqlite` (direct FFI over
  libsqlite3 — the dbquery pattern), `core:utf8` (validation and codepoint
  iteration, strict rejection of overlong/surrogate/truncated sequences), and
  `core:zip` (reader/writer over a new raw-deflate path in `core:compress`,
  CRC-32 in-package). `core:compress` gained raw deflate/inflate.
- **Vendored dependencies (phase 2).** The importer-relative import story
  documented as the vendoring convention (`vendor/` layout, flat-vendor
  shape); `tycho-fetch <url> <name>` downloads and unpacks a package tarball
  into `vendor/` with sha256 verification (dogfooding core:http + core:sha256
  + core:io); `core:http` gained `body_bytes(r)` — the string `body()`
  truncated at NULs, so binary downloads were impossible.
- **Debugger (phase 3).** `tycho debug <file.ty>` / `tools/tycho-debug/` — a
  gdb adapter: compiles with `-g`, drives a scripted gdb session over pipes
  (breakpoints by source line, step/next, locals under their Tycho names,
  Ctrl-C interrupts a running inferior). Gated by `make debug-check`.
- **1.0 promotion (this phase).** `tychoc --version` and the version constant;
  the README's status banner flips from research prototype to the 1.0
  contract (stable: the language surface and the spec; not stable:
  performance tuning and benches); a security review of the FFI shims
  (string/bytes ownership, the core:os shell-out paths, the TLS wrapper)
  with findings recorded in `SECURITY.md`; and the corelib 1.0 API-freeze
  decision with its deprecation path, recorded in `docs/guides/corelib.md`.

## [0.1.0] — earlier

The pre-1.0 research-prototype release. See `RELEASE_NOTES.md` at the
`v0.1.0` tag for what it contained; it carried no stability guarantees.
