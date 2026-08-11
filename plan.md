# Close the open FRICTION.md entries

## Goal

Every open entry in `docs/internals/FRICTION.md` is either fixed with a fixture
that can fail, or **struck with the measurement that shows why it does not
reproduce**. Done when no `### N.` heading in that file lacks a `~~strikethrough~~`
or an explicit "open, refused because …".

## Pre-flight

- **Worst case:** a phase "fixes" an entry whose premise is false, adding API
  surface against a hazard that is not there — exactly what nearly happened with
  `sha256.hex_bytes` before the probe. Second worst: a corelib signature change
  silently widens what a caller accepts, as `parse_int_checked` would have done
  to `tycho-ar`'s header fields.
- **Reversibility:** every phase commits alone, so `git revert <sha>` undoes one
  without touching the others. Nothing here is destructive; no data is at risk.
- **Verified:** three of the last four entries were wrong about themselves —
  #2's NUL truncation did not reproduce (`io.read` returns len 5 for `AB\0CD`),
  #4's requested parser already shipped as `corelib/strings/strings.ty@parse_int_checked`,
  and #3's fix was real but its cost estimate was right for the wrong reason.
- **Assuming:** the remaining entries are as stale as the ones already read. If
  that assumption is wrong the phases still hold — re-probe is the first action
  of each, so a true entry simply proceeds to the fix.

## Phases

- [x] **Phase 1 — #5 `bytes` slices clamp, so a slice is not a bounds check**
  - Scope: `corelib/` bytes/string slicing, or a correction to the entry.
  - Done when: either a bounds-checking accessor exists with a fixture whose
    out-of-range case fails, or the entry is struck with the probe output.
  - Verify: `make corelib` green; the fixture's must-fail case observed failing
    before the fix.
  - **Outcome: the entry is TRUE and was fixed, not struck.** Probe first, one
    5-byte `bytes` and one 5-element `[int]`, compiled and run:

    ```
    len(b)=5
    b[2:10] len=3        b[7:9] len=0        b[-3:2] len=2
    t[2:10] len=3        t[7:9] len=0        (identical string clamp)
    ```

    and the array half of the entry's "the hazard is the spelling" claim, same
    syntax, opposite outcome:

    ```
    tycho: slice [2:10] out of bounds (len 5)
    array-slice exit=1
    ```

    So `x[a:b]` aborts for an array and clamps for `bytes` and `string`, exactly
    as `docs/spec/03-types.md:139` specifies and exactly as the entry describes.
  - Fix: `corelib/strings/strings.ty@slice_bytes` and `@slice_str`, same
    `(start, stop)` the language takes, returning `Result(_, SliceErr)` —
    `OutOfBounds` for `start < 0` or `stop > len`, `Inverted` for
    `start > stop`. Clamping stays the default; no existing caller moves.
  - Must-fail case observed failing BEFORE the fix, with the fixture already
    written:

    ```
    corelib/test/strings/main.ty:118: error: package 'strings' has no symbol 'slice_bytes'
    ```

    and after, the ten new golden lines, `sb past=OutOfBounds` being the
    `b[2:10]` that clamped to 3 above:

    ```
    sb in  =ok:3:BCD    sb full=ok:5:ABCDE   sb zero=ok:0:
    sb past=OutOfBounds sb far =OutOfBounds  sb neg =OutOfBounds  sb inv =Inverted
    ss in  =ok:2:BC     ss past=OutOfBounds  ss inv =Inverted
    ```

  - Golden: `corelib/test/strings.out` 42 -> 52 lines. Ten lines APPENDED, no
    existing line moved (`diff` reported `42a43,52` and nothing else).
  - Gates: `make corelib` -> "corelib: all green (tychoc matches goldens)".
    `make check-links` -> "citation check: ok (132 anchored ... 163 `path@SYMBOL`
    definition refs name a symbol still in their file)". `make test` was not run
    and cannot redden for this: `tests/run.sh:113` never descends into
    `corelib/`.
  - Entry #5 in `docs/internals/FRICTION.md` updated with the probe output and
    the answer. Not struck — it reproduced.

