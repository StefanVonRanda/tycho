# Native Windows support — plan (separate track from plan.md)

> Assessed 2026-08-05, **executed and closed 2026-08-07**. The port happened:
> `main` carries 71 `_WIN32` guards across the C sources, and `ROADMAP.md`
> condition 5 was closed on 2026-08-10 with `make ci` green under MSYS2 — the
> one measured behavioural difference (a thread parked in `recv` winds down
> within its idle timeout rather than within a millisecond) is a documented
> platform limit, not an open item.
>
> **This file is kept as the record of the port's design, not as live work.**
> It is load-bearing: `tests/run.sh`, `tests/conc/run.sh`, `tests/ffi/run.sh`,
> `scripts/ci.sh`, `scripts/release.sh`, the four `scripts/wine_*.sh` and six
> `examples/*/run.sh` all cite "windows-port.md phase N" as the stated reason
> they skip a sanitizer leg on Windows. Those phase numbers must keep resolving
> to the sections below, so do not renumber or delete them.
>
> The assessment below was written by reading the source on a Linux box, before
> anything ran under Windows; its `path:line` claims are from that date and have
> not been re-verified since. Read it as the plan that was followed, not as a
> current description of the tree.
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
  `dirent` (opendir/readdir/closedir, `src/tychoc.c:5080@opendir`), `popen`
  (`src/tychoc.c:14414@popen`), `realpath`, `access`, `vasprintf`
  (`src/tychoc.c:193@vasprintf`), and `newlocale/uselocale`
  (`src/tychoc.c:288@uselocale`). mingw-w64 provides no POSIX
  `newlocale`/`uselocale`/`locale_t` at any version (checked against upstream
  master 2026-08-05: only the MSVC-style `_locale_t` API) -- the compiler and
  runtime already have localeconv-based fallback legs, so this costs a guard,
  not a shim. `vasprintf` is present in mingw 15.2; a 10-line fallback if ever
  absent.
  Estimate: ~1 day.
- The **runtime** (`runtime/tycho_rt.c`, embedded verbatim into every emitted
  program) needs: winpthreads for the whole concurrency model
  (`runtime/tycho_rt.c:862@pthread_create` — free under `-pthread`),
  `clock_gettime`/`nanosleep`/`sched_yield`/`sysconf`
  (`runtime/tycho_rt.c:68@sysconf` — mingw shims, a few lines), and **the one
  hard piece**: the deep-recursion stack-overflow guard built on
  `sigaltstack`/`sigaction`/`ucontext` (`runtime/tycho_rt.c:52-53`,
  `:167@sigaltstack`). The per-platform pattern already exists
  (`runtime/tycho_rt.c:128@__APPLE__`); Windows gets a third branch via
  `GetCurrentThreadStackLimits` + `AddVectoredExceptionHandler` catching
  `EXCEPTION_STACK_OVERFLOW` (~60-100 lines). Estimate: 2-3 days.
- Two corelib shims are **already ported**: `core:os` has `_popen`/`_pclose`
  (`corelib/os/os_shim.c:25-28`), `core:net` has a real Winsock2 path
  (`corelib/net/net_shim.c:31-40`). The rest split into small ports (`core:io`
  — `getline` at `corelib/io/io_shim.c:59`, `pread` at `:141`), one big port
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
- **The compiler's own binary stdio** (found this phase): `src/tychoc.c` needs
  the same `_setmode` binary-mode fix as the runtime — the CRT's text mode
  would CRLF the diagnostics goldens and the `--print-shims` output that
  shells splice onto cc lines.

Gate (on the Linux box): `sh scripts/wine_test.sh` — the whole plain corpus
(examples + tests + pkg packages + abort + diag) cross-compiled and run under
Wine against the Linux goldens, byte-for-byte — plus `--print-shims`/
`--print-deps` parity and the `-g`/DWARF line-table check, and the Linux tree
stays green (`make test`, `make tools-check`). Expected: the corpus passes
under mingw; any fixture that reddens only for Windows-environment reasons
(paths, shell-out, POSIX-only assertions) is PARKED for phase 6's golden
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

