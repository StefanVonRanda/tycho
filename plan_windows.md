# Native Windows support — plan (separate track from plan.md)

> 2026-08-05 assessment, not yet started. plan.md's agenda explicitly deferred
> native Windows ("WSL suffices") as an owner decision; this file records the
> technical assessment that the deferral is a choice, not a wall, and phases
> the port for when the owner wants it. **No phase here has been started.**
> WSL2 remains the supported path until the closing phase lands.
>
> The assessment was done by reading the source on a Linux box (each claim
> carries its `path:line` below). **Nothing has been executed under Windows** —
> the phases open with the spike that tests the assessment's foundation.
>
> SCOPE. In: a native Windows build of the compiler + runtime + corelib +
> tools + harness, targeting **MSYS2/mingw-w64 gcc** as the C environment (the
> same `cc` the transpiler already invokes). Out: MSVC as the C target
> (the emitted-C cc line `-pthread -fwrapv -O3 -lm`, `__thread`, the runtime's
> POSIX surface, no winpthreads/dirent/getline/pread, no pkg-config, and all
> 65 shell runners would each need real work — 3-4x the cost for no
> distribution win), and any change to the language surface.

## Why it is feasible at all — the assessment in one screen

- The **compiler** (`src/tychoc.c`) is mostly portable C. Its POSIX surface is
  `dirent` (opendir/readdir/closedir, `src/tychoc.c:4440@opendir`), `popen`
  (`src/tychoc.c:13241@popen`), `realpath`, `access`, `vasprintf`
  (`src/tychoc.c:98@vasprintf`), and `newlocale/uselocale`
  (`src/tychoc.c:193@uselocale`). mingw-w64 provides no POSIX
  `newlocale`/`uselocale`/`locale_t` at any version (checked against upstream
  master 2026-08-05: only the MSVC-style `_locale_t` API) -- the compiler and
  runtime already have localeconv-based fallback legs, so this costs a guard,
  not a shim. `vasprintf` is present in mingw 15.2; a 10-line fallback if ever
  absent.
  Estimate: ~1 day.
- The **runtime** (`runtime/tycho_rt.c`, embedded verbatim into every emitted
  program) needs: winpthreads for the whole concurrency model
  (`runtime/tycho_rt.c:784@pthread_create` — free under `-pthread`),
  `clock_gettime`/`nanosleep`/`sched_yield`/`sysconf`
  (`runtime/tycho_rt.c:61@sysconf` — mingw shims, a few lines), and **the one
  hard piece**: the deep-recursion stack-overflow guard built on
  `sigaltstack`/`sigaction`/`ucontext` (`runtime/tycho_rt.c:52-53`,
  `:166@sigaltstack`). The per-platform pattern already exists
  (`runtime/tycho_rt.c:120@__APPLE__`); Windows gets a third branch via
  `GetCurrentThreadStackLimits` + `AddVectoredExceptionHandler` catching
  `EXCEPTION_STACK_OVERFLOW` (~60-100 lines). Estimate: 2-3 days.
