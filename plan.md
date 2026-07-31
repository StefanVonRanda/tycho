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

- [ ] **Phase 1 — the format, and `c` (create)**
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

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. If this
  program wants to hash files in parallel it will meet it, and that is worth
  recording — but the language change is its own plan.
- **`crypto`, `pool`, `uuid`, `bignum`, `decimal`.** Named in the Pre-flight as
  deliberately not forced in.
- **Encryption.** An archiver that encrypts is a different program with a key
  management story; this one does not.
