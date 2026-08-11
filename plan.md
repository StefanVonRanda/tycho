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
    `src/tychoc.c:5084@eprint`, runtime `runtime/tycho_rt.c@tycho_eprint`
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

- [x] **Phase 4 — #8 mtime is captured and cannot be restored**
  - Scope: `core:io` / `tools/tycho-ar`, or the entry.
  - Done when: mtime is restorable, or the entry states the refusal and why the
    gate is green over the hole.
  - Verify: `make corelib` and `make ar-check`.

    **The entry REPRODUCED.** A probe calling `io.set_mtime` did not compile:

    ```
    main.ty:12: error: package 'io' has no symbol 'set_mtime'
        12 |     match io.set_mtime(p, 1416470400):
    ```

    and `grep -rn "mtime\|utime\|st_mtim" corelib/io/ corelib/os/ corelib/path/`
    returned **no `utime`/`utimensat` hit at all** — every hit was the read side
    or a comment. The entry's framing was also checked and held: the archive
    format really does carry the field (`tools/tycho-ar/main.ty:36`), so this was
    a missing setter, not a missing format.

    **Closed by adding the write side**, not by redesigning anything:
    `corelib/io/io_shim.c@iox_set_mtime` (utimensat with `UTIME_OMIT` on atime;
    `_utime` after a stat on Windows, which has no utimensat) and
    `corelib/io/io.ty@set_mtime` returning `Result(bool, IoErr)` — the shape
    `write_bytes` and `make_dir` already use, so no new convention. The same
    probe after the change:

    ```
    read mtime ok, nonzero=true
    set ok
    --- ground truth (date -r) ---
    1416470400
    ```

    `date -r` is the independent check: the epoch the shell reads back is the one
    Tycho asked for, so the assertion is not the code marking its own homework.

    **Fixture** — two lines in `corelib/test/io/main.ty`, locked into
    `corelib/test/io.out`, byte-identical over two runs:

    ```
    set_mtime=did exact=1 set_missing=NotFound made_nothing=1
    set_dir=did dir_back=1 rm=did
    ```

    `exact` is an epoch this run did not produce, so a no-op cannot pass it.
    `set_missing` is the case that MUST fail, and `made_nothing` proves the
    failure did not create the file on the way past.

    **Negative control.** The shim's `utimensat` call was replaced with
    `(void)ts;` and the fixture re-run — **three of the four assertions flipped**:

    ```
    set_mtime=did exact=0 set_missing=did made_nothing=1
    set_dir=did dir_back=0 rm=did
    ```

    The shim was restored from a backup and `grep` re-confirmed the real call at
    `corelib/io/io_shim.c:399` before any gate was run.

    **Gates run and why.** `make corelib` (a corelib change): `corelib: all green
    (tychoc matches goldens)`. `make shim-check` (touched `io_shim.c`, and it is
    the only lane that compiles one standalone under `-std=c11` — the lane that
    would catch a missing feature-test macro for `utimensat`): `shim-check: 8 ok,
    6 skipped, 0 failed`. `make ar-check` (touched `tools/tycho-ar/main.ty`):
    `tycho-ar: green (create twice byte-identical; …)`. `make goldens-check`
    (a golden moved): `goldens-check: ok`. `python3 scripts/check_citations.py`:
    `citation check: ok`; `sh scripts/check_links.sh`: `link check: ok`.
    **`make test` not run** — it never descends into `corelib/`, and nothing
    under `src/` or `tests/` changed, so it could not have reddened.

    **Deliberately NOT done:** `tycho-ar`'s `x` still does not call `set_mtime`.
    The phase brief forbade a format change and this needs none, but it does need
    a gate that compares mtimes — filed as its own phase below rather than
    absorbed here. The now-stale comment in `tools/tycho-ar/main.ty` that said
    the corelib had "nothing to set one" was corrected in place, since leaving a
    provably false claim in the tree is worse than the one-line diff.

- [x] **Phase 5 — tycho-q #1 `core:json` accepts input it cannot represent**
  - Scope: `corelib/json/`.
  - Done when: the three unrepresentable cases are reported, with a fixture, or
    the entry is corrected.
  - Verify: `make corelib` and `make q-check`.

  **Outcome: the entry did NOT reproduce, and it already said so.** The banner on
  it has read `[FIXED, 2026-08-01]` since two earlier plans closed the error
  channel and the float path. This phase re-probed the *title's* claim — "input
  it cannot represent" — along the five axes a value model can lose something on,
  and **found no loss on any of them**, so nothing was fixed. What it did find
  was two real decisions written down nowhere, and those were documented and
  gated.

  **The probe** (`corelib/json/json.ty` read first: `JObj([string], [Json])` is
  parallel arrays, and `JFloat(float, string)` carries the original lexeme — the
  shape predicts four of the five answers). Compiled with `./tychoc` under
  `TYCHO_CORELIB`, exit 0, verbatim output:

  ```
  --- 1. integers vs floats
  1        : num -> 1 identical=yes fixed=yes
  1.0      : float -> 1.0 identical=yes fixed=yes
  bigint   : float -> 123456789012345678901234567890 identical=yes fixed=yes
  i64max   : num -> 9223372036854775807 identical=yes fixed=yes
  i64max+1 : float -> 9223372036854775808 identical=yes fixed=yes
  --- 2. object key order
  zyx      : obj -> {"z":1,"y":2,"x":3} identical=yes fixed=yes
  keys     : zyx
  --- 3. duplicate keys
  dup rt   : obj -> {"a":1,"a":2} identical=yes fixed=yes
  dup keys : 2
  dup get  : 1
  --- 4. unicode, NUL, non-ASCII
  uA       : str -> "A" identical=no fixed=yes
  u00e9    : str -> "é" identical=no fixed=yes
  emoji    : str -> "😀" identical=no fixed=yes
  rawutf8  : str -> "é" identical=yes fixed=yes
  NUL len  : 3
  NUL bytes: 61 00 62
  NUL rt   : str -> "a\u0000b" identical=yes fixed=yes
  --- 5. null vs absent vs empty string
  n        : null
  e        : str
  absent   : null
  n=absent : yes
  ```

  Reading the five, in order: **(1)** an integer stays `JNum` and re-emits as
  itself; every number outside 64-bit `int` becomes `JFloat` carrying its lexeme
  and re-emits byte-identically, so `9223372036854775808` and a 30-digit integer
  both survive. **(2)** insertion order is preserved — parallel arrays, `keys`
  returns `zyx` for a document written `z,y,x`. **(3)** both duplicate members are
  kept, so the round trip is byte-identical; `get` answers with the first.
  **(4)** a `\uXXXX` escape decodes to UTF-8 bytes, so `identical=no` on the
  escaped spellings is deliberate and documented at `corelib/json/json.ty:801-805`
  — the property that holds is the fixed point, `fixed=yes` on every line. An
  embedded NUL survives as the three bytes `61 00 62` (Tycho strings are not
  NUL-terminated) and re-emits as an escape. **(5)** the tree holds all three
  states distinctly; only `get` collapses null and absent, which is an accessor
  limit, not a round-trip loss.

  **What changed, therefore: documentation and a fixture, no behaviour.**
  `corelib/json/json.ty` gained a 13-line `OBJECTS` header section stating the
  two decisions (duplicates all kept / `get` returns the first; null and absent
  are one answer from `get`, ask `keys` instead). `corelib/test/json/main.ty`
  gained a `has_key` helper and eight assertion lines so neither can drift
  silently — before this, nothing in the tree asserted either
  (`grep -n "dup\|duplicate\|absent"` over the test, the golden and the package
  returned nothing). `docs/guides/corelib.md`'s `core:json` bullet was corrected
  against the source: it still described the enum without `JFloat`, never
  mentioned `parse_checked`, and closed with "Scope: integers (no floats), the
  four common escapes" — all three false since 2026-08-01.

  **Verification.**

  - `make corelib` → `corelib: all green (tychoc matches goldens)`.
  - **Negative control, so the fixture is not vacuous.** `json.get` was patched
    in place to last-wins (accumulate into `hit`, return after the loop) and the
    lane was re-run: `FAIL json (output != golden)`, `corelib: FAIL`. Reverted
    from a pre-patch copy and `grep -n "return vs\[i\]"` confirms first-wins is
    back at `corelib/json/json.ty:898`.
  - `make q-check` → green, 35-query transcript unchanged (`core:json` behaviour
    is untouched, so it could not have moved — run because the gate table names
    it for any `core:json` change).
  - `make corelib-examples` → `corelib examples: all green` (`core:json` has a
    worked example; the change is comment-only, so this confirms rather than
    discovers).
  - `python3 scripts/check_citations.py` → ok. `sh scripts/check_links.sh` → ok
    (119 markdown files, no dead relative links).
  - `make test` NOT run: nothing under `src/` or `tests/` changed, and its corpus
    never descends into `corelib/`.

  **One defect introduced and caught during this phase, recorded because it was
  invisible to every gate:** writing the FRICTION banner through `Edit` put a
  literal `0x00` byte into `docs/internals/FRICTION.md` where the prose meant to
  name the escape. `cat -A` found it; it was stripped with a byte-exact `python3`
  replacement and the file now contains zero NUL bytes. Neither doc gate checks
  for control bytes in Markdown — filed below.

- [x] **Phase 6 — tycho-q #2 `core:decimal` has no `div`**
  - Scope: `corelib/decimal/`. NOTE: ROADMAP's probe table already claims
    "`decimal.div` exists" — re-probe first, this may be closed already.
  - Done when: division exists with a documented rounding rule and a fixture, or
    the entry is struck as already fixed.
  - Verify: `make corelib` and `make q-check`.

  **The note was right and the FRICTION entry was wrong.** `div` was implemented
  2026-08-02 by commit `4251339`; FRICTION entry 2's banner still read
  "[STILL OPEN, checked 2026-08-01]" and entry 1's banner still said "there is
  still no `decimal.div`". **No code was written this phase** — `corelib/` is
  byte-identical to its parent commit. The deliverable is the three stale
  documents.

  **Re-probe, not a source read** (`/tmp/decprobe/main.ty`, compiled with
  `./tychoc`, run):

  ```
  1/3  @5 half_up = 0.33333
  2/3  @2 half_up = 0.67
  2/3  @2 trunc   = 0.66
  -2/3 @2 half_up = -0.67
  1/0  @2         = Err(DivByZero)
  1/2  @-1        = Err(BadScale -1)
  1/2  @2 mode 9  = Err(BadMode 9)
  ```

  So the design questions the brief asked me to decide were already decided, in
  the shape the finding asked for: the scale is an explicit **argument**, not
  inherited; the rounding mode is an explicit argument with **no default**
  (`HALF_UP` ties away from zero, `TOWARD_ZERO` matches `rescale`); and a zero
  divisor is `Err(DivByZero)`, which is the `Result` direction of FRICTION 3, 7
  and 8 rather than a sentinel.

  **Negative control — the fixture can fail.** `corelib/decimal/decimal.ty:127`
  `>= 0` → `> 0` (half-up stops rounding exact ties up). `make corelib` went
  from `corelib: all green` to `corelib: FAIL`, and the divergence is exactly
  the two tie cases, nothing else:

  ```
  14,15c14,15
  < div_modes=0.67 0.66 1 0        > div_modes=0.67 0.66 0 0
  < div_scale0=4 3                 > div_scale0=3 3
  ```

  Restored; `git diff --stat corelib/` is empty and `make corelib` is
  `all green` again.

  **Gates:** `make corelib` all green · `make q-check` green (35-query
  transcript == golden, division by zero refused with empty stdout) ·
  `check_citations.py` ok · `check_links.sh` ok (119 files). `make test` NOT run
  — nothing under `src/` or `tests/` changed, and its corpus never descends into
  `corelib/`.

