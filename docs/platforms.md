# Platforms and toolchains, in detail

The [README](../README.md#building) says which platforms are gated and names
the three caveats you can hit in your first hour. This page is the rest: which
lanes skip on which host, why each one skips, and which C compilers have been
measured against the corpus. It is written for someone porting, packaging or
debugging a red gate — not for someone deciding whether to try the language.

Every skip named here prints its own reason at gate time. An unenumerated skip
is a gate failure, not a quiet pass ([controls](controls.md#the-practice)).

## C toolchains

**gcc, or clang 15 or newer.** The floor is not a guess; it is where a measured
miscompile stops.

| toolchain | measured | result |
|---|---|---|
| clang 14 | Ubuntu 22.04 | **not supported** — builds the compiler, then miscompiles what it emits: eight fixtures come back with empty strings where a string literal was expected |
| clang 15+ | the floor this repo states | supported |
| clang 19 | whole fixture corpus, against gcc | agrees on every fixture |
| clang 22.1.8 | 2026-09-11, whole corpus | builds `tychoc`, bootstraps `tychoc1` through both stages, corpus **1039 / 0** — each fixture built twice, native and under `-fsanitize=address,undefined`, byte-identical output required between them |
| gcc (C11) | the development default | supported |

Until the 2026-09-11 run, the newest clang anyone had measured was three major
versions old, so "clang 15 or newer" rested on an untested assumption at the top
of its range. It no longer does.

MSVC is not a supported C target and is not planned. Native Windows means MSYS2
+ mingw-w64.

## macOS / Apple Silicon

Gated since 2026-09-19: `make ci` is green on `darwin-arm64`, 267–390s across
repeated runs — the spread is FRICTION-91. Setup is `xcode-select --install`.

It carries **12 skips over 7 causes**, each printing its reason: gdb,
`-Wl,--wrap`, the glibc symbol floor, `ilp32`, `resource.prlimit`, an x86-64
`--target` leg, and LeakSanitizer.

**Apple's AddressSanitizer ships no LeakSanitizer**, so the `fuzz-leak` lane and
the raytrace and mandelbrot leak legs do not run on macOS. ASan, UBSan and TSan
all do. Linux is the only host that scores leaks, which is why a leak regression
must be caught there.

## Linux

**x86-64** is the development host and the reference platform: the full fixture
suite under ASan/UBSan/LeakSanitizer, plus every gate lane.

**arm64** is gated since 2026-09-19 — `make test` is 1065/1065 on Ubuntu 26.04 /
aarch64, run in a VM on the Apple Silicon box at native speed. No Graviton
artifact is built yet.

### The comma-decimal locale (FRICTION-111)

`tests/float_lit_locale` and `float_str_locale` prove that float formatting does
not move when the C locale does, which needs a locale whose decimal point is not
`.`:

```sh
sudo locale-gen da_DK.UTF-8      # or de_DE.UTF-8 / fr_FR.UTF-8
```

A stock container or CI image ships only `C`/`POSIX`/`en_US`, so without one
both fixtures fail with `hostile=1 expected, got 0` — the fixture refusing to
pass while proving nothing, which is the correct behaviour for a control that
cannot discriminate. The cure is the line above.

### Optional corelib dev libraries

The FFI-backed corelib packages need their dev libraries, or `make shim-warn`
refuses — it compiles 9 shims, wants 10, and will not read an empty warning file
as a pass:

```sh
sudo apt-get install zlib1g-dev libssl-dev libcurl4-openssl-dev \
                     libpng-dev libsqlite3-dev pkg-config
```

## Windows

Two supported paths. **WSL2** is the zero-setup one and behaves exactly like
Linux. **Native Windows is MSYS2 + mingw-w64**, and `make ci` is green there on
x86-64 Windows 11 under mingw-w64 gcc.

Read that green with its scope. It is **one box, one toolchain**, and it carries
**49 Windows-specific skips**, each printing its reason:

- the sanitizer lanes — mingw ships no ASan/UBSan runtime, and gcc has no TSan
  for a Windows target at all
- the fuzzer
- the 32-bit lane
- the `LD_PRELOAD` locale lane
- the POSIX-only emitted C
- the perf gate
- every lane that needs a POSIX signal delivered to a process — `core:signal`'s
  test and the HTTP server's six shutdown cases — because MSYS2's `kill`
  terminates a native Windows program instead of signalling it

**Green there means nothing reddened, not that everything ran.**

### One measured behavioural difference

A thread parked in `recv` on an accepted connection is not released by the
shutdown handler as it is on Linux, so a Windows server winds down within its
idle timeout rather than within a millisecond. Nothing is lost or corrupted —
the wind-down is slower, and only that. This is a documented platform limit, not
a bug awaiting a fix; [SECURITY.md](../SECURITY.md) carries the measurement.

### The sanitizer hole, and what closed most of it

For a long time the Windows-only code paths were the newest code in the tree and
the only code with no memory-safety checking at all. Installing MSYS2's
**clang64** toolchain closes most of that — 60 corpus fixtures under ASan+UBSan
and the 13-program concurrency suite under UBSan all come back clean — but ASan
itself does not work on threaded programs with that toolchain, so the channel and
allocator paths have UBSan coverage only.

That is not parity with Linux, where everything runs under ASan/UBSan/TSan and a
fuzz campaign. The behavioural gaps that survive the port — non-ASCII filenames,
`TZ` handling, `core:os` through `cmd.exe`, the debugger's Ctrl-C — are listed in
[SECURITY.md](../SECURITY.md).

**Windows arm64** is a mingw target, executed by `make platform-check`. A
subnormal-literal defect there was a toolchain bug that lane found, and it is
fixed.

## Release artifacts

Release tarballs are built per platform by `scripts/release.sh` (`--mingw`
cross-builds the Windows one). `make release-check` builds and smoke-tests the
current-version tarball twice and requires byte-identical archives.

See also: [SUPPORT.md](../SUPPORT.md) for which platforms carry a *promise* ·
[controls](controls.md) for why every skip is enumerated ·
[the friction log](internals/FRICTION.md) for FRICTION-91 and FRICTION-111.
