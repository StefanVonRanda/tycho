# Native Windows support — design record

The port is done: `make ci` runs natively under MSYS2 + mingw-w64 (see
README's platform table). This file records the design decisions. Test
runners, `scripts/ci.sh` and `examples/*/run.sh` cite "windows-port.md phase N"
as the reason they skip a leg on Windows, so the phase headings below must keep
their numbers.

**Scope.** In: the compiler, runtime, corelib, tools and harness built with
**MSYS2/mingw-w64 gcc**, the same `cc` the transpiler already invokes. Out:
MSVC as the C target (`__thread`, the cc line `-pthread -fwrapv -O3 -lm`, the
runtime's POSIX surface, no pkg-config, and every shell runner would need real
work — 3-4x the cost for no distribution win), and any language-surface
change. WSL2 stays a first-class path.

The surviving behavioural gaps are listed in [SECURITY.md](../../SECURITY.md).

## Phases

### Phase 1 — Toolchain: the compiler builds under mingw

- mingw-w64 has **no POSIX `newlocale`/`uselocale`/`locale_t` at any version**
  (only MSVC-style `_locale_t`). The localeconv-based fallback legs absorb it,
  so this is a guard, not a shim; the runtime's float-roundtrip test hook
  returns -1 (untestable) on Windows.
- winpthreads is a **dynamic** dll (`libwinpthread-1.dll`): present in MSYS2,
  absent on a bare Windows box.
- `scripts/release.sh --mingw` cross-builds the `mingw64` tarball: the native
  compiler emits the target-neutral C (its `_WIN32` branches are compile-time)
  and mingw links it, with `--print-shims` supplying the corelib shims.
  The Wine lanes source `scripts/wine_compiler.sh`, which rebuilds the mingw
  compiler whenever `compiler/` is newer than it.

### Phase 2 — Runtime port

- **Stack-overflow guard.** POSIX uses sigaltstack + SIGSEGV + SP-vs-bounds.
  Windows uses a vectored handler (`runtime/tycho_rt.c@AddVectoredExceptionHandler`)
  that catches the distinct `EXCEPTION_STACK_OVERFLOW` code — no bounds
  recording, no SP comparison — prints the same one-line message and exits 1;
  anything else returns `EXCEPTION_CONTINUE_SEARCH` and crashes normally. It
  must be ported, not skipped: deep recursion failing closed is a stated
  security posture.
- **Binary stdout/stderr.** The CRT's text mode turns `\n` into `\r\n`, so no
  golden would match. The runtime's constructor sets stdout/stderr to
  `_O_BINARY`; stdin stays text mode so CRLF input reads as LF, matching POSIX.
  The compiler is a Tycho program, so it inherits the same fix.
- `aligned_alloc` is missing: `ty_aligned_alloc64` wraps malloc + align + stash.
- **Sanitizers.** mingw gcc ships no `-lasan`/`-lubsan`, and gcc has no
  Windows-target ThreadSanitizer at all. Every ASan/UBSan/TSan leg skips loudly
  on Windows (`tests/run.sh` forces `TYCHO_NO_ASAN=1`); use-after-free on
  Windows is invisible to the suite.

### Phase 3 — Emitted-C toolchain

- `--print-shims`/`--print-deps` output is identical between the Linux and
  mingw compilers (pure compiler-side).
- `-g`: the emitted `#line` directives map into the DWARF line table under
  mingw gdb.
- A fixture whose output legitimately differs on Windows carries a
  `<golden>.win` sibling, selected by `tests/run.sh` (for example
  `tests/float_str_locale.out.win`, which records the newlocale-less `rt=-1`).
  Fix drift at the runner (normalize before compare) before re-recording per
  platform.

### Phase 4 — Corelib shims

- The `deps` mechanism has a `_WIN32:` section (`compiler/driver/driver.ty`):
  raw linker flags for Windows builds, appended verbatim, never pkg-config'd,
  never listed by `--print-deps`, skipped by a non-Windows compiler. It is what
  links `core:net` (`-lws2_32`) and `core:regex` (`-lpcre2-posix`).
- `core:io`: `getline`/`pread`/`pwrite` shims. Windows `fopen`/`open` of a
  directory fails with `EACCES`, not `EISDIR`, so the error classifier
  re-checks by `stat`; directory removal dispatches `_rmdir`; touching a
  directory's mtime uses `FILE_FLAG_BACKUP_SEMANTICS` + `SetFileTime`.
- `core:regex`: `<pcre2posix.h>` under `_WIN32`, a POSIX-signature drop-in.
- `core:signal`: `SetConsoleCtrlHandler` + `shutdown`/`closesocket` on the
  listener. Whether it wakes a blocked `accept` is Windows-version dependent;
  the flag is set either way.
- `core:datetime`: `localtime_s`/`gmtime_s`, `_putenv`, and a portable
  mktime-difference in place of glibc's `tm_gmtoff`. Windows `_tzset` reads a
  signed POSIX TZ string with the opposite sign.
- An FFI boundary typed `long` is 32-bit on Windows: extern-facing C uses
  `int64_t` (`tycho_int`'s width).

### Phase 5 — Tools

- Every `tools/` entry cross-builds; tools that shell out need MSYS2's `sh`.
- `tycho-debug`'s Windows branch (`tools/tycho-debug/debug_shim.c`):
  `CreateProcess` with pipes and `CREATE_NEW_CONSOLE` replaces `setpgid`
  isolation. Gap: no Ctrl-C interrupt while the inferior runs (the console
  handler runs on a new thread and a blocked pipe read has no EINTR); `q`
  still quits.

### Phase 6 — Harness and goldens

- Goldens are LF and path-free; the one `\r` golden is
  `corelib/test/httpd.out`'s intentional HTTP `\r\n`.
- `scripts/check_goldens.py` builds paths with `posixpath`; `os.path` gave
  backslashes that matched none of git's paths.
- Linux-only lanes skip loudly on Windows: locale-check (`LD_PRELOAD`),
  asan-self, ilp32, the fuzz differentials, and every sanitizer leg (phase 2).

The Wine approximation, runnable from Linux (manual targets, not gates):

| target | runs |
|---|---|
| `make wine-smoke` | conc suite, clock, floats, list_dir, and the stack guard on main thread and spawned task |
| `make wine-test` | the plain fixture corpus, cross-compiled |
| `make wine-corelib` | every corelib test that links on the box |
| `make wine-tools` | every tool cross-built, the deterministic ones run |
| `make wine-ffi` | the FFI lane's runnable legs |
| `make wine-ubsan` | the corpus with UB trapping, plus a control that must trap |

Wine is evidence of the port, never a Windows verdict.

### Phase 7 — Release and documentation

`scripts/release.sh --mingw` ships `tychoc.exe` with `tychofmt.exe`,
`tycho-lsp.exe` and `tycho-debug.exe`, and smoke-tests the staged layout under
Wine (the packaged compiler must find `corelib/` beside itself). The user-facing
Windows statement lives in README; the gaps in SECURITY.md.
