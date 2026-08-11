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
    `src/tychoc.c:5052@eprint`, runtime `runtime/tycho_rt.c@tycho_eprint`
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

- [ ] **Phase 6b — the `core:decimal` worked example never mentions `div`**
  - Found by phase 6, out of its scope. `examples/corelib/decimal/main.ty` has
    no `div`, no `half_up`/`toward_zero` and no `DivErr`: grep for `div` in it
    returns nothing. So the one place a reader goes to see the package used
    shows only the exact operations, which is the half of `core:decimal` that
    needed no explanation. The rounding-mode choice and the zero-divisor `Err`
    are the parts a caller gets wrong.
  - Done when: the example demonstrates `div` at both modes and the
    `Err(DivByZero)` path, with its golden re-recorded.
  - Verify: `make corelib-examples` (~44s). Not `make corelib`, not `make test`.

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

- [ ] **Phase 8 — tycho-q #6 add a cause-preserving `map_err_with` to `core:result`**
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

- [ ] **Phase 9 — tycho-q #7 `core:iter` has no fallible stage and spells predicates as `int`**
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

- [ ] **`tycho-ar x` still does not restore mtime, and its gate still cannot see that**
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

- [ ] **The same right-to-left hazard is still live for plain `+` and call arguments**
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

- [ ] **Phase N — no gate rejects a raw control byte in a tracked Markdown file**
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

## Out of scope

`make ci` and standalone `make test` are not run as ritual — each phase runs only
the lane that can redden for what it touched. Pushing is the user's call, not a
phase's; phases commit and stop.