- [x] **Phase 6b — the `core:decimal` worked example never mentions `div`**
  - Found by phase 6, out of its scope. `examples/corelib/decimal/main.ty` has
    no `div`, no `half_up`/`toward_zero` and no `DivErr`: grep for `div` in it
    returns nothing. So the one place a reader goes to see the package used
    shows only the exact operations, which is the half of `core:decimal` that
    needed no explanation. The rounding-mode choice and the zero-divisor `Err`
    are the parts a caller gets wrong.
  - Done when: the example demonstrates `div` at both modes and the
    `Err(DivByZero)` path, with its golden re-recorded.
  - Verify: `make corelib-examples` (~44s). Not `make corelib`, not `make test`.
  - **Done 2026-08-11.** Three `println`s and one four-line `show` helper appended
    to `examples/corelib/decimal/main.ty`, continuing the shopkeeper narrative that
    was already there (`0.1 + 0.2`, `19.99 x 3`, `change`) rather than replacing it:
    the same `subtotal` of `59.97` is now split eight ways.
  - **The two modes had to differ on the SAME division, or the argument looks
    decorative.** `59.97 / 8` is exactly `7.49625`, so at scale 2
    `decimal.half_up()` gives `7.50` and `decimal.toward_zero()` gives `7.49` — one
    cent apart, on screen, side by side. `59.97 / 0` prints
    `refused (divide by zero)` through the helper's `Err` arm, so the zero divisor
    is handled and visible rather than unwrapped away.
  - Signature re-verified at the source: `corelib/decimal/decimal.ty:116` is
    `fn div(a: Decimal, b: Decimal, scale: int, mode: int) -> Result(Decimal, DivErr)`
    — the brief's line number is right. Note `mode` is a plain `int`;
    `decimal.half_up()` and `decimal.toward_zero()` (`:97`, `:100`) are functions
    returning one, not an enum.
  - Golden `examples/corelib/decimal.out` is **+3 / −0**, a pure append — the three
    new lines and nothing else, because no existing line's arithmetic changed.
  - Evidence:
    ```
    $ make corelib-examples
    ok   decimal
    corelib examples: 36 ok, 1 SKIPPED -- image(missing: libpng)
    $ make goldens-check
    goldens-check: ok
    $ make check-links
    link check: ok · citation check: ok
    ```

- [x] **Phase 14 — a qualified nested pattern does not count toward exhaustiveness — DOES NOT REPRODUCE, entry was wrong**
  - **The report's premise was false and no compiler change was made.** It rested on
    "`DivByZero` — the only variant of `decimal.DivErr`". `decimal.DivErr` has
    THREE variants (`corelib/decimal/decimal.ty:105-108`: `DivByZero`,
    `BadScale(int)`, `BadMode(int)`), and it already had all three at the commit
    that filed the report — `git show 8d98a91:corelib/decimal/decimal.ty` shows the
    same enum. So `Err(decimal.DivByZero):` as the ONLY refined `Err` arm was
    genuinely non-exhaustive; refusing it was correct. There is no qualification
    bug.
  - **The decisive probe** — the reported construct, on the reported package, with
    all three variants named and no `_` arm. It compiles and runs:
    ```
    $ cat /tmp/dec2/main.ty
    package main
    import "core:decimal"

    fn show(r: Result(decimal.Decimal, decimal.DivErr)) -> string:
        match r:
            Ok(d): return decimal.to_str(d)
            Err(decimal.DivByZero): return "refused (divide by zero)"
            Err(decimal.BadScale(s)): return "bad scale " + str(s)
            Err(decimal.BadMode(m)): return "bad mode " + str(m)
    ...
    $ ./tychoc /tmp/dec2/main.ty -o /tmp/dec2/bin && /tmp/dec2/bin
    built /tmp/dec2/bin
    refused (divide by zero)
    bad scale -1
    ```
    Minimal pair, differing only in qualification, both green:
    a two-variant package enum matched `Err(pk.A)` + `Err(pk.B(k))` builds and
    prints `ok 3`; the same enum declared locally and matched `Err(A)` + `Err(B(k))`
    builds. A single-variant package enum matched `Err(one.Solo)` alone builds and
    prints `solo` — the exact shape the entry claimed was refused. The only refusal
    was the genuinely partial `Ok(v)` + `Err(pk.A)`, missing `B`.
  - **Why the mechanism could not have worked the way the entry assumed.** A
    qualified nested pattern is mangled at parse time — `src/tychoc.c:3337`
    (`arm->sub = sfmt("%s%s", pkg_prefix_for(sn->text), inm)`) — and an unqualified
    one is kept as written (`src/tychoc.c:3339`). `enum_variant_index`
    (`src/tychoc.c:1250-1257`) accepts EITHER spelling: it compares against
    `variants[v].name` (mangled, `src/tychoc.c:4616`) and against `variants[v].raw`
    (as written, `src/tychoc.c:4617`). Both spellings therefore reach the same `vi`
    and the same `sc->cov` slot in `match_arm_payload` (`src/tychoc.c@match_arm_payload`),
    which is what `side_total` (`src/tychoc.c:7613-7619`) reads.
  - **Caller audit — all five qualified coverage sites verified green in one probe,
    every match wildcard-free:** a plain enum match with `pk.A:` / `pk.B(k):` arms
    (`src/tychoc.c:7925-7926`), a nested pattern under `Some` (`src/tychoc.c:7880`),
    one under `Ok`/`Err` (`src/tychoc.c:7903-7905`), `e is pk.A`, and `... is Ok`.
    Output: `enum B 1` / `some B 1` / `err A` / `is A` / `is Ok`, exit 0. No site in
    the class mishandles a package prefix.
  - No fixture was added: there is nothing to regress against, and
    `corelib/test/result/main.ty` already exercises a qualified nested pattern
    across a package boundary (`Err(io.NotFound):`).
  - Verified: `make check-links` — link check ok, citation check ok. `make test`,
    `make vm-check` and `scripts/entrypoints.sh` deliberately NOT run: no file
    outside `plan.md` changed, so none of them can redden.

- [ ] **Phase 15 — a partially covered Ok/Err side reports the wrong thing**
  - Discovered while disproving phase 14, out of its scope. This diagnostic is what
    made phase 14 get filed at all: when refined `Err(...)` arms cover SOME but not
    all variants of the error enum, the compiler says
    ```
    d_qual_partial/main.ty:5: error: match on a Result must cover both Ok and Err
    ```
    — but both `Ok` and `Err` ARE written. The real fault is a missing variant, and
    the message never names it. The plain-enum path gets this right
    (`src/tychoc.c:7954`, "non-exhaustive match: missing variant %s of %s").
  - Mechanism: `side_total` (`src/tychoc.c:7613-7619`) collapses the whole side to
    one bit and discards which slot of `sc->cov` was clear — it does not even use
    its `pt` argument (`src/tychoc.c:7617` is `(void)pt;`). The caller
    (`src/tychoc.c:7912`) therefore has no variant name to print.
  - A second, smaller item from the same finding:
    `examples/corelib/decimal/main.ty:11` carries the comment "DivByZero is DivErr's
    only variant", which is false against `corelib/decimal/decimal.ty:105-108`. That
    false comment is the origin of the phase 14 report. Its `Err(e)` arm also returns
    "refused (divide by zero)" for `BadScale` and `BadMode`, so fixing the comment
    honestly means refining the arm, which moves `examples/corelib/decimal.out`.
  - Done when: the message names the missing variant, and the decimal example no
    longer states a false fact about the enum it matches.
  - Verify: `make test`, then `make corelib-examples` for the example (and re-record
    its golden if the arm is refined).

- [x] **Phase 6c — triage tycho-q #5, #6, #7 against the source (no fixes)**
  - Dispatched out of plan order, because five of the eight entries worked in
    this chain turned out to be already fixed or wrong about their own
    mechanism. One probe per entry, compiled and run against the compiler at
    `680d30d`; FRICTION.md corrected in place. No implementation.
  - **Verdicts.** #5 **CONFIRMED with two sub-claims corrected** — no `is`, no
    tag accessor, but a match arm needs the right *arity*, not a name (`VInt(_)`
    compiles), and `v == VNull` already discriminates a nullary variant.
    #6 **WRONG MECHANISM** — `result.map_err`
    (`corelib/result/result.ty:120`) already bridges two error types into one
    `or_return` chain; only *implicit* conversion is missing, and a
    cause-preserving combinator is four lines of Tycho. #7 **CONFIRMED**,
    both diagnostics verbatim, and its "a signature, not a language change"
    sizing is now verified by a working generic `try_map`.
  - Evidence: the probe outputs are quoted inside each entry's triage block in
    `docs/internals/FRICTION.md`.
  - Verified: `make check-links` (link check ok, citation check ok). `make test`,
    `make corelib` and `make ci` deliberately not run — a Markdown-only change
    cannot redden any of them.

- [ ] **Phase 7 — tycho-q #5 a payload-carrying variant has no value-level discriminator**
  - **Re-scoped by the phase 6c triage.** The gap is real but narrower than the
    entry claimed: `_` binders and `==` on nullary variants already cover the
    cheap half, so what remains is a way to test a *payload-carrying* variant
    without a `match`.
  - **This is a language change — `src/tychoc.c` — and the only one of the
    three that is.** It needs a new expression form (an `is` operator, or a
    `tag(v)` accessor yielding an `int`), which touches the lexer, the parser,
    the resolver's type rules and the C emitter, plus spec text in
    `docs/spec/02-grammar.md` and `docs/spec/10-statements.md`, plus fixtures.
    Order of cost: days, not the hour a corelib addition costs. The workaround
    (`tools/tycho-q/main.ty@kind`, one hand-written function per enum that
    needs it) is cheap enough that this should stay below both other items.
  - Done when: a payload-carrying variant can be tested without a `match`, with
    fixtures — or the entry is closed as accepted-cost with the spelling above.
  - Verify: `make test`.

- [x] **Phase 8 — tycho-q #6 add a cause-preserving `map_err_with` to `core:result`**
  - **Re-scoped by the phase 6c triage: this is NOT a language change.** The
    original framing ("no function can propagate across the boundary") was
    disproven by probe. What is left is one gap in `core:result`: `map_err`
    replaces the error with a constant, so the cause is lost.
  - Scope: `corelib/result/result.ty` — roughly four lines, the same shape
    already probed working in a `package main` program:
    `fn map_err_with(r: Result($T, $E), f: fn($E) -> $F) -> Result($T, $F)`,
    plus a test case in `corelib/test/result/main.ty` and its golden, plus the
    package header note explaining when to reach for it over `map_err`.
  - Judgment call to make first: `map_err`'s header argues *against* a function
    form on purpose. That argument is about the common case; it does not cover
    an error carrying a payload worth keeping. Decide, and record the decision —
    "won't do, with reasons" is a legitimate outcome here.
  - Done when: added with a corelib test, or refused in the entry with reasons.
  - Verify: `make corelib` (~49s). Not `make test`.
  - **Done 2026-08-11 — ADDED, not refused.** `corelib/result/result.ty@map_err_with`,
    placed directly after `map_err`, same argument order (`r` first, the mapper
    second), same doc voice, four code lines. The judgment call the entry asked for:
    `map_err`'s "WHY A VALUE AND NOT A FUNCTION" argument survives intact and is
    NOT overturned — it argues that the caller with one target variant in mind
    should not pay for a named helper, and that stays true and stays the default.
    What it never covered is an error carrying a payload worth keeping, which is a
    different caller, so the two coexist and each header points at the other.
  - **The brief's premise re-verified at the source.** `corelib/result/result.ty:120`
    is `fn map_err(...)` as claimed, and the FRICTION #6 triage's correction is
    right: `map_err` already bridges two error enums into one `or_return` chain.
    The gap really is only the cause.
  - Evidence:
    ```
    $ make corelib
    ok   result
    corelib: 45 ok, 1 SKIPPED -- image(missing: libpng)
    $ make corelib-examples
    corelib examples: 36 ok, 1 SKIPPED -- image(missing: libpng)
    $ make check-links
    link check: ok (119 markdown files, no dead relative links)
    citation check: ok            # counts elided -- they move every commit
    ```
    The `image` SKIP is a missing libpng on this host, present before this phase
    and unrelated to `core:result`. Golden `corelib/test/result.out` is **+5 / −0**,
    a pure append.
  - **Negative control, two of them.** (1) Corelib-side, `Err(e): return Err(e)` —
    the compiler refuses before any golden is consulted: `corelib/test/result/main.ty:136:
    error: declared type Result(int, Wrapped) but value is Err(Local)`. A
    payload-DROPPING body is not even expressible inside the generic: there is no
    way to build an `$F` without calling `f`. (2) So the payload control was run at
    the call site, which is the question that matters — `cause_of` switched to plain
    `result.map_err(pick(k), Plain)`. It compiles, runs, and flips exactly one line:
    ```
    30c30
    < with_pay  = cause 9
    ---
    > with_pay  = plain
    ```
    `with_ok`, `with_flat`, `with_chain` and `with_early` did **not** move — which is
    the proof the brief asked for: a fixture asserting only the error VARIANT would
    have stayed green under plain `map_err`, and only the payload assertion catches it.
  - **A sixth stale bullet in `docs/guides/corelib.md`, found and removed.** Its
    `result` bullet still claimed "Nothing in `corelib/` may use a nested pattern
    itself: those packages are also compiled by the frozen `compiler/tychoc0.ty`."
    Both halves are dead — the tychoc0 lanes were retired 2026-07-29 and
    `corelib/result/result.ty:29-34` records the retirement in the very package the
    bullet describes. Deleted, and `map_err_with` documented in the same bullet.