Gate (on the Linux box): `sh scripts/wine_corelib.sh` — every corelib test
that can link here, cross-compiled under mingw and run under Wine against the
Linux goldens (pass or skip-with-reason; nothing fails) — plus the Linux tree
stays green (`make corelib`, `make shim-check`, `make test`). A compiler
addition lands in this phase: the deps mechanism gains `_WIN32:` sections —
raw link flags for the Windows build, inert and invisible to `--print-deps`
elsewhere — which is what lets core:net (`-lws2_32`) and core:regex
(`-lpcre2-posix`) link on Windows at all. Expected: all packages green or
loudly skipped; the regex/signal ports land compile-verified, with their
Windows tests (which need Windows-native mechanisms) parked for phase 6.

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

Gate (on the Linux box): `sh scripts/wine_tools.sh` — every tool
cross-compiles under mingw (the Windows build proof) and the deterministic
ones run correctly under Wine against their lane goldens where cheap — plus
the Linux tree stays green (`make tools`, `make debug-check`). The full lane
fixture suites and tycho-debug's live gdb session are the CI leg's job.
Expected: the tools build and run; tycho-debug's Windows interrupt is a
documented gap (no Ctrl-C while the inferior runs; `q` quits).

### Phase 6 — Harness and golden audit under MSYS2

- `make ci` under MSYS2's `sh`/`make`; the Linux-only lanes (locale-check's
  `LD_PRELOAD`, asan-self, ilp32) skip loudly with their reasons.
- The long tail: audit every byte-compared golden for Windows-only drift —
  `\r\n` line endings (the runners and goldens must agree), embedded path
  separators, and any tool that prints a platform string. Fix at the runner
  level (normalize before compare), not by re-recording goldens per platform.
- The fuzz lanes (Python — portable) and the editors lanes.

Gate (the Linux box, done): the golden drift AUDIT — every byte-compared
golden scanned for Windows-hostile content (CRLF, absolute paths, platform
strings), the Linux-only lanes enumerated to skip loudly, and the remaining
runnable lane (ffi) added to the wine coverage. The VERDICT — `make ci`
(N=0) green under MSYS2 sh/make on a real Windows environment — is the CI
leg's, by definition; the four wine lanes plus wine-ffi are its Linux
approximation. Expected: the goldens are platform-identical; any that cannot
be are re-recorded deliberately with the reason in the commit.

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
> to contain tychoc.exe. windows-port.md itself is committed with this phase.
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

> Phase 3 evidence — 2026-08-05 (toolchain verification on the Linux box via
> scripts/wine_test.sh; the definitive make-test-on-Windows pass stays the
> windows CI leg).
>
> CORPUS UNDER WINE: 318 fixtures (plain corpus + examples, 15 package
> programs, 17 runtime aborts, 23 diagnostics goldens) — **317 byte-identical
> to the Linux goldens, 1 park item**. The abort fixtures all die cleanly
> with a 'tycho:' message; the 23 diagnostics goldens match the MINGW
> compiler's stderr byte-for-byte (which is what the compiler binary-stdio
> fix this phase was FOR).
>
> THE ONE PARK ITEM: tests/float_str_locale — its `rt=` column asserts
> `rt=1` from the runtime roundtrip test hook, which returns -1 on Windows
> BY DESIGN (the hook needs newlocale, absent from mingw-w64 at every
> version — phase 1/2). The float formatting itself (1.5 = 1.5) is correct
> under Wine; only the POSIX-only assertion differs. Phase 6: split the
> fixture or give it a Windows-aware golden.
>
> TOOLCHAIN: `--print-shims` and `--print-deps` output is IDENTICAL between
> the Linux and mingw compilers (transitive shim closure + pkg-config names
> are pure compiler-side). The pkg-config RESOLUTION cannot run under Wine
> (the Linux-side pkg-config binary is unreachable from cmd.exe) — the same
> environment class as the cc invocation; on a real MSYS2 host pkg-config
> exists. `-g`: the emitted C's `#line` directives map into the DWARF line
> table — Debian's gdb-mingw-w64 (a NATIVE Linux cross-gdb) resolves
> `info line prog.ty:13` to the right address and accepts a `prog.ty:13`
> breakpoint. Live STEPPING under gdb cannot be driven from the Linux side
> (the cross-gdb cannot exec a PE; the wine exec dance is unreliable) — that
> half is the CI leg's job on real Windows.
>
> COMPILER FIX (this phase): src/tychoc.c gains the same _WIN32 binary
> stdout/stderr `_setmode` as the runtime — without it the diagnostics
> goldens and `--print-shims`/`-deps` output would carry CRLF on Windows.
>
> SHELL-OUT FINDING (recorded for phases 4/5): os.system on Windows runs
> cmd.exe, not sh — `rm -f`, `> /dev/null`, tar pipelines and the build
> tool's recipes need the MSYS2 sh or cmd-compatible forms. None of the
> plain corpus fixtures shell out, so nothing reddened for it here.
>
> GATES (Linux tree): make test 591/591, tools-check ok, goldens-check ok,
> doc gates ok (117 citations re-anchored, diff-hunk-mapped and
> token-verified). make wine-test is a manual target, not a gate.

