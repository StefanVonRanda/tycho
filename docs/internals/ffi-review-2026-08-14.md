# FFI boundary review, 2026-08-14

`ROADMAP.md` §7 asks for an external security review, naming the FFI boundary:
"unsafe by design and nobody outside the project has looked at it."

**This is not that review.** It is a structured pass over the C shims by a
reviewer who did not write them, which is a different and lesser thing than a
third-party professional audit. §7 stays open. What this buys is that the obvious
classes have now been looked for by someone, with the method written down so the
next pass can start further along, and three real findings fixed.

## Scope and method

3,714 lines across 13 `corelib/*/*_shim.c` files plus `runtime/tycho_rt.c`. Each
class below was grepped for tree-wide **and the grep was proved able to match on
a synthetic first** — a pattern that silently matches nothing is worse than not
looking, because it produces a clean report.

| class | result |
|---|---|
| `strcpy` / `strcat` / `sprintf` / `gets` / `alloca` | **none anywhere**, control matched 1/1 on a synthetic |
| allocation-size arithmetic (`malloc(a*b)`, `realloc(p, a+b)`) | 12 sites reviewed by hand; one finding (below) |
| process execution | `posix_spawnp` with a real argv vector, no shell |
| unbounded accumulation from the network | one finding (below) |
| redirect / protocol handling | two findings (below) |

## What is sound

**`corelib/os/os_shim.c` is the part most likely to be wrong and is not.** The
argv path builds a NULL-terminated vector and hands it to `posix_spawnp` — no
shell, so no quoting question and no injection surface. Its reasoning for
`posix_spawnp` over `fork`+`execvp` is correct and is the sort of thing that is
usually got wrong: between `fork()` and `exec()` in a **threaded** process only
async-signal-safe calls are legal, and a Tycho program is always threaded because
the scheduler owns worker threads. `osx_system` still exists, but that is the
documented shell API doing what it says.

**No unbounded string operation exists in the whole shim surface.** For 3.7k
lines of hand-written boundary C that is a genuinely good result.

## Findings, all three fixed in this commit

### 1. A response body was accumulated with no cap (`corelib/http/http_shim.c@collect`)

curl's write callback `realloc`d and appended for as long as the server sent
bytes. There was no `CURLOPT_MAXFILESIZE` and no check in the callback, so a
hostile or merely broken URL could drive the process into memory exhaustion,
bounded only by `CURLOPT_TIMEOUT` (30s) times the link speed.

`CURLOPT_MAXFILESIZE` alone would not have fixed it: it acts on a
`Content-Length` curl was **given**, so a chunked response, or one that lies,
walks straight past it. The callback is the only place that sees the bytes
actually arrive, so the cap belongs there. Now 64 MiB — far above anything
corelib fetches.

### 2. The same accumulation could WRAP on a 32-bit target (same function)

`r->len + add + 1` is `size_t` arithmetic. On LP64 reaching 2^64 is not a
concern. **On ILP32 `size_t` is 32 bits and this wraps past 4 GiB** — `realloc`
would then honour a small size and the following `memcpy` would run off the
buffer. This is a supported target: the tree has an `ilp32` lane and runs the
whole corpus under `gcc -m32`.

The check is written to refuse **before** the arithmetic —
`if (add > MAX || r->len > MAX - add) return 0;` — because a check written after
the sum would be testing an already-wrapped value.

### 3. Redirects were followed with no bound and no scheme pin (`@perform`)

`CURLOPT_FOLLOWLOCATION` was on with neither `CURLOPT_MAXREDIRS` nor an explicit
redirect-protocol set. That is the SSRF amplifier shape: an arbitrarily long
chain, and — more importantly — **which schemes a redirect may switch to was left
to whatever the linked curl defaults to**, and `CURLOPT_REDIR_PROTOCOLS`' default
has changed across curl versions. A program fetching an `https` URL could be
walked onto `file://` by the remote side, on a curl old or configured enough.

Now `MAXREDIRS 10` and redirect protocols pinned to `http,https`, using
`CURLOPT_REDIR_PROTOCOLS_STR` where available and the older bitmask otherwise.

**Only REDIRECT schemes are restricted, deliberately.** `corelib`'s own test
fetches a `file://` URL directly, and that stays legal: the danger is a *remote
party* changing the scheme, not the caller choosing one.

### 4. Decompression had no ceiling — a bomb was attempted until the host gave out

`compress_shim.c`'s inflate loop grew its output buffer by **doubling with no
limit**. zlib cannot know the decompressed size in advance, so nothing else in
the path could stop it either: a small archive that expands enormously was
attempted until `realloc` failed or the machine did.

Measured 2026-08-14, before the fix: a **199 KB** gzip decompressed to **200 MB**
with no complaint — a 1000:1 ratio, and a real bomb reaches petabytes at the same
ratio. `compress.decompress` is reachable from untrusted bytes through
`tycho-ar`, `core:zip` and any gzip-encoded HTTP body.

Now capped at 1 GiB with its own `ZErr.TooBig`, kept distinct from `Corrupt` and
`Truncated` because **the input is VALID** — that is exactly what makes it
dangerous, and a caller that treats it as corruption learns the wrong thing.

The ceiling is `#ifndef`-overridable so the gate can prove it fires **without
allocating a gigabyte**: `tools/tycho-ar/run.sh` rebuilds the shim with
`-DZD_MAX_OUT=65536` and the same 325-byte input must flip from `out 300000` to
`refused`. Removing the ceiling reddens that leg — confirmed, not assumed. A
ceiling nothing ever crosses is a ceiling nobody has tested.

## What this pass did NOT cover

Named so the next reviewer starts here rather than repeating the above:

- **TLS.** `corelib/tls/tls_shim.c` was not reviewed. Certificate validation,
  hostname checking and session handling are the highest-value target left.
- **`corelib/regex`.** A regex over attacker-controlled input is a
  catastrophic-backtracking (ReDoS) question that grep cannot answer.
- **`corelib/image`.** `decode_bmp`/`decode_qoi` parse untrusted bytes into a
  pixel buffer — the classic decoder-overflow surface. They return `Result`, so
  the error path exists; the arithmetic was not audited.
- ~~**`corelib/compress`.** Decompression bombs~~ — **REVIEWED AND FIXED, see
  finding 4 below.**
- Anything about the **generated C** rather than the hand-written shims.
- Fuzzing of any boundary. The tree's fuzzer targets the compiler, not the shims.