- [x] **Phase 2 — #6 There is no `eprintln`**
  - Scope: the builtin table in `src/tychoc.c` (`eprint` is registered there), or
    the entry.
  - Done when: `eprintln(s)` writes to stderr with a newline and has a fixture,
    or the entry is struck.
  - Verify: `make test` — this one touches `src/tychoc.c`, so it is the gate.

  **DONE 2026-08-11 — entry STRUCK, no compiler change. The premise above is
  wrong twice**: the phase does not touch `src/tychoc.c`, so `make test` was
  never its gate, and the entry's own load-bearing claim does not reproduce.

  - The entry says "the builtins are `println`, `die` and `exit(n)`", so "a
    non-fatal warning is inexpressible". `eprint(s)` is a builtin —
    `src/tychoc.c:5051@eprint`, runtime `runtime/tycho_rt.c@tycho_eprint`
    (`fputs(s, stderr)`), specified at `docs/spec/16-builtins.md:74@eprint` as
    "no newline, **no exit**". `git log -L 5051,5051:src/tychoc.c` dates it to
    `61fa0dc`, 2026-06-14 ("+ eprint primitive") — before the entry was written.
    Nine non-frozen `.ty` files already call it, `corelib/log/log.ty` included.
  - **The probe that decided fix-vs-correct** (a `t`-shaped listing: warn on one
    member, keep listing, exit 0):

    ```
    $ ./warn 2>/dev/null            # stdout is data only
    a.txt
    c.txt
    $ ./warn 2>&1 >/dev/null        # the warning, on its own channel
    tycho-ar: BAD.bin: payload digest mismatch, skipped
    $ ./warn >/dev/null 2>/dev/null; echo $?
    0
    ```

    So the "removed a feature" half is false: `t`'s all-or-nothing interface was
    a choice, not a consequence of a missing channel.
  - Only the heading's first four words survive: `eprintln("x")` gives
    `error: unknown procedure 'eprintln'; did you mean 'println'?`, exit 1. The
    residue is a `+ "\n"` the tree already writes in nine places — sugar, not a
    channel. Adding the builtin was refused on that basis: it would have cost a
    Sig row, a codegen row, three spec pages, a fixture and a tree-wide citation
    reanchor to sweeten a workaround nobody is actually blocked by.
  - Gates: `make check-links` -> "link check: ok (119 markdown files, no dead
    relative links)" + "citation check: ok (134 anchored ... 165 `path@SYMBOL`
    definition refs name a symbol still in their file)". `make test` ->
    `passed: 618   failed: 0   all green` — run as the brief asked, unchanged
    from the pre-phase count, and it **could not have moved**: the only files
    this phase touched are Markdown, and `tests/run.sh` globs `.ty` only. No new
    golden, so `make goldens-check` has nothing to see.