- Two corelib shims are **already ported**: `core:os` has `_popen`/`_pclose`
  (`corelib/os/os_shim.c:25-28`), `core:net` has a real Winsock2 path
  (`corelib/net/net_shim.c:31-40`). The rest split into small ports (`core:io`
  — `getline` at `corelib/io/io_shim.c:46`, `pread` at `:141`), one big port
  (`core:regex` — POSIX `regcomp`/`regexec` at `corelib/regex/regex_shim.c:20, corelib/regex/regex_shim.c:26`
  don't exist on Windows; PCRE2 behind the same API, or a documented gap),
  one redesign (`core:signal` — the `sigaction`-based socket-shutdown design
  ports to `SetConsoleCtrlHandler` + `closesocket`), and the external-dep
  packages (`tls`/`http`/`compress`/`image`/`crypto`/`sqlite`) that just need
  their MSYS2 dev libraries present (the existing skip-if-absent convention
  covers absence).
- The **harness** (65 shell scripts + `make` + `ci.sh`) runs under MSYS2's
  `sh`/`make` **unmodified** — that is what MSYS2 is. The Linux-only lanes
  skip (locale-check's `LD_PRELOAD`, asan-self, ilp32). The risk is not the
  porting; it is the **long tail**: byte-compared goldens that embed path
  separators or `\r\n`, mingw-gdb transcript drift in the debug lane, and a
  `deps` pkg-config name resolving differently.
- `tycho-debug` needs a real Windows interrupt story: `setpgid` + SIGINT-to-
  the-inferior (`tools/tycho-debug/debug_shim.c:11-17`) → `GenerateConsoleCtrlEvent`
  / `TerminateProcess` and no `setpgid`. mingw gdb exists, so the rest of the
  adapter ports.

## Phases

Each phase verified and committed on its own. Every phase's gate list names
only the lanes that can actually redden; the closing phase runs the full sweep.

### Phase 1 — Toolchain spike: does the compiler build under mingw?

The assessment's foundation is unverified: nothing has been compiled under
Windows. This phase is the one-day spike that confirms or kills the estimate.

- Install MSYS2 + mingw-w64 gcc + winpthreads + pkg-config + make + sh + tar
  + the dev libraries (openssl, libcurl, zlib, libpng, sqlite3).
- `make` the compiler (`src/tychoc.c` + the runtime embed) under mingw gcc.
  Fix what reddens: `newlocale`/`uselocale` (absent from mingw-w64 at
  every version -- route to the existing localeconv fallback legs),
  `dirent`/`realpath`/`access` details.
- Compile and run `examples/hello.ty` end to end: the transpiler's cc line
  (`cc -O3 -fwrapv -pthread ... -lm`) must work on mingw gcc unchanged; fix
  or parameterize it here if not.
- Record the compiler's Windows build in `scripts/release.sh` (a
  `dist/tycho-<v>-mingw-<arch>` tarball leg).

Gate: the compiler builds and runs hello on the Windows box; the Linux tree
is untouched (no source change lands from this phase without its own gate).
Expected: the first phase commits are the small portability shims above.

### Phase 2 — Runtime port (the hard piece)

- winpthreads threading: verify `pthread_create`/`mutex`/`cond`/`once`/`self`
  under `-pthread` — the whole `core:conc` model (spawn, channels, parallel
  for) must work as-is. (Phase 1 already proved this COMPILES and LINKS;
  phase 2 proves it RUNS.)
- Clock/timer/park: `clock_gettime`, `nanosleep`, `sched_yield`, `getpid`
  are all provided by winpthreads — verified in the installed headers, so
  the "shims" bullet is a verification task, not a writing task. Only
  `sysconf` needed a branch (done in phase 1: `GetActiveProcessorCount`).
- **The stack-overflow guard**: the Windows branch does NOT need stack-bounds
  recording or an SP comparison — `EXCEPTION_STACK_OVERFLOW` is a distinct
  exception code, so a vectored handler (`AddVectoredExceptionHandler`)
  catches that code alone, prints the same one-line message, and exits;
  everything else returns `EXCEPTION_CONTINUE_SEARCH` and crashes normally.
  Deep recursion must keep failing closed with the clean one-line error,
  never a silent crash.
- **Binary stdout/stderr on Windows** (discovered in phase 2's conc
  verification): the CRT's text mode translates `\n` to `\r\n`, so every
  golden would mismatch. The runtime sets stdout/stderr to `_O_BINARY` at
  startup (`_setmode`); stdin stays text mode so a CRLF input reads as LF,
  matching POSIX.
- The float-to-string locale guard (the localeconv fallback legs — Windows
  has no `newlocale` at any version, phase 1) and `__thread` (mingw gcc
  supports it).
- `list_dir` (mingw's `dirent`).

Gate (this phase, on the Linux box): `sh scripts/wine_smoke.sh` — the
cross-compiled fixtures must be byte-identical to the Linux goldens under
Wine (conc suite, clock, floats, iobuiltins) and the deep-recursion fixtures
must fail closed on the main thread AND in a spawned task — plus the Linux
tree stays green (`make conc`, `make recursion`, `make test`). The ASan/TSan
conc legs: TSan has no Windows-target support in gcc and mingw ASan is
experimental — both are SKIPS on the Windows side. Expected: conc green under
winpthreads; recursion green via the vectored handler.

DEFERRED by owner decision (2026-08-05, "we'll rerun it later under proper
windows"): the definitive pass — these exact fixtures on a real Windows
scheduler — lands on the windows-latest CI leg in phase 6/7. Wine smoke is
evidence of the port, never a Windows verdict.

### Phase 3 — Compiler + emitted-C toolchain verification

- The transpiler's cc invocation and link line on Windows: flags, the `-o`
  binary naming, shim discovery (`--print-shims`), pkg-config `deps` flags.
- `-g`/`#line` under mingw gdb (DWARF is supported; verify stepping).
- The `--version`/usage output (no change expected).

Gate: `make test` on the Windows box (or `test-fast`), `make tools-check`.
Expected: the 591 fixtures pass under mingw; any fixture that reddens on
Windows only (path separators, line endings) is parked for Phase 6's golden
audit rather than patched ad hoc.

### Phase 4 — Corelib shims

- `core:io`: `getline` (15-line replacement) and `pread` (→
  `_lseeki64`+read or `ReadFile` with an offset, keeping `read_at`'s
  contract).
- `core:strings`: `strtod_l`/locale handling against mingw's `_strtod_l`.
- `core:regex`: the decision phase — PCRE2 behind the same `regcomp`/`regexec`
  API, or a documented Windows gap for the package (it is in the 1.0 surface,
  so a gap is a contract change; prefer the port).
- `core:signal`: `SetConsoleCtrlHandler` for SIGINT/SIGTERM-equivalent, the
  `shutdown_requested` flag survives, the socket shutdown maps to
  `closesocket`.
- Verify the external-dep packages (`tls`/`http`/`compress`/`image`/
  `crypto`/`sqlite`) against their MSYS2 libraries; the skip-if-absent
  convention covers a missing one.

Gate: `make corelib` + `make corelib-examples` + `make shim-check` on the
Windows box. Expected: all packages green or loudly skipped; the regex/signal
ports land with their own tests + goldens.

### Phase 5 — Tools

- The pure-Tycho tools (`tychofmt`, `tycho-lsp`, `tycho-ar`, `tycho-q`,
  `tycho-vm`, `tycho-scheme`, `tycho-fetch`, `tycho-build`, `tycho-kv`,
  `tycho-chess`, `tycho-rsa`, `tycho-sat`, `tycho-kvsrv`, `tycho-scheme`)
  compile once runtime+corelib do; the ones that shell out (`tar`, build
  recipes) need MSYS2's tools, which are present.
- `tycho-debug`: the Windows interrupt story — replace `setpgid`/SIGINT-to-
  inferior with `GenerateConsoleCtrlEvent`/`TerminateProcess` (or a
  documented gap: no Ctrl-C interrupt on Windows, `q` only). mingw gdb as the
  backend; the debug-check lane runs under it or skips.

Gate: `make tools` builds every tool; each tool's own lane (`ar-check`,
`q-check`, `vm-check`, `scheme-check`, `kv-check`, `chess-check`, `rsa-check`,
`kvsrv-check`, `sat-check`, `build-check`, `debug-check`) green or loudly
skipped. Expected: the tool lanes port with their goldens intact.

### Phase 6 — Harness and golden audit under MSYS2

- `make ci` under MSYS2's `sh`/`make`; the Linux-only lanes (locale-check's
  `LD_PRELOAD`, asan-self, ilp32) skip loudly with their reasons.
- The long tail: audit every byte-compared golden for Windows-only drift —
  `\r\n` line endings (the runners and goldens must agree), embedded path
  separators, and any tool that prints a platform string. Fix at the runner
  level (normalize before compare), not by re-recording goldens per platform.
- The fuzz lanes (Python — portable) and the editors lanes.

Gate: `make ci` (N=0) green on the Windows box with only the named skips.
Expected: the goldens are platform-identical; any that cannot be are
re-recorded deliberately with the reason in the commit.

### Phase 7 — Close: full sweep, release, and documentation

- The full `make ci` on Windows as the closing sweep (the phase-6 gate was
  the discovery loop; this is the confirmation).
- `scripts/release.sh` produces a Windows tarball and its sha256; the
  version-vs-`--version` check applies unchanged.
- Documentation: the README status banner's "no Windows" note becomes the
  Windows support statement (MSYS2 required, WSL2 still supported); the
  sharp-edges list in `SECURITY.md` records the Windows gaps (if any survive:
  regex/signal/debugger interrupt); plan.md's "native Windows (deferred,
  WSL suffices)" deferral is retired or re-scoped.

