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

- [x] **Phase 3 — allocation range + OOM traps**
  - Three entries: `reserve capacity %ld out of range`
    (`runtime/tycho_rt.c:190`), `string length %ld out of range` (`:858`),
    `out of memory` (`:93` — tychoc0's `blk_get` mallocs unchecked).
  - Done when: all three entries are deleted and each trap is demonstrably
    reachable.
  - Verify: as above. OOM is hard to trigger honestly — if you cannot force it
    without a fake allocator, say so in plan.md and prove that check by source
    trace rather than faking a pass.

  **The three predicates, read off the reference before writing anything.**

  1. `reserve capacity %ld out of range` — `tycho_cap_check`
     (`runtime/tycho_rt.c:190-195`): `if (n < 0 || (unsigned long)n > (size_t)-1
     / elem)`. `n == SIZE_MAX/elem` is LEGAL; only a negative `n` or one whose
     `n*elem` would wrap `size_t` traps. Copied verbatim into a new `hi_cap_check`.
  2. `string length %ld out of range` — `tycho_str_alloc`
     (`runtime/tycho_rt.c:854-866`): `if (n < 0)` only. The comment there
     (`:855-857`) states WHY there is no upper arm — lengths are 64-bit, so a
     POSITIVE `n` can never overflow `(size_t)(8 + n + 1)` on a 64-bit host.
     Ported exactly, no upper arm.
  3. `out of memory` — `tycho_oom` (`runtime/tycho_rt.c:93`): `fprintf(stderr,
     "tycho: out of memory\n"); exit(1);`, called after EVERY `malloc` /
     `aligned_alloc` in the reference (`:381 :536 :666 :668 :1002 :1270`, and
     `arena_alloc`'s malloc is the block one at `:380-381`).

  **Contradicting plan.md's description of the gap — checked, as Phase 2 warned.**

  - The entry says "tychoc0's `Arr_*_reserve` … allocate the request unchecked",
    singular. But tychoc does NOT check every reserve: the three SCALAR arrays
    route through `tycho_cap_check` (`runtime/tycho_rt.c:1292/:1391/:1475`), while
    the GENERATED composite `tycho_arr_C%d_reserve` (`src/tychoc.c:10503-10508`)
    has NO check at all. Verified by running both compilers on
    `reserve([]P, 2305843009213693953)` where `P` is a struct: BOTH print `1` and
    exit 0 today. So the guard is gated on the element type in tychoc0's single
    `_slice`/`_reserve` template (`gen_arr_fns`, `compiler/tychoc0.ty:9911-9924`):
    `int`/`float`/`str` get `hi_cap_check`, every other element type is left as
    unchecked as the reference leaves it. Guarding ALL of them would make tychoc0
    abort where tychoc returns — a NEW divergence, exactly what this lane exists
    to catch.
  - `hs` is not the only unchecked string alloc, and the OOM path threads through
    it: `hs` calls `amem`, `amem`'s malloc arm was itself unchecked, and both
    `hi_input`/`hi_read_all` malloc+realloc a scratch buffer directly. So case 2
    and case 3 interact: `hs(ar, -1)` now traps as case 2, and `hs(ar, huge)`
    reaches `amem` → arena → `blk_get`'s `malloc(cap)`, which now traps as case 3.

  **Emission sites changed — all in `compiler/tychoc0.ty`, all of them:**
  - Two new preamble helpers: `hi_oom` (after the `Arena` typedef,
    `:9638-9643`) and `hi_cap_check` (after `hi_f2i`, `:9767-9773`) — placed to
    mirror tychoc's `tycho_oom`/`tycho_cap_check` ordering.
  - `hs` (`:9749`): prepend the `n < 0` string-length guard.
  - Every malloc/realloc/aligned_alloc site now checks its result and calls
    `hi_oom`: `blk_get` (`:9686`, both the header and `->mem` mallocs), `amem`
    (`:9744`), `hi_intern` (`:9750`), `bi_from_ints` (`:9787`), `hi_input`
    (`:9794`), `hi_read_all` (`:9799`), `tycho_task_new` (`:9702`),
    `tycho_chan_new` (`:9726`, both `ch` and `ch->cells`). That is every
    `malloc(`/`aligned_alloc(`/`realloc(` in the emitted runtime — matches the
    `grep` set on `runtime/tycho_rt.c`.
  - `gen_arr_fns` `_reserve` template (`:9911-9924`): element-type-gated
    `hi_cap_check(n, sizeof(T))` on `int`/`float`/`str` only.

  The WHOLE diff of tychoc0's emitted C on `tests/rtparity/surface.ty` is 13
  lines changed (before/after), nothing else moved — one `hi_oom` decl added,
  one `hi_cap_check` decl added, the `hs` guard, and one `if (!x) hi_oom();` per
  alloc site (`blk_get`, `tycho_task_new`, `tycho_chan_new`, `amem`, `hi_intern`,
  `bi_from_ints`, `hi_input`, `hi_read_all`), plus `hi_cap_check(...)` inside the
  three scalar `Arr_*_reserve`.

  **1. The guards fire, identically on both sides.** Each fixture built by
  tychoc AND by tychoc0 (`tychoc0 < f.ty > f.c && cc`), stdout/stderr captured
  separately, `cmp` on both streams, exit status compared:

  ```
  case             fixture / input                tychoc                                              tychoc0
  reserve range    abort/reserve_range.ty  tycho: reserve capacity 2305843009213693953 out of range  identical
                   (reserve []int, 2^61+1) ex=1                                                       ex=1
  out of memory    abort/oom_alloc.ty      tycho: out of memory                                       identical
                   (reserve []int, 1e18)   ex=1                                                       ex=1
  ```

  For both, `cmp` on the two stderr streams says IDENTICAL, `cmp` on stdout
  IDENTICAL (both empty), exit status matches. `od -c` confirms the exact bytes
  and the trailing `\n`:
  `t y c h o :   r e s e r v e   c a p a c i t y   2 3 0 5 8 4 3 0 0 9 2 1 3 6 9 3 9 5 3   o u t   o f   r a n g e \n`
  and `t y c h o :   o u t   o f   m e m o r y \n`.

  Before the fix (built from `git show HEAD:compiler/tychoc0.ty`), the same
  inputs under tychoc0 gave: `reserve_range` → `1` exit 0 (allocated a tiny
  buffer under a `2^61+1` cap — the heap-corruption the entry warns of);
  `oom_alloc` → SIGSEGV 139 (`blk_get`'s `malloc(8e18)` returned NULL, then
  `b->mem = malloc(cap)` … `b->cap = cap` dereferenced it). That is the
  memory-safety part: not a missing message, an out-of-bounds write and a
  null-deref.

  **OOM triggered honestly, TWO independent ways — no fake allocator.**
  - The `abort/oom_alloc.ty` fixture above asks for `1e18` `int` = 8 EB, which is
    under `tycho_cap_check`'s wrap cap (`SIZE_MAX/8 = 2305843009213693951`) so it
    PASSES the range check and reaches the real `malloc` — which cannot return 8
    EB on any 64-bit host (exceeds the 128 TiB user address space, overcommit or
    not). Both compilers abort with `tycho: out of memory`, exit 1.
  - Also on the INCREMENTAL growth path, under a real `ulimit -v` cap (the same
    mechanism `tests/conc/run.sh:29` and `tests/recursion/run.sh:35` use). A
    `push` loop to 1e9 under `( ulimit -v 300000 )` (≈300 MB):

    ```
    === tychoc under ulimit -v 300000 ===    exit=1   tycho: out of memory
    === tychoc0 under ulimit -v 300000 ===   exit=1   tycho: out of memory
    ```
    `cmp` on the two stderr streams: IDENTICAL. Before the fix, tychoc0 exited
    139 (SIGSEGV) here. So OOM is exercised at runtime, not merely source-traced.

  **String-length guard (case 2) — reachable but not via a committed fixture.**
  `hs`'s `n < 0` arm fires when a negative length reaches a string allocation.
  Every `hs` caller in the emitted runtime derives `n` from a header length or a
  concat sum (`sc`, `i2s`, `substr`, …), and `substr` CLAMPS negatives before
  calling `hs` (`compiler/tychoc0.ty:9790`), so no ordinary Tycho program passes
  a negative directly — matching the reference, whose comment (`:855`) calls this
  a "corrupted length" guard. It is proven by SOURCE TRACE: the emitted text is
  byte-identical to `tycho_str_alloc`'s guard (rtparity now counts it as shared,
  see §3), same predicate, same message, same `exit(1)`. I did not fabricate a
  runtime firing for it. Its sibling in the same commit — the `hs(ar, huge)` →
  `amem` → `blk_get` OOM — IS exercised above, which is the allocation half of
  the same function.

  **2. It does not fire on valid input.**
  - `make fixpoint` — `ok B == C : tychoc0 reproduces itself byte-identically
    (34436 lines C)`; `ok split tychoc0 (2 packages) self-hosts E==F and matches
    the single-file compiler`; `fixpoint: all green`. tychoc0 `reserve`s and
    string-allocates on every line while compiling itself, so an over-tight bound
    could not self-host. 34436 vs Phase 2's 34425 (+11): accounted for exactly —
    the tychoc0 self-compile diff is `-21 +32` lines, and every one of the +11
    net lines is either a new runtime string literal (`hi_oom`, `hi_cap_check`,
    the guarded reserve lines) or the `capck :=`/`if et …:` gating block in
    `gen_arr_fns`; ZERO are scratch-arena renumbering. Verified line-by-line.
  - `make test` — `passed: 405   failed: 0` / `all green`, including
    `ok    abort_oom_alloc` and `ok    abort_reserve_range` (whole abort lane
    re-listed by hand, all 15 fixtures `ok`). 404 → 405 is the one new fixture
    (`oom_alloc`; `reserve_range` already existed) and nothing regressed.
  - `make corelib` — `corelib: all green (tychoc and tychoc0 agree, match
    goldens)`, all 37 modules ok.
  - Boundary cases run under BOTH compilers, byte-identical: `reserve([]int, 0)`,
    `reserve([]int, -1)` (both no-op: `-1 <= cap` returns before the check),
    `reserve([]int, 1000000)`, `reserve([]float, …)`, `reserve([]string, …)`,
    `substr("hello", 4, 2)` (empty), `substr("hello", 1, 3)` — all identical
    stdout, exit 0. `reserve([]int, 2305843009213693951)` (== `SIZE_MAX/8`, the
    cap boundary) passes `hi_cap_check` on both, then the huge alloc fails as OOM
    on both (tychoc glibc-aborts, tychoc0 cleanly OOMs — but neither
    silently corrupts, and the range check itself did not fire, which is the
    point of the boundary).

  **3. `make rtparity`** — all three entries deleted and the lane agrees:
  ```
  rtparity: env knobs         3 shared, 0 allowlisted difference(s) (ok)
  rtparity: diagnostics      23 shared, 4 allowlisted difference(s) (ok)
  rtparity: arena-stats rows  5 shared, 0 allowlisted difference(s) (ok)
  rtparity: the two runtimes agree on env knobs, diagnostics and arena stats
  ```
  **4**, down from 7; the three diagnostics moved into the 23 shared (was 20).
  The four remaining allowlisted differences are Phase 4's map 2^31 guards.

  **4. What the fixtures do and do not lock.** Unchanged from Phases 1-2:
  `tests/abort/` builds with `$TYCHOC` only (`tests/run.sh:180`), so
  `oom_alloc` and `reserve_range` lock the REFERENCE side. On the tychoc0 side
  the lock is rtparity (text present in the emitted C) plus the side-by-side
  `cmp` in (1). Phase 5 is queued to make that lane differential.

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

- [x] **Phase 4 — the four map 2^31 guards**
  - `[string:int]`, `[string:float]`, `[int:int]`, `[int:float]`
    (`runtime/tycho_rt.c:1794`, `:1937`, `:2124`, `:2263`). tychoc0 emits maps
    per-type with no cap.
  - Done when: all four entries are deleted, `ALLOW_MSG_TYCHOC_ONLY` is empty,
    and rtparity reports `0 allowlisted difference(s)` on every key.
  - Verify: as above. Do NOT allocate 2^31 entries — prove by source trace that
    the guard sits on the same growth path as the reference, and state plainly
    that it is unexercised at runtime.

  **The predicate, read off the reference before writing anything.** All four
  reference guards are byte-identical bar the `[K:V]` display name and sit at the
  SAME spot in `tycho_map_<kv>_append`: immediately after the `if (m->ecount ==
  m->ecap) { ...grow/compact... }` block and before `long e = m->ecount++;`
  (`runtime/tycho_rt.c:1794` si, `:1937` sf, `:2124` ii, `:2263` if):

  ```
  if (m->ecount >= 2147483000L) { fprintf(stderr, "tycho: [string:int] map exceeds 2^31 entries\n"); abort(); }
  ```

  Note `abort()`, NOT `exit(1)` — copied exactly; and `[string:int]` has NO
  space after the colon. rtparity keys on the `tycho: ...` TEXT only, so exit vs
  abort is invisible to it, but the reference wording is copied verbatim anyway.

  **Only these four (k,v) combos are guarded — checked against the reference,
  not assumed.** tychoc's `map_of` (`src/tychoc.c:1102-1103`) canonicalizes ONLY
  `[string:int]/[string:float]/[int:int]/[int:float]` (raw `T_STRING`/`T_INT`
  key, raw `T_INT`/`T_FLOAT` value) to the four built-in guarded runtime maps
  (`tycho_map_si/sf/ii/if`). Every other map type — a newtype/enum/struct key
  (`:1106-1113`), or any other value — routes to the GENERATED composite
  `tycho_mapc%d_append` (`src/tychoc.c:10600-10609`), which has NO 2^31 guard.
  So a blanket cap on tychoc0's unified map template would DIVERGE (emit a
  guard text tychoc never emits, e.g. `[string:string]`). tychoc0 embeds
  `tycho_rt.c` whole on the tychoc side, so all four texts are always present in
  tychoc's `--emit-c`; the only way to match with an empty allowlist is for
  tychoc0 to emit exactly these four and no other map-cap text.

  **The change.** One gated block in `gen_map_fns` (`compiler/tychoc0.ty`), the
  sole per-type map emitter, just before the `_app` template. tychoc0 spells
  `string` as `str` internally (`compiler/tychoc0.ty:1727-1728`), so the guard
  fires on `(k == "str" or k == "int") and (v == "int" or v == "float")` and
  maps `str -> "string"` for the message. `newtype`/`enum`/`struct`/`tuple`/
  `array` keys carry the raw type name in `k` (not `"str"`/`"int"`), so they are
  excluded exactly as the reference excludes them:

  ```
  capg := ""
  if (k == "str" or k == "int") and (v == "int" or v == "float"):
      kd := "int"
      if k == "str":
          kd = "string"
      capg = "if (m->ecount >= 2147483000L) { fprintf(stderr, \"tycho: [" + kd + ":" + v + "] map exceeds 2^31 entries\\n\"); abort(); } "
  ```
  inserted into the `_app` line as `... m->ecap = nc; } } " + capg + "long e = m->ecount; ...`.

  **1. Source trace — guard on the SAME growth path, reference vs tychoc0
  emitted (built by tychoc0 on `tests/rtparity/surface.ty`, which uses all four
  map types at `surface.ty:53,56,58,60`):**

  ```
  reference  (tycho_rt.c:1791-1795):
      m->ekeys = nk; m->evals = nv; m->elive = nl; m->ecap = nc;
    }
  }
  if (m->ecount >= 2147483000L) { fprintf(stderr, "tycho: [string:int] map exceeds 2^31 entries\n"); abort(); }
  long e = m->ecount++;

  tychoc0 emitted:
  ... m->ecap = nc; } } if (m->ecount >= 2147483000L) { fprintf(stderr, "tycho: [string:int] map exceeds 2^31 entries\n"); abort(); } long e = m->ecount; m->ecount = m->ecount + 1;
  ```

  Same predicate, same text, same `abort()`, same position (after the grow
  block `} }`, before `long e`). tychoc0 writes `long e = m->ecount;
  m->ecount = m->ecount + 1;` where the reference writes `long e = m->ecount++;`
  — behaviourally identical, a pre-existing tychoc0 codegen style, not part of
  this change. All four emitted texts confirmed present and unique:
  `[string:int]`, `[string:float]`, `[int:int]`, `[int:float]`.

  **UNEXERCISED AT RUNTIME — stated plainly.** The guard fires only at
  `m->ecount >= 2147483000` (~2.1 billion live entries in a single map), which
  cannot be reached in a test without tens of GB of allocation. Per the phase
  instruction it is NOT triggered at runtime; it is proven by the source trace
  above (identical text, identical growth-path position) and by rtparity, which
  now counts all four as SHARED between the two runtimes.

  **2. Does-not-fire-on-valid-input gates, all four green:**
  - `make fixpoint` — `fixpoint: all green (self-hosting; B==C; single files +
    packages; tychoc0 self-split dogfood)`; `ok B == C : tychoc0 reproduces
    itself byte-identically (34450 lines C)`. tychoc0 inserts into maps on every
    line while compiling itself, so a guard that mis-fired (or a mis-gated
    element type) could not self-host byte-identically.
  - `make test` — `passed: 405   failed: 0` / `all green`.
  - `make corelib` — `corelib: all green (tychoc and tychoc0 agree, match
    goldens)`.
  - `make rtparity` — the four entries deleted, `ALLOW_MSG_TYCHOC_ONLY` now
    `set()` (empty), lane green:
    ```
    rtparity: env knobs         3 shared, 0 allowlisted difference(s) (ok)
    rtparity: diagnostics      27 shared, 0 allowlisted difference(s) (ok)
    rtparity: arena-stats rows  5 shared, 0 allowlisted difference(s) (ok)
    rtparity: the two runtimes agree on env knobs, diagnostics and arena stats
    ```
    0 allowlisted differences on every key; the four map diagnostics moved into
    the 27 shared (was 23 + 4 allowlisted). The whole `ALLOW_MSG_TYCHOC_ONLY`
    allowlist is now empty — every tychoc0 runtime gap on this lane is closed.
