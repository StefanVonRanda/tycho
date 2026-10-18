<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-<version>-*.tar.gz dist/tycho-<version>-*.sha256 --notes-file RELEASE_NOTES.md
  gh release edit <version> --prerelease
Build one tarball per platform (there is no hosted CI); attach them all to the release.
Name the version in the globs: dist/ still holds the previous release's tarballs.
-->

Tycho 0.8.7 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**This is a correctness release: it refuses programs 0.8.6 accepted, and
closes the 90-finding defect hunt of 2026-09-25.** Every refusal names the
rule it enforces, and almost all of them are programs that were already wrong:
they wrapped a value silently, emitted C that did not build, or read memory
they should not have. There are no language features and no surface removals;
`surface.lock` moved by addition only (seven corelib functions joined it).

## Programs now refused

- **An out-of-range literal wherever it adapts.** `x + 300` at a `u8`, a `300`
  passed to a variadic or UFCS parameter, a lambda body, a `m.get` default, a
  subscript assignment, a value `match` arm, a `send`, or the broadcast scalar
  of a sized-int array wrapped silently and printed the wrapped value. Every
  site now says `300 does not fit u8 (its range is 0..255)`.
- **An empty literal `[]` for a `[N]T` or `vector[N]T`.** The reference
  compiler emitted an uninitialised C aggregate; the self-hosted one built a
  length-0 "fixed" array. Now: `a fixed-size array of length 2 needs 2
  elements, got 0`.
- **A `[N]T` whose `N` is an `int` const losing its length** in the
  self-hosted compiler: wrong-arity literals, `push` onto it, zero or negative
  `N`, and mismatched fixed-array arguments all compiled and ran with the
  wrong `len`.
- **An f-string hole holding more than one expression.** `f"{price:.2f}"`
  parsed the first expression, discarded the rest, and printed the bare value.
- **The identifier prefix `Ztycho_`.** Compiler temporaries shared the user
  namespace, so a local named `_fc0` was silently replaced by the compiler's
  own and the program printed a wrong answer. The prefix is reserved now.
- **A handle `free:` destructor returning an aggregate**, which passed the
  check and failed later inside `cc`.
- **An empty `-o ""` or a trailing bare `-L`/`-I` on the compiler command
  line.**

## Fixed

- **The defect hunt is closed: 90 findings, all fixed.** Eleven finder agents
  on one surface each, every finding handed to a verifier told to refute it.
  The ledger is
  [`docs/internals/audit-2026-09-25.md`](docs/internals/audit-2026-09-25.md).
  Beyond the refusals above, the compiler half:
  - **Division by zero traps cleanly** in the self-hosted compiler instead of
    SIGFPE/SIGILL, on every sized integer and element-wise array arithmetic.
  - **An `if`/`elif` chain and a long `+` chain emit flat.** The self-hosted
    compiler needed 5.3 GB for 800 arms and died at 1250; the reference
    compiler segfaulted near 7000 arms and clang refuses 256 bracket levels.
    A 1400-term sum now emits at depth 4 from both compilers.
  - **Compile time on nested calls is no longer exponential**: nested generic
    method chains, 900-struct `program_in` copies, one long f-string, lambdas
    among many locals, and the reference front end's quadratic lookup.
  - **A constant array literal is a static table plus `memcpy`**, not one
    store per element — a 15,625-element table took gcc 21 s.
  - **Enum variants named `EOF`, `NULL`, `default` or `while` build**, under
    prefixed C names.
  - **A `packed` field passed as `inout` goes through an aligned local** in
    both emitters, and no longer draws a `-Wunused-value` warning in the
    user's build.
  - **Composite float map keys hash `+0.0` and `-0.0` alike**; a map held two
    `==`-equal keys.
  - **The reference compiler's heap use-after-free on nested generic
    instantiation is gone**, with `read_file` on pipes, the error-recovery
    depth counter, `__` type names, `clamp` naming, `--symbols` on invalid
    programs, and cross-package suggestions fixed alongside.
