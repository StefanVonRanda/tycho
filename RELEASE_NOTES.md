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

Tycho 0.8.6 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**This release refuses programs 0.8.5 accepted, and removes 136 functions from
the public corelib surface.** The list is below under *Programs now refused*.
Every refusal names the rule it enforces. Most of them are programs that were
already wrong: they failed later inside `cc`, leaked or double-freed a resource,
or read freed memory. Three are plain API changes: `or_else` is now a keyword,
the corelib helpers are private, and `io.open_lines` returns a `Result`.

## macOS on Apple silicon

**A `darwin-arm64` archive is attached for the first time.** Both compilers
build native Mach-O arm64 with no source change. The full `make ci` gate has
been green on macOS arm64 since 2026-09-19, and this archive is built twice and
compared byte for byte (`make release-check`), then extracted and checked
(`make release-content`).

Running the gate on macOS for the first time found defects that were not
specific to macOS. It was simply the first platform to show them:

- **Every Tycho server refused connections under a burst on macOS.**
  `net.listen` asked for a backlog of 16. When the accept queue is full, Linux
  drops the SYN and the client retries, while macOS and the BSDs reset the
  connection. The backlog is `SOMAXCONN` now.
- **A server could hang on shutdown**, about one run in eight, because a worker
  blocked in `accept()` never saw SIGTERM.
- **Floating point could round differently on ARM.** The emitted C left FMA
  contraction to the C compiler. It fuses `a*b + c` on aarch64 and cannot on
  baseline x86-64, so the same program printed `0.30000000000000006` on one and
  `...004` on the other. Contraction is off now, so a program rounds as its
  source says on every host.

The binaries are **not notarized** by Apple. If macOS refuses to start them
after a browser download, clear the quarantine flag on the unpacked directory
with `xattr -dr com.apple.quarantine tycho-v0.8.6-darwin-arm64`.

## New in the language

- **`or_else`: an early return that maps the error.**

  ```text
  cfg := load(path) or_else e: Err(ConfigErr(e))
  ok := parse(s) or_else _: false
  ```

  On `Ok`/`Some` the expression is the payload, as with `or_return`. On `Err(e)`
  the function **returns** the handler. The handler has the enclosing
  function's return type, which need not be a `Result`. It replaces the
  four-line `match` that unwraps a value or returns a wrapped error. It is
  refused in a function that returns nothing, because there is no value to
  return; use `or_return` or a `match` there. Spec §14.6.1. The tree's own code
  moved to it at 79 call sites.
- **`size_of$(T)` accepts an `align(N)` struct**, so `align` can be observed
  from inside the language. The size is implementation-defined and is
  guaranteed to be a multiple of `N`.
- **`math.min`, `math.max` and `math.clamp` work lane by lane on a
  `vector[N]T`**, as one compare and blend.

The layout and SIMD surface itself (`vector[N]T`, `align(N)`, `packed`,
swizzling) shipped in 0.8.5. This release adds its first in-tree user,
`tools/tycho-grade`.

## Programs now refused

- **`or_else` as an identifier.** It is a keyword.
- **A call to a corelib internal helper.** 136 functions such as
  `json.parse_value` and `bignum.mag_cmp` were public only because their names
  lacked a leading underscore. They are package-private now, and the error
  names the private spelling. Every function found being called from outside its
  package stayed public. The corelib surface is 423 functions, down from 559.
- **`io.open_lines` returning a nullable `ptr`.** It returns
  `Result(LineReader, IoErr)`. The null check becomes the `Err` arm.
  `CHANGELOG.md` shows the migration.
- **`reserve` or `pop` on a fixed-size `[N]T` or `vector[N]T`.** The reference
  compiler emitted C that did not build. The shipped compiler accepted `pop` on
  a `[4]int` and silently shortened it.
- **Four misuses of a `handle`:**
  - a use after a `close(h)` that closes it on every path;
  - `close(h)` on a handle parameter, which freed the resource twice;
  - an opener whose result is not bound directly to a variable, which leaked it
    for the life of the process;
  - a `free:` that does not name an `extern fn` taking that handle type.
- **An import one file declares and only a sibling file uses.** It is reported
  as unused in the file that declares it.

## Fixed

- **The shipped compiler returned freed memory** in two shapes. An
  `or_return`/`or_else` payload returned straight to the caller was built in an
  arena the return had just freed. `to_bytes(local)` kept inside a returned
  value pointed into the freed scope; in `core:httpd` this corrupted a request
  body.
- **The shipped compiler crashed on `x[a:b]` over a struct or enum**, with an
  internal message and no line. It is a type error now.
- **A local `bounded[N]T` grew past its capacity.** It now traps on the push
  that overflows it.
- **`core:tls` did not guard an interior NUL in the host name.** A string like
  `"good.example.com\0anything"` connected to, announced and verified
  `good.example.com`, while the caller's own checks saw the whole string. It
  fails closed now.
- **`core:os` refuses to spawn a `.bat` or `.cmd` on Windows.** `cmd.exe`
  re-parses the command line and defeats the argument quoting. A PATH-resolved
  name with no extension is not caught.
- **Subnormal float literals became `0.0` on aarch64 Windows**, through mingw's
  `printf`.
- **The vector-width lint gave x86-64 advice on ARM**, a flag `cc` then refused.
  It now suggests a narrower vector first.
- **A generic instantiation error names the whole chain of calls**, starting
  from the outermost one, which is the line you own.
- Every build importing `core:strings` drew two `-Wunused-value` warnings from
  the reference compiler.
- The build failed to link on Fedora with gcc 16: `-static-pie` needs `-fPIE`
  beside it.

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

There has been **no third-party security review**, and the FFI boundary is
unsafe by design. See [SECURITY.md](SECURITY.md), and
[docs/internals/audit-brief.md](docs/internals/audit-brief.md) if you are
willing to be one.

Two things are unverified rather than known, because you may hit them:

- **This Windows archive has been run under Wine, not on Windows.** Its
  content gate passed under Wine 10 on x86-64 Linux: every `.exe` starts, and a
  real program is emitted, linked and run from a foreign directory. The
  toolchain itself has passed its suite on real Windows guests, 289/289 on both
  windows-x86_64 and windows-arm64 (`make platform-check`), but that was an
  earlier build, not this tarball.
- **Server shutdown on Windows has never been observed on Windows.** The code
  reads sound: a worker re-reads the shutdown flag at least ten times a second,
  and the console handler shuts down the listener and every registered
  connection. What is untested is whether `WSAPoll` returns early on a socket
  that has been shut down, and whether a blocked `accept` is released. Expect
  wind-down within the 100 ms poll tick. Treat anything faster as unproven.

## Status

0.8.6 is pre-1.0 and there are **no stability guarantees**: anything here may
change. [ROADMAP.md](ROADMAP.md#what-10-requires) lists what 1.0 requires, and
the blocking item is not engineering. Nobody outside this repo has written a
real program in Tycho yet. If you write one, the friction you hit is the most
useful thing you can send back.
