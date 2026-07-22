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

- [x] **Phase 2 — slice bounds check**
  - `xs[a:b]` is bounds-checked inline by tychoc (`src/tychoc.c:8475`, `:8494`);
    tychoc0's `Arr_*_slice` forwards lo/hi to `Arr_*_from` unchecked — reading
    out of bounds instead of aborting. Memory safety, not tidiness.
  - Done when: an out-of-range slice aborts with the reference text in both, the
    entry is deleted, and a fixture locks it.
  - Verify: as above. Cover a negative index and `hi > len`.

  **The predicate, read off the reference before writing anything.**
  `src/tychoc.c:8493` (arrays) and `:8474` (soa) emit the SAME guard inline:

  ```
  if (_lo < 0 || _hi > _sv.len || _lo > _hi) {
      fprintf(stderr, "tycho: slice [%ld:%ld] out of bounds (len %ld)\n", _lo, _hi, _sv.len); exit(1); }
  ```

  So `lo == len`, `hi == len` and an empty range are all LEGAL; only a negative
  `lo`, `hi > len`, and an inverted `lo > hi` trap. (`hi < 0` needs no arm of its
  own: `lo >= 0 && lo <= hi` already implies `hi >= 0`.) Confirmed by RUNNING
  tychoc first, not by reading alone — `xs[0:0] xs[n:n] xs[0:n] xs[1:1] xs[1:]
  xs[:2] xs[:]` on a 3-element array prints `0 0 3 0 2 2 3`, exit 0.

  **Emission sites changed** — both in `compiler/tychoc0.ty`, and those are all
  of them (`grep '_slice('` finds no third):
  - `:9892` `gen_arr_fns` — the single `Arr_<T>_slice` template, instantiated
    per element type: 11 copies in the rtparity probe, 28 in tychoc0's own C.
  - `:5254` `gen_soa_slice` — the inline soa sub-range. It had no temp to name
    the length, so it now binds source/lo/hi in a statement expression exactly
    as tychoc does (`src/tychoc.c:8473`); as a side effect the source, `lo` and
    `hi` are now evaluated ONCE instead of once per field.

  The whole diff of tychoc0's emitted C (11 identical array lines on
  `tests/rtparity/surface.ty` — nothing else moved — plus the soa line, taken
  from `tests/soa.ty`):

  ```
  - static Arr_int Arr_int_slice(Arena* ar, Arr_int a, long lo, long hi) { return Arr_int_from(ar, a.data + lo, hi - lo); }
  + static Arr_int Arr_int_slice(Arena* ar, Arr_int a, long lo, long hi) { if (lo < 0 || hi > a.len || lo > hi) { fprintf(stderr, "tycho: slice [%ld:%ld] out of bounds (len %ld)\n", lo, hi, a.len); exit(1); } return Arr_int_from(ar, a.data + lo, hi - lo); }

  - (Soa_Particle){ (h_q).f0 + (1), (h_q).f1 + (1), (h_q).f2 + (1), (h_q).f3 + (1), (3) - (1), 0 }
  + ({ Soa_Particle _sv = (h_q); long _lo = (1), _hi = (3); if (_lo < 0 || _hi > _sv.len || _lo > _hi) { fprintf(stderr, "tycho: slice [%ld:%ld] out of bounds (len %ld)\n", _lo, _hi, _sv.len); exit(1); } (Soa_Particle){ _sv.f0 + _lo, _sv.f1 + _lo, _sv.f2 + _lo, _sv.f3 + _lo, _hi - _lo, 0 }; })
  ```

  **What is NOT in scope — checked, not skipped.**
  - `s[a:b]` (strings) does not trap in the REFERENCE: `tycho_str_substr`
    CLAMPS (`runtime/tycho_rt.c:1056-1058`), and tychoc0's emitted `substr`
    clamps with the identical three tests (`compiler/tychoc0.ty:9775`). Ran
    `s[0:99]`, `s[-2:3]`, `s[4:2]` on `"hello"` — both compilers print
    `hello|hel||`, exit 0. Nothing to close; adding a trap here would DIVERGE.
  - `bytes` cannot be sliced at all — tychoc rejects it at compile time
    (`error: can only slice an array, soa, or string`).
  - `bounded[N]T` is rejected by tychoc0 at `compiler/tychoc0.ty:6053`.
  - `[N]T` fixed arrays: both compilers already fail to BUILD a slice of one
    (tychoc's own emitted C does not compile; tychoc0 emits an incompatible
    `Arr_int_slice` argument). Pre-existing, symmetric, untouched here — it is
    a fixarr-codegen gap, not a slice-bounds gap.

  **1. The guard fires, identically on both sides.** Four fixtures, each built
  by tychoc AND by tychoc0 (`tychoc0 < f.ty > f.c && cc`), stdout and stderr
  captured separately:

  ```
  fixture                    tychoc                                       tychoc0
  abort/slice_hi_oob     tycho: slice [0:5] out of bounds (len 3)  ex=1  identical, ex=1
  abort/slice_neg_lo     tycho: slice [-1:1] out of bounds (len 2) ex=1  identical, ex=1
  abort/slice_lo_gt_hi   tycho: slice [2:1] out of bounds (len 3)  ex=1  identical, ex=1
  abort/slice_soa_oob    tycho: slice [0:5] out of bounds (len 2)  ex=1  identical, ex=1
  ```

  For all four, `cmp` on the two stderr streams says IDENTICAL, `cmp` on the two
  stdout streams says IDENTICAL (both empty), and the exit status matches.
  `od -c` confirms the trailing `\n` and no more, e.g.
  `t y c h o :   s l i c e   [ 0 : 5 ]   o u t   o f   b o u n d s   ( l e n   3 ) \n`.

  Before the fix (built from `git show HEAD:compiler/tychoc0.ty`) the same four
  programs under tychoc0 gave: `5` exit 0 (it read FIVE elements out of a
  three-element buffer), `3` exit 0 (read behind the buffer), SIGSEGV 139
  (`hi - lo` = -1 reached `_from`, which allocated `sizeof(T) * (size_t)-1`),
  and `5` exit 0 for the soa. That is the memory-safety part of this entry: not
  a missing message, an actual out-of-bounds read.

  **2. It does not fire on valid input.**
  - `make fixpoint` — `ok B == C : tychoc0 reproduces itself byte-identically
    (34425 lines C)`; `ok split tychoc0 (2 packages) self-hosts E==F and matches
    the single-file compiler`; `fixpoint: all green`. tychoc0 slices arrays while
    compiling, so an over-tight bound could not self-host.
    (34425 vs Phase 1's 34428: accounted for entirely inside `gen_soa_slice` —
    the loop body's four `hi_append` calls of interpolated `basec`/`loc` fold
    into two constant literals, −4, and the new `nm :=` adds +1. Net −3. The
    rest of the 69-line self-compilation diff is the per-type guard.)
  - `make test` — `passed: 404   failed: 0` / `all green`, including
    `ok    abort_slice_hi_oob`, `ok    abort_slice_lo_gt_hi`,
    `ok    abort_slice_neg_lo`, `ok    abort_slice_soa_oob`. 400 → 404 is
    exactly the four new fixtures and nothing regressed.
  - `make corelib` — `corelib: all green (tychoc and tychoc0 agree, match
    goldens)`, all 37 modules ok.
  - The reference's boundary cases re-run under tychoc0 AFTER the change:
    `0 0 3 0 2 2 3`, exit 0 — byte-identical to tychoc. `tests/soa.ty`'s
    `q[1:3]` (the repo's only soa slice) still passes under both.

  **3. `make rtparity`** — the entry is deleted and the lane agrees:
  ```
  rtparity: env knobs         3 shared, 0 allowlisted difference(s) (ok)
  rtparity: diagnostics      20 shared, 7 allowlisted difference(s) (ok)
  rtparity: arena-stats rows  5 shared, 0 allowlisted difference(s) (ok)
  rtparity: the two runtimes agree on env knobs, diagnostics and arena stats
  ```
  7, down from 8; the diagnostic moved into the 20 shared (was 19).

  **4. What the fixtures do and do not lock.** Unchanged from Phase 1:
  `tests/abort/` builds with `$TYCHOC` only (`tests/run.sh:180`), so these four
  fixtures lock the REFERENCE side. On the tychoc0 side the lock is rtparity,
  which proves the TEXT is present in the emitted C, not that it fires on the
  same input — which is exactly why the side-by-side `cmp` in (1) exists. Phase 5
  is queued to make that lane differential.

  **Contradicting plan.md's description of the gap.** The entry said "tychoc0's
  `Arr_*_slice` forwards lo/hi to `Arr_*_from` unchecked". True, but incomplete:
  there is a SECOND unchecked path, `gen_soa_slice` (`compiler/tychoc0.ty:5254`),
  which is not a `_slice` function at all — it is inlined per field at the use
  site. It had the same hole and needed its own fix and its own fixture.

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