- **Appending through an `inout string` in a helper no longer falls back to
  quadratic concatenation.** Both emitters pass a per-binding append state
  that grows in the owner's arena (`tests/inout_append.sh` bounds the growth).
  The self-hosted compiler's redundant AST staging is gone with it.
- **Size arithmetic on untrusted counts is checked before it allocates**:
  `reserve` on composite arrays and `channel(T, cap)` with a huge capacity
  could wrap to a tiny allocation and write past it. Both abort by name now.
- **Overwriting a heap value in a long-lived map reclaims the old value.**
  `tycho-kvsrv` grew ~1 KB of RSS per `PUT` to the same key without limit.
- **Enum payloads recycle at their exact size**, not `sizeof(*v)`, in both
  emitters.
- **Negative literals adapt like positive ones** in the self-hosted compiler
  (`x: f32 = -0.5` compiled under one compiler and not the other); float
  `to_u8`..`to_i64` are total; a `u64` shift past 2^63 is zero per spec.
- **The shipped compiler calls itself `tychoc`** instead of `tychoc1`, says
  `cannot open <path>` for a missing source, and writes `--emit-c` output to
  stdout with no `-o`.
- **`core:tls` verifies the hostname with `X509_VERIFY_PARAM_set1_host`**
  rather than the deprecated call, and `core:datetime` bounds the libc TZ
  fallback on macOS.

## Install

Download the tarball for your platform below, verify it, and unpack it:

```
tar xzf tycho-<version>-<os>-<arch>.tar.gz
cd tycho-<version>-<os>-<arch>
./tychoc examples/hello.ty && ./examples/hello
```

`tychoc` writes the binary beside its source unless you pass `-o`, so the
program built from `examples/hello.ty` is `examples/hello`.

The core library ships inside the tarball, beside the compiler, so there is
nothing to configure. You still need a C compiler (`cc`) on your `PATH`, because
Tycho transpiles to C: gcc, or clang 15 or newer. On macOS that is the Xcode
command line tools. Each tarball's SHA-256 is published alongside it.

## What this release does not have

There has been **no third-party security review**, and the FFI
(foreign-function interface, `extern fn`) boundary is
unsafe by design. See [SECURITY.md](SECURITY.md), and
[docs/internals/audit-brief.md](docs/internals/audit-brief.md) if you are
willing to be one.

Two things are unverified, and you may hit them:

- **This Windows archive has been run under Wine, not on Windows.** In a
  `debian:trixie` container, the staged `tychoc.exe` started under Wine and
  emitted C for a program using `core:strings` found beside the binary. That
  is the whole Windows verification: the 23-leg content gate ran over the
  native Linux archive only. The toolchain itself has passed its suite on real
  Windows guests, 289/289 on both windows-x86_64 and windows-arm64
  (`make platform-check`), but that was an earlier build, not this tarball.
- **Server shutdown on Windows has never been observed on Windows.** The code
  reads sound: a worker re-reads the shutdown flag at least ten times a second,
  and the console handler shuts down the listener and every registered
  connection. What is untested is whether `WSAPoll` returns early on a socket
  that has been shut down, and whether a blocked `accept` is released. Expect
  wind-down within the 100 ms poll tick. Treat anything faster as unproven.
- **There is no `darwin-arm64` archive this time.** The 0.8.6 Mac archive
  stands; this release attaches Linux and Windows only, both built from the
  tagged tree. A Mac build needs a Mac.

## Status

0.8.7 is pre-1.0 and there are **no stability guarantees**: anything here may
change. [ROADMAP.md](ROADMAP.md#what-10-requires) lists what 1.0 requires, and
the blocking item is not engineering. Nobody outside this repo has written a
real program in Tycho yet. If you write one, the friction you hit is the most
useful thing you can send back.
