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

- [x] **Phase 1 — `split(s, "")` behaviour divergence**
  - The only entry that is not check-vs-no-check: tychoc aborts
    (`runtime/tycho_rt.c:1582`), tychoc0's `hi_split` returns the whole string as
    one element. Two compilers, two answers, same program.
  - Done when: both compilers abort identically on `split(s, "")`, the entry is
    gone from `ALLOW_MSG_TYCHOC_ONLY`, and a fixture locks it.
  - Verify: an abort fixture under `tests/abort/` (see `tests/run.sh` for how
    that lane works) plus the four gates above.

  **The change.** One line of `gen_strlib` (`compiler/tychoc0.ty:9982`, the sole
  `hi_split` emission site — called once, `:15999`). The empty-separator arm
  stopped guessing and now fails closed with the reference's exact text:

  ```
  - if (sl == 0) { Arr_str_push(ar, &r, substr(ar, s, 0, n)); return r; }
  + if (sl == 0) { fprintf(stderr, "tycho: split with an empty separator\n"); exit(1); }
  ```

  That is the WHOLE diff of tychoc0's emitted C for `tests/rtparity/surface.ty`
  (broad probe, ~3k lines) — before vs after, one line, nothing else moved.

  **1. The guard fires, identically on both sides.** `tests/abort/split_empty_sep.ty`
  (separator via an array element, so nothing folds it at compile time):

  ```
  === tychoc (reference) ===        === tychoc0 (self-hosted) ===
  tycho: split with an empty         tycho: split with an empty
  separator                          separator
  exit=1                             exit=1
  ```

  Both write to stderr and nothing to stdout; `cmp` on the two stderr streams
  says IDENTICAL, `od -c` confirms the trailing `\n` and no more:
  `t y c h o :   s p l i t   w i t h   a n   e m p t y   s e p a r a t o r \n`.

  Before the fix, the same program under tychoc0 printed `1` and exited 0 (built
  from `git show HEAD:compiler/tychoc0.ty`) — that was the divergence.

  **2. It does not fire on valid input.**
  - `make fixpoint` — `ok B == C : tychoc0 reproduces itself byte-identically
    (34428 lines C)`; `ok split tychoc0 (2 packages) self-hosts E==F`;
    `fixpoint: all green`. tychoc0 splits its own source on `chr(10)`
    (`compiler/tychoc0.ty:15789`), so a guard that over-fired could not self-host.
  - `make test` — `passed: 400   failed: 0` / `all green`, including
    `ok    abort_split_empty_sep`.
  - `make corelib` — `corelib: all green (tychoc and tychoc0 agree, match
    goldens)`, all 24 modules ok (`strings`, `path`, `httpd` and `csv` all call
    `split` with a non-empty literal; grep found no caller anywhere in
    `corelib/`, `examples/` or `tests/` passing an empty or non-literal
    separator).
  - Neighbouring cases checked by hand, tychoc vs tychoc0 byte-identical:
    `split("a,b,c", ",")`=3, `split("", ",")`=1 (empty STRING still yields one
    empty field — a different case from an empty SEPARATOR), `split("a::b::c",
    "::")`=3, `split(",a,", ",")[1]`=`a`, `split("x", "yy")`=`[x]`.

  **3. `make rtparity`** — the entry is deleted and the lane agrees:
  ```
  rtparity: env knobs         3 shared, 0 allowlisted difference(s) (ok)
  rtparity: diagnostics      19 shared, 8 allowlisted difference(s) (ok)
  rtparity: arena-stats rows  5 shared, 0 allowlisted difference(s) (ok)
  rtparity: the two runtimes agree on env knobs, diagnostics and arena stats
  ```
  8, down from 9; the diagnostic moved into the 19 shared (was 18).

  **4. What the fixture does and does not lock.** `tests/abort/` builds with
  tychoc only (`tests/run.sh:180` uses `$TYCHOC`), and so does the conc abort
  lane (`tests/conc/run.sh:79`) — there is no differential abort lane in the
  repo. So the fixture locks the REFERENCE side; on the tychoc0 side the lock is
  rtparity, which now requires the text to be present in tychoc0's emitted C and
  will fail if it is ever dropped. That is a text-level lock, not a
  fires-on-the-same-input lock — exactly the Pre-flight "Assuming" caveat, which
  is why the side-by-side run above exists. Closing it properly needs the abort
  lane made differential, which is outside this phase's allowed edits.

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

- [ ] **Phase 5 — make the abort lane differential** (discovered in Phase 1)
  - `tests/abort/` builds with `$TYCHOC` only (`tests/run.sh:180`), and so does
    the conc abort lane (`tests/conc/run.sh:79`). Every runtime trap this plan
    adds is therefore locked on the reference side only; on the tychoc0 side the
    lock is rtparity, which proves the TEXT exists in the emitted C, not that it
    fires on the same input.
  - Done when: `tests/abort/` asserts both compilers die with the same message
    and the same exit status on the same fixture.
  - Verify: flip one guard in tychoc0's emitted runtime and show the lane
    failing; restore; full suite green.
  - Scope: `tests/run.sh` (+ `tests/conc/run.sh` if it applies). No compiler
    changes.

- [ ] **Phase 4 — the four map 2^31 guards**
  - `[string:int]`, `[string:float]`, `[int:int]`, `[int:float]`
    (`runtime/tycho_rt.c:1794`, `:1937`, `:2124`, `:2263`). tychoc0 emits maps
    per-type with no cap.
  - Done when: all four entries are deleted, `ALLOW_MSG_TYCHOC_ONLY` is empty,
    and rtparity reports `0 allowlisted difference(s)` on every key.
  - Verify: as above. Do NOT allocate 2^31 entries — prove by source trace that
    the guard sits on the same growth path as the reference, and state plainly
    that it is unexercised at runtime.
