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
- **`make ci` is green on native Windows — 2026-08-08, x86-64 Windows 11
  26200 under KVM, MSYS2 + mingw-w64 gcc 16.1.0, gdb 17.2.** Exit 0 from a
  clean checkout with an empty working tree, and **114 skips**, every one of
  them printing its reason. Green means nothing reddened, not that everything
  ran: the sanitizer legs (mingw ships no ASan/UBSan runtime and gcc has no
  Windows-target TSan), the fuzz lanes, `ilp32`, `locale-check`, `asan-self`,
  `selfhost-check`, `bench-guard`, `core:signal`'s test, `server/run.sh`'s six
  shutdown cases and `tycho-ar`'s newline-in-filename member do not execute
  there. The prior recorded run (2026-08-07, ARM64 under Prism emulation) was
  `make test` 589/591, `conc` 34/38, `corelib` 43 ok / 2 fail; the residuals it
  could not separate from emulator noise are resolved below.
- **Six port defects the sweep found, each measured before and after.**
  - *The `tycho` dispatcher was entirely broken on Windows.* `tycho_run` is
    `system()`, which is cmd.exe there: `> /dev/null` is a path and not a sink,
    so `run`, `build`, `check`, `watch` and `fmt` all died at their first
    shell-out — each reporting a misleading cause (`check` said "has errors"
    about a file that compiles). `rm -f`/`cp`/`mv`/`diff -q` moved into
    `tools/tycho_shim.c` as native calls, `MoveFileEx` keeps `fmt -w`'s atomic
    install, and the file comparison returns 1/0/**−1** so an unreadable file
    cannot compare equal and fail open. The five commands had no lane of their
    own; `scripts/tools_check.sh` gained one.
  - *`tycho-debug` was dead on Windows*, all 6 legs, while shipping in the
    release tarball. Three causes: the same cmd.exe shell-outs; `_fullpath`
    answers in backslashes while gdb reports forward slashes, so every stop
    took the "runtime glue" branch (whole path, raw `h_main`, no source line);
    and `_fullpath` succeeds for a file that does not exist, so the "no such
    file" refusal never fired.
  - *`core:time` violated its own contract.* `sleep_ms(200)` read back 199 —
    winpthreads wakes a timer tick before the observing clock crosses the mark.
    1 in 60 under CPU contention, 0 in 60 idle, which is why it surfaced only
    inside a full sweep. The Windows branch now sleeps against a deadline read
    from the same `CLOCK_MONOTONIC` the caller measures with.
  - *`tycho-fetch`'s `tar` line was spelled for `sh`*, so cmd handed `tar`
    paths with the quotes still in them and every fetch died as "not a tarball
    with a single top-level directory" — naming the wrong cause.
  - *`check_citations.py` passed by not looking.* `git ls-files "*.md"` through
    `subprocess` returns 8 files on Windows where the shell returns 126, so the
    gate reported ok over 8 anchored and 6 bare citations instead of 134 and
    812. It filters by suffix in Python now; both platforms print the identical
    summary. Same class as the `posixpath` bug in `check_goldens.py`.
  - *Nine CI lanes carried POSIX assumptions* — five sanitizer legs, the server
    shutdown cases (which **hung** the sweep for 43 minutes rather than failing
    it), `selfhost`, `bench-guard`, and leg 7's `file://`/recipe/`.exe` paths.
- **The packaged Windows tools are now run, not just built.** The
  `release.sh --mingw` layout was previously smoke-tested only as far as
  `tychoc.exe --emit-c` under Wine. Staged and exercised on Windows: a full
  build with `corelib/` found beside the compiler and no `TYCHO_CORELIB`, a
  `core:strings` import resolved from that corelib, `tychofmt.exe` formatting
  idempotently, `tycho-lsp.exe` answering a framed `initialize`, and
  `tycho-debug.exe` setting and hitting a breakpoint under real gdb.
- **`scripts/release.sh --mingw` now ships the tools.** The Windows tarball
  carries `tychofmt.exe`, `tycho-lsp.exe` and `tycho-debug.exe` beside
  `tychoc.exe` (parity with the native leg), and the staged layout is
  smoke-tested under Wine — the packaged compiler must find `corelib/` beside
  itself and emit C — skipping loudly when Wine is absent.
- **`make selfhost-check` retired; nothing builds `tychoc0` now.** It was
  `make ci` step [3n/20] and cost ~50s of every sweep re-asserting that the
  frozen compiler's own emission reproduces itself. `ROADMAP.md` has claimed
  since 2026-07-29 that "nothing builds it at all"; that sentence is true as of
  this change and was not before. `compiler/selfhost.sh` is kept unrun, with a
  header recording what it proved — the same treatment `compiler/fixpoint.sh`
  and `scripts/frontparity.sh` got. What is given up: the byte-identity is no
  longer re-checked at each HEAD. What is not: `compiler/tychoc0.ty` is still
  compiled by every sweep as ASan/UBSan **input** to `scripts/asan_self.sh`, so
  the largest Tycho source in the tree still exercises the compiler, and
  self-hosting remains a fact about the commit that proved it.
- **`core:os` gains `exec` / `exec_out` — an argv no shell parses.** They take
  a `[string]` and start no shell: `posix_spawnp` on POSIX, `CreateProcess`
  with no `cmd.exe` anywhere on Windows. This closes the injection class
  `SECURITY.md` had carried as its one open backlog item; `system`/`run` keep
  their documented shell semantics and nothing is deprecated. A `[string]`
  cannot cross the FFI (`docs/spec/14-ffi.md` crosses only `[int]`/`[float]`),
  so argv is pushed into an opaque builder one string at a time. Fail closed
  with `-1` on an empty argv, over 4096 entries, an allocation failure
  mid-build, or a program that cannot be spawned. Windows joins the vector by
  the `CommandLineToArgvW` quoting rules, round-tripped through the real
  splitter by `corelib/os/os_argv_quotecheck.c` — a new `make shim-check` leg
  that builds and RUNS on Windows (9 cases: embedded quotes, trailing
  backslashes, an empty argument, shell metacharacters) and skips loudly
  elsewhere. Residual and callee-side: a program that parses its own command
  line by other rules — `cmd.exe`, a `.bat` file — can still read it
  differently.
- **The last two Windows skips are gone.** `corelib/test/signal` was skipped
  because MSYS2's `kill` terminates a native PE instead of signalling it; it
  now steps onto a private console and raises a real `CTRL_BREAK`, and its
  output there is byte-identical to the Linux golden. `make corelib` on
  Windows has **zero skips**. `server/run.sh`'s six shutdown cases and the
  access-log tail run there too, through `server/winsignal.c`, which launches
  the server with `CREATE_NEW_PROCESS_GROUP` so the event can target it alone
  — group 0 would Ctrl-Break the sweep's own console — and reports the
  **Windows** pid, since `$!` is an MSYS pid and the console API will not take
  it. One measured behavioural gap stays and is recorded in `SECURITY.md`: a
  thread parked in `recv` on an accepted connection is not released by the
  handler's `shutdown()` as it is on Linux, so a Windows server winds down
  within its idle timeout rather than within a millisecond.
- **`goldens-check` never saw the `.win` siblings.** A `<golden>.win` ends
  `.win`, not `.out`/`.err`, so the token scan matched none — three existed
  and all three went unchecked, on the platform whose harness is thinnest. The
  gate now derives them from the resolved set: 432 files where it reported
  429.
- **The Wine lanes skipped on every modern Wine.** Five lanes and
  `release.sh` guarded on `command -v wine64`; Wine 9.0 merged it into a
  single `wine`, so the guard was false on any current install and each lane
  exited 0 saying "wine64 not on PATH". `release.sh` had it worst — the same
  test decided whether the staged Windows tarball got its smoke test, so it
  shipped unrun from a box that could have run it.
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
