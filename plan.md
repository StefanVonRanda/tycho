# Close every tychoc0 runtime gap on the rtparity allowlist

Supersedes the completed drift-guard/region-study plan (phases done in
`e8c7a54`, `6907e9d`; evidence is in those commits).

## Goal

`tests/rtparity/run.py` allowlists 9 differences where `tychoc` traps and
`tychoc0` does not. Close all 9 in `compiler/tychoc0.ty`'s emitted runtime and
delete each allowlist entry as it closes. Done = `ALLOW_MSG_TYCHOC_ONLY` is
empty, `make rtparity` green with an empty allowlist, and every gate green.

The lane already forces honesty here: a closed gap whose entry is still listed
fails as a stale entry, so an entry can only go away by the gap actually
closing.

## Pre-flight

- Worst case: a wrong guard in the emitted runtime aborts or miscompiles VALID
  programs — `tychoc0` builds itself, the corelib and every fixture, so a bad
  bound (off-by-one on a slice, a too-tight map cap) breaks the compiler for
  everyone, not just the error path. Second worst: the emitted C stops compiling
  and every tychoc0-built binary dies at `cc`.
- Reversibility: git; each phase is one commit, additive to a runtime emitted as
  string literals. No user data, no persistent state.
- Verified: the 9 entries and their reasons are enumerated at
  `tests/rtparity/run.py:79-103`; each cites the tychoc side that traps
  (`runtime/tycho_rt.c:93`, `:190`, `:858`, `:1582`, `:1794`, `:1937`, `:2124`,
  `:2263`; `src/tychoc.c:8475`, `:8494`). `make fixpoint` proves tychoc0 still
  compiles itself byte-identically (`compiler/fixpoint.sh:16-30`).
- Assuming: the trap TEXT is the whole contract. Two runtimes can print the same
  string on different conditions and rtparity cannot tell — so each phase must
  also prove the guard fires on the same input, not merely that the text exists.

## Phases

Every phase, without exception:
- edits ONLY `compiler/tychoc0.ty` (its emitted-runtime string literals) plus
  `tests/rtparity/run.py` (deleting the entries it closed) plus any new fixture;
- must NOT touch `runtime/tycho_rt.c` or `src/tychoc.c` — tychoc is the
  reference and is already correct;
- copies the reference wording EXACTLY, including `\n` and format specifiers, or
  rtparity will still see a difference;
- proves the guard FIRES: a program that trips it, built by tychoc0, dies with
  the message and a nonzero exit — and the same program under tychoc dies the
  same way. Paste both outputs;
- proves it does NOT fire on valid input: `make fixpoint`, `make test`,
  `make corelib`, `make rtparity`.

- [ ] **Phase 1 — `split(s, "")` behaviour divergence**
  - The only entry that is not check-vs-no-check: tychoc aborts
    (`runtime/tycho_rt.c:1582`), tychoc0's `hi_split` returns the whole string as
    one element. Two compilers, two answers, same program.
  - Done when: both compilers abort identically on `split(s, "")`, the entry is
    gone from `ALLOW_MSG_TYCHOC_ONLY`, and a fixture locks it.
  - Verify: an abort fixture under `tests/abort/` (see `tests/run.sh` for how
    that lane works) plus the four gates above.

- [ ] **Phase 2 — slice bounds check**
  - `xs[a:b]` is bounds-checked inline by tychoc (`src/tychoc.c:8475`, `:8494`);
    tychoc0's `Arr_*_slice` forwards lo/hi to `Arr_*_from` unchecked — reading
    out of bounds instead of aborting. Memory safety, not tidiness.
  - Done when: an out-of-range slice aborts with the reference text in both, the
    entry is deleted, and a fixture locks it.
  - Verify: as above. Cover a negative index and `hi > len`.

- [ ] **Phase 3 — allocation range + OOM traps**
  - Three entries: `reserve capacity %ld out of range`
    (`runtime/tycho_rt.c:190`), `string length %ld out of range` (`:858`),
    `out of memory` (`:93` — tychoc0's `blk_get` mallocs unchecked).
  - Done when: all three entries are deleted and each trap is demonstrably
    reachable.
  - Verify: as above. OOM is hard to trigger honestly — if you cannot force it
    without a fake allocator, say so in plan.md and prove that check by source
    trace rather than faking a pass.

- [ ] **Phase 4 — the four map 2^31 guards**
  - `[string:int]`, `[string:float]`, `[int:int]`, `[int:float]`
    (`runtime/tycho_rt.c:1794`, `:1937`, `:2124`, `:2263`). tychoc0 emits maps
    per-type with no cap.
  - Done when: all four entries are deleted, `ALLOW_MSG_TYCHOC_ONLY` is empty,
    and rtparity reports `0 allowlisted difference(s)` on every key.
  - Verify: as above. Do NOT allocate 2^31 entries — prove by source trace that
    the guard sits on the same growth path as the reference, and state plainly
    that it is unexercised at runtime.
