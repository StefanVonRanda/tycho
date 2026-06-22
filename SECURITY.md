# Security policy

**Tycho is experimental, proof-of-concept software** with no stability or security
guarantees. It has **not** been security-audited. Please do not use it for
anything where a compromise would matter.

Some sharp edges are inherent, by design:

- **FFI (`extern fn`) is unsafe**, like any C interop — a wrong signature or a
  misbehaving C library can corrupt memory. The boundary is checked only at the
  type level (scalars / `string` / opaque `ptr`).
- A few corelib codecs (`base64`, `hex`, `url`) note a **`0x00`-byte caveat** on
  decode — a tycho string built with `chr()` can't hold an interior NUL, so a
  decoded NUL byte is dropped. They are exact for text and non-NUL binary.
- The hashes in `core:hash` are **non-cryptographic**; `core:md5` is broken for
  security (use `core:sha256` or a real KDF where it matters).

## Reporting a vulnerability

If you find a memory-safety or other security issue in the **compiler or
runtime** (a miscompile that breaks the language's value-semantics/arena
guarantees, an arena/UAF bug, etc.), please report it privately rather than
opening a public issue:

- Use GitHub's **private vulnerability reporting** ("Report a vulnerability"
  under the repository's **Security** tab), or
- contact the maintainer directly.

Include a minimal `.ty` (or input) that reproduces it and your platform. Because
this is a pre-1.0 research project, expect a best-effort response, not an SLA.

Routine miscompiles and crashes that aren't security-sensitive are fine to file
as ordinary [bug reports](.github/ISSUE_TEMPLATE/bug_report.md).
