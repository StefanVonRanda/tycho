# Brief for an external security review

`ROADMAP.md` §7 asks for a third-party audit and has been open since the roadmap
was written. It is the last 1.0 condition that no amount of internal work can
close, and it stayed open partly because "get an audit" is not a task anyone can
pick up — it needs a scope, a threat model, and an honest account of what has
already been looked at. **This is that packet.** Hand it to a reviewer.

It deliberately does not summarise
[docs/internals/ffi-review-2026-08-14.md](ffi-review-2026-08-14.md) or
[SECURITY.md](../../SECURITY.md); it points at them and adds what a stranger
needs that an insider would not think to write down.

---

## 1. What the thing is, in one paragraph

Tycho is a compiled language whose compiler (`src/tychoc.c`, a single
dependency-free C file) **transpiles to C** and hands the result to `cc`. There
is no VM and no runtime interpreter. Programs get value semantics, arena
allocation freed at scope exit, and an affine `handle` type whose destructor runs
at scope exit. The standard library (`corelib/`, 46 packages) is written in Tycho
except where it binds a system library, and those bindings are the subject below.

**The security-relevant consequence of the design:** a Tycho program's memory
safety is whatever the emitted C has, plus whatever the hand-written C shims
have. The first is machine-generated and sanitizer-tested; the second is
hand-written and is where a reviewer should spend their time.

---

## 2. Threat model — where untrusted bytes actually enter

Ranked by how directly attacker-controlled data reaches hand-written C.

| entry point | package | reaches |
|---|---|---|
| an HTTP response body | `core:http` (libcurl) | a `realloc` accumulation loop in `http_shim.c` |
| a TLS stream | `core:tls` (OpenSSL) | `tls_shim.c`, 128 lines, client side only |
| a decompressed stream | `core:compress` (zlib) | an output-growth loop; reached from `tycho-ar`, `core:zip`, any gzip body |
| an archive | `core:zip`, `tools/tycho-ar` | `le16`/`le32` offset arithmetic, path handling (zip-slip) |
| a PNG | `core:image` (libpng) | size arithmetic before a `malloc` |
| an HTTP request | `core:httpd`, `server/` | request-line and header parsing, connection lifetime |
| a text document | `core:json`, `csv`, `toml`, `markdown`, `utf8` | pure Tycho, but feeds everything above |
| a key or ciphertext | `core:crypto` (OpenSSL) | key import, AEAD, memory hygiene |
| a SQL parameter | `core:sqlite` | prepared statements, `sqlite3_bind_text` |

**Two things a reviewer should know before reading any of it:**

- **The FFI boundary is unsafe by design.** `extern fn` declarations are not
  checked against the C they name. A wrong signature is a silent ABI mismatch.
  There are 13 `corelib/*/*_shim.c` files; `wc -l` them plus `runtime/tycho_rt.c`
  for the current size.
- **`core:os` runs processes without a shell** (`posix_spawnp` with a real argv
  vector). `osx_system` exists and is the documented shell API doing what it
  says. That distinction is load-bearing.

---

## 3. What has already been measured, and the honest scope of each claim

This is the part that saves a reviewer time, and every row understates rather
than overstates. Each named lane is runnable — see §5.