Gate: the full `make ci` on Windows plus the doc gates
(`check_citations.py`, `check_links.sh`). Expected: the README flips its
Windows caveat; the two supported paths are WSL2 (zero setup) and native
MSYS2.

## Risks and the honest cost

- **The port itself is ~1.5-2 weeks** of focused work (compiler 1d, runtime
  2-3d, corelib 2-3d, tools+debugger 2d, harness+skips 1-2d, golden long
  tail 2-3d). MSVC would multiply by 3-4x and is out of scope.
- **The long tail is the risk**, not the porting: each `make ci` red on a
  Windows box is a discovery, and the byte-compared goldens are where they
  hide.
- **`core:regex` and `core:signal` are contract decisions**, not porting
  chores: if their Windows ports are deferred, the 1.0 surface statement in
  `docs/guides/corelib.md` must name the gap.
- **The stack-overflow guard must be ported, not skipped**: `tests/recursion`
  would redden, and deep recursion failing with a clean error is a stated
  security posture (no stack-overflow DoS).
- Every claim above was verified by reading the source with the cited lines;
  none was executed under Windows. Phase 1 exists to test exactly that.

## Out of scope (unchanged from plan.md)

MSVC as a C target, a REPL, native terminal UI work, and any change to the
language surface. WSL2 stays a first-class supported path.