- [x] **Phase 3 — #7 gzip byte-determinism is undocumented and load-bearing**
  - Scope: documentation only, unless the probe finds it is not actually
    deterministic — in which case report, do not "fix" zlib.
  - Done when: the guarantee (or its absence) is stated where a caller reads it,
    with the probe that establishes it.
  - Verify: the probe re-run; `make check-links`.
  - **Done 2026-08-11. The entry reproduced — it is one of the true ones.**
    Output IS byte-deterministic, so no zlib decision goes back to the user.
    The guarantee is now written in three places a caller reads:
    `docs/spec/18-library.md` §33.3 (binding on any implementation),
    `corelib/compress/compress.ty`'s package header, and
    `docs/guides/corelib.md`'s `compress` bullet. FRICTION #7 struck with the
    probe and the enumeration of dependents.

    **Probe** (`scratchpad/gzdet/main.ty`: gzip a fixed 2,400-byte payload to a
    file; run twice, three seconds apart, in different time zones):

    ```
    run1 09:09:52Z TZ=UTC
    wrote 82 bytes to .../p1.gz
    run2 09:09:55Z TZ=Pacific/Auckland
    wrote 82 bytes to .../p2.gz
    cmp: identical
    f6429240f0406b23e1c1bc472208144cf15d8ed5463d1a62c52f313ec11895a1  p1.gz
    f6429240f0406b23e1c1bc472208144cf15d8ed5463d1a62c52f313ec11895a1  p2.gz
    hdr :  1f 8b 08 00 00 00 00 00 00 03      <- ours, MTIME (bytes 4..7) zero
    gzip:  1f 8b 08 00 f1 e5 7a 6a 00 03      <- gzip 1.13, MTIME filled
    .../p1.gz .../g1.gz differ: byte 5, line 1
    ```

    The last line is the **negative control**: the `cmp` can fail, and it fails
    at byte 5 — the first byte of the MTIME field — which is exactly the
    mechanism `compress_shim.c@zx_compress` predicts (it never calls
    `deflateSetHeader`, so zlib leaves MTIME zero).

    **What depends on it** (twelve files name `core:compress`; only one gate can
    see the property):
    - `tools/tycho-ar/run.sh` leg **[1] create twice, byte-identical** — the only
      gate that would redden. `make ar-check`: green.
    - `corelib/test/compress.out` — boolean invariants only, no bytes or lengths;
      cannot catch a loss of determinism. `corelib/test/zip.out` locks `usz=`,
      never `csize`. `tools/tycho-ar/expected.out` holds each member's
      *uncompressed* size and the sha256 of the *original* payload. All three are
      blind to it.

    **Gates run and why.** `make check-links` (Markdown + a comment edit):
    `link check: ok (119 markdown files…)` / `citation check: ok (136 anchored…)`.
    `sh scripts/spec_check.sh` (touched `docs/spec/18-library.md`):
    `spec-examples: 9 runnable example(s), all pass`. `make corelib` (touched
    `corelib/compress/compress.ty`, comment only, but it is a corelib change):
    `corelib: all green (tychoc matches goldens)`. `make ar-check` (it is the
    lane whose dependency this phase documents): `tycho-ar: green (create twice
    byte-identical; …)`. **`make test` not run** — it never descends into
    `corelib/`, and nothing under `src/` or `tests/` changed, so it could not
    have reddened.

- [ ] **Phase 4 — #8 mtime is captured and cannot be restored**
  - Scope: `core:io` / `tools/tycho-ar`, or the entry.
  - Done when: mtime is restorable, or the entry states the refusal and why the
    gate is green over the hole.
  - Verify: `make corelib` and `make ar-check`.

- [ ] **Phase 5 — tycho-q #1 `core:json` accepts input it cannot represent**
  - Scope: `corelib/json/`.
  - Done when: the three unrepresentable cases are reported, with a fixture, or
    the entry is corrected.
  - Verify: `make corelib` and `make q-check`.

- [ ] **Phase 6 — tycho-q #2 `core:decimal` has no `div`**
  - Scope: `corelib/decimal/`. NOTE: ROADMAP's probe table already claims
    "`decimal.div` exists" — re-probe first, this may be closed already.
  - Done when: division exists with a documented rounding rule and a fixture, or
    the entry is struck as already fixed.
  - Verify: `make corelib` and `make q-check`.

- [ ] **Phase 7 — tycho-q #5 an enum cannot be asked which variant it is**
  - Scope: `src/tychoc.c`. The entry notes the pattern-discard fix mitigated it;
    establish whether anything remains before designing syntax.
  - Done when: a tag test exists with fixtures, or the entry is struck as
    mitigated with the spelling that works today.
  - Verify: `make test`.

- [ ] **Phase 8 — tycho-q #6 two error types cannot share an `or_return` chain**
  - Scope: likely `corelib/result/`. `result.map_err` already exists and closed a
    sibling entry — re-probe whether this one is the same fact recorded twice.
  - Done when: fixed, or struck with the spelling that works.
  - Verify: `make corelib`.