| claim | how it is held | what it does NOT cover |
|---|---|---|
| `core:tls` verifies chain AND hostname | `make tls-verify` — a real `openssl s_server` on loopback, three outcomes that must disagree | the tls shim's memory handling, error paths, session lifetime |
| `core:http` verifies certificates | `make http-verify` | anything above the transport; the CA-override env vars are trusted input |
| no secret survives in memory the crypto shim released | `sh scripts/crypto_hygiene.sh` — interposes `free`, scans released blocks | memory the shim never owned (OpenSSL's own allocations, the runtime's FFI copy) |
| key-import hex decode is constant-time | same lane, under valgrind with the input marked undefined | cache-line and table-lookup side channels; everything else in the package |
| decompression cannot run away | a 1 GiB ceiling with its own error, proved to fire at a reduced ceiling | nothing else in zlib's surface |
| the emitted C traps out-of-bounds indexing | probes with the true length in the message | `bytes`/`string` slices CLAMP by design (FRICTION #5) |
| Windows undefined behaviour | `make wine-ubsan` — 361 fixtures, trap-mode UBSan | **use-after-free and heap overflow: mingw-w64 here ships no ASan** |
| text parsers survive mutation | `scripts/fuzz_shims.sh` | seed quality is the whole game — see §4 |
| markdown escapes text, scheme AND attributes | `corelib/test/markdown`, with the escaper defeated as a control | raw-HTML passthrough is not a feature, so it is untested rather than proven absent |

**Nothing here is a substitute for §7 and this table is not a defence.** It is a
map of which stones have been turned over by someone who chose which stones to
turn.

---

## 4. Why an outside reviewer is expected to find things

Not an argument from principle — from the record.

- The **first** structured pass over the shims (2026-08-14) found 5 issues in
  code that was green under every gate: an uncapped response body, a `size_t`
  wrap on ILP32, unbounded redirects with no scheme pin, an unbounded
  decompressor, and an `SSL_read` length truncation.
- A **second** pass the next day, choosing different targets, found **9 more** —
  plaintext left in freed heap in both AEAD directions, a hex decode that timed
  where the bad digit was, `javascript:` hrefs emitted live, a strict check
  placed downstream of a lenient parse so it never ran, and others.
- A **third** pass (2026-08-15, recorded in FRICTION #65–#70) found **10 more**,
  including a bound SQL parameter silently truncated at its first NUL — where
  the failure mode is *collision*, two distinct values becoming one row.

Three passes, three different sets of targets, twenty-four findings, zero
overlap. That is the shape of a surface that has not been reviewed enough, and
the reason §7 is written as a hard condition rather than a nice-to-have.

**The other half of the record is about instruments, and it is the more useful
warning.** Roughly a third of the time in those passes went into measurements
that were themselves wrong *and looked like results*: a fuzzing corpus whose
seeds died at the first byte while reporting 1950 clean mutants; a wine sweep
where every invocation was `command not found`; a differential lane that scored
1197 clean answers while the defect it was built for sat untested in front of it,
because the corpus was integers and the bug was in floats. **Budget for the
instrument being the defect.** Every lane added since refuses to score anything
until a deliberately wrong expectation has been shown to fail.

---

## 5. Reproducing anything here

No cloud CI, deliberately. Everything runs locally with a C compiler and `make`.

```
make                      # build ./tychoc -- one C file, no dependencies
make test                 # 560 fixtures under ASan/UBSan/LeakSanitizer (~8 min)
make corelib              # every corelib package against its golden (~49s)
make ci                   # the whole sweep (~8¼ min, parallel)
```

Security-specific lanes, each cheap and each with its controls documented in its
own header:

```
make tls-verify           make http-verify          make math-diff
sh scripts/crypto_hygiene.sh                        sh scripts/fuzz_shims.sh
make wine-ubsan           # ~13 min, Windows UB only
sh scripts/asan_self.sh   # the COMPILER under ASan/UBSan over the whole corpus
```

`CONTRIBUTING.md` has a table mapping each part of the tree to the one lane that
can redden for it. A package whose system dependency is absent is **skipped, not
failed**, and the verdict line names it — read the last line, not the exit code.

---

## 6. Where we would start, if we were you

Offered as a starting point, not a scope — a reviewer who picks differently is
the entire point of the exercise.

1. **`corelib/zip/` and `tools/tycho-ar`.** Archive parsing is offset arithmetic
   over attacker-controlled integers, and the zip-slip defence rests on
   `path.safe_join`, which has a property test but no adversarial one. The
   fuzzing seeds for this were wrong twice before they reached the parser at all.
2. **`core:httpd` and `server/`.** Slowloris is bounded, not eliminated, and the
   residual is a design property of connection-per-worker. Request parsing
   survived every malformed shape thrown at it by one person.
3. **The FFI signature surface itself.** Not any single shim — the fact that
   nothing checks an `extern fn` against the C it names. A systematic diff of
   declarations against headers is work nobody has done.
4. **`core:crypto` beyond the comparison.** `cx_ct_equal` uses `CRYPTO_memcmp`
   and the key-import decode is constant-time; the rest of the package's
   side-channel surface is unexamined.
5. **Windows use-after-free.** Structurally uncovered: mingw-w64 here ships no
   ASan, so `make wine-ubsan` catches undefined behaviour and nothing else.

## 7. What we want back

A list of findings with reproductions, and — more valuable — **which classes you
looked for and found nothing in**, since that is what tells us where the map is
genuinely blank rather than merely unvisited. Negative results with their
controls are recorded here as first-class (see the markdown entry at the end of
`FRICTION.md`), and yours would be too.

Report privately per [SECURITY.md](../../SECURITY.md).