- [x] **Phase 9 — tycho-q #7 `core:iter` has no fallible stage and spells predicates as `int`**
  - **Confirmed by the phase 6c triage; sizing verified, not argued.** A
    corelib change only.
  - Scope: `corelib/iter/iter.ty` (44 lines today), two independent halves —
    (a) add `try_map` / `try_filter` over `fn($T) -> Result(_, $E)`; the generic
    signature is already probed compiling and running.
    (b) flip `filter`'s `keep` and `count`/`any`'s `pred` from `fn($T) -> int`
    to `fn($T) -> bool`. Five call sites in the whole tree, all under
    `corelib/test/iter/` and `examples/corelib/iter/` — a breaking change with
    a five-line blast radius, so do it now or never.
  - Done when: a fallible stage is expressible with a corelib test, goldens
    re-recorded, and `examples/corelib/iter/main.ty` updated if (b) lands.
  - Verify: `make corelib` (~49s), plus `make corelib-examples` (~44s) only if
    the worked example changed.
  - **Done 2026-08-11 — half (a) only.** `corelib/iter/iter.ty@try_map` and
    `corelib/iter/iter.ty@try_filter`, each placed directly after the total
    sibling it mirrors, same argument names and order, callback returning
    `Result`. `try_map` is `push(out, f(v) or_return)`; `try_filter` binds
    `k := keep(v) or_return` and keeps the existing nonzero-is-true convention,
    because flipping that convention is half (b) and is now Phase 15. No shim:
    `corelib/iter/` holds `iter.ty` and nothing else. `reduce`/`count`/`any` got
    **no** fallible sibling — no caller in the tree wants one, and the entry's
    motivating shape (a query engine's row filter and its projection) is exactly
    the two that were added.
  - Evidence:
    ```
    $ make corelib
    corelib: all green (tychoc matches goldens)
    $ make corelib-examples
    corelib examples: all green
    $ make check-links
    link check: ok (no dead relative links)
    citation check: ok            # counts elided -- they move every commit
    ```
    Four new golden lines in `corelib/test/iter.out`, from two happy-path and two
    must-fail cases. Each must-fail input carries **two** failing elements plus a
    `999` whose callback calls `die`, so continuing past the first failure is
    observable twice over: `[2, 5, 4, 7, 999]` must report `5`, not `7`;
    `[2, 3, -3, 4, -7, 999]` must report `-3`, not `-7`.
  - **Negative control, both new functions.** `try_map` rewritten to `match f(v)`
    with the `Err` arm swallowing and the loop continuing:
    ```
    FAIL iter (output != golden)
          > try_map walked past the first error
    corelib: FAIL
    ```
    exit 1, stderr `try_map walked past the first error`, and the diff lost three
    golden lines (`trymap err=5`, `tryfilter ok=2,4`, `tryfilter err=-3`) — the
    program died at the sentinel before reaching them. The same swallow in
    `try_filter` (its `Err` arm setting `k = 0`) died at its own sentinel and lost
    `tryfilter err=-3`. Restored, `make corelib` green again.
    The "first, not later" half was proved separately, because the sentinel kills
    any continuing implementation before it can report the wrong element: a
    last-error variant of the same walk, run on `[2, 5, 4, 7]` with no sentinel,
    printed `last-error variant reports=7` where the golden demands `5`.
  - `docs/guides/corelib.md`'s `iter` bullet was checked against the source before
    editing and was **accurate** — five functions, `map`'s two type variables, the
    int-as-bool predicate note all matched `corelib/iter/iter.ty`. Unlike the
    `core:json` and `core:decimal` bullets found stale by earlier phases, this one
    needed only the two new signatures appended.

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

- [x] **Phase 11 — the one-paragraph slice warning in `docs/spec/03-types.md`**
  - Entry #5 asked for prose beside the `b[i:j]` row at
    `docs/spec/03-types.md:139` saying in words that a clamping slice cannot be
    used as a bounds check. Phase 1 answered the entry in corelib instead and
    left the prose unwritten, deliberately: the spec was outside its scope.
  - Done when: the row's neighbourhood says it, and points at
    `strings.slice_bytes` / `strings.slice_str` for the failing-closed version.
  - Verify: `sh scripts/spec_check.sh` and `make check-links`. Not `make test`.
  - **Evidence (2026-08-11).** `docs/spec/03-types.md:139` is the right row —
    the brief's citation held. Behaviour re-verified against today's `./tychoc`
    rather than trusting `124dc2f`'s record:

        bytes  b[2:10] len = 3
        string s[2:10] len = 3
        bytes  b[-3:2] len = 2
        bytes  b[4:1]  len = 0
        tycho: slice [2:10] out of bounds (len 5)     # xs := [1,2,3,4,5]

    stdout stops before the array line and the process exits 1, so the array
    slice aborts where the two buffer slices clamp — start below zero to `0`,
    stop past the end to `len`, inverted to empty. **Not a bug**: `16.6` already
    declares the divergence normative and gives the reason (a buffer slice is
    `substr`, which clamps by definition; an array slice has no function form),
    and `corelib/strings/strings.ty:356-369` was written against the same
    reading. So it is documented as deliberate, not filed.
  - The paragraph sits directly under the `bytes` operator table, where the
    clamping row is, and names the abort's exact diagnostic so a reader can
    recognise it. `docs/spec/18-library.md` §32.4 gained `slice_bytes` /
    `slice_str` in its list — the pointer had nowhere to land, since the spec
    did not mention either function anywhere before today.
  - Gates: `make check-links` ok (119 files), `sh scripts/spec_check.sh` exit 0
    (11 runnable examples, all pass). No fence was added, so the example count
    is unchanged.

- [x] **Phase 12 — the false "no stderr channel" claim is still load-bearing in
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
  - **Evidence (2026-08-11).** The builtin is real and predates the comment:

    > Provenance: `src/tychoc.c:5084@eprint`

        g_sigs[g_nsigs++] = (Sig){ .name="eprint", .ret=T_VOID, .params={ T_STRING }, .nparams=1, .builtin=1 };

    The brief for this phase put that declaration five lines earlier; the
    citation gate disagreed and was right, which is the second brief in this
    chain to mis-cite a range. It landed in `61fa0dc` (2026-06-14, "hierc0 parity for the loop-progress
    warning (+ eprint primitive)"), before the rename, which is why
    `git log -S'.name="eprint"' -- src/tychoc.c` bottoms out at `39d75be`
    (2026-06-22) — that commit moved the file from `src/hierc.c`.
    `grep -rln eprint --include='*.ty' .` reports 13 files calling it.
  - Two sites corrected in `tools/tycho-ar/main.ty`, both comment-only:
    - the `t` header (was `:222-224`, now `:218-232`) — the false premise is
      gone and the all-or-nothing listing is stated as a decision: `tar t`
      streams and discovers damage partway, so one verification pass before any
      output closes that window. stdout-is-data still holds and is kept.
    - the `set_mtime` note (was `:767-770`, now `:765-768`) — the "no
      non-terminating way to report" clause is replaced by the real reason,
      consistency with every other partial failure in `x`.
  - The claim had propagated into `docs/internals/FRICTION.md`; that copy is
    corrected too, and #6's entry now records the header as fixed rather than
    still-wrong.
  - Comments-only: the −2 line shift broke `plan.md:811`'s
    `:471@m.mtime` anchor, caught by the gate; four refs in this file and two
    in FRICTION.md were re-anchored. `python3 scripts/check_citations.py` ok
    (141 anchored, 908 bare, 181 source→doc), `make check-links` ok (119 files),
    `make ar-check` green (create twice byte-identical; `t` == golden; `diff -r`
    empty; extracted mtimes == archived; five refusals still refuse).
  - **No bug found.** `cmd_t`'s only stdout write is `println(line)` at
    `tools/tycho-ar/main.ty:707`, one line per member, so no diagnostic has ever
    been able to reach the machine-readable listing. The false comment cost
    reasoning, not correctness.

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

- [x] **Phase 14 — `core:image` still fails closed to a sentinel, the exact
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
  - **Done 2026-08-11.** The entry was NOT stale: the re-probe reproduced the
    sentinel on all four inputs before any edit.

    **Host note.** This box has libpng's runtime (`libpng16.so.16`) but no dev
    package, so `corelib/run.sh`'s dep check SKIPS `image` here and the lane is
    green without running it. Everything below was run against a static libpng
    1.6.50 built into `/tmp/pngdev` and selected with `PKG_CONFIG_PATH` — nothing
    in the repo or on the system was changed. Without that, this phase would have
    shipped unverified.

    Probe before the change (`corelib/image/image.ty@decode` was `-> Image`,
    `@encode` was `-> bytes`):
    ```
    valid png bytes: 90
    (a) valid: width=2 height=2 pxlen=16
    (b) truncated: width=0 height=0 pxlen=0
    (c) garbage: width=0 height=0 pxlen=0
    (d) empty: width=0 height=0 pxlen=0
    encode bad dims -> len=0
    encode short pixels -> len=0
    ```
    Three distinct failures, one answer. Probe after:
    ```
    (a) valid: Ok 2x2 pxlen=16
    (b) truncated@20: Err(NotPng)
    (b) truncated@40: Err(NotPng)
    (b) truncated@60: Err(Corrupt)
    (b) truncated@89: Ok 2x2 pxlen=16
    (c) garbage: Err(NotPng)
    (d) empty: Err(Empty)
    encode 0x0: Err(BadDims)
    encode short: Err(ShortPixels)
    ```
    Two facts that decided the enum. Truncating *inside* the header gives the
    same branch as garbage (libpng's `png_image_begin_read_from_memory` refuses
    both), so there is no honest `Truncated` cause to hand out — `NotPng` is
    worded "the header did not read" for exactly that reason. Truncating *past*
    the header reaches `png_image_finish_read`, which is the `Corrupt` branch.
    And `truncated@89` still decodes: the simplified reader does not require
    IEND, so dropping the last byte is not detectable and is not claimed to be.
    Causes are the five the shim's branches actually separate, plus `Failed`
    for allocation — the same unreachable-in-test variant `compress.ZErr` ships.

    A 0×0 image is not representable: PNG's IHDR rejects a zero dimension, so
    `Ok` from `decode` always carries `width >= 1` and every empty answer is an
    `Err`. Stated in `corelib/image/image.ty:14-20` and in §33.4.

    Callers — the whole set, from `grep -rn 'core:image' . --exclude-dir=.git`:
    - `corelib/test/image/main.ty` — rewritten; the three decode failures and
      both encode failures now assert their named cause.
    - `examples/corelib/image/main.ty` — rewritten to `match` both Results, and
      it now shows two failures being named. Golden `examples/corelib/image.out`
      re-recorded (2 lines added).
    - `docs/spec/18-library.md` §33.4, `docs/guides/corelib.md`'s `image` bullet
      — both rewritten. The guide bullet was stale beyond the signature: it said
      "fail-closed on a non-PNG", which was the bug described as a feature.
    - `SECURITY.md:91` and `docs/guides/corelib.md:493` mention `core:image` but
      make no signature claim; unchanged and re-read to confirm.
    No other consumer exists — no `tools/`, `server/` or top-level example
    imports it, so no tool lane applies.

    Negative control. `decode`'s `return Err(_cause(st))` replaced by
    `return Ok(Image(0, 0, to_bytes("")))`, rebuilt, run against the golden:
    ```
    5,7c5,7
    < truncated=Err(Corrupt)
    < garbage=Err(NotPng)
    < empty=Err(Empty)
    ---
    > truncated=Ok 0x0
    > garbage=Ok 0x0
    > empty=Ok 0x0
    ```
    Exactly the three must-fail assertions flip; the four happy-path lines do
    not move, which is the point — the old fixture had only those four and would
    have passed. Restored and re-verified byte-identical to the golden.

    Gates, each once, in the foreground:
    ```
    $ PKG_CONFIG_PATH=/tmp/pngdev/lib/pkgconfig make shim-check
    ok   corelib/image/image_shim.c
    shim-check: 9 ok, 5 skipped, 0 failed

    $ PKG_CONFIG_PATH=/tmp/pngdev/lib/pkgconfig make corelib
    ok   image
    corelib: all green (tychoc matches goldens)

    $ PKG_CONFIG_PATH=/tmp/pngdev/lib/pkgconfig make corelib-examples
    ok   image
    corelib examples: all green

    $ sh scripts/spec_check.sh
    spec-examples: 11 runnable example(s), all pass

    $ make check-links
    link check: ok (119 markdown files, no dead relative links)

    $ python3 scripts/check_citations.py
    citation check: ok (139 anchored ..., 871 bare in bounds, ...)
    ```
    `make test` deliberately not run: its corpus never descends into `corelib/`,
    and nothing under `src/` or `tests/` changed.

- [ ] **Phase 15 — `core:iter`'s predicates are `fn($T) -> int` in a language that
      has `bool`** (*found by phase 6c, deferred by phase 9*)
  - This is half (b) of the phase 9 brief, split out on purpose: phase 9 added
    `try_map`/`try_filter`, which are pure additions and revert cleanly, whereas
    this is a **breaking change to three shipped signatures** —
    `corelib/iter/iter.ty@filter`'s `keep`, and `corelib/iter/iter.ty@count` and
    `corelib/iter/iter.ty@any`'s `pred`. Mixing the two in one commit would mean
    a revert of the break also loses the fallible stages.
  - Blast radius measured by the phase 6c triage and unchanged by phase 9: the
    int-predicate call sites are all under `corelib/test/iter/` and
    `examples/corelib/iter/`. Re-count before editing rather than trusting the
    figure — phase 9 added `try_filter`, whose `keep` returns `Result(int, $E)`
    and would become `Result(bool, $E)` in the same sweep.
  - Also decide what `iter.count`'s own body does: it compares `pred(v) != 0`
    today, which a `bool` makes `if pred(v)`.
  - Done when: the predicates take `bool`, every caller and both goldens follow,
    and the package header plus `docs/guides/corelib.md`'s `iter` bullet stop
    saying "an int used as a boolean".
  - Verify: `make corelib` (~49s) and `make corelib-examples` (~44s) — the example
    changes this time — plus `make check-links`. Not `make test`.

- [x] **`tycho-ar x` still does not restore mtime, and its gate still cannot see that**
  - Found by phase 4, not absorbed into it — phase 4's brief scoped it to the
    corelib and explicitly deferred any `tools/tycho-ar` behaviour change.
  - The blocker is gone: `corelib/io/io.ty@set_mtime` exists as of 2026-08-11, so
    `x` can now put back the `st_mtime` the member header has carried all along.
  - Scope: `tools/tycho-ar/main.ty` (the extract path) and `tools/tycho-ar/run.sh`.
  - Done when: `x` restores each member's mtime, AND the round-trip leg compares
    them. **The gate change is the load-bearing half** — `diff -r` does not compare
    mtimes, so `run.sh` is green today over exactly this hole and would stay green
    over a broken restore. Add an explicit mtime comparison and confirm it CAN
    fail by stubbing the restore out.
  - Not a format change: the field is already in the header
    (`tools/tycho-ar/main.ty:36`).
  - Verify: `make ar-check`. Not `make corelib` unless the corelib is touched.
  - **Evidence (2026-08-11).** The create side was checked first, because a fix to
    `x` is worthless if `c` stored a zero. It stores the real `st_mtime` and dies
    rather than guessing:

    > Provenance: `tools/tycho-ar/main.ty:442-446`

        match io.mtime(abs):
            Ok(t):
                mt = t
            Err(e):
                die("tycho-ar: cannot stat " + abs)

    and `mt` reaches the header through `tools/tycho-ar/main.ty:462@Member` and
    `tools/tycho-ar/main.ty:469@m.mtime`. So the field was populated all along and
    only the extractor was missing.
  - The extract half is five lines at `tools/tycho-ar/main.ty:769-773`, placed
    AFTER the write because the write stamps the file with now. **A failed
    `set_mtime` is fatal, not a warning** — matching every other partial failure
    in this program (`io.write` failing is `die`, an unreadable file is `die`).
    There is no third option here: `x` has no non-terminating way to report
    anything, since there is no `eprintln` and its stdout is a status line, so
    "warn and continue" would mean restoring a wrong time in silence — the same
    defect being fixed.
  - The load-bearing half is leg 3b in `tools/tycho-ar/run.sh`. It compares the
    times with two reference files and POSIX `find -newer` (a file has the
    fixture's 1700000000 iff it is newer than a ref stamped at …19 and not newer
    than one stamped at …20), so no GNU `stat` and no per-file shell loop — the
    member whose NAME CONTAINS A NEWLINE is covered like any other. The leg also
    runs the same test over the source tree, which must find nothing: without
    that, an empty result could mean the comparison itself is broken.
  - **NEGATIVE CONTROL — the gate was proven able to fail.** With the
    `io.set_mtime` call commented out, `make ar-check` exited nonzero and named
    every member (all eight, including `.hidden` and the newline name):

        FAIL: round trip: extracted files do not carry the archived mtime (1700000000)
              /tmp/tmp.1DeIZA60O7/out/with space.txt
              /tmp/tmp.1DeIZA60O7/out/sub/nul.bin
              /tmp/tmp.1DeIZA60O7/out/sub/multichunk.txt
              /tmp/tmp.1DeIZA60O7/out/sub/deep/d.txt
              /tmp/tmp.1DeIZA60O7/out/new
              line.txt
              /tmp/tmp.1DeIZA60O7/out/empty
              /tmp/tmp.1DeIZA60O7/out/a.txt
              /tmp/tmp.1DeIZA60O7/out/.hidden
        tycho-ar: FAIL
        make: *** [Makefile:335: ar-check] Error 1
        make ar-check exit=2

    `diff -r` in leg 3 stayed silent through that entire run, which is the hole
    this phase existed to close. The five lines restored:

        tycho-ar: green (create twice byte-identical; t == golden; diff -r round
        trip empty; extracted mtimes == archived mtimes; traversal, absolute
        path, flipped payload, forged csha and truncation all refused)

  - The `t` golden did not move and was not re-recorded: nothing on the create
    side changed, and `expected.out` is `t`'s listing.

- [x] **f-string interpolations run their side effects RIGHT-TO-LEFT**
  - Found by phase 4 while writing a fixture, where it produced a wrong golden
    value: a `println(f"…{io.mtime(d)}…{io.remove(d)}")` printed the mtime as if
    the file were already gone, because the `remove` ran first. Split into
    separate statements to get the intended reading; the underlying behaviour was
    then isolated and is unchanged.
  - Reproducer — the printed text is left-to-right, the effects are not:
    ```
    println(f"{side("A", &acc)}{side("B", &acc)}{side("C", &acc)}")
    -->  ABC
         eval order: C,B,A
    ```
  - Scope: `src/tychoc.c` (f-string lowering), and `docs/spec/` if the answer is
    to specify the order rather than change it.
  - Done when: either the evaluation order is left-to-right and a fixture pins it,
    or the spec states the order explicitly and a fixture pins THAT. Deciding it
    is unspecified and leaving it undocumented is not one of the options — this
    already cost one wrong golden.
  - Verify: `make test` (this is a `src/tychoc.c` change), plus `sh scripts/spec_check.sh`
    if a spec example moves.
  - **Evidence (2026-08-11).** It reproduced on the first try. Probe
    `f"{side(a)}{side(b)}{side(c)}"`, compiled with `./tychoc` and run:

        BEFORE            AFTER
        call C            call A
        call B            call B
        call A            call C
        ABC               ABC

    **Mechanism — an ordering bug in the lowering, not a deliberate choice.**
    `--emit-c` showed all three interpolations landing as arguments of one call:

        tycho_print_s(tycho_str_concat3(&_t, h_side(&_t, h_a),
                                             h_side(&_t, h_b), h_side(&_t, h_c)))

    C leaves argument order unspecified and gcc evaluates right-to-left. The fold
    that builds it (`src/tychoc.c@fseq`) even carried a comment
    asserting "side-effect ordering is unchanged" — true relative to the pairwise
    concat it replaced, which has the identical hole.

    **What the spec said.** `docs/spec/09-expressions.md` §13.4 called argument and
    operand order *unspecified*, and Appendix F item 1 listed it as **deliberate**,
    matching Swift and Odin — with one exception already carved out, the
    assignment-place index, on the grounds that it "is never short-circuited" so
    sequencing is cheap and sound. Nothing anywhere mentioned f-strings. That
    silence is the finding: the desugar to `+` quietly inherited a rule written
    about hand-written operands.

    **Rule decided.** F-string holes are pinned **left-to-right**, as a second
    exception on exactly the reasoning the first one used: one hole is never
    short-circuited against another, and the holes' *printed* order is their
    source order, so leaving the effects reversed was actively misleading. Bare
    `+` and call arguments stay unspecified — unchanged. Written into
    `docs/spec/09-expressions.md` §13.4, `docs/spec/appendix-f-impl-defined.md`
    item 1, and the Appendix E conformance row.

    **Fix.** `src/tychoc.c@interp_join` marks each desugared `+` node `fstr`;
    codegen pins a chain carrying that mark *and* a call by binding every piece
    but the last to a temp in a braced group, so each is sequenced before the
    next. Both concat paths are covered (the 3..6 `concatN` fold and the pairwise
    fallback). Emitted for the probe:

        ({ char *_fi0 = h_side(&_t, h_a); char *_fi1 = h_side(&_t, h_b);
           tycho_str_concat3(&_t, _fi0, _fi1, h_side(&_t, h_c)); })

    A braced group is a GNU extension, but the emitted C **already** requires GNU
    C (`__attribute__((constructor))`, `__thread`) and is compiled without
    `-pedantic`, so this adds no new portability class. It is safe everywhere an
    f-string can appear: Tycho has no top-level variables (a top-level `:=` is
    rejected with "expected 'fn'"), so no f-string is ever a constant expression.

  - **Fixture.** `tests/fstring_eval_order.ty` + `.out` pins the ORDER OF EFFECTS,
    not the text — three holes (the fold), two holes (the pairwise path), holes
    with literal text between them, seven pieces (outside the fold), a nested
    f-string, and a hole-free control. Plain `+` is deliberately absent: a golden
    over it would pin what §13.4 leaves unspecified.
  - **Negative control.** `git stash push src/tychoc.c`, rebuilt, re-ran the
    fixture: the output diverged from the golden in all five ordered cases
    (`diff` rc=1, every `call` line displaced). Restored with `git stash pop`,
    rebuilt, `cmp` against the golden clean again.
  - **Gates.** `make test` → `passed: 619  failed: 0  all green`, against the 618
    recorded earlier in this plan — +1 is exactly the new fixture, no silent loss.
    Re-run once after the citation re-anchor, still 619. `sh scripts/spec_check.sh`
    green (Appendix E fixture citations resolve, 9 runnable examples pass).
    Inserting into `src/tychoc.c` staled 81 citations, all of them into that one
    file and nothing else stale — the re-anchor tool's documented safe case;
    `--apply` rewrote 116 files with 0 needing a human, and `check_citations.py`
    and `check_links.sh` are green after it.

- [x] **The same right-to-left hazard is still live for plain `+` and call arguments**
  - Found by the f-string phase above, not absorbed into it — that phase was
    scoped to f-strings, and this is a different (and spec-sanctioned) case.
  - `a() + b()` on strings, and `f(g(), h())`, still fire right-to-left under gcc.
    §13.4 permits it, so this is **not a compiler bug** and MUST NOT be "fixed" by
    widening the f-string pin without deciding to change the language rule.
  - What is missing is that a reader has no way to discover the hazard before it
    bites: §13.4 states the rule in one line and gives no example of what it looks
    like when it goes wrong, which is exactly how the f-string case survived long
    enough to produce a wrong golden.
  - Scope: `docs/spec/09-expressions.md` §13.4 only — a worked example showing the
    reversed order and the explicit-binding fix the rule already tells you to use.
    A `.ty` fixture is NOT appropriate here: pinning unspecified order in a golden
    is exactly what the rule forbids relying on.
  - Done when: §13.4 carries the example. Optionally, decide whether a lint for a
    multi-call string `+` is worth it — a separate question, do not assume yes.
  - Verify: `sh scripts/spec_check.sh` and `make check-links`. **Not `make test`** —
    a spec-prose change cannot redden it.
  - **Evidence (2026-08-11).** The hazard is still live, probed rather than
    assumed. The fence now in the spec, run verbatim:

        call B
        call A
        AB
        call B
        call A
        AB

    First pair is `side("A") + side("B")`, second is `join2(side("A"),
    side("B"))` — both fire **B first** and both still print `AB`. The same
    program's f-string form fires `A` then `B`, which is the pin `680d30d` put
    in, so the two cases visibly differ today.
  - **No `output` fence is paired with the example**, so `spec_examples.sh` does
    not run it and no golden records the order — pinning it is exactly what
    §13.4 forbids relying on. `docs_fences.sh` still parses and typechecks it
    (`ok docs/spec/09-expressions.md:177`); the three-line explicit-binding
    snippet below it is a FRAGMENT and skipped, as intended.
  - The prose states which two exceptions ARE pinned and why the reasons do not
    reach an operand list: no short-circuit can cut across a place index or an
    f-string hole, and both had an order that was already visibly contradicted
    (a compiler divergence; holes printed left to right but fired right to
    left). An argument may sit inside `f(x, cond and g())`, where lifting it
    would evaluate what the short-circuit exists to avoid.
  - Not done, and deliberately left open: the optional lint for a multi-call
    string `+`. Nothing here decides it either way.
  - Gates: `make check-links` ok (119 files), `sh scripts/spec_check.sh` exit 0
    (11 runnable examples, unchanged — the new fence is not one), `make
    docs-fences` 50 compiled / 69 skipped / 0 failures.

- [x] **Phase N — no gate rejects a raw control byte in a tracked Markdown file**
  - Found by the `core:json` phase above, not absorbed into it. Writing that
    phase's prose put a literal `0x00` into `docs/internals/FRICTION.md`, and
    again into `plan.md`, from text that meant to *name* the escape `\u0000`.
    Both survived `python3 scripts/check_citations.py` and
    `sh scripts/check_links.sh` — neither looks at bytes — and were found only
    because `grep` went silent on a file it now considered binary. A NUL in a
    tracked doc is invisible in every renderer and breaks line-oriented tools
    without an error.
  - Scope: `scripts/check_links.sh` or a new few-line gate. The check is one
    pass over the tracked `*.md` set for bytes `00-08 0B 0C 0E-1F`, naming the
    file and the byte offset. TAB and LF stay legal.
  - Done when: the gate exists, names the offending file and offset, and a
    deliberately NUL-bearing scratch file is observed reddening it.
  - Verify: the new gate, plus `sh scripts/check_links.sh`. **Not `make test`** —
    no `.ty` file is involved.

  **Evidence (2026-08-11).** Wired into `scripts/check_links.sh` rather than
  added as a lane, per the filing: it is the sub-second doc gate everyone
  already runs, and it already walks `git ls-files '*.md'`.

  Byte set, as filed: **00-08 0B 0C 0E-1F rejected; TAB, LF and CR legal** — the
  three that carry meaning in Markdown, and rejecting them would redden the whole
  tree. DEL (7F) is deliberately NOT rejected: it is not C0, it is not what bit
  us, and the set stays the one the filing reasoned about. Detection is one
  `LC_ALL=C tr -d '\11\12\15\40-\377'` per file — deleting every legal byte leaves
  exactly the rejected set — and `od` runs only for a file that already failed,
  so the green path never pays for it.

  Scope is `*.md`, not all tracked text. Tracked binaries (the PNG fixtures) are
  full of NULs by construction, so a whole-tree check would need a binary
  exclusion list: more machinery than this failure is worth, and `*.md` is both
  where it happened and the set the host gate already globs.

  **Whole tree passes**, so nothing in it legitimately holds a rejected byte:

      $ time sh scripts/check_links.sh
      link check: ok (119 markdown files, no dead relative links; 126 free of raw control bytes)
      sh scripts/check_links.sh  0.45s user 0.14s system 121% cpu 0.488 total

  **Negative control.** A scratch `docs/_ctl_scratch.md` holding
  `ok tab\there\r\nnow NUL:\x00 and VT:\x0b end`, `git add`ed so `git ls-files`
  sees it:

      CTRL  docs/_ctl_scratch.md  byte offset 21 (0x00)
      CTRL  docs/_ctl_scratch.md  byte offset 30 (0x0B)
      link check: FAILED (dead links or raw control bytes above)
      exit=1

  Both offsets are correct by hand-count, both bytes are named, and the TAB, CR
  and LF in the same line were NOT flagged — so the check discriminates rather
  than rejecting whitespace. Removed and unstaged; re-run returns `exit=0` and
  the ok line above. The scratch file is not committed.

  **A wrong claim, caught by testing it.** The first draft carried a comment
  saying `[ ... ] && continue` would abort the loop's subshell under `set -e`
  and leave the rest of the corpus unchecked. That is false — POSIX exempts a
  command that is not the final operand of an `&&` list — and
  `sh -c 'set -eu; printf "a\nb\n" | while read -r x; do [ "$x" = zzz ] && continue; echo "saw $x"; done'`
  printed both lines and exited 0. The `if` form was kept; the false comment was
  deleted rather than shipped.

- [x] **FRICTION #5 — an enum could not be asked which variant it holds; added `is`**
  - Authorised directly by the repo owner ("just add `is`"), so this phase was
    created after the fact rather than planned.
  - Scope: `src/tychoc.c` lexer/parser/resolver/codegen, `tools/tychofmt.ty`,
    `tools/lsp.ty`, `editors/`, `docs/spec/`, three `tests/` fixtures.
  - **Design, as shipped.** `v is V` -> `bool`, true iff `v` holds variant `V`.
    Uniform over nullary and payload-carrying variants; binds nothing; does not
    chain. Precedence sits between the additive and comparison levels, so
    `a is X and b is Y` groups as `(a is X) and (b is Y)`. The right operand is
    a variant NAME, not an expression — parsing it as one would hit the
    payload-carrying variant's "write V(...)" rejection at
    `src/tychoc.c:5638@carries a payload`, which is the whole gap.
  - **`is` is a RESERVED word, not contextual.** The grep that decided it, over
    every `.ty` in the tree with strings and comments stripped:

    ```
    $ python3 - <<'EOF'   # \bis\b outside strings/comments, all *.ty
    ...
    EOF
    0 hits
    ```

    and separately `grep -rnE '\.is\b' --include='*.ty' .` -> no output, so no
    field named `is` either. Nothing in the tree used the name, including the
    frozen `compiler/tychoc0.ty`. The `pass` precedent (contextual, because two
    files already used the name — `docs/spec/appendix-b-keywords.md` B.2) did
    not apply. Reserving follows `in`, the language's other binary-operator
    keyword.
  - **A bad variant name is a compile error naming both**, reusing the match
    arm's wording verbatim (`src/tychoc.c:7927@is not a variant of`):

    ```
    tests/reject/enum_is_unknown_variant.ty:10: error: 'VFloat' is not a variant of Value
    tests/reject/enum_is_not_an_enum.ty:5: error: `is` asks an enum value which variant it holds; int is not an enum
    ```

  - **Cross-package spelling is `match`'s, exactly.** `is` reuses
    `pkg_prefix_for`/`pkg_mangle`/`check_pkg_private`, so `c is shapes.Circle`
    works and a bare `c is Circle` on an imported enum is refused with the same
    message a bare match arm gets (`'Circle' is not a variant of shapes__Shape`).
    Both observed on a scratch two-package program.
  - **Emitted C** — the same tag test `match` already emits, for
    `println("a is VInt  = " + str(a is VInt))`:

    ```c
    tycho_bool_to_str(&_t, ((h_a)->tag == 1))
    ```

  - **Negative control.** With `src/tychoc.c` reverted to HEAD and rebuilt, the
    fixture does not compile at all — every one of its 15 golden lines is lost,
    not just the `is` ones:

    ```
    tests/enum_is.ty:11: error: expected ':' before the block
        11 |     if v is VNull:
    ```

    Restored, rebuilt, `diff` against the golden clean. Worth recording that the
    two reject fixtures are NOT a control: they exit non-zero at HEAD too, for
    the syntax error rather than the semantic one, and `tests/run.sh:291-306`
    asserts only a non-zero exit with a non-empty diagnostic, never the text.
  - **`v == VNull` still works** and is deliberately untouched; the overlap for
    nullary variants is documented in `docs/spec/09-expressions.md` §13.2 rather
    than removed.
  - Gates, each run once: `make check-links` ok (136 anchored, 835 bare,
    190 `path@SYMBOL`) after `scripts/reanchor_citations.py --apply` remapped
    **101 stale `src/tychoc.c` citations across 116 files, 0 needing a human** —
    every stale ref was a `src/tychoc.c` one, which is the tool's stated
    precondition. `sh scripts/spec_check.sh` ok, 10 runnable examples all pass
    (the new §19.8 example among them). `make editors-check` ok, 940 files.
    `sh scripts/tools_check.sh` ok. `make goldens-check` ok — it reddened first,
    correctly, on `tests/enum_is.out` being untracked. **`make test`: 622
    passed, 0 failed**, against 619/0 before: +1 positive fixture and +2 rejects,
    no silent loss.

- [x] **`is` has no answer for `Option` and `Result`, the two enums used most**
  - Discovered by this phase and deliberately NOT absorbed into it. `v is Some`,
    `r is Ok`, `r is Err` are all rejected: the resolver's `is` arm requires
    `IS_ENUM(lt)`, and `Option`/`Result` are not user enums in that sense, so
    the message a reader gets is "`is` asks an enum value which variant it
    holds; Option(int) is not an enum" — accurate about the implementation and
    unhelpful about the language.
  - Why it matters more than it looks: FRICTION #5's own workaround survey found
    the gap in `tools/tycho-q/main.ty@kind`, but `Option`/`Result` are where
    "which variant is this?" is asked most often in this tree, and `or_return`
    only covers the propagate-it case, not the test-it case.
  - Scope: the `TK_IS` arm in `resolve_expr`, plus whatever the two types'
    runtime discriminator is — it is NOT the `->tag` field user enums use, so
    this is not a one-line widening and must not be guessed at.
  - Done when: `r is Ok` / `r is Err` / `o is Some` / `o is None` compile to the
    right test, with a fixture covering all four and a `tests/reject/` case for a
    name that is neither.
  - Verify: `make test`, which was **622** at this phase.

### Evidence — `is` for `Option` and `Result`

**Verdict: (a) CONTAINED.** The phase that filed this was right that the
discriminator is not `->tag`, and wrong that this made it large. Probed with
`./tychoc /tmp/probe_isopt.ty --emit-c`, both types are **by-value structs whose
discriminator is a plain leading field**:

```c
struct TychoOpt0_ { char has; tycho_int val; };
struct TychoRes0_ { char ok; tycho_int okv; char *errv; };
```

and `match` branches on exactly that field — no unwrap, no helper, no tag table:

```c
TychoOpt0 _m1 = h_o;
if (_m1.has) { tycho_int h_x = _m1.val; ... } else { ... }      /* Some / None */
TychoRes0 _m2 = h_r;
if (_m2.ok) { tycho_int h_v = _m2.okv; } else { char *h_e = _m2.errv; }  /* Ok / Err */
```

`or_return` reads the same field: `if (!_or0.ok) { ... return _rr0; } _or0.okv;`.
So `is` reuses the discriminator `match` already uses, exactly as `810c8c3` reused
`->tag` — no representation change, no unification of two mechanisms. Three edits,
smaller than `810c8c3`'s: an `IS_OPT`/`IS_RES` branch in the `TK_IS` resolver arm
(`src/tychoc.c@is not a variant of`), a two-way branch in the `TK_IS` codegen
(`src/tychoc.c:10651-10657`), and the package-mangling exemption for the four
builtin names in `parse_is` (`src/tychoc.c:3066-3071`) — the same exemption the
match-arm parser already carries at `src/tychoc.c:3320@"Err"`.

**Design decisions, checked against the source rather than against Rust:**

- Names are `Some`/`None` and `Ok`/`Err`, the constructors' own spellings
  (`src/tychoc.c:2797-2811`) and the match arms' (`src/tychoc.c:7868-7894`).
- `is` binds nothing, unchanged: `parse_is` reads one `TK_IDENT` and never an
  `LPAREN`, so `o is Some(x)` cannot parse as a binding.
- Wrong family is a **compile error** naming the type and both valid names:
  ``error: 'Some' is not a variant of Result(int, string); write `Ok` or `Err` ``
  Pinned by `tests/reject/optres_is_wrong_family.ty`.
- The four names are never package-qualified, matching the match arm.
- The non-enum message widened to "`is` asks an enum, Option or Result value
  which variant it holds; %s is none of those", since the old wording is now
  false.

**Emitted C for the new form** (from `tests/optres_is.ty --emit-c`):

```c
tycho_bool_to_str(&_t, ((h_some).has != 0))   /* some is Some */
tycho_bool_to_str(&_t, ((h_some).has == 0))   /* some is None */
tycho_bool_to_str(&_t, ((h_ok).ok != 0))      /* ok is Ok    */
tycho_bool_to_str(&_t, ((h_ok).ok == 0))      /* ok is Err   */
```

`!= 0` / `== 0` rather than the bare field because the field is a `char` and
`bool` is emitted as `int` (`src/tychoc.c:1703`).

**`810c8c3` unchanged.** `tests/enum_is.ty` was not touched, and its output
`cmp`s byte-identical to `tests/enum_is.out`; the emitted C still carries 23
`->tag ==` tests, the enum arm being untouched.

**Negative control.** `git stash push src/tychoc.c`, `make tychoc`, then:

```
tests/optres_is.ty:15: error: `is` asks an enum value which variant it holds; Result(int, string) is not an enum
    15 |     if r is Ok:
```

The positive fixture stops compiling at the first `is Ok` — `make test` would
score it `tychoc failed`. `tests/enum_is` stayed byte-identical without the
patch, confirming the control isolates the new code and not the old.
**Honest limit:** `tests/reject/optres_is_wrong_family.ty` does NOT redden under
the control. It still exits non-zero with a diagnostic — the old, wrong one —
and `tests/run.sh:291-302` scores a reject on exit status and a non-empty
message only, never on the message's text. The reject fixture pins that the
program is refused, not that the wording is the family-specific one; the wording
is pinned only by this evidence block. Restored with `git stash pop`; the
fixture is byte-identical to its golden again.

**Gates.** `make check-links` ok (reanchored first: 116 files rewritten, 0 needing
a human, then 138 anchored + 865 bare + 181 source→doc all resolve).
`sh scripts/spec_check.sh` ok, 11 runnable examples all pass — the new §19.8
Option/Result example among them, up from 10. `make editors-check` ok, 943 files,
known-bad set unchanged: **no tooling change was needed**, checked rather than
assumed — `is` was already reserved and the four names are ordinary identifiers
to the grammar. `make vm-check` green. `make goldens-check` ok, 446 goldens (it
reddened first, correctly, on `tests/optres_is.out` being untracked; the fix was
`git add`, the `.gitignore:124` un-ignore already covering `tests/*.out`).
**`make test`: 625 passed, 0 failed**, against the 623/0 baseline: +1 positive
fixture, +1 reject, no silent loss.

- [x] **`sig_find`'s `Sig *` dangles across argument resolution (live bug on `main`)**
  - Symptom: `./tychoc tools/tycho-vm/main.ty` died with
    `tools/tycho-vm/main.ty:967: error: argument 1 of 'print' is inout; pass it
    as '&variable'`. The claim is false — `print` is declared
    `.params={ T_STRING }, .nparams=1` with no inout at `src/tychoc.c:5082`.
    `make ci` was red at `[3g] vm-check`.
  - Mechanism: `sig_find` (`src/tychoc.c@sig_find`) returns `&g_sigs[i]`, a
    pointer INTO the growable table. The E_CALL resolver took that pointer and
    held it live across `resolve_exp(e->args[i], s->params[i])`, which can
    instantiate a generic and append to `g_sigs` — realloc'ing it and leaving
    the pointer dangling for every later `s->inout[i]`, `s->params[i]`,
    `s->builtin` and `s->ret` read.
  - Why it surfaced when it did: `17c47c4` changed only `corelib/`
    (`io.set_mtime` + one `extern fn`). `src/tychoc.c` is byte-identical across
    that boundary. Two extra sigs pushed the table over a doubling boundary that
    the same argument loop happened to straddle — the defect was latent, and a
    corelib addition merely exposed it.
  - Fix: snapshot the index and re-derive after each append-capable call, the
    idiom this file already uses for the same hazard at `src/tychoc.c:5378` and
    `src/tychoc.c:5495`. Chosen over copying the `Sig` by value because the
    index stays correct if a `Sig` is ever legitimately mutated in place, and
    because it costs two lines rather than a ~280-byte struct copy per call.

### Evidence

**Realloc proof** (temporary `fprintf` around the call, since removed; the
diagnosis was NOT taken on trust):

```
DBG line 967: 'print' arg 0: g_sigs 0x5589da9c2c70(cap 128) -> 0x5589da9def10(cap 256), s=0x5589da9c2c70 DANGLING
```

The base pointer moves during the argument loop and `s` still names the freed
block. `print` is table index 0, so `s` is literally the old base.

**Caller audit** — every `sig_find(` call site and every other pointer into
`g_sigs`, checked for the same pattern. The question asked at each: is the
pointer held across a call that can append to `g_sigs`? The only appenders
reachable during resolution are `instantiate_generic`, `resolve_parfor` and
`resolve_program`.

| Site | Verdict |
|---|---|
| `src/tychoc.c:6556` (E_CALL) | **EXPOSED — fixed.** Held across `resolve_exp` |
| `:5484` (E_SPAWN) | Clear. `resolve_expr(c)` runs BEFORE `sig_find`; already stores an index |
| `:5645`, `:5648` (fn-as-value) | Clear. Only `note_fnval` (appends to `g_fnval`) and `funcc_of` intervene; neither mentions `g_sigs` |
| `:5921`, `:5924`, `:5930` (UFCS on qualified) | Clear. Derefs are immediate; `ufcs_generic`/`type_pkg_prefix` do not touch `g_sigs` |
| `:6000`, `:6003`, `:6009` (UFCS on field) | Clear, same shape |
| `:6069`, `:6081`, `:8394`, `:8652`, `:8658` | Clear. Truthiness test only, never dereferenced |
| `:6511` (variadic probe) | Clear. All derefs before any append-capable call |
| `:7463` (`Sig *sg`, parallel-for) | Clear. Unused after `resolve_block`; already carries `sg_id` as an index |
| `:8684` (`main` lookup) | Clear. NULL-tested immediately, never used again |
| `:9823`, `:10131`, `:13367`, `:13407` | Clear. Emit phase. Generic-instance bodies are all resolved in `gen_program`'s dedicated pre-pass loop, which finishes before any emit loop, so `gen_expr` cannot reach `instantiate_generic` |
| `:10475`, `:13330`, `:10995`, `:11005` | Clear. Index-based (`g_sigs[g_spawn[i]]`, `g_sigs[pf->sig]`), not pointers |

**Regression fixture** — `tests/generic_sig_realloc.ty`. 200 distinct generic
instantiations inside the arguments of `print`/`str`, which crosses at least one
realloc boundary whatever capacity the table starts at (instantiations added
exceed the headroom of any plausible starting cap). Honest limits, measured
rather than asserted:

- The *mechanism* reproduces deterministically. Instrumented on the unfixed
  compiler, the fixture crossed `cap 64 -> 128` inside `print`'s argument loop
  with the pointer stale.
- The *symptom* is allocator-dependent, and the first version of this fixture
  was theatre: it crossed the boundary and still read back
  `s->nparams=1 inout0=0`, compiling clean on the broken compiler. Two changes
  made it redden — targeting `print` (table index 0, where the first
  post-realloc allocation lands) rather than `println` (index 1, ~280 bytes
  further into the freed block), and fattening the generic bodies so
  instantiation allocates enough to overwrite the freed slot before it is read.
  With both, the unfixed compiler reports `stale s->nparams=21866 inout0=21866`
  and dies with the production error message.
- So: this fixture reddens today and its comment says which property is
  load-bearing, but a future allocator or a trimmed body could make it silent
  while still crossing the boundary. `make vm-check` remains the lane that
  caught this for real.

**Negative control.** Fix reverted (`git stash`), rebuilt:
`./tychoc tools/tycho-vm/main.ty` → `error: argument 1 of 'print' is inout`, and
`./tychoc tests/generic_sig_realloc.ty` → the same error at its line 254.
Restored and rebuilt: both compile clean, and the fixture's output matches its
golden.

**Gates.** `make vm-check` green (was the red lane). `make check-links` green —
after `python3 scripts/reanchor_citations.py --apply`, which the two inserted
lines staled: 39 anchored citations reported, 116 files remapped, 0 needing a
human; the re-run reports 138 anchored, 836 bare, 181 source→doc, 259
source→source, 192 `path@SYMBOL` all resolving. **`make test`: 623 passed, 0
failed**, against a 622/0 baseline — +1, exactly the new fixture, no silent loss.
`make corelib` not run and not needed: the only `corelib/` file touched is
`corelib/net/net_shim.c`, and only a `path:line` inside a comment.

- [x] **The gate table sends a corelib-only change to a lane that cannot redden for it**
  - The bug above was introduced by `17c47c4`, a commit that changed **only**
    `corelib/`. `CLAUDE.md`'s gate table (and its contributor-facing copy in
    `CONTRIBUTING.md`) says: "**A `corelib/` change** → `make corelib`, plus
    `make corelib-examples` … **Not `make test`, which cannot redden for it**".
    That rule is correct about `make test` and incomplete about everything else.
  - What it misses: a corelib change alters the compiler's global state — here,
    two extra `Sig` entries — in a way that can break an unrelated **consumer
    program**. `make corelib` builds `corelib/test/<pkg>/main.ty`, and
    `make corelib-examples` builds `examples/corelib/**`. Neither compiles
    `tools/tycho-vm/main.ty`. `make vm-check` is "**the only lane that runs
    anything under `tools/tycho-vm/`**" — by the table's own words — and it is
    not in the corelib row. So the commit was, by the book, correctly gated, and
    shipped a red `make ci` anyway.
  - This is a gap in the routing table, not in the lanes. Every tool lane
    (`vm-check`, `kv-check`, `q-check`, `ar-check`, `scheme-check`) has the same
    exposure, and `scripts/entrypoints.sh` — which does compile every entry point
    in the tree for milliseconds — is listed only under its own row.
  - Scope: `CLAUDE.md`'s "The rule" section and the trimmed copy under
    "Which gate for which change" in `CONTRIBUTING.md`. **They will drift; both
    must be edited.** Deliberately NOT fixed by the phase that found it.
  - Candidate fix to evaluate, not to assume: add `sh scripts/entrypoints.sh` to
    the corelib row as the cheap consumer-compile check. Verify first that it
    actually reddens for this class — recompile `tools/tycho-vm/main.ty` at
    `17c47c4` under it and confirm a red, rather than asserting it.
  - Done when: both files name a lane that compiles the tree's consumer programs
    for a corelib-only change, and that lane is shown to redden at `17c47c4`.
  - Verify: `python3 scripts/check_citations.py` and `sh scripts/check_links.sh`
    only — the phase edits Markdown. **Do not run `make test` or `make ci`.**

  **Evidence (2026-08-11). The candidate fix was WRONG, and measuring it is what
  showed that.** `scripts/entrypoints.sh` globbed `examples/*/` and appended
  `server/main.ty` — it never touched `tools/`. Checked out `17c47c4` in a
  worktree, deleted `tychoc` and rebuilt it there so the binary was genuinely
  that commit's, then ran the candidate unmodified:

      entrypoints: ok (11 entry points compile with tychoc)
      entrypoints exit=0

  Green at the commit that shipped a red `make ci`. Adding it to the corelib row
  as filed would have documented a check that does not check. The breakage is
  real and reproducible there — compiling each `tools/*/main.ty` by hand at that
  commit, 12 pass and one does not:

      FAIL  tools/tycho-vm/main.ty
      tools/tycho-vm/main.ty:967: error: argument 1 of 'print' is inout; pass it as '&variable'

  **So the fix is to close the lane's hole, then name it.** `scripts/entrypoints.sh`
  now also globs `tools/*/main.ty`, with `tools/tycho-vm/main.ty` added to its
  fail-closed MUST list and its floor raised from 6 to 18 so a broken glob still
  cannot go vacuous. On `main` it is green over the wider set and costs
  essentially nothing:

      entrypoints: ok (24 entry points compile with tychoc)
      exit=0  elapsed=.141452176s

  Copied that same script back into the `17c47c4` worktree — the decisive test:

      FAIL    tools/tycho-vm/main.ty
      entrypoints: FAILED (1 of 24 entry points do not compile)
      exit=1

  **The cost trade, measured rather than dodged.** Telling every corelib change
  to run every tool lane would be `vm-check`+`kv-check`+`q-check`+`ar-check`+
  `scheme-check` ≈ 12s — not ruinous, but it *still* misses the eight tool
  programs with no lane at all (`prunner`, `tycho-build`, `tycho-chess`,
  `tycho-debug`, `tycho-fetch`, `tycho-kvsrv`, `tycho-rsa`, `tycho-sat`). The
  0.15s compile-only sweep covers all thirteen. It buys strictly less per program
  — compile, not run — but it is the right instrument for THIS failure class,
  which is a consumer that stops compiling.

  Both tables updated together, as the standing rule requires: `CLAUDE.md` gains
  an `sh scripts/entrypoints.sh` row and a new bullet under "The rule";
  `CONTRIBUTING.md` gains the matching row under "Which gate for which change".
  Both spell out that the trigger is a **symbol** change, not any corelib edit.

  Gates: `sh scripts/entrypoints.sh` (24/24, exit 0) since a script changed,
  plus `make check-links`. Not `make ci`.

- [x] **Phase N — `tests/reject/` fixtures pin *that* a program is refused, never
      *why*** (*filed by the `core:image` phase; third independent sighting*)
  - `tests/run.sh:299-305` scores a reject fixture on two things only: `tychoc`
    exits non-zero, and `$TMP/rj.log` is non-empty. The message text is never
    read. A change that makes the compiler refuse a program for the WRONG reason
    — a parse error where a type error was meant, a diagnostic naming the wrong
    line or the wrong identifier — passes this lane unchanged.
  - Evidence, two phases that each hit it and each said so in their own commit
    rather than fixing it: `810c8c3` and `2fe0f6b`. Read both messages before
    designing the fix; they describe what their own fixtures could not pin.
  - Note what the lane DOES check, so the fix does not re-litigate it: the same
    loop already refuses a fixture carrying a `package` header
    (`tests/run.sh:295-298`), because a valid program dropped into that directory
    was measured scoring `ok` on a sibling's diagnostic. So the "is it even
    failing for its own reason" hole is half-closed already.
  - Candidate shape, to evaluate rather than assume: an opt-in `# expect: <substring>`
    comment in the fixture, asserted against `rj.log` when present. Opt-in keeps
    all 249 existing flat fixtures scoring exactly as they do now, which means the
    count must not move — state the expected `make test` figure in the brief and
    check it.
  - Verify: `make test` (~8 min), which is the lane that owns `tests/run.sh`, plus
    a deliberate negative control — a fixture whose `expect:` does not match must
    redden. **Not `make ci`.**

  **Evidence (2026-08-11).** The filing's `tests/run.sh:299-305` was right for
  the pre-edit file, and the `package`-header guard it describes is still at
  `tests/run.sh:295-298`. Both read before editing; the post-edit line numbers
  below are the ones on disk now.

  Shipped the filing's candidate shape, opt-in `# expect: <text>`, asserted with
  `grep -qF` against the captured diagnostic (`tests/run.sh:304-315`). Substring,
  not an exact golden, and deliberately not a fourth `.err` convention:
  **`tests/diag/` already does the exact-golden job** (`tests/run.sh:378-396`
  `cmp`s the whole stderr against
  `tests/diag/<name>.err`, `RECORD=1` re-records). A fixture that wants the full
  rendering — message, source snippet, caret, did-you-mean — should *be* a diag
  fixture. What `tests/reject/` was missing is cheaper and different: pin the
  REASON without owning the caret column and the line number, which move
  whenever a fixture gains a line and carry no signal. `grep -rl '^# expect:'
  tests/` was empty beforehand, so no existing fixture was silently opted in.

  Adopted for the three fixtures whose own commits reported the limitation —
  `tests/reject/enum_is_not_an_enum.ty`, `tests/reject/enum_is_unknown_variant.ty`
  (both `810c8c3`) and `tests/reject/optres_is_wrong_family.ty` (`2fe0f6b`).
  Every other reject fixture is untouched and scores exactly as before.

  **Negative control, run as the full gate, not a stand-in.**
  `tests/reject/enum_is_unknown_variant.ty`'s expectation was replaced with
  `# expect: NEGATIVE CONTROL this text is not in any diagnostic` and `make test`
  run to completion:

      passed: 624   failed: 1
      failed: reject_enum_is_unknown_variant
      make: *** [Makefile:157: test] Error 1

  One fixture, the one broken — the other two `# expect:` fixtures passed in that
  same run, so the check discriminates rather than reddening the lane wholesale.
  Restored, re-run: `passed: 625   failed: 0`, `all green` — the 625/0 baseline,
  unmoved, as an opt-in scheme requires.

  **Not fixed here, filed as its own phase below:** the comment at
  `tests/run.sh:287` still says "0 of the 249 flat fixtures declare one today".
  Its load-bearing claim (none declares a `package` header) still holds; the
  figure does not.

- [x] **Phase N — `tests/run.sh`'s reject-lane comment carries a stale fixture
      count** (*found by the `# expect:` phase, not absorbed into it*)
  - `tests/run.sh:287` reads "0 of the 249 flat fixtures declare one today".
    `ls tests/reject/*.ty | wc -l` disagrees. The claim the sentence exists to
    make — that the `package`-header guard enforces the arrangement rather than
    repairing a violation — is still true; only the count rotted, which is
    exactly the failure `CLAUDE.md`'s "never copy a figure the gate prints into
    prose" warns about.
  - Fix shape: name the command instead of the number, or drop the figure.
  - Verify: `make test` only if the edit touches anything but a comment; a
    comment-only edit is covered by reading it. **Not `make ci`.**
  - **Done 2026-08-11 — the figure was DROPPED, not updated, and the judgement
    was made first.** The brief offered "keep it as a floor if the script actually
    enforces one". It does not: `grep -n "floor\|vacuit\|-lt \|-gt \|at least"
    tests/run.sh` finds nothing of the kind anywhere in the file, and the
    `package`-header guard is per-fixture (`tests/run.sh:295-298` notes and fails
    that one fixture), never a minimum over the corpus. So the count was
    **decorative** — a snapshot of the directory on the day someone wrote it — and
    `CLAUDE.md`'s "never copy a figure the gate prints into prose" applies exactly
    as written. The comment now names the command:
    `` `grep -l '^package [A-Za-z_]' tests/reject/*.ty` prints nothing ``, which is
    the load-bearing claim and cannot go stale, and says in the same breath that
    nothing floors the fixture count.
  - **No floor was invented.** Adding one would be executable behaviour in the
    lane, would put this phase on the ~8 min `make test`, and is a different
    decision from fixing a comment — so it is not smuggled in here.
  - Both numbers re-measured before editing: **283** flat fixtures today (the
    brief's figure is right; the comment's 249 was wrong twice over, appearing
    both as "all 249 of its siblings" and as the "0 of the 249" claim), and **0**
    of them declare a `package` header, so the sentence's real assertion survives
    intact.
  - Verified: comment-only, so no gate beyond the doc lane can redden — proven,
    not assumed, by `git diff` touching only `#` lines and by `sh -n tests/run.sh`
    → clean. `make test` deliberately NOT run.
    ```
    $ sh -n tests/run.sh          # syntax ok
    $ git diff --stat tests/run.sh
     tests/run.sh | 12 +++++++-----
    $ make check-links
    link check: ok · citation check: ok
    ```

- [x] **Phase N — `corelib/run.sh`'s dependency SKIP hides a package from its own
      gate, silently** (*filed by the `core:image` phase*)
  - `corelib/run.sh:69-72` skips a package whose `deps` do not resolve via
    pkg-config, printing `skip <name> (missing dependency: ...)`. That is the
    right behaviour — it is what keeps `make ci` green on a host without libpng —
    but the verdict line at the end is `corelib: all green`, which is also what a
    host that ran everything prints. The two are indistinguishable in a log.
  - Measured on this box 2026-08-11: `image` skipped, because libpng's runtime is
    installed and its dev package is not. The `core:image` phase would have
    shipped an unrun fixture and a golden nobody executed, had it not checked
    `pkg-config --exists libpng` by hand first. `crypto`, `http` and `tls` are in
    the same position on any such host.
  - Candidate fix, to evaluate: make the summary count them —
    `corelib: all green (N ran, M skipped: image tls)` — so a log shows what was
    not covered. Same question for `examples/corelib/run.sh`, which has the
    identical shape, and for `make shim-check`, which already prints its skip
    count and is the model to copy.
  - Deliberately NOT fixed by the phase that found it: it changes a gate's
    output, which is a `make ci` step's text.
  - Verify: `make corelib` and `make corelib-examples`, plus `make shim-check` if
    its summary is touched. **Not `make test`.**
  - **DONE 2026-08-11.** Fixed in `corelib/run.sh` only; `examples/corelib/run.sh`
    and the other silent-skip lanes are filed as their own phase below (scope lock).

    **Line check first.** The brief cited `corelib/run.sh:69-72`. The pkg-config
    block was `:69-73` and the skip's `echo` was on `:72` — close enough to find
    it, but the block's last line was outside the range. Post-fix the skip is
    `corelib/run.sh:76-77` and the verdict is `corelib/run.sh:101-105`.

    **Skip survey (step 1).** Every dependency/environment skip reachable from a
    gate, and whether the lane's own final line admits it:

    | Lane | Skip trigger | Announced in the verdict? |
    |---|---|---|
    | `corelib/run.sh:76` | any `--print-deps` pkg name failing `pkg-config --exists` | **was SILENT** — printed `skip <name>` mid-run, then `corelib: all green`. Fixed here |
    | `examples/corelib/run.sh:38` | identical pkg-config test, identical shape | **SILENT** — ends `corelib examples: all green`. Filed below, not fixed |
    | `scripts/locale_check.sh:68` and its two siblings | no `locale(1)`, no comma-decimal locale, preload does not take | **LOUD** — `locale-check: SKIP (<reason>)`, exit 0, and the skip *replaces* the ok line. This is the model copied here |
    | `scripts/shim_check.sh@skipped` | a shim with no standalone-compile path | **LOUD** — counted in `shim-check: $ok ok, $skipped skipped, $fail failed` |
    | `examples/fetch/run.sh:45` | `libcurl` absent | **LOUD** — `fetch: SKIP (...)` is the only line; the ok line never prints |
    | `examples/sqlite/run.sh:29`, `bench/dbquery/run.sh:22` | `libsqlite3` absent | **LOUD** — same shape, whole lane exits 0 with only the SKIP line |
    | `server/run.sh:44` | no `python3` | **LOUD** — same shape |
    | `compiler/selfhost.sh:65` | Windows | **LOUD** — same shape |
    | `Makefile:617` (ilp32 ASan leg) | 32-bit ASan runtime absent | **LOUD** — prints `ilp32: ASan lane SKIPPED ...` and `Makefile:612` refuses to skip the lane itself |
    | the ASan/TSan legs in `examples/site/run.sh:56`, `examples/raytrace/run.sh:40`, `examples/mandelbrot/run.sh:51`, `tests/conc/run.sh:63`, `tests/ffi/run.sh:53` | mingw/macOS sanitizer runtime absent | **LOUD by construction** — each rewrites the summary's own label, e.g. `examples/site/run.sh:69` sets `SAN="ASan SKIPPED (no mingw runtime)"` so the final line carries it |
    | `bench/*/run.sh` `(build skipped)` | a comparison toolchain absent | out of scope — `make bench` is not a gate, and `bench/dbquery/run.sh:42` already records this exact concern |

    So the SILENT pattern is **two lanes, not one**: this one and the corelib
    examples lane. Everything else was already honest.

    **Both summary lines (step 3).** The skip case, on this box as-is (libpng
    runtime present, dev headers absent):

    ```
    $ make corelib
    ...
    corelib: 45 ok, 1 SKIPPED -- image(missing: libpng) -- NOT all green (those packages were not run)
    ```

    And the not-skipped case, against the static libpng 1.6.50 the `core:image`
    phase built into `/tmp/pngdev` (still on disk; nothing in the repo and nothing
    installed system-wide):

    ```
    $ PKG_CONFIG_PATH=/tmp/pngdev/lib/pkgconfig make corelib
    ...
    corelib: all green (46 ok, tychoc matches goldens)
    EXIT=0
    ```

    45 + 1 = 46, which is the check that the skip is the *only* difference between
    the two runs. Exit status stays 0 in both: a missing optional dependency is
    still not a failure, only a fact the verdict now states.
  - Gates: `make corelib` both ways above · `make check-links` →
    `link check: ok (119 markdown files, no dead relative links)` ·
    `python3 scripts/check_citations.py` → ok. `make test` deliberately not run
    (its corpus never descends into `corelib/`).
  - Docs: the `make corelib` row in `CONTRIBUTING.md` (tracked) and in `CLAUDE.md`
    (gitignored) both gained the skip behaviour, per the rule that the two move
    together. Neither quoted the old verdict string, so neither was stale — this
    is an addition, not a repair.

- [x] **Phase N+1 — `examples/corelib/run.sh` has the same silent skip**
  - `examples/corelib/run.sh:38` skips an example whose pkg-config dependency is
    absent and the run still ends `corelib examples: all green`, exactly the
    defect just fixed one directory over. On this box the `image` example is the
    one that vanishes.
  - Fix is the same shape as `corelib/run.sh:101-105` — count skips, name them in
    the verdict, keep exit 0. Copy it rather than re-deriving it.
  - Verify: `make corelib-examples` (~44s) twice, once as-is and once under
    `PKG_CONFIG_PATH=/tmp/pngdev/lib/pkgconfig`, the same both-ways check used
    above. **Not `make test`, not `make ci`.**
  - **DONE 2026-08-11.** `corelib/run.sh`'s fix copied, not re-derived.

    **Line check.** The brief cited `examples/corelib/run.sh:38`, and that was
    exactly the skip's `echo` — the first citation in this chain that landed on
    the line it named. Post-fix the counters are `examples/corelib/run.sh:20-22`,
    the skip is `:41-44` and the verdict is `:54-61`.

    **Skip survey, this script only.** One dependency skip, the pkg-config test
    at the old `:38` — now counted. The other two `continue`s are not skips of a
    real example: `examples/corelib/run.sh:21` is the empty-glob guard (`[ -e
    "$entry" ] || continue`, which fires only when the glob matches nothing), and
    the `RECORD=1` branch at `:48` re-records rather than skipping, so it counts
    toward `ran` exactly as `corelib/run.sh` counts it. Every other lane was
    already surveyed by the phase above and found LOUD; nothing new was found,
    and the SILENT pattern is now zero lanes rather than two.

    **Both summary lines.** As-is on this box (libpng runtime present, dev
    headers absent):

    ```
    $ make corelib-examples
    ...
    corelib examples: 36 ok, 1 SKIPPED -- image(missing: libpng) -- NOT all green (those examples were not run)
    EXIT=0
    ```

    And with the skip not happening, against the same static libpng 1.6.50 in
    `/tmp/pngdev` the phase above used (nothing in the repo, nothing installed
    system-wide):

    ```
    $ PKG_CONFIG_PATH=/tmp/pngdev/lib/pkgconfig make corelib-examples
    ...
    corelib examples: all green (37 ok, tychoc matches goldens)
    EXIT=0
    ```

    36 + 1 = 37 — the skip is the only difference between the two runs. Exit 0
    both ways, deliberately: a missing optional dependency is still not a
    failure, only a fact the verdict now states.
  - Gates: `make corelib-examples` both ways above · `make check-links` →
    `link check: ok (119 markdown files, no dead relative links)` ·
    `python3 scripts/check_citations.py` → ok (a `path@SYMBOL` ref was added to
    `CLAUDE.md`). `make test`, `make corelib` and `make ci` deliberately not run —
    none can redden for a change to this one shell script.
  - Docs: the `make corelib-examples` row in `CLAUDE.md` (gitignored) gained the
    skip behaviour it had never described, and the `corelib` row in
    `CONTRIBUTING.md` (tracked) gained one clause saying the examples lane skips
    and reports the same way. Changed together, per the standing rule.

- [x] **`tools/tycho-q/main.ty` carries the same false stderr claim, and names
      tycho-ar as its source** (*found by the tycho-ar stderr phase, 2026-08-11*)
  - `tools/tycho-q/main.ty:566-570`: "`die` is the only route to stderr in this
    language -- the builtins are `print`, `println`, `die` and `exit(n)`, with no
    `eprintln` (**the tycho-ar plan, carried forward**) -- so an error is
    necessarily fatal." Every clause of the premise is false; `eprint` has
    shipped since `61fa0dc` (2026-06-14). The parenthesis is the point: this is
    the tycho-ar comment propagating between tools, so a third copy is likely.
  - The conclusion it supports is still sound on its own — "a query that does not
    parse has no partial result worth printing" needs no language limitation —
    so this is the same shape of fix, not a behaviour change.
  - Done when: the premise is gone, the conclusion stands on its own reason, and
    a tree-wide grep for the claim (`grep -rn 'no eprintln\|only route to
    stderr' --include='*.ty' .`) is empty.
  - Verify: `python3 scripts/check_citations.py` and `make check-links`. Add
    `make q-check` **only if a line of code moves**, which it should not.
  - **Evidence (2026-08-11).** The brief's range was right for once: the claim
    is exactly `tools/tycho-q/main.ty:566-570`, five comment lines, replaced by
    five. The builtin is real:

    > Provenance: `src/tychoc.c:5084@eprint`

        g_sigs[g_nsigs++] = (Sig){ .name="eprint", .ret=T_VOID, .params={ T_STRING }, .nparams=1, .builtin=1 };

    `grep -rln eprint --include='*.ty' .` → 13 files. The rewritten comment
    states the real reason a parse error is fatal — a query that does not parse
    has no partial result, and stdout IS the result — and says the non-fatal
    channel exists and is deliberately unused, so the decision no longer reads
    as forced.
  - **The propagation stops here, in `.ty`.** `grep -rn 'no eprintln\|only
    route to stderr' --include='*.ty' .` is now empty, and
    `grep -rn eprintln --include='*.ty' tools/ server/ examples/ corelib/` is
    empty too — no third copy exists. No line of code moved.
  - **One site left standing, deliberately:** `plan.md:851-854`, the mtime
    phase's evidence, still argues `x` must die because "there is no
    `eprintln`". That is a completed phase's record rather than a live comment,
    and the open entry below ("Should `x` warn rather than die…") already
    carries the re-judgement, so the record is left as written rather than
    retconned. Nothing outside `plan.md` repeats it.
  - Gates: `make check-links` ok (119 markdown files, 142 anchored / 910 bare
    citations), `make q-check` green — 35-query transcript == golden, both
    renderers agree under `cmp`, all refusals still refuse with empty stdout.

- [ ] **Should `x` warn rather than die on a failed `set_mtime`?** (*found by the
      tycho-ar stderr phase, 2026-08-11*)
  - `66ce390` made a failed `io.set_mtime` fatal, and its stated reasoning cited
    the now-deleted "there is no eprintln" comment. With the false premise
    removed the decision needs re-judging on its merits, which is a behaviour
    change and deliberately was not made in the comment phase.
  - **The assessment, so this is decided rather than re-derived:** leave it
    fatal. `x`'s contract is that exit 0 means the tree is fully restored; a
    warned-past wrong mtime makes exit 0 mean "restored, mostly", and the only
    consumer that could act on the warning is a human reading a terminal.
    `tools/tycho-ar/run.sh` asserts extracted mtimes equal archived mtimes, so
    downgrading to a warning would also need that assertion weakened — which is
    the tell that the fatal version is the one the gate believes in.
  - So the likely outcome is **close as won't-do**, recorded here because the
    justification changed even though the behaviour should not.
  - If it is changed anyway: `make ar-check`, and the golden moves.

## Out of scope

`make ci` and standalone `make test` are not run as ritual — each phase runs only
the lane that can redden for what it touched. Pushing is the user's call, not a
phase's; phases commit and stop.
