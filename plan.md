# `tycho-ar`: a deterministic archive tool

Previous plan complete and archived at
[docs/internals/plan-shim-gate-DONE.md](docs/internals/plan-shim-gate-DONE.md).
Its unclosed discoveries carry forward at the bottom.

## Goal

Write a real program that is **not** the web server, to find out what the
language and corelib do badly when the work is batch and data-shaped rather than
socket-shaped.

`tycho-ar` creates, lists and extracts an archive: walk a directory, hash each
file, gzip each member, write one file; and reverse it byte-for-byte.

**Why this program.** Fourteen corelib packages have exactly one consumer each,
and in every case that consumer is the package's own demo under
`examples/corelib/`. A demo passes literals; a real caller passes what it has.
`iter.map` was `[$T] -> [$T]` — unable to change element type at all — until this
morning, and nothing noticed because only its own demo ever called it.

Done looks like: `tycho-ar c`, `t` and `x` work, an extracted tree is
`cmp`-identical to the input, the archive is byte-identical across runs, a gate
proves it, and `FRICTION.md` has whatever the writing surfaced.

## Pre-flight

- **Worst case:** a tool that silently corrupts data it was trusted to store. An
  archiver that drops a byte, mangles a path, or extracts outside its
  destination is worse than no archiver. Every phase asserts a **round trip**
  against `cmp`, never a length or a digest alone, and extraction must refuse a
  member path that escapes the destination — `path.safe_join` exists and is the
  server's precedent for exactly this.
- **Reversibility:** total. A new program under `tools/` or `examples/`; nothing
  existing changes unless a corelib gap forces it, which is the point.
- **Verified — the fourteen single-consumer packages.** `grep -rl "core:<pkg>"`
  over `server/ examples/ tools/ bench/` returns exactly
  `examples/corelib/<pkg>/main.ty` for `base64`, `bignum`, `char`, `compress`,
  `crypto`, `decimal`, `fmath`, `hash`, `iter`, `md5`, `pool`, `signal`, `tls`,
  `uuid`.
- **Verified — the interfaces this program will actually use.**
  `corelib/compress/compress.ty@compress` is `bytes -> bytes` gzip, and
  `@decompress` inflates gzip or zlib and **fails closed to empty bytes** on a
  corrupt or truncated input. `corelib/sha256/sha256.ty@hex` is
  `string -> string`. `corelib/path/path.ty` has `join`, `base`, `dir`, `ext`,
  `stem`, `split_path` and `safe_join`. `io.read_at`, `io.size` and `io.mtime`
  landed today.
- **Verified — and this is the seam the program will hit first.**
  `sha256.hex` and `base64.encode` both take **`string`**; `io.read_bytes` and
  `io.read_at` both return **`bytes`**. So hashing a file means crossing that
  boundary for every binary file in the tree. `FRICTION.md` records that a
  `string` is fully byte-safe — interior NULs survive concat, index, slice and
  `len`, measured — so the crossing is lossless, and the split "buys type-level
  intent and FFI shape, not binary safety". Whether that is *ergonomic* when a
  real caller does it per file is what this program will answer.
- **Assuming — `compress.decompress` failing closed to empty bytes is
  indistinguishable from a legitimately empty member.** An archive can contain a
  zero-byte file. If both are empty `bytes`, corruption reads as an empty file,
  which is the Worst case wearing a disguise. **Phase 2 must check this and, if
  it holds, the format must carry the original length so the two are
  distinguishable.** Risk if wrong: silent data loss on a corrupt archive.
