# FFI boundary review, 2026-08-14

`ROADMAP.md` §7 asks for an external security review, naming the FFI boundary:
"unsafe by design and nobody outside the project has looked at it."

**This is not that review.** It is a structured pass over the C shims by a
reviewer who did not write them, which is a different and lesser thing than a
third-party professional audit. §7 stays open. What this buys is that the obvious
classes have now been looked for by someone, with the method written down so the
next pass can start further along. Five findings fixed, three areas measured clean.

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
| unbounded output from a DECOMPRESSOR | one finding (below) |
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

## Findings

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

### 5. TLS is configured correctly — the notable result is a negative one

`corelib/tls/tls_shim.c` (128 lines) does all four of the things this class of
code usually gets wrong, and none of the shortcuts:

| the classic mistake | what it does |
|---|---|
| verification left off | `SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL)` |
| no CA store | `SSL_CTX_set_default_verify_paths` |
| cert checked, hostname NOT | `SSL_set1_host(ssl, host)` |
| no SNI, so the server picks the wrong cert | `SSL_set_tlsext_host_name(ssl, host)` |

plus `SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)`. `tlsx_read` guards
`max <= 0`, a failed `malloc` and the `SSL_read` result.

**One narrow fix.** `SSL_read` takes an `int` and `tycho_int` is 64-bit, so a
caller asking for more than `INT_MAX` handed it a truncated or negative length
while the buffer really was that large — undefined, and silently. Now clamped;
"up to max" is the documented contract, so a short read loses nothing. The
sibling `netx_read` has the same shape but passes `size_t` and needs no clamp.

Not reachable from the network — `max` comes from the calling Tycho program —
which is why it is a hardening rather than a vulnerability.

### 6. `core:regex` does NOT backtrack catastrophically — measured, with a control

The concern was ReDoS: the shim uses POSIX `regcomp`/`regexec`, and a backtracking
engine on attacker-controlled input is an exponential-time DoS. Measured
2026-08-14 with the textbook killer, `^(a+)+$` against `a{n}!`:

| n | Python's `re` (backtracking) | `core:regex` here |
|---|---|---|
| 18 | 9 ms | 2 ms |
| 22 | 146 ms | 2 ms |
| 26 | 2251 ms | 2 ms |
| 28 | 8940 ms | 2 ms |
| 20000 | — | **2 ms** |

The Python column is the **control**: it proves the probe shape detects blowup,
so the flat column is a result and not a probe that measures nothing — which is
the failure mode a clean security finding usually has. glibc's `regexec` is a
hybrid automaton and does not explore that space.

**Scope of the claim, stated because it is narrower than "regex is safe":** one
pattern class, on glibc, on this host. The shim binds whatever POSIX regex the
platform provides, and Windows and musl are different implementations that were
NOT measured. A pattern with backreferences — which force backtracking in any
engine — was also not tried.

### 7. The PNG decoder allocates exactly what libpng will write

`imgx_decode` uses libpng's **simplified** API, which is the one that validates:
`png_image_begin_read_from_memory` parses and checks the header, `PNG_IMAGE_SIZE`
computes the output size from the validated fields, the `malloc` is that size, and
`png_image_finish_read` writes into exactly that. Failure at each step is handled
and mapped to a distinct status. The format is forced to `PNG_FORMAT_RGBA`, so
the size is not a function of anything the file chooses.

That is the arithmetic the "did not cover" note was worried about, and it holds.

**One residual, not fixed:** there is no ceiling on the decoded size, so a PNG
declaring enormous dimensions asks for a correspondingly enormous allocation. It
fails *safely* — `malloc` returns NULL and that branch is handled — so this is
memory pressure, not corruption. Unlike the decompression bomb (finding 4) the
allocation is a single sized request rather than an unbounded doubling loop, and
the caller learns about it. Worth a cap if `core:image` ever decodes untrusted
input in a long-lived process.

### 8. The emitted C: bounds checks hold, and no user data reaches a format string

The previous note listed this as uncovered while conceding it is sanitizer-tested
(`make test` runs all 717 fixtures under ASan/UBSan/LeakSanitizer; `asan-self`
covers the compiler's own execution over 736 programs). What was missing was a
look at the emitted code as CODE. Two classes checked, both by probe rather than
by reading:

**Format strings.** Every `printf`/`fprintf` in the emitted C takes a string
LITERAL as its format; no value from the program is ever passed as one. That is
the format-string class closed by construction rather than by discipline.

**Bounds.** Each shape was indexed out of range through a `fn at(n: int) -> int`
so nothing folds at compile time, and every one traps with the offending index
AND the true length:

```
xs[-1]                  tycho: index -1 out of bounds (len 3)
xs[INT64_MAX]           tycho: index 9223372036854775807 out of bounds (len 3)
xs[1:99]                tycho: slice [1:99] out of bounds (len 3)
[3]int   a[5]           tycho: index 5 out of bounds (len 3)
bounded[3]int b[7]      tycho: index 7 out of bounds (len 2)
```

The `bounded` case reports the LIVE length (2), not the capacity (3), which is the
right answer and the one a caller can act on. Negative indices and an index at
`INT64_MAX` are both caught, so the check is not a naive `i >= len` on a signed
value. `-fwrapv` is passed by the build, so the arithmetic reaching those checks
is defined rather than UB the optimiser may assume away.

**What this does NOT establish:** that every emitted construct is checked, only
that these five are. `bytes`/`string` slices CLAMP rather than trap — that is
deliberate and has its own recorded verdict at FRICTION #5, re-probed 2026-08-13
against the reference languages.

## Where a next reviewer should start

Every item this pass originally listed as uncovered has since been worked — TLS,
regex, the image decoders, compression bombs, boundary fuzzing and the emitted C.
That is not the same as "there is nothing left", so this section is now a list of
what is genuinely open rather than a list of struck-through entries.

**The largest gap is not technical.** `ROADMAP.md` §7 asks for a THIRD-PARTY
review. Everything here was done by one reviewer who did not write the shims but
is not independent of the project either, and who chose which classes to look
for. A reviewer who picks different classes will find different things; that is
the whole argument for §7 and nothing in this document substitutes for it.

Concretely open:

- **`corelib/zip` and `corelib/markdown`** — parsers over untrusted input, not
  reviewed and not fuzzed. `json` and `csv` were on this line and are now IN
  `scripts/fuzz_shims.sh`: 1650 mutated inputs, 0 failures, reached through
  `io.read_text` so invalid UTF-8 gets into the parser too. That extension also
  needed JSON- and CSV-SHAPED SEEDS — with only the gzip-shaped ones, those two
  parsers rejected at the first byte and deep state (nesting, escapes, quoted
  commas, embedded newlines) was never reached, so the coverage would have been a
  number rather than a result.
- **`corelib/net`'s accept/recv path** under hostile peers. `server-check` proves
  the daemon survives two rude clients; that is not the same as an adversary.
- **The image decoders' uncapped allocation** (finding 7) — safe today because
  `malloc` failure is handled, worth a ceiling if `core:image` ever decodes
  untrusted input in a long-lived process.
- **Windows.** Every measurement here is Linux. The wine lanes cover behaviour,
  not memory safety: there is no ASan equivalent in that path.
- **Timing and side channels.** Not considered at all. `core:crypto` compares
  digests; whether any comparison is variable-time was not examined.