> Phase 1 evidence — 2026-08-05 (the mingw toolchain spike, run on Linux with
> the mingw-w64 CROSS compiler + Wine — no Windows box; the definitive Windows
> pass stays the phases-6/7 CI leg, per the assessment's warning that green
> under Wine is not green on Windows).
>
> VERDICT: the assessment held, with two corrections. The compiler cross-built
> under mingw with three small guards, the emitted runtime needed four, and
> hello ran end to end under Wine: `what is your name: hello igzo`.
>
> WHAT REDDENED (each fixed with an #ifndef _WIN32 guard, zero Linux effect):
>   src/tychoc.c — `newlocale`/`uselocale`/`locale_t` (167-215: the pre-existing
>   localeconv fallback legs absorb it), `readlink("/proc/self/exe")` in exe_dir
>   (argv0 fallback + both separator kinds), `realpath(dir, NULL)` in canon_dir
>   (`_fullpath`).
>   runtime/tycho_rt.c — `ucontext.h` missing: the whole stack-overflow guard is
>   COMPILED OUT on Windows, marked `/* gap: */` (the SEH port is phase 2; until
>   then Windows deep recursion dies with a plain access violation); `sysconf`
>   in tycho_ncpu (GetActiveProcessorCount); `newlocale`/`uselocale` in the
>   float-to-str guard AND tycho_test_float_roundtrip (the fix_decimal_point
>   fallback absorbs it; the roundtrip test returns -1/untestable on Windows);
>   `aligned_alloc` missing (a malloc+align+stash wrapper pair, because the
>   channel free path uses plain free()).
>
> CORRECTIONS TO THE ASSESSMENT: (1) "mingw-w64 v8+ provides newlocale" is
> WRONG for mingw-w64 at ANY version (verified against upstream master's
> locale.h, 2026-08-05) — only the MSVC-style _locale_t exists;
> the fallback legs are what make the port small, and the locale-check LANE
> (LD_PRELOAD setlocale) is Linux-only either way. (2) winpthreads is a
> DYNAMIC dll (libwinpthread-1.dll) — present system-wide in MSYS2, but a
> bare Windows box without it fails to load the exe; the release tarball must
> note it (or static-link later). (3) The transpiler's system() inside Wine
> cannot reach a Linux-side cc — the spike used --emit-c + a Linux-side mingw
> compile; on a real Windows/MSYS2 host the normal build path works. The cc
> line `-O3 -fwrapv -pthread ... -lm` is accepted by mingw gcc unchanged.
>
> SHIPPED: scripts/release.sh gains `--mingw` — cross-builds
> dist/tycho-<v>-mingw64-<arch>.tar.gz (compiler + corelib + docs + hello; the
> tools join in phase 5), version-checked like the native leg, smoke-verified
> to contain tychoc.exe. plan_windows.md itself is committed with this phase.
>
> GATES (Linux tree): make test 591/591 (env -u LD_PRELOAD), tools-check ok,
> goldens-check ok, doc gates ok. The compiler/runtime line shifts re-anchored
> 112 citations (diff-hunk-mapped, token-verified, gate re-run to zero).
>
> HOW TO REPRODUCE: `sudo apt install gcc-mingw-w64-x86-64-posix wine64`;
> `make -s build/tycho_rt_embed.h`; `x86_64-w64-mingw32-gcc -O2 -fwrapv -std=c11
> -Ibuild src/tychoc.c -o build/tychoc-mingw.exe`; `wine64 ./build/tychoc-mingw.exe
> examples/hello.ty --emit-c -o build/hello-mingw`; `x86_64-w64-mingw32-gcc -O3
> -fwrapv -pthread -o build/hello-mingw.exe build/hello-mingw.c -lm`;
> `WINEPATH='Z:\usr\x86_64-w64-mingw32\lib' wine64 ./build/hello-mingw.exe`.

> Phase 2 evidence — 2026-08-05 (the runtime port, verified by
> `sh scripts/wine_smoke.sh` on the Linux box; definitive pass deferred to
> the windows CI leg per the owner's decision).
>
> THE CODE: the runtime's guard section now has two platform branches. POSIX
> unchanged (sigaltstack + SIGSEGV + SP-vs-bounds). Windows: a vectored
> exception handler (`AddVectoredExceptionHandler`) catches the distinct
> `EXCEPTION_STACK_OVERFLOW` code — no bounds recording, no SP comparison,
> which is SIMPLER than the POSIX heuristic — prints the same one-line
> message via WriteFile and ExitProcess(1) (nothing that needs real stack;
> the handler runs on the just-committed guard page); anything else returns
> EXCEPTION_CONTINUE_SEARCH and crashes as a real crash. Plus the binary-
> stdio fix discovered this phase: `_setmode(_fileno(stdout/stderr),
> _O_BINARY)` in the same constructor — without it the CRT's text mode
> turns every `\n` into `\r\n` and NO golden matches (first observed in the
> phase-1 workers test; fixed and re-verified). No per-thread state is
> needed on Windows, so the task trampoline's init/fini calls are no-ops.
>
> VERIFIED UNDER WINE (scripts/wine_smoke.sh, 16 checks, all green):
>   byte-identical to the Linux goldens: the whole conc positive suite
>   (basic, chan, chancap1, workers, parfor, select, select_parfor,
>   implicit — spawn/wait, channels, backpressure, parallel-for reductions,
>   select), plus clock (winpthreads timers + the park ladder), floats (the
>   localeconv fallback + Windows CRT %.15g), and iobuiltins (list_dir via
>   mingw dirent).
>   the stack guard: 2M-deep recursion fails closed on the main thread AND
>   in a spawned task — exit 1, empty stdout, "tycho: stack overflow --
>   recursion too deep" on stderr, identical to POSIX — and the modestly
>   nested controls run (f(1000) -> 500501, spawned deep(1000) -> 0), so the
>   guard is not trigger-happy.
>   hello runs interactively with bare-LF output.
>
> NOT VERIFIED (the CI leg's job): the guard under a real Windows kernel
> (Wine's stack/exception semantics are an approximation), and the conc
> goldens under the real Windows scheduler. TSan has no Windows-target
> support in gcc and mingw ASan is experimental — those conc legs are skips
> on the Windows side.
>
> GATES (Linux tree): make conc 38/38, make recursion all green (both the
> compiler-input gate and the generated-code guard leg), make test 591/591
> (env -u LD_PRELOAD), goldens-check, doc gates. The runtime line shifts
> re-anchored 14 citations (diff-hunk-mapped, token-verified, gate re-run
> to zero). `make wine-smoke` is a manual target, not a gate and not in
> make ci.
