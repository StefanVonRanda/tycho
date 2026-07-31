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

- [x] **Phase 2 — `t` (list) and `x` (extract), and the round trip**
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

  **Done.** `tools/tycho-ar/main.ty` only; nothing outside `tools/tycho-ar/`
  changed. Built first try — the only compile was the final one.

  **The Pre-flight's assumption holds exactly, and it was measured, not
  reasoned.** A probe compressing `""`, a 65-byte payload with byte 12 XORed by
  `0xFF`, and that payload cut in half:

  ```
  legit-empty: gzip len=20 inflated len=0
  legit-empty: sha=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  good      : gzip len=65 inflated len=51
  corrupt   : gzip len=65 inflated len=0
  corrupt   : sha=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  truncated : inflated len=0
  truncated : sha=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  ```

  Legitimate-empty, corrupt and truncated are **byte-identical answers**. The
  return value carries no discriminator at all, which matches the shim: every
  error path in `corelib/compress/compress_shim.c@zx_decompress` sets
  `*outlen = 0` and returns. So the format's out-of-band fields are load-bearing,
  and the reader applies three, in this order: `csha` over the raw payload
  **before** decompression; `size` against `len(inflated)`, which is the actual
  empty-versus-corrupt discriminator; then `sha` over the inflated bytes. A
  zero-byte file survives all three (`size 0`, e3b0c442…), so empty files round
  trip rather than being rejected defensively.

  **The corruption case was run twice, to prove the discriminator in
  isolation.** Flipping a payload byte is caught by `csha`, which would leave
  `size` untested — so a second archive flips the byte *and recomputes `csha` to
  match*, neutralising check 1:

  ```
  $ python3 surgery.py flip t.tyar flip.tyar        # csha left alone
  $ ./build/tycho-ar x flip.tyar out_flip
  tycho-ar: flip.tyar: member 1 (a.txt): payload digest mismatch (csha)
  x exit=1        # out_flip was never created

  $ python3 surgery.py flip-and-fix-csha t.tyar fixed.tyar
  flipped byte 12 of a.txt's payload AND recomputed csha to match;
  size=12, so the inflated length is the only remaining witness
  $ ./build/tycho-ar x fixed.tyar out_fixed
  tycho-ar: fixed.tyar: a.txt: inflated to 0 bytes, header says 12 (corrupt payload)
  x exit=1
  ```

  Which check caught it: **`csha`** in the first, **`size`** in the second. Plus
  the trailer count, against an archive with a whole member sheared off:
  `trailer says 8 members, found 7`, exit 1.

  **Traversal, asserted rather than assumed** — a hand-built archive (an
  independent Python emitter working from the format comment, not the Tycho
  writer) holding `ok.txt` then `../escape`:

  ```
  $ ./build/tycho-ar x esc.tyar dest
  tycho-ar: esc.tyar: refusing member 1: path escapes the destination: ../escape
  x exit=1
  dest contents: dest        # empty -- ok.txt was NOT written first
  escape: No such file or directory
  ```

  An absolute member path is refused the same way. `ok.txt` staying unwritten is
  the point of validating **every** path before the first write: an escape in the
  last member must not get the earlier ones extracted.

  **Round trip:**

  ```
  $ ./tychoc tools/tycho-ar/main.ty -o build/tycho-ar
  built build/tycho-ar
  $ ./build/tycho-ar c d1.tyar tree && ./build/tycho-ar c d2.tyar tree && cmp d1.tyar d2.tyar
  create-twice: CMP-IDENTICAL
  $ ./build/tycho-ar x d1.tyar out2
  tycho-ar: out2: 8 files extracted
  $ diff -r tree out2 && echo EMPTY
  EMPTY
  ```

  Fixture tree as phase 1's: nested dirs, an empty file, a dotfile, a space in a
  name, a **newline** in a name, 6 binary bytes with interior NULs, 100 KB of
  `/dev/urandom`. `cmp` individually on the named cases: empty file identical at
  0 bytes; interior-NUL file identical (`od`: `A \0 B \0 C \0`);
  newline-in-filename identical; 100 KB random identical. Phase 1's archive bytes
  are unchanged by this phase (`cmp d1.tyar t.tyar` identical), so create-twice
  still holds. `python3 scripts/check_citations.py` → `citation check: ok`.

  (`find tree -type f | wc -l` says 9 for this tree and the tool says 8. The tool
  is right: `wc -l` counts the newline **inside** a filename as a line break. A
  fixture designed to break line-oriented tools breaks the one you check it
  with.)

  **The `t`-and-stdout decision, since phase 1's note 4 landed here.** There is
  no `eprintln`, so a non-fatal warning can only reach stdout, and `t`'s stdout
  **is** its data. The resolution is an interface decision rather than a
  workaround: **`t` is all-or-nothing.** It verifies framing, paths, footers,
  payload digests and the trailer count for every member *before* printing a
  single line, then prints one line per member and nothing else — no summary
  line, no "listed 8 of 9", no per-member warning. Each of those would be a
  diagnostic sharing a stream with the listing, and a consumer cannot tell a
  diagnostic from a filename. A bad archive gets an empty stdout, a reason on
  stderr and exit 1, which is a signal a caller can act on. The cost is real and
  is the finding: **the missing channel removed a feature**, not just a
  convenience. `tar t` can report a bad member and keep listing; this cannot.

  **Notes for phase 4 — what phase 2 surfaced.**

  9. **`strings.parse_int` fails OPEN, and a format parser cannot use it.** It
     returns 0 for `""`, 0 for a leading non-digit, and stops at the first
     non-digit without objecting (`corelib/strings/strings.ty@parse_int`) — so a
     damaged `clen` of `"1x4"` parses as `1`. Correct for user input, where 0 is
     a fine default; wrong for a length field, where a wrong length that parses
     is exactly how a reader hashes the wrong span. The program carries its own
     strict `parse_uint` returning `-1`. The corelib has no strict counterpart
     and no `Option`-returning variant, which is the gap: `Result(int, ...)` is
     the shape every other fallible corelib call now uses, and this one predates
     it.
  10. **`bytes` slices CLAMP, so slicing is not a bounds check.**
      `data[p:p + 8]` on a truncated archive yields three bytes rather than
      trapping (`docs/spec/03-types.md:139`). A reader that treats the slice as
      the check gets the right answer by accident on the footer compare and the
      *wrong* one when it hashes a payload prefix. Every read in the reader
      therefore tests `len(...)` against what it asked for. The clamp is right
      for a text tool and a trap for a parser; the danger is that the trapping
      behaviour (array slices abort) and the clamping behaviour (string and
      `bytes` slices) are spelled identically.
  11. **No `mkdir -p`.** `io.make_dir` is one `mkdir(2)`
      (`corelib/io/io_shim.c@iox_make_dir`) — the right primitive, and not the
      one an extractor wants. The component-at-a-time chain is 18 lines here and
      will be rewritten by the next program that extracts anything. `Ok(false)`
      for "already a directory" is what makes the loop idempotent, and that part
      of the interface is exactly right. Filed as phase 11 below.
  12. **mtime is stored and cannot be restored.** Every member header carries the
      real `st_mtime`, `t` prints it, and `x` drops it: `core:io` reads mtime and
      has nothing to set one — no `utimensat`, `utimes` or `chmod` anywhere in
      `corelib/io/io.ty` or `corelib/io/io_shim.c`. This is a different shape of
      gap from the four phase 1 named: those are data the format never captured,
      this is data captured faithfully that the extractor cannot apply. **And
      `diff -r` does not compare mtimes**, so the gate this phase is verified by
      is green over a hole a user meets on the first restore. Filed as phase 12.
  13. **A match arm cannot be empty, and this note is also a correction.** The
      draft of it claimed there was no `_` wildcard binding, so the do-nothing
      arm of `match io.make_dir(cur)` was written `Ok(made): cur = cur`. That was
      an unverified absence claim and it is **false**: `Ok(_)` compiles, and a
      whole-arm `_:` wildcard is specified at `docs/spec/12-aggregates.md:653`.
      What is genuinely missing is an **empty block** — an arm with no body is
      `error: expected an indented block`, caret on the *next* arm's line, so a
      success case with no work still needs a statement. The arm is now
      `Ok(_): continue`, which is honest because the match is the last thing in
      the loop. Small on its own; kept because "the grammar has no way to say
      nothing" is the same family as phase 1's note 1, and because the near-miss
      is the point — the first draft would have shipped a made-up gap into
      `FRICTION.md`, which is the one file here where that is expensive.

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
      question as phase 9. **Phase 2 settled what `tycho-ar` does about it —
      `t` is all-or-nothing, verify everything then print only data — and that
      resolution cost a feature `tar t` has, so this is now a measured gap
      rather than a predicted one.**