> Phase 4 evidence — 2026-08-05 (corelib shims, verified by
> `sh scripts/wine_corelib.sh` on the Linux box: 37 pass / 9 skip-with-reason /
> 0 fail; definitive pass deferred to the windows CI leg).
>
> THE CODE:
>   Compiler — the deps mechanism gains `_WIN32:` sections (src/tychoc.c
>   add_pkg_deps): raw linker flags for Windows builds, appended to the cc
>   line verbatim, never pkg-config'd, never listed by --print-deps; a
>   non-Windows compiler skips the section entirely. This is what makes
>   core:net (-lws2_32) and core:regex (-lpcre2-posix) LINK on Windows at all.
>   core:io — getline/pread/pwrite (one-arg _mkdir) _WIN32 shims; plus the
>   directory-classification fix: Windows fopen/open of a DIRECTORY fails with
>   EACCES, not POSIX's EISDIR, so ty_rf_errno now re-classifies EACCES by
>   stat, and iox_remove dispatches _rmdir for directories (Windows remove()
>   is unlink-only). Before this, every io directory path answered Err(Failed)
>   instead of Err(IsDir).
>   core:strings — the newlocale handle is now guarded by TY_HAVE_STRTOD_L,
>   which already excludes mingw; the localeconv fallback (already correct
>   under any locale) is the Windows path.
>   core:signal — the fail-closed stub is replaced by the real Windows branch:
>   SetConsoleCtrlHandler + shutdown/closesocket on the listener and the
>   registry (shared, platform-neutral array). The accept-wake behaviour is
>   Windows-version dependent; the flag is set either way (CI leg verifies).
>   core:datetime — localtime_r/gmtime_r (via localtime_s/gmtime_s),
>   setenv/unsetenv (via _putenv), and tm_gmtoff (glibc-only member) replaced
>   by the portable mktime-difference.
>   core:regex — `#ifdef _WIN32` includes <pcre2posix.h> (POSIX-signature
>   drop-in: regcomp/regexec/regfree unchanged), verified to compile against
>   pcre2posix.h; the -lpcre2-posix link comes from the deps _WIN32 section.
>
> VERIFIED UNDER WINE (wine-corelib): 37 packages byte-identical to their
> goldens, including io, strings, datetime (non-TZ paths), compress + zip
> (against libz-mingw-w64), net and httpd (against ws2_32). 9 skips with
> reasons: 6 are MSYS2-only library links (crypto/http/image/sqlite/tls —
> OpenSSL/curl/png/sqlite3 mingw builds; regex — pcre2-posix) whose shim code
> is portable and compile-verified; 3 are TEST-side POSIX mechanisms, not
> shim bugs:
>   os — the test drives `true`/`false`/`printf` which cmd.exe lacks (exit
>   code 9009); the PACKAGE works (cmd runs, exit codes map, `exit 7` passes).
>   datetime — offset_at's signed POSIX TZ strings come out sign-inverted
>   (Windows _tzset uses the opposite convention); `utc0` matches.
>   signal — the test kills itself via `kill -TERM $PPID` from a POSIX shell;
>   cmd has no kill, so the handler never fires and the accept blocks (this
>   one HUNG the lane until it was classified as a skip).
>   All three need Windows-native test variants in phase 6.
>
> GATES (Linux tree): make corelib all green (46 packages — the _WIN32
> branches are inert), shim-check 9 ok / 4 skipped / 0 failed, make test
> 591/591 (env -u LD_PRELOAD), doc gates; 53 citations re-anchored
> (diff-hunk-mapped with the whole-span range fix, token-verified). make
> wine-corelib is a manual target, not a gate.

