# Security policy

**Tycho is 1.0 software** (see the README's status banner): the language surface
and the spec are stable. It has undergone the **1.0 security review** recorded
below — a review of the FFI shims, not a formal third-party audit. Please don't
use it for anything where a compromise would actually matter.

A few sharp edges are inherent, by design:

- **FFI (`extern fn`) is unsafe**, like any C interop — a wrong signature or a
  misbehaving C library can corrupt memory. The boundary is checked only at the
  type level (scalars / `string` / opaque `ptr`).
- A few corelib codecs (`base64`, `hex`, `url`) note a **`0x00`-byte caveat** on
  decode — a tycho string built with `chr()` can't hold an interior NUL, so a
  decoded NUL byte is dropped. They are exact for text and non-NUL binary.
- The hashes in `core:hash` are **non-cryptographic**; `core:md5` is broken for
  security (use `core:sha256` or a real KDF where it matters).
- **Native Windows (MSYS2/mingw) keeps a few behavioural gaps** the POSIX build
  does not have, and they are part of the sharp-edge list rather than the
  stability contract: `core:signal`'s Windows handler is
  `SetConsoleCtrlHandler` and whether it wakes a blocked `accept` is
  Windows-version dependent — the flag is set either way, so a program that
  polls it is portable and one that relies on the wake is not. Measured
  2026-08-09 on Windows 11 26200, and it is the sharper half of that gap: a
  thread parked in `recv` on an ACCEPTED connection is **not** released by the
  handler's `shutdown()` the way it is on Linux, so shutting down costs one
  `SO_RCVTIMEO` per parked reader rather than milliseconds
  (`server/run.sh`'s parked-reader case needs a 15s bound there against 3s on
  Linux, with an 8s idle timeout). `closesocket()` would release it — the
  listener already gets both — but a connection fd is churned by its worker,
  and closing one hands the number back out while another thread may still be
  blocked on it, which is the hazard `signal_shim.c`'s registry header rejects
  with measurements. A server on Windows therefore winds down within its idle
  timeout, not within a millisecond;
  `core:datetime`'s `local_offset` reads the SYSTEM zone through the CRT and
  ignores the `TZ` environment variable, and it did not track DST across the
  instants measured — on a box set to Pacific it answered `-28800` for both a
  January and a July timestamp under three different `TZ` settings, where the
  POSIX build tracked each one. Use `offset_at` with an explicit POSIX rule
  when the answer has to be reproducible; that path parses the rule itself on
  Windows and was checked against the POSIX build over 108 (zone, instant)
  pairs, all identical. **Non-ASCII filenames do not survive**: the emitted
  program uses the narrow (ANSI) CRT and directory APIs, so on the usual
  code page 1252 a path like `日本語.txt` is written to disk under a mojibake
  name and `read_dir` cannot return it — round-tripping inside one Tycho
  program works, but the name on disk is not the name you asked for and any
  other program sees the corruption. ASCII paths are unaffected.
  mingw has no `newlocale`, so `float(str)` under a comma-decimal locale falls
  back to the `localeconv` path and rejects what glibc would accept; `core:os`
  shells out through `cmd.exe`, which has neither `true`/`false` nor `kill`;
  and `tycho-debug` cannot interrupt a running inferior with Ctrl-C (`q` still
  quits). **WSL2 has none of these** — it is the Linux build.

## 1.0 security review — 2026-08-05

Scope (per the 1.0 promotion plan): the FFI shims' **string/bytes ownership**,
the **shell-out paths in core:os**, and the **TLS wrapper**. The boundary rules
they rely on are `docs/spec/14-ffi.md` §24.1 and the codegen they lower to.

### The boundary rules (verified)

1. A `-> string` / `-> Option(string)` return is **copied into the caller's
   arena at the call site and NOT freed** (`tycho_str_from_c`, src/tychoc.c:9760).
   The shim contract: the returned pointer must stay valid only until the call
   returns — recycled buffers are safe, owned buffers must be freed by a paired
   call (`osx_run_free`, `http_free`) or they leak.
2. A `bytes` return (the `(unsigned char **out, long *outlen)` out-param form)
   is **copied into the arena and the C buffer freed** (`tycho_bytes_from_c`).
   The shim contract: hand over a malloc'd buffer; ownership transfers.
3. Every C-returned value is deep-copied — a Tycho program never holds a live
   pointer into C-owned memory.

### Findings

**String/bytes ownership — no defects found; two conventions, each audited:**
- `core:crypto`: hex returns use a `__thread` recycled buffer (safe under rule
  1, copy at the call site); secret key material stays in an opaque handle and
  is wiped with `OPENSSL_cleanse` on free. `cx_key_export_hex` is the one
  deliberate re-materialization, documented.
- `core:net` `peer_addr`: `static __thread char buf[INET6_ADDRSTRLEN]` — safe
  under rule 1; the shim comment records why `__thread` (concurrent workers).
- `core:io` `read_line`: getline's reused buffer, valid until the next call —
  safe under rule 1; the contract is documented in the shim header.
- `core:os` `run`: the captured-output buffer is handle-owned and freed by the
  paired `osx_run_free`; the Tycho wrapper copies then frees. Correct.
- `core:http`: body buffers are Resp-owned, freed by `http_free`; the binary
  `body_bytes` path hands over a COPY because the bytes wrapper frees the
  pointer it receives (the double-free this avoids is documented in the shim).
- `core:tls` `read`, `core:compress`, `core:image`: malloc'd bytes out-params —
  rule 2, freed by the runtime. Correct.
- The `-> string` truncation at NUL (a Tycho string limitation) is documented
  per call site; the binary paths exist where it matters.

**core:os shell-out — by design, caller-side, documented:**
- `os.system` / `os.run` pass the command to `/bin/sh -c` **verbatim** — shell
  metacharacters are live, so a command built from untrusted input is injectable.
  This is documented in the package header. The shim does no quoting and no
  escaping; the in-tree callers that interpolate paths quote them (e.g.
  tycho-debug's `shq`).
- **`os.exec` / `os.exec_out` (added 2026-08-09) remove the class, on both
  platforms.** They take a `[string]` argv and start no shell: `posix_spawnp`
  on POSIX, `CreateProcess` with no `cmd.exe` anywhere on Windows.
  - *POSIX* is asserted end to end in `corelib/test/os/main.ty`: the same
    hostile text yields `INJECTED` through `os.run` and the literal
    `; printf INJECTED` through `exec_out`, so the test reddens if a shell is
    ever reintroduced underneath. Run under `-fsanitize=address,undefined`
    with `detect_leaks=1`, exit 0.
  - *Windows* has no `execve`, so the vector is joined into one command line by
    the `CommandLineToArgvW` quoting rules (the CRT's own `_spawnvp` is not used
    — its joiner has historically mishandled trailing backslashes). The join is
    round-tripped through the real splitter in
    `corelib/os/os_argv_quotecheck.c`, a `make shim-check` leg that builds and
    RUNS on Windows and skips loudly elsewhere: 9 cases including embedded
    quotes, trailing backslashes, an empty argument and
    `; rm -rf / & echo x | y > z`, all splitting back to the input vector.
    Measured on Windows 11 26200, mingw gcc 16.1.0.
  - **Residual, callee-side and not fixable here:** a program that parses its
    own command line by rules other than `CommandLineToArgvW`'s — `cmd.exe`
    itself, a `.bat`/`.cmd` file, a hand-rolled splitter — can still read the
    line differently. Do not pass untrusted argv to a batch file. Batch files
    are not refused by extension; the callee is a PATH-searched name that
    `CreateProcess` resolves, so it cannot be classified beforehand (`gap:` in
    `os_shim.c`).
- `os.run` captures stdout only; stderr passes through to the parent —
  documented. Wait-status decoding maps a signal death to `128+signum`, never a
  bare confusion with a real exit code (spawn failure is `-1`).

**TLS wrapper — verification chain sound:**
- `SSL_VERIFY_PEER` + the system CA store + `SSL_set1_host` (hostname check) +
  SNI + a TLS 1.2 minimum — the correct combination for a verified client.
- Fail-closed: any resolve/connect/handshake/verification failure yields a NULL
  handle, so a caller that forgets to check gets no connection, not an insecure
  one.
- Residual, non-memory-safety findings: `read` returns empty on BOTH clean EOF
  and error (a caller cannot distinguish them — documented); `read`'s `max` is
  caller-controlled with no cap (an int64 > INT_MAX truncates at the `SSL_read`
  cast — low severity, the caller is the program itself); no ALPN or
  certificate pinning (feature choices, not defects).

### Residual risks

- **The FFI boundary stays unsafe by design** — type-level checks only; a wrong
  `extern` signature can corrupt memory.
- **Shell-out is injection-prone by design** — the caller owns quoting.
- **Non-cryptographic hashes** and the **NUL caveat** (sharp edges above).
- This review covered the shims the 1.0 plan named; it is not a formal
  third-party audit, and the fuzzer + sanitizer lanes in `make ci` keep running
  over both the compiler and the programs it emits.

## Reporting a vulnerability

If you find a memory-safety or other security issue in the **transpiler or
runtime** (a miscompile that breaks the language's value-semantics/arena
guarantees, an arena/UAF bug, and so on), please report it privately rather than
opening a public issue:

- Use GitHub's **private vulnerability reporting** ("Report a vulnerability"
  under the repository's **Security** tab), or
- contact the maintainer directly.

Include a minimal `.ty` (or input) that reproduces it, plus your platform.
Expect a best-effort reply, not an SLA.

Routine miscompiles and crashes that aren't security-sensitive are fine to file
as ordinary [bug reports](.github/ISSUE_TEMPLATE/bug_report.md).