- [ ] **Phase 11** — no `mkdir -p` in `core:io`. `io.make_dir` is one `mkdir(2)`
      (`corelib/io/io_shim.c@iox_make_dir`), which is the correct primitive;
      every caller that writes into a tree it does not own has to build the
      component chain itself, as `tools/tycho-ar/main.ty@mkdir_p` does in 18
      lines. Found by phase 2; a corelib change, so not absorbed. Phase 4 decides
      whether this is a `FRICTION.md` entry or an `io.make_dirs`.

- [ ] **Phase 12** — mtime is readable and not writable. `io.mtime` exists;
      nothing in `corelib/io/io.ty` or `corelib/io/io_shim.c` sets one
      (no `utimensat`/`utimes`, and no `chmod` either). `tycho-ar` therefore
      stores a faithful mtime it cannot restore, and `diff -r` — the gate phase 2
      is verified by — does not compare mtimes, so the gap is invisible to the
      check that would otherwise catch it. Found by phase 2; a corelib change.

- [ ] **Phase 13** — `strings.parse_int` fails open: `""` and a leading
      non-digit both return 0, and it stops silently at the first non-digit
      (`corelib/strings/strings.ty@parse_int`). Right for user input, wrong for
      any parser with a length field, which phase 2 discovered by having to write
      its own strict version. There is no strict or `Result`-returning
      counterpart in `core:strings`. Found by phase 2; a corelib change.

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. If this
  program wants to hash files in parallel it will meet it, and that is worth
  recording — but the language change is its own plan.
- **`crypto`, `pool`, `uuid`, `bignum`, `decimal`.** Named in the Pre-flight as
  deliberately not forced in.
- **Encryption.** An archiver that encrypts is a different program with a key
  management story; this one does not.