- **Assuming — I have not counted the packages this program genuinely needs, and
  I will not pad it to raise the number.** `compress`, `sha256`, `io`, `path`,
  `cli`, `sort` and `strings` are load-bearing. `crypto` (opaque `ptr` handles
  with a manual `key_free` — the FFI's manual-memory escape hatch), `pool`,
  `uuid`, `bignum` and `decimal` are **not** to be forced in. If the program
  wants one, it will say so; building to a coverage list is the failure mode
  this plan exists to avoid.

## Phases

- [x] **Phase 1 — the format, and `c` (create)**
  - Scope: a new program directory; no existing file changes.
  - Walk a directory, and for each regular file record path, size, mtime and
    `sha256`; gzip the contents; write one archive. **Entry order must be
    sorted** so the same tree produces a byte-identical archive twice — that is
    the property the whole gate rests on.
  - Design the format in the file, in a comment, before writing the writer: a
    reader that cannot find the member boundary without trusting a length field
    is a reader that cannot detect truncation.
  - Done when: creating twice over the same tree gives `cmp`-identical archives,
    and the header/member layout is documented in the source.
  - Verify: build it, create twice, `cmp`. Not `make ci`.

  **Done.** `tools/tycho-ar/main.ty`, one new file, nothing existing touched.
  The format is documented in its header comment; the `tools/prunner/`
  directory-not-flat-file reasoning was re-checked against `Makefile:128` and
  still holds.

  ```
  $ ./tychoc tools/tycho-ar/main.ty -o build/tycho-ar
  built build/tycho-ar
  $ ./build/tycho-ar c c1.tyar tree && ./build/tycho-ar c c2.tyar tree
  tycho-ar: c1.tyar: 9 files, 101822 bytes
  tycho-ar: c2.tyar: 9 files, 101822 bytes
  $ cmp c1.tyar c2.tyar && echo CMP-IDENTICAL
  CMP-IDENTICAL
  $ python3 scripts/check_citations.py
  citation check: ok
  ```

  Fixture tree: nested dirs, an empty file, a dotfile, a file with a space in
  its name, a file with a **newline** in its name, 6 binary bytes with interior
  NULs, and 100 KB of `/dev/urandom`. An independent Python reader parsed the
  archive and confirmed, per member: the `\nTYAR-M\n` footer sits exactly where
  `clen` predicts, `csha` matches the payload, the payload inflates, `size` and
  `sha` match the inflated bytes, the inflated bytes equal the file on disk, and
  `mtime` equals `st_mtime`. All 9 members, plus the trailer count.

  **All eight fixture digests match `sha256sum` byte for byte**, including the
  interior-NUL file and the 100 KB random one.

  **mtime is stored** (real `st_mtime`). Two runs over one tree are identical —
  which is what the gate asserts — and a copied tree deliberately is not: an
  archiver exists to give back the tree it was handed, and dropping mtime to
  make two unrelated directories compare equal trades the tool's purpose for a
  property nothing asked for. Content equality across trees is a question about
  the `sha` fields, which a reader can ask directly.

  **Notes for phase 4 — what the writing surfaced.**

  1. **No expression line continuation.** `x := a + b +` then a continuation
     line is `error: expected an expression`, caret at the column after the
     trailing `+`. Every header build in this program has that shape, so one
     two-line expression became three statements. It is the only thing in this
     phase that changed the code rather than the comments.
  2. **The `bytes`/`string` seam cost one call and no copy — the Pre-flight's
     prediction was half wrong.** `sha256.hex(to_str(raw))` is the whole
     crossing; `to_str` is a zero-cost reinterpret of the same length-headered
     buffer (`docs/spec/06-conversions.md`) and the digests prove it is
     lossless. **But the real cost is not conversion, it is that the wrong
     choice compiles silently.** Nothing at the type level distinguishes "this
     `string` is text" from "this `string` is raw bytes", so a caller who reaches
     for `io.read` instead of `io.read_bytes` gets a digest over a NUL-truncated
     prefix, with no diagnostic and no wrong-looking output. The split buys FFI
     shape, not a guard rail.
  3. **`io.read_bytes` has no counterpart.** Writing bytes is
     `io.write(p, to_str(b))`. It is correct — `tycho_write_file` fwrites the
     length header to a `"wb"` handle
     (`runtime/tycho_rt.c@tycho_write_file`) — but a caller has to go read the
     runtime to know that, because the signature says `string`. An
     `io.write_bytes` would make the read/write pair symmetric and the safety
     legible. Reported, not patched; carried forward below.
  4. **No stderr without exiting.** The builtins are `println`, `die` (stderr
     then exit 1) and `exit(n)`; there is no `eprintln`. A warning that does not
     terminate — the exact shape `server/main.ty` uses for an empty document
     root — can only go to stdout. Harmless here, where stdout is one summary
     line, and a real problem for `t`, which writes its listing to stdout and
     would interleave any diagnostic with the data. Carried forward below.
  5. **gzip determinism held on an unstated default.** RFC 1952 puts an MTIME
     field in every gzip header; a compressor that fills it in would have made
     every archive differ from the last. zlib writes zero there unless the caller
     supplies a `gz_header`, and
     `corelib/compress/compress_shim.c@zx_compress` supplies none. Nothing in
     `docs/spec/18-library.md`'s `core:compress` entry says the output is
     byte-deterministic, so this program depends on a property that is true but
     undocumented. Verified by reading the shim, not by assuming.
  6. **`readdir` order is the nondeterminism, confirmed at the source.**
     `runtime/tycho_rt.c@tycho_list_dir` returns entries in filesystem order.
     `sort.asc` over `[string]` fixes it, and because string comparison is
     `memcmp` over the length header (`runtime/tycho_rt.c@tycho_str_cmp`) the
     order is unsigned-byte lexicographic and locale-free — which is what a
     format needs, and which a locale-aware comparison would have quietly
     broken.
  7. **`push` has no inverse.** No `pop`, so the directory walk is a queue with a
     cursor rather than a stack. Not a defect — the queue reads better — but it
     was a choice made by the corelib, not by the program.
  8. **What the format cannot store, because the corelib cannot ask:**
     permission bits, empty directories, symlinks-as-links (`io.is_dir` is
     `stat(2)`, which follows them; there is no `lstat`), and hard links. Each is
     a real archiver feature blocked on a missing call. Named in the source
     header so nothing infers otherwise.

- [ ] **Phase 2 — `t` (list) and `x` (extract), and the round trip**
  - Scope: the same program.
  - `t` lists without extracting. `x` writes the tree out and **must** refuse a
    member whose path escapes the destination — use `path.safe_join`, which
    returns `""` fail-closed, and assert the refusal rather than assuming it.
  - **Settle the Pre-flight's empty-versus-corrupt question first** and report
    it. If `decompress` cannot distinguish them, the format carries the original
    length and the extractor checks it.
  - Done when: `diff -r` between the source tree and an extracted tree is empty,
    including a zero-byte file and a binary file; a hand-corrupted archive is
    **rejected**, not silently extracted as empty.
  - Verify: the round trip by `diff -r`, the corruption case, the traversal
    refusal.

- [ ] **Phase 3 — chunked hashing through `io.read_at`**
  - Scope: the same program.
  - Today the tool reads each file whole. `io.read_at` landed this morning with
    a fixture and **no caller**; chunked hashing is what it was built for.
  - The digest of a file hashed in chunks must equal the digest from phase 1's
    whole-file read — that equality is the verification, and it is only
    meaningful because phase 1 exists first.
  - Done when: hashing is chunked, digests match phase 1's byte for byte on a
    file larger than one chunk, and the archive is unchanged.
  - Verify: the digest comparison, then create-twice `cmp` still holds.

- [ ] **Phase 4 — the gate, and what the program surfaced**
  - Scope: a runner, its `Makefile` target, a `scripts/ci.sh` step,
    `CLAUDE.md`'s gate table, and `FRICTION.md`.
  - Unlike the server this is a batch program, so it gates with a golden and
    needs no daemon harness — closer to `examples/*/run.sh` than to
    `server/run.sh`.
  - **The `FRICTION.md` entries are the deliverable, not a formality.** Record
    what was awkward with specifics: a line you could not write and the
    diagnostic you got, a `bytes`/`string` crossing that cost something real, a
    package whose interface did not fit a real caller. If a predicted problem did
    not materialise, say that too — the Pre-flight's `bytes`/`string` seam is a
    prediction, and predictions in this repo have been wrong roughly as often as
    right.
  - Done when: the gate is wired and green, `make ci` is green with an observed
    exit code, and `FRICTION.md` carries what was learned.
  - Verify: the gate, then `make ci` once, waited on in-turn, observed.

## Carried forward

- [ ] **Phase 5** — a skipped shim is compiled by nothing on this host: `make
      corelib` and `scripts/shim_check.sh` both skip `image` for the same missing
      libpng, so a real defect there is invisible here.
- [ ] **Phase 6** — nothing checks that a document is *reachable*; the cheap
      version of that gate would have stayed green through `docs/bootstrap.md`'s
      entire outage.
- [ ] **Phase 7** — `corelib/test/result/main.ty` claims a construct "still
      fails"; a compile probe shows it builds clean. Disproved, not yet corrected.
- [ ] **Phase 8** — `README.md:223` documents `make bootstrap` and `make
      fixpoint`; neither target exists in the `Makefile`.

- [ ] **Phase 9** — `io.read_bytes` has no counterpart: writing bytes is
      `io.write(p, to_str(b))`, correct only because `tycho_write_file` is
      length-header-driven, which the `string` signature does not say. Found by
      phase 1; a corelib change, so not absorbed into it. Phase 4 decides whether
      this is a `FRICTION.md` entry or an `io.write_bytes`.

- [ ] **Phase 10** — there is no `eprintln`: `die` is the only route to stderr
      and it exits. A non-fatal warning is inexpressible, so it lands on stdout
      alongside a tool's actual output. Bites `t` in phase 2. Same disposition
      question as phase 9.

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. If this
  program wants to hash files in parallel it will meet it, and that is worth
  recording — but the language change is its own plan.
- **`crypto`, `pool`, `uuid`, `bignum`, `decimal`.** Named in the Pre-flight as
  deliberately not forced in.
- **Encryption.** An archiver that encrypts is a different program with a key
  management story; this one does not.
