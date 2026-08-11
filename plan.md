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

- [ ] **Phase 1 — #5 `bytes` slices clamp, so a slice is not a bounds check**
  - Scope: `corelib/` bytes/string slicing, or a correction to the entry.
  - Done when: either a bounds-checking accessor exists with a fixture whose
    out-of-range case fails, or the entry is struck with the probe output.
  - Verify: `make corelib` green; the fixture's must-fail case observed failing
    before the fix.

- [ ] **Phase 2 — #6 There is no `eprintln`**
  - Scope: the builtin table in `src/tychoc.c` (`eprint` is registered there), or
    the entry.
  - Done when: `eprintln(s)` writes to stderr with a newline and has a fixture,
    or the entry is struck.
  - Verify: `make test` — this one touches `src/tychoc.c`, so it is the gate.

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

## Out of scope

`make ci` and standalone `make test` are not run as ritual — each phase runs only
the lane that can redden for what it touched. Pushing is the user's call, not a
phase's; phases commit and stop.