- [ ] **Phase 9 — tycho-q #7 `core:iter` unusable for a fallible pipeline stage**
  - Scope: `corelib/iter/`.
  - Done when: a fallible stage is expressible with a fixture, or the entry
    states the refusal with its cost.
  - Verify: `make corelib`.

- [ ] **Phase 10 — enum variant names are PACKAGE-scoped, not enum-scoped**
  - Found by phase 1, not absorbed into it. Adding a second `Result`-returning
    function to `core:strings` with the obvious variant name was refused:

    ```
    corelib/strings/strings.ty:374: error: variant name 'OutOfRange' is already used in this package
    ```

    because `IntErr` at `corelib/strings/strings.ty:112` already spends it. The
    workaround cost nothing here (the variant became `OutOfBounds`) but it means
    a package's variant names are one flat namespace: every new fallible
    function in a package competes with every existing one for `NotFound`,
    `Empty`, `Syntax`, `OutOfRange`. A caller already writes
    `Err(strings.OutOfBounds)`, qualified by PACKAGE and not by enum, so the
    flatness is visible at the call site too.
  - Scope: decide whether this is intended. If it is, it is a FRICTION entry and
    a sentence in the enum section of the spec; if it is not, it is a resolution
    rule in `src/tychoc.c` and out of scope for a corelib phase either way.
  - Done when: an entry states the rule with the diagnostic above, or the phase
    is closed as intended-and-documented.
  - Verify: `make check-links` only, if the change is Markdown.

- [ ] **Phase 11 — the one-paragraph slice warning in `docs/spec/03-types.md`**
  - Entry #5 asked for prose beside the `b[i:j]` row at
    `docs/spec/03-types.md:139` saying in words that a clamping slice cannot be
    used as a bounds check. Phase 1 answered the entry in corelib instead and
    left the prose unwritten, deliberately: the spec was outside its scope.
  - Done when: the row's neighbourhood says it, and points at
    `strings.slice_bytes` / `strings.slice_str` for the failing-closed version.
  - Verify: `sh scripts/spec_check.sh` and `make check-links`. Not `make test`.

