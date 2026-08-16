# Security audit: `core:zip` and `core:net`

Maintainer material. Everything below was executed, not read — where a claim
rests on running something, the command and the answer are named.

Scope: `corelib/zip/zip.ty` (240 lines, pure Tycho) and `corelib/net/net.ty`
with `corelib/net/net_shim.c` (226 + 417 lines). Both appear in
[`audit-brief.md`](audit-brief.md)'s threat model as places attacker-controlled
bytes arrive.

## The language property both packages rest on

Established by running it, because it decides whether every unchecked read
below is a bug:

| construct | out of range | result |
|---|---|---|
| `b[i]` | index | **aborts** — `tycho: string index 99 out of bounds (len 4)` |
| `b[a:c]` | slice | **clamps** — `b[2:60000]` on 4 bytes gives length 2 |

Both are memory-safe. An over-long slice is a short answer; an over-long index
is a trap that kills the process. Neither reads out of bounds. A parser here
therefore cannot leak adjacent memory, but it *can* be made to abort, which is
a denial of service for anything parsing an upload.

## `core:zip`

**No memory-safety defect found.** The parse path is guarded where it needs to
be, and the language covers the rest.

- `le16`/`le32` do no bounds checking of their own. Every call is reached
  through a guard: `list` and `entry_body` both test `pos + 46 > len(b)` before
  `le32(b, pos)`, and **`or` short-circuits** — confirmed by running a case
  whose right arm would abort if evaluated.
- `find_eocd` scans backward from `len(b) - 22`, so its reads stay in range for
  any input, including one shorter than 22 bytes (the loop does not run).
- The EOCD scan is **unbounded** — real readers stop after 64 KB, since the
  comment length is 16-bit. Measured: 1 MB → 0.01 s, 4 MB → 0.02 s, 16 MB →
  0.07 s, 64 MB → 0.26 s. Linear, ~4 ns/byte, and the archive is already in
  memory by then. Not a practical denial of service; not worth a ceiling.
- `entry_body` **omits** the full-record bound that `list` applies
  (`pos + 46 + nlen + elen + clen > len(b)`). Because slices clamp, the effect
  is a truncated name that fails to match — fail-closed, not a memory error.
  Reachable only by calling `entry_body` directly; `extract` runs `list` first,
  which validates every record it returns.

**The finding is the API, not the parser.** `extract` returns bare `bytes`, and
an empty result is four different answers: the name is absent, the archive is
malformed, the CRC did not match, or the member is genuinely zero bytes. A
caller writing that to disk turns a CRC failure into an empty file silently.
`core:compress` named this `Truncated` and `core:image` named it `Corrupt`,
both for the same reason. `core:zip` has no error channel at all. Marked
`gap:` in the package header.

No shipped consumer is exposed: `tools/tycho-snap` only calls `zip.create`,
`zip.list` and `zip.crc32`, never `extract`.

Entry names are returned verbatim, including `../../etc/passwd` — correct for a
reader, and already stated in the header and pinned by the package test.

## `core:net`

**No memory-safety defect found.** The two receive paths are the whole of the
untrusted surface and both are careful:

- `netx_read` and `netx_udp_read` refuse `max <= 0` *before* the cast to
  `size_t`, so a negative length cannot become an enormous allocation.
- Both check `malloc` for NULL and fail closed with an empty result rather than
  a partial read.
- `netx_read` distinguishes EOF, timeout and error through a status
  out-parameter, which `net.read` maps to `Err(Eof)` / `Err(Timeout)` /
  `Err(Failed)`.
- `has_nul` guards `listen`, `connect`, `udp_bind` and `udp_send`, so a
  NUL-bearing host is refused rather than silently truncated at the FFI
  boundary.

**One defect found and fixed:** the comment on `netx_udp_read`'s error arm said
a zero-length datagram would "keep buf, outlen 0", and the line below it freed
and returned. Both arms produce the identical observable result, so a legal
zero-length UDP datagram cannot be told apart from a receive error. The
behaviour is a real limitation of a sentinel-returning API; the comment
described an intent the code did not implement. Corrected, and marked `gap:`
with the shape a fix would take.

## What this audit did not cover

- **ILP32.** `make ilp32` builds the corpus under `-m32`, where `size_t` is 32
  bits. None of the size arithmetic here was re-read under that assumption.
- **`create`.** The writer was not audited; the threat model is a hostile
  archive being read, not one being produced.
- **Concurrency.** Neither package was examined for behaviour under
  simultaneous use from several tasks.

## What now runs that did not before

`corelib/test/zip/main.ty` gained a structural-malformation leg: every
truncation of a valid archive (442 of them), an EOCD whose central-directory
offset points past the end, and a record claiming a 60000-byte name in a
68-byte archive. Each must list nothing and extract nothing, and reaching the
final line is the proof that none of them aborted.

The fixture asserts its own shape first, because the first cut of that leg was
four bytes short and put the 60000 in `usize` instead of `nlen` — it passed
while testing nothing. Control observed: deleting `list`'s record-length guard
turns the 0-entry answer into 1.
