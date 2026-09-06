<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-*.tar.gz dist/*.sha256 --notes-file RELEASE_NOTES.md
  gh release edit <version> --prerelease
Build one tarball per platform (there is no hosted CI); attach them all to the release.
-->

Tycho 0.8.5 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**This release refuses programs 0.8.0 accepted.** Not a change of intent — those
programs were always invalid, and the shipped compiler failed to say so. A
survey of the C bootstrap found 495 user-facing rules, 262 of which no test in
the tree reached; the self-hosted compiler was missing a share of the rules it
had never been asked about. Around 270 rejection fixtures were written and the
gaps behind them closed. If your program stops compiling, the diagnostic names
the rule it broke. The two compilers now agree on every accept/reject verdict
across the whole corpus, and on which rule fired; the exact message text still
differs on 14 of 593 rejection fixtures.

Measured, not assumed: the 0.8.0 compiler was rebuilt and run beside this one
over the whole accept corpus — 420 programs, all 420 of which 0.8.0 compiled,
and all 420 still compile. Outside that corpus, three patterns 0.8.0 accepted
are now refused, and 0.8.0 did not merely accept them — it built and ran them:
`push` on a fixed-size array wrote past its storage, a slice of one read
storage that has no backing pointer, and an out-of-range `u32` literal was
truncated to `0` in silence. A constant division overflow crashed the 0.8.0
compiler outright; it is a diagnostic now.

## Windows finally works

**Every executable in the 0.8.0 Windows archive fails to start.** All four were
linked `-pthread` without `-static` and import a `libwinpthread-1.dll` the
archive does not carry. That archive is unusable and always was; this release is
the fix. The Windows tarball also now contains the **self-hosted** compiler
rather than the C bootstrap, which it shipped through 0.8.0 — worth about 3x
on a tree-walking benchmark. And `tychoc.exe` can be run from any directory: the
lookup for its own corelib split paths on `/` only, so with tycho on `PATH`
every build outside the archive directory failed.

The archive's contents are now gated, not just its bytes: it is extracted and
read on every check — the right compiler, the runtime present, no missing DLL
imports, every `.exe` starting, and a real program compiled and run from a
foreign directory.

**All of that ran under Wine on Linux, not on Windows.** No binary in this
release has been started on a real Windows kernel by anyone here.

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
nothing to configure. You still need a C compiler (`cc`) on your `PATH` — Tycho
transpiles to C. Each tarball's SHA-256 is published alongside it.

## New in the language

- **`vector[N]T`** — a fixed array whose arithmetic is one machine instruction.
  A power-of-two count from 2 to 64, `int`/`float`/`f32` elements. In 0.8.0 this
  type existed in the documentation and not in the compiler that shipped: the
  self-hosted compiler lowered it to a dynamic array and emitted no vector
  instructions at all. It emits real ones now.
- **`--target <level>`** — raise the x86-64 baseline: `x86-64-v2`, `-v3`, `-v4`.
  Worth knowing, because plain x86-64 is SSE2 and a register is 16 bytes, so a
  32-byte `vector[4]float` is split in half in every operation and can be slower
  than the plain array it replaced. The compiler warns when that happens and
  says which flag fixes it. Without the flag nothing changes: an unflagged build
  is byte-identical to one made before the flag existed.
- **`align(N)` and `packed`** — state a struct's layout. `packed` is byte-exact
  with no padding; `align(N)` raises alignment, capped at 8 because that is what
  the arena guarantees. A request the allocator cannot honour is refused at
  compile time rather than rounded down in silence.
- **Simultaneous assignment** — `(x, y) = (y, x)`. Targets are places, so a
  field, an array element or a map value may stand on the left, and every
  right-hand side is evaluated before any target is written. `(a, b) = (b, a+b)`
  is one Fibonacci step.
- **Swizzling** — `v.(x, y)` is a tuple of two components of one value, and it
  reads, binds and assigns. Fixed arrays and vectors of four lanes or fewer name
  them `.x .y .z .w`, or `.r .g .b .a` for the same four, wherever a field
  access is accepted.
- **A byte bridge** — `to_bytes`, `from_bytes$(T)` and `size_of$(T)` over a
  packed struct, little-endian on every host.

## Fixed

- **The example server starved under load.** A worker served one connection
  start to finish, so 64 idle peers delayed a request by 2080 ms, and 64 parked
  keep-alive peers meant only four requests — the worker count — could be
  answered at all. Rewritten around one `poll(2)` per worker: 21–33 ms with all
  64 answered, unchanged at 256.
- **A double free of a `core:crypto`, `core:tls`, `core:http` or `core:image`
  handle** segfaulted, and using one after free returned a garbage number and
  exited 0. Both die by name now.
- **Float-to-integer conversions were undefined** for a NaN, an infinity or an
  out-of-range value. They are checked at run time.
- **The format parsers stopped failing open** — `core:csv`, `core:json`,
  `core:toml`, `core:cli` and `core:markdown`.

## Security

- `core:http` refuses `file://` URLs — a URL from anywhere untrusted could read
  a local file through the HTTP client.
- `tycho-httpd` refuses ambiguous request framing (request smuggling), and the
  example server refuses a symlink escape out of its document root.
- Three sites interpolated attacker-shaped text into a shell command line; all
  are quoted now.
- **`tycho-rsa` is gone.** A pure-Tycho RSA cannot be constant-time — the
  modular exponentiation leaks the key through timing — and padding it did not
  change that. Use `core:crypto`, which is OpenSSL.

## The surface moved, deliberately

0.8.0 froze the keyword set, the builtin set and every corelib signature, and
said no new language features before 1.0. This release breaks that: the layout and
SIMD work above needed surface, and it was judged worth taking now rather than
after 1.0, when it could not be taken at all. The lock still exists and still
gates — 115 keywords, 41 builtins, 559 corelib functions — but it records what
was added instead of forbidding additions. Corelib may still gain a function and
may not lose one or change a signature.

## What this release does not have

There has been **no third-party security review**, and the FFI boundary is
unsafe by design — see [SECURITY.md](SECURITY.md), and
[docs/internals/audit-brief.md](docs/internals/audit-brief.md) if you are
willing to be one.

One thing is unverified rather than known, because you may hit it:

- **Server shutdown on Windows has never been observed on Windows.** The code
  reads sound in both halves: a worker parks in `net.wait_readable` with a tick
  of at most 100 ms (`server/main.ty@POLL_TICK_MS`), so it re-reads the
  shutdown flag ten times a second whether or not anything wakes it, and the
  console handler shuts down the listener and every registered connection
  (`corelib/signal/signal_shim.c@sigx_ctrl_handler`). What is untested is
  whether `WSAPoll` returns early on a socket that has been shut down, and
  whether a blocked `accept` is released; both are Windows-version dependent
  and no lane here can reach a real Windows kernel. Expect wind-down within the
  poll tick; treat anything faster as unproven.

## Status

0.8.5 is pre-1.0 and there are **no stability guarantees**: anything here may
change. [ROADMAP.md](ROADMAP.md#what-10-requires) lists what 1.0 requires, and
the blocking item is not engineering — it is that nobody outside this repo has
written a real program in Tycho yet. If you write one, the friction you hit is
the most useful thing you can send back.
