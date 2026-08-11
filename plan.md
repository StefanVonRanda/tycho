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

- [ ] **Phase 3 — #7 gzip byte-determinism is undocumented and load-bearing**
  - Scope: documentation only, unless the probe finds it is not actually
    deterministic — in which case report, do not "fix" zlib.
  - Done when: the guarantee (or its absence) is stated where a caller reads it,
    with the probe that establishes it.
  - Verify: the probe re-run; `make check-links`.

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

## Out of scope

`make ci` and standalone `make test` are not run as ritual — each phase runs only
the lane that can redden for what it touched. Pushing is the user's call, not a
phase's; phases commit and stop.