- [ ] **Phase 12 — the false "no stderr channel" claim is still load-bearing in
      `tools/tycho-ar/main.ty`** (*found by phase 2*)
  - `tools/tycho-ar/main.ty:222-224`, under the banner "WHY `t` PRINTS NOTHING
    BUT MEMBER LINES", asserts "There is no `eprintln`. The builtins are
    `println`, `die` ... and `exit(n)`, so the only way to write to stderr is to
    terminate." Phase 2 disproved that with a running probe: `eprint` has
    shipped since 2026-06-14. This is the *source* of FRICTION #6, and it is
    worse there than in the friction list, because it is presented as the
    justification for a shipped interface — a reader auditing `t` is told a
    language limitation forced a design that was actually chosen.
  - Out of phase 2's scope, which was the builtin table or the entry.
  - Done when: the comment states the all-or-nothing listing as a **decision**
    (one verification pass before any output beats `tar t`'s partial listing) and
    stops citing a limitation that does not exist. `t`'s behaviour does not
    change — this is a comment, not a code phase.
  - Verify: `python3 scripts/check_citations.py` and `make check-links`. **Not
    `make ar-check`** unless a line of code moves, and none should.

- [x] **Phase 13 — `docs/spec/18-library.md` §33.3 still documents the OLD
      `decompress` signature** (*found by phase 3*)
  - The `core:compress` spec entry says `decompress(data) -> bytes` with "empty
    bytes on corrupt/truncated input — fails closed". That has been false since
    2026-08-10: it returns `Result(bytes, ZErr)`, and the whole point of the
    change (FRICTION #3) was that empty-on-failure conflated a corrupt stream
    with a legitimately empty payload. The guide and the package header are both
    current; only the **spec** — the document a second implementer would build
    from — still describes the behaviour that was removed for causing data loss.
  - Phase 3 added the determinism sentence to this same paragraph and did not
    touch the stale one: out of its scope.
  - Done when: §33.3 states the `Result(bytes, ZErr)` signature and the three
    causes, and `raw_decompress` likewise if it is named there.
  - Verify: `sh scripts/spec_check.sh` and `make check-links`. Not `make test`,
    not `make corelib` — no code moves.
  - **Done 2026-08-11.** One occurrence in the whole tree, `docs/spec/18-library.md:456-458`,
    rewritten from `decompress(data) -> bytes` + "empty bytes on corrupt/truncated
    input — fails closed" to the `Result(bytes, ZErr)` signature, the three causes
    (`Corrupt` / `Truncated` / `Failed`), and "an `Ok` of length 0 is a real empty
    payload and is distinct from every failure", with the reason (a corrupt member
    of a container reading as a legitimate zero-byte file) stated so a second
    implementer cannot re-derive the old behaviour. `raw_compress` / `raw_decompress`
    were not named in §33.3 at all and now are; the raw-stream caveat that
    `Truncated` also covers "not a deflate stream" is carried over from the guide.
    Signatures verified against `corelib/compress/compress.ty@decompress` and
    `corelib/compress/compress.ty@raw_decompress`, not from the guide's prose.
    Phase 3's gzip-determinism sentence (commit `dcc87fa`) is intact — the edit
    ends at "**`compress` is byte-deterministic**" and nothing after it moved.
  - Evidence:
    ```
    $ grep -rn 'decompress' docs/spec/
    docs/spec/18-library.md:457:bytes` (gzip-compress), `decompress(data) -> Result(bytes, ZErr)` (inflate a
    docs/spec/18-library.md:459:`raw_decompress(data) -> Result(bytes, ZErr)` for a headerless deflate stream.
    docs/spec/18-library.md:463:failure** — a decompressor must not signal failure by returning empty bytes.

    $ make check-links
    link check: ok (119 markdown files, no dead relative links)
    citation check: ok (136 anchored contain the token they name and each names one
    line, 817 bare in bounds, 181 source->doc citations resolve, 258 source->source
    in bounds, 6 source->source anchored, 166 `path@SYMBOL` definition refs name a
    symbol still in their file)

    $ sh scripts/spec_check.sh
    spec-examples: 9 runnable example(s), all pass
    ```
    No stale signature remains: all three surviving hits are the new wording.
    `make test` / `make corelib` deliberately not run — Markdown-only change,
    neither can redden for it.

- [ ] **Phase 14 — `core:image` still fails closed to a sentinel, the exact
      pattern FRICTION #3 removed from `core:compress`** (*found by phase 13*)
  - `corelib/image/image.ty@decode` is `decode(data: bytes) -> Image` returning a
    0×0 `Image` on any error, and `corelib/image/image.ty@encode` returns empty
    `bytes` on a bad image — verified by grep, there is no `Result` and no error
    enum in the file. `docs/spec/18-library.md` §33.4 documents this faithfully
    ("a 0×0 Image on any error — check `width > 0`"), so the **spec is not wrong**;
    the behaviour is. This is the same conflation phase 13 just finished writing
    up as a data-loss bug one section higher: a truncated PNG, a PNG that is
    actually a JPEG, and a legitimately 0-wide image are one answer, and a caller
    who forgets `width > 0` gets silence.
  - Out of phase 13's scope, which was doc-only and forbidden from touching code.
  - Done when: `decode`/`encode` return a `Result` with named causes, §33.4 and
    `docs/guides/corelib.md` follow, and every in-tree caller is updated.
  - Verify: `make corelib` (image has a test package), plus `make corelib-examples`
    if a worked example calls it, plus the two doc gates. Sequence the code change
    before the doc change so the spec never describes something that is not shipped
    — which is exactly how phase 13's bug was born.

## Out of scope

`make ci` and standalone `make test` are not run as ritual — each phase runs only
the lane that can redden for what it touched. Pushing is the user's call, not a
phase's; phases commit and stop.