> Phase 5 evidence — 2026-08-05 (tools, verified by `sh scripts/wine_tools.sh`:
> 14 tools cross-built for Windows + 3 run checks green, 0 fail, 1 skip).
>
> THE BUILD PROOF: every tools/ entry cross-compiles and links under mingw —
> tycho-ar (-lz), tycho-kvsrv (-lws2_32), tycho-debug (its shim), the
> dispatcher, tychofmt, lsp, and the eight pure tools (q/vm/scheme/kv/chess/
> rsa/sat/build). tycho-fetch is the one skip: its core:http shim needs mingw
> libcurl, which lives in MSYS2.
>
> RUN CHECKS under Wine (vs the lane goldens' shape): tycho-q answered a
> query with the full CSV table; tycho-scheme ran fib; tycho-ar created and
> listed a deterministic archive (names + sizes + sha256s). The tools are not
> just buildable — they WORK.
>
> THE DEBUG_SHIM WINDOWS BRANCH (the phase's coding piece): CreateProcess
> with pipes and CREATE_NEW_CONSOLE (the console isolation that replaces
> setpgid — a terminal Ctrl-C in the tool's console never reaches gdb), the
> child ends inherited, the parent ends _open_osfhandle'd onto the SAME fd
> Line reader dbgx_write/readline already use, SetConsoleCtrlHandler setting
> the same interrupt flag, _fullpath/_getpid, and WaitForSingleObject for
> waitpid. Compile-verified under BOTH mingw and Linux (the POSIX branch is
> untouched — make debug-check still green, 6 legs).
>
> TWO DOCUMENTED GAPS on Windows (both the CI leg's job): (1) no Ctrl-C
> interrupt while the inferior runs — a Windows console handler runs on a new
> thread and a blocked pipe read has no EINTR to wake, so the flag is set but
> unread until the next command; a clean stop-at-line needs
> GenerateConsoleCtrlEvent + overlapped reads. `q` still quits. (2) the
> tool's gdb command line is POSIX-shell-quoted (main.ty's shq); under
> cmd.exe those quotes are literal, so a path with spaces breaks the gdb
> invocation.
>
> FINDINGS RECORDED (not this phase's work): the DISPATCHER cross-builds but
> its shell lines (rm -f, > /dev/null) are cmd-incompatible — it needs
> cmd-compatible forms or MSYS2 sh on PATH (the harness phase). tycho-build
> cross-builds but its LANE's recipes (cat/cp/touch) are POSIX — same
> test-side park class as core:os's (phase 4).
>
> GATES (Linux tree): make debug-check 6 legs green (the POSIX debugger is
> unchanged), make tools builds, doc gates green (0 citations shifted — the
> shim's cited lines are bare ranges). make wine-tools is a manual target.

> **Phase 6 re-measured 2026-08-13 at `7ddfcef8`, and the lanes were the
> problem more than the port.** All five wine lanes rebuilt `tychoc-mingw.exe`
> only `if [ ! -x ]`, so this box had been testing an **Aug 5 compiler**: 25 of
> wine-test's 28 failures were features that postdated it. With that fixed, plus
> the lane learning to link `<pkg>_shim.c` and to honour a sibling `.err` the way
> `tests/run.sh:381` does, and one real port fix (`iox_set_mtime` could not touch
> a DIRECTORY on Windows — `_utime` fails EACCES, so the directory case now uses
> `FILE_FLAG_BACKUP_SEMANTICS` + `SetFileTime`):
>
> | lane | 2026-08-13 |
> |---|---|
> | wine-smoke | all green |
> | wine-test | **passed 347, failed 2** — both float park candidates (`c_float_roundtrip`, `c_float_str_locale`), down from 28 |
> | wine-corelib | 36 ok, 9 skipped, 1 park (`io`: `sync_dir=Unsupported`, which `iox_sync` returns deliberately) |
> | wine-ffi | 10 passed, 0 failed |
> | wine-tools | built+ran 17, failed 0, skipped 1 (tycho-fetch/libcurl) |
>
> The park list is therefore SHORTER than this plan recorded: float text under
> mingw, `sync` on a directory, and the os/datetime/signal test mechanisms. The
> phase-7 gate is unchanged and still needs the box — `make ci` under MSYS2.
>
> Phase 6 evidence (the Linux half) — 2026-08-05: the golden audit is DONE,
> the ffi lane gained wine coverage, and the verdict itself is the CI leg's.
>
> THE GOLDEN DRIFT AUDIT (scanned every tests/*.out, tests/conc/*.out,
> tests/pkg/*.out, corelib/test/*.out, examples/*/expected.out,
> tools/*/expected.out): the goldens are byte-clean for Windows — no CRLF
> drift (the binary-stdio fix of phase 2 made emitted output LF; the one
> golden containing \\r is corelib/test/httpd.out's intentional HTTP \\r\\n in
> raw response lines, and httpd passes byte-identical under Wine anyway), no
> absolute /tmp paths, no platform strings (linux/x86_64/uname), no drive
> letters. The only path-looking golden (corelib/test/path.out) is pure
> string-math output (forward-slash normalization) — platform-stable. The
> phase-6 expectation "goldens platform-identical" is essentially ALREADY
> MET, verified two ways: the wine lanes and this scan.
>
> THE ONE REAL FIND: the FFI fixture's boundary used C `long`, which is
> 32-bit on Windows but 64-bit on Linux — so `ffi_read(null)` returned
> 4294967295 (0xFFFFFFFF, upper bits unspecified) instead of -1 under Wine.
> Fixed: tests/ffi/demo.c's extern-facing `long` -> `int64_t` (tycho_int's
> width). Linux-identical (long == int64_t there; make ffi still green,
> ASan-clean under env -u LD_PRELOAD) and Windows-correct (the wine-ffi
> golden is byte-identical).
>
> THE FFI LANE UNDER WINE (scripts/wine_ffi.sh, 10/10): the golden over a
> mingw-built libffidemo.a (byte-identical), the --shim leg (triple=42), the
> package-scoped extern (tri6=42), the sized-int ABI values (5032704
> 8589934592 -5), and the compiler-side REJECTIONS through the MINGW compiler
> (the four affine handle bans, the shell-injection refusals -- driven with
> -o, because the guard lives at link-line construction which --emit-c skips
> -- and the inout-string out-param ban). The one leg that cannot run here is
> the ASan/UBSan recompile: mingw ASan is experimental -- the CI's job.
>
> THE LINUX-ONLY LANES (skip loudly on Windows, with reasons): locale-check
> (its LD_PRELOAD setlocale constructor is Linux-only), asan-self, ilp32,
> conc's ASan/TSan legs (TSan has no Windows-target support in gcc at all),
> and the ffi lane's ASan leg (above). The editors lanes are node/tree-sitter
> based -- portable, not Linux-only.
>
> THE PARK LIST, consolidated (the phase-6 golden-audit inputs, all recorded
> with their reasons in the phase-4/5 evidence): os/datetime/signal corelib
> tests (POSIX-shell mechanisms and POSIX-TZ sign), tycho-build's lane recipes
> (cat/cp/touch), the dispatcher's cmd-incompatible shell lines (rm -f, >
> /dev/null), and tycho-fetch (mingw curl). Each needs a Windows-native test
> variant in the CI leg.

> Phase 6/7 harness evidence — 2026-08-07 (commit 1ca7e80, recorded here after
> the fact; the run itself was on a real Windows VM: ARM64 Windows 11 running
> the x86-64 MSYS2/mingw toolchain under **Prism emulation**).
>
> THE HARNESS GAPS the plan predicted, all landed as LOUD skips naming their
> reason: ci.sh skips ilp32/asan-self/fuzz; tests/run.sh sets TYCHO_NO_ASAN=1
> on Windows, names binaries `.exe`, gives each fixture its own temp dir,
> retries with backoff, chunks `--one`, and selects a `<golden>.win` when one
> exists (`float_str_locale.out.win` carries rt=-1, the newlocale-less
> rendering phase 3 parked); tests/conc + tests/ffi skip their asan/tsan legs;
> tests/recursion probes for GNU timeout (cmd.exe's is a different program);
> corelib/run.sh retries and skips the signal test. One real harness BUG, not
> a skip: check_goldens.py built patterns with `os.path`, so on Windows every
> resolved path had backslashes and matched none of git's forward-slash paths
> — every golden reported NOT tracked. Fixed with posixpath.
>
> THE RUN, stated as measured, not as a verdict: goldens-check / entrypoints /
> spec-check / docs-fences / check-links / shim-check green; `make test`
> 589/591; `make corelib` 43 ok / 2 fail (datetime's POSIX-TZ sign, time's
> timing bound) / 1 skip (signal); `make conc` 34/38. The 2 + 4 test/conc reds
> are ONE class — the emulator's startup heap-corruption race (0xC0000374 ->
> bash 127), which is why the runner retries.
>
> **The phase-7 gate is therefore NOT met.** Its wording is "the full `make ci`
> on Windows, green". What exists is a run with a short, classified residual
> list on an EMULATED Windows box. Nothing here has been run on non-emulated
> x86-64 Windows, so the emulator class is unseparated from a real port bug by
> measurement — it is separated only by its signature (a startup crash before
> main, at a rate that retries clear). That is the honest state.

> Phase 7 GATE MET — 2026-08-08, on a native x86-64 Windows 11 26200 guest
> (WinBoat/podman over KVM, MSYS2 + mingw-w64 gcc 16.1.0, gdb 17.2). Not the
> emulated ARM64 box of 2026-08-07.
>
> THE GATE: `make ci` exit 0, from a CLEAN CHECKOUT at c4994d6 with an empty
> working tree, 114 skips each printing its reason. Nine sweeps were needed —
> every one died one step further along than the last, which is what the plan
> predicted the long tail would feel like. The order they fell in: `site` (no
> -lasan/-lubsan) -> `fetch` (a file:// URL built from an MSYS $PWD that
> libcurl cannot open) -> `server-check` (HUNG, 43 minutes) -> `selfhost`
> (frozen tychoc0 emits POSIX-only C) -> `build-check` (leg 7's URL, recipe and
> .exe) -> `bench-guard` (peakrss is fork/wait4/getrusage) -> `corelib time`
> (a real contract bug, below) -> green.
>
> SIX PORT DEFECTS, not harness noise, each measured both ways on the box:
>   THE DISPATCHER (tools/tycho.ty) was entirely broken — all five commands
>   died at their first shell-out, each reporting a misleading cause. It also
>   had NO lane: `make debug-check` leg 6 drove `tycho debug` and nothing drove
>   run/build/check/fmt, which is how five commands stayed broken with nothing
>   red. scripts/tools_check.sh gained five legs.
>   TYCHO-DEBUG was dead in all 6 legs while SHIPPING in the release tarball:
>   the same shell-outs, plus _fullpath's backslashes never matching gdb's
>   forward slashes (every stop fell to the glue branch), plus _fullpath
>   succeeding for a file that does not exist so the fail-closed refusal never
>   fired.
>   CORE:TIME broke its documented "at least the requested duration": 199 ms
>   for a 200 ms sleep, 1 in 60 under contention and 0 in 60 idle — invisible
>   outside a full sweep. Now deadline-based against the clock the caller
>   measures with.
>   TYCHO-FETCH's tar line was sh-only, so every fetch failed while blaming the
>   tarball.
>   CHECK_CITATIONS.PY PASSED BY NOT LOOKING — `git ls-files "*.md"` through
>   subprocess yields 8 files on Windows where the shell yields 126, so the
>   gate reported ok over 8 anchored + 6 bare citations instead of 134 + 812.
>   It reported CI GREEN in sweep 8 while checking almost nothing; that is the
>   most dangerous thing found in this whole port.
>   NINE LANES carried POSIX assumptions. The server one HUNG rather than
>   failing — MSYS2's kill terminates a native PE instead of signalling it, so
>   the lane waited forever for a graceful wind-down that cannot happen.
>
> THE PACKAGED TOOLS ARE NOW RUN. Phase 7's Linux half built them and executed
> only tychoc.exe under Wine. Staged the release layout on Windows and drove
> all four: a full build with corelib beside the compiler and no
> TYCHO_CORELIB, a core:strings import out of that corelib, tychofmt.exe
> idempotent, tycho-lsp.exe answering a framed initialize, tycho-debug.exe
> setting and hitting a breakpoint under real gdb. Caveat: these were built
> NATIVELY, not cross-linked, so this tests the packaged shape and the tools'
> behaviour, not the exact bytes `release.sh --mingw` emits.
>
> WHAT IS STILL NOT COVERED, and what "green" does not mean. 114 skips is not
> 114 passes. Not exercised on Windows at all: the sanitizer legs (mingw ships
> no ASan/UBSan runtime; gcc has no Windows-target TSan), the fuzz lanes,
> ilp32, locale-check, asan-self, selfhost (the FROZEN compiler/tychoc0.ty
> emits POSIX-only C at :10688 -- un-freezing it is an owner decision, not a
> port chore), bench-guard (peakrss.c is fork/wait4/getrusage, and a
> wall-clock ratio gate does not survive a VM), core:signal's test,
> server/run.sh's six shutdown cases, and tycho-ar's newline-in-filename
> member. The two BEHAVIOURAL gaps in SECURITY.md are unchanged: non-ASCII
> filenames mojibake through the narrow CRT, and local_offset ignores TZ.
> ONE BOX, ONE TOOLCHAIN: x86-64 mingw64 gcc 16.1.0 in a VM. No UCRT64, no
> native ARM64, no second gcc, no bare metal. The 2026-08-07 ARM64 run had a
> different failure set entirely, which is the standing argument against
> reading one green sweep as a platform guarantee.
>
> SANITIZERS ON WINDOWS, measured 2026-08-08 and NOT wired into a lane. The
> gap that mattered most above -- the _WIN32 branches being the newest code in
> the tree AND the only code with no memory-safety coverage -- is mostly
> closable today. mingw64 gcc has ZERO ASan libraries (`ls /mingw64/lib/libasan*`
> -> nothing, and `gcc -fsanitize=address` fails at `cannot find -lasan`), but
> MSYS2 ships a clang64 toolchain: `pacman -S mingw-w64-clang-x86_64-clang
> mingw-w64-clang-x86_64-compiler-rt` installs clang 22 with a working
> compiler-rt. Against tychoc's --emit-c output, with the lanes' own flags
> (-fsanitize=... -fno-sanitize-recover=all -g -O1 -fwrapv; the -fwrapv is the
> language's overflow contract and omitting it produces false UBSan reports on
> deliberate wrapping):
>   60 plain corpus fixtures, ASan+UBSan -> 60 clean, 0 findings.
>   the 13-program conc suite, UBSan only  -> 13 clean, 0 findings.
> THE LIMIT: ASan does not work on threaded programs with this toolchain.
> tests/conc/basic under ASan exits 139 with an EMPTY stderr -- no report, so
> it is ASan's own machinery failing, not a finding; the same program is
> correct under no sanitizer, under UBSan alone, and under mingw gcc. Isolated
> by building it four ways. So the channel/allocator paths -- exactly where
> 07c3e4b's heap corruption lived -- have UBSan coverage only, and the bug
> class that actually bit this port is still the one ASan would catch.
> Also unproven: clang64 is not the shipping toolchain, so a fault specific to
> mingw gcc's codegen would not appear here. Wiring this into `make ci` was
> considered and NOT done: it makes clang64 a prerequisite of the Windows CI
> box, which is the owner's call, and the run above is reproducible by hand.

> Phase 7 close (the Linux half) — 2026-08-07.
>
> RELEASE. `scripts/release.sh --mingw` shipped the compiler and corelib only,
> with a comment deferring the tools to "when they cross-compile" — phase 5
> proved they do. It now cross-builds `tychofmt.exe`, `tycho-lsp.exe` and
> `tycho-debug.exe` too (parity with the native leg, which ships the same three
> beside tychoc and no dispatcher). Method: the NATIVE compiler emits the C
> (target-neutral, its `_WIN32` branches are compile-time) and mingw links it,
> with `--print-shims` supplying the corelib shims and the explicit `--shim`
> file appended by hand — it is not in that list, the same trap wine_tools.sh
> documents. Then a Wine smoke test of the STAGED layout: the packaged
> tychoc.exe must find `corelib/` beside itself with no TYCHO_CORELIB and emit
> C (it stops at --emit-c because there is no Windows `cc` under Wine); it
> skips loudly, saying the tarball is unrun, when wine64 is absent.
>
> RAN, both legs, on this Linux box: `sh scripts/release.sh v1.0.0 --mingw` ->
> compiler + 3 tools built, Wine smoke test ok, tarball + sha256 written; and
> `sh scripts/release.sh v1.0.0` (the native leg, to prove it was not
> disturbed) -> its own smoke test ok. NOT run: the tools' Windows *behaviour*
> — they are built and packaged, and only tychoc.exe was executed.
>
> DOCS. README's platform note flips from "No native Windows, but WSL is fine"
> to the two supported paths (WSL2 zero-setup, native MSYS2 + mingw-w64, MSVC
> out of scope) with the loud-skip lane list and pointers to CHANGELOG (the
> run) and SECURITY.md (the gaps). SECURITY.md's sharp-edge list gains the
> surviving Windows behavioural gaps: signal's version-dependent accept-wake,
> datetime's mirrored POSIX-TZ sign, the newlocale-less float(str) path,
> core:os through cmd.exe, tycho-debug's Ctrl-C. corelib.md's 1.0 API surface
> says the surface and every signature are identical on Windows and names the
> three packages whose BEHAVIOUR differs. plan.md's "native Windows (deferred,
> WSL suffices)" is struck in both places it appears, dated. CHANGELOG gains an
> [Unreleased] section carrying the port, the recorded run with its residuals,
> the release change, and the two post-1.0 compiler commits (the pattern-discard
> fix and the branch-type diagnostic), which had never been recorded.
>
> NOT DONE, and it is the phase's own gate: the full `make ci` on Windows,
> green. It needs the box, not this one. The park list above is unchanged —
> os/datetime/signal test variants, tycho-build's lane recipes, the
> dispatcher's cmd-incompatible shell lines, tycho-fetch's mingw curl.
>
> **Status 2026-08-15.** Still not done and still hardware-bound — it needs a
> Windows box, which is the same class of blocker as ROADMAP §1 and §7 needing
> people, not a task anyone can pick up here. What HAS been built since that line
> was written is a wine substitute deep enough to be worth naming, because "no
> native CI" now understates the coverage badly:
>
> | lane | what it runs under wine |
> |---|---|
> | `scripts/wine_test.sh` | the plain fixture corpus, cross-compiled (phase 3) |
> | `scripts/wine_corelib.sh` | every corelib test that links on this box (phase 4) |
> | `scripts/wine_tools.sh` | every `tools/` tool cross-compiled (phase 5) |
> | `scripts/wine_ffi.sh` | the FFI lane's runnable legs (phase 6) |
> | `scripts/wine_smoke.sh` | the cross-compiled smoke |
> | `make wine-ubsan` | the same corpus with UB TRAPPING — 361 fixtures, 0 failures |
>
> **What was re-run on 2026-08-15, and what was not.** `wine_smoke.sh` is green
> here today (cross-compiled programs run under wine, `prog_ok_big` printed
> **all six were re-run and all six are green**, so the table above is a
> statement about today rather than about the day each landed:
>
> | lane | 2026-08-15 |
> |---|---|
> | `wine_smoke.sh` | green (`prog_ok_big` printed 500501) |
> | `wine_test.sh` | passed 361, failed 0 |
> | `wine_corelib.sh` | passed 37, failed 0, skipped 9 |
> | `wine_tools.sh` | built+ran 17, failed 0, skipped 1 |
> | `wine_ffi.sh` | passed 12, failed 0 |
> | `make wine-ubsan` | green over the fixture corpus AND every corelib package that ports, with its control firing: the same source exits 0 plain and **29** with the trap flags |
>
> The skips are the parked list, not failures: 9 corelib packages and 1 tool whose
> dependencies do not cross to mingw. `wine-ubsan`'s control is the leg that
> matters most — without it, "0 failures" is also what a sweep reports when the
> trap never arms.
>
> And 0.7.0 ships a mingw tarball that was verified under wine before publishing:
> `tychoc.exe` reports its version and refuses all 51 `affine_*`/`generic_*`
> reject fixtures. What wine still cannot give is a native `make ci` and anything
> only ASan would catch — mingw-w64 here ships neither libasan nor libubsan, so
> use-after-free on Windows stays invisible. That limit is recorded in
> `docs/internals/audit-brief.md` §3 as well.
>
> GATES (this box): doc gates (check_citations.py, check_links.sh) green; both
> release legs ran. `make test` was NOT run and cannot redden — no compiler,
> runtime, corelib or fixture changed; the diff is Markdown plus the mingw
> branch of a release script that no gate calls.
