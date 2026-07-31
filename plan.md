# Conditional requests and byte ranges: two `core:io` gaps the server exposes

Previous plan complete and archived at
[docs/internals/plan-citation-gate-DONE.md](docs/internals/plan-citation-gate-DONE.md).

## Goal

Extend `tycho-httpd` with the two features its own README lists as deliberately
excluded and that a costing exercise ranked first and second — **not** because
HTTP wants them, but because each forces `core:io` to expose something it
half-has. That is the same shape as `is_dir` and `getpeername`, both of which
this program surfaced and both of which are now in the corelib.

- **`If-Modified-Since` / `Last-Modified`.** Needs a file's modification time.
  `mtime` does not exist anywhere in the tree.
- **A single byte range.** Needs a read at an offset. `pread`/`lseek` do not
  exist anywhere in `corelib/`.

Done looks like: a conditional GET answers `304` with no body, a `Range` request
answers `206` with `Content-Range`, both are asserted by `make server-check`, and
`core:io` has gained two capabilities that any Tycho program can use.

## Pre-flight

- **Worst case:** a caching bug that serves stale content. A `304` is the server
  telling a client "what you have is current" — if the comparison is wrong in the
  permissive direction, a browser keeps a file that changed. This is the one
  feature here where being wrong is worse than being absent, so phase 2's
  assertions must include the *negative* case: a file modified after the client's
  timestamp must produce `200`, not `304`.
- **Reversibility:** full. Two corelib additions and two server code paths;
  `server/run.sh` is the gate that proves them and it is at 61 assertions.
- **Verified — `mtime` exists nowhere.** `grep -rn "mtime\|st_mtim\|modified"` over
  `corelib/`, `src/tychoc.c` and `runtime/` returns nothing outside
  `corelib/test/`.
- **Verified — the syscall is already being made and the field discarded.**
  `corelib/io/io_shim.c:149@iox_stat_kind` calls `stat(2)` into a local
  `struct stat st` and returns `S_ISDIR(st.st_mode) ? TY_RF_DIR : TY_RF_OK`.
  `st.st_mtim` is in that struct, unused. The comment above it explains the
  return-code space it shares with `iox_read_file`, which is the constraint any
  new accessor has to fit.
- **Verified — no offset read exists.** `grep -rn "pread\|lseek\|fseek" corelib/`
  returns nothing. `io.read_bytes` reads whole files, so serving 1 KB of a 1 GB
  file allocates 1 GB — expressible today via `bytes` slicing, and wrong.
- **Verified — the server already has the gate to prove this in.**
  `make -s server-check` runs `server/run.sh` at 61 assertions, proven 10/10
  stable, with a python3 raw-socket client that can send arbitrary headers. No
  new test harness is needed.
- **Assuming — `datetime` can format and parse an HTTP-date, and I have not
  checked.** `Last-Modified` and `If-Modified-Since` use RFC 7231 IMF-fixdate
  (`Sun, 06 Nov 1994 08:49:37 GMT`), which is a specific format with English
  month and day names. `corelib/datetime/` exists. **Risk if wrong:** the feature
  needs a formatter written from scratch, which is a bigger phase than costed.
  Phase 2 must check before building, and say what it found.
- **Assuming — one range is enough.** `Range: bytes=0-499` is the shape worth
  having; multipart ranges (`bytes=0-99,200-299`) need a multipart body and are
  explicitly out of scope. A request for multiple ranges should be answered with
  the whole file and `200`, which is what the RFC permits and what most servers
  do. Phase 4 states this in the README rather than leaving it implicit.

## Phases

- [x] **Phase 1 — `io.mtime` through the shim**
  - Scope: `corelib/io/io_shim.c`, `corelib/io/io.ty`, and a fixture.
  - The `stat(2)` is already there. Decide whether to extend
    `iox_stat_kind` or add a sibling — read its comment about the shared
    return-code space with `iox_read_file` first, because that is the constraint
    that shaped it. A `Result(int, IoErr)` returning epoch seconds is the obvious
    surface; justify whatever you choose.
  - Done when: a fixture reads the mtime of a file it just wrote and the value is
    sane, with a golden; the error path (missing file) returns an `Err`.
  - Verify: `make test`, then `make -s corelib`.

  **Evidence (2026-07-31).**

  *The accessor, and why it is a sibling.* `corelib/io/io_shim.c@iox_stat_mtime`
  is a new function taking `(const char *path, tycho_int *mtime)` and returning
  the status. The constraint the phase pointed at is real and it decides the
  shape: `corelib/io/io_shim.c@iox_stat_kind` can return a plain scalar because
  a *kind* is drawn from a four-value set that the error codes can be interleaved
  with. An mtime cannot join that set — a file last modified at epoch second 2 is
  indistinguishable from `TY_RF_DIR` — so payload and status must occupy
  different slots. That is the same separation `corelib/io/io_shim.c@iox_read_file`
  makes, and the assignment is simply whichever half fits the return: there the
  payload is `bytes`, which has no out-param ABI on the Tycho side (the compiler
  says so in as many words at `src/tychoc.c:3915`), so the status took the
  `inout`; here the payload is a scalar, so it takes the `inout` and the status
  keeps the return with the shared code space intact.

  *Not folded into `iox_stat_kind`.* One stat(2) could serve both, but
  `iox_stat_kind` has three callers — `corelib/io/io.ty@is_dir`,
  `corelib/io/io.ty@exists`, and `corelib/io/io_shim.c@iox_make_dir`, which calls
  it from C — and every one of them would have to pass an mtime it discards, to
  spare a syscall for a caller that has not asked yet. So `io.mtime` is a second
  stat(2) for a caller that wants both. Worth naming, because the phase brief
  costed it as a per-request expense and it is not one in the case that matters:
  a conditional GET that answers `304` calls only this, and never opens the file,
  so the hot 304 path is *cheaper* than today's serve path, not dearer.

  *The `IoErr` variants used, and the one deliberately not used.*
  `corelib/io/io.ty@mtime` returns `Result(int, IoErr)` with `Err(NotFound)` for
  ENOENT/ENOTDIR and `Err(Failed)` for everything else. `Exists` is documented
  `make_dir`-only and does not apply. **`IsDir` is not used: a directory is
  `Ok(secs)`.** A directory has a modification time and stat(2) fills it in;
  returning an error would be this module discarding a field the kernel already
  handed it, which is precisely the defect the phase exists to fix. That mirrors
  `is_dir`, where `IsDir` is likewise absent because being a directory is the
  answer, not a failure to give one.

  *Precision: whole seconds, as instructed.* The shim reads `st.st_mtime`, not
  `st.st_mtim.tv_sec`. Same value on any POSIX.1-2008 system, but it needs no
  feature macro and no macOS `st_mtimespec` spelling. The nanosecond half is
  dropped: HTTP-date is whole seconds, `datetime.from_unix` takes whole seconds
  (`corelib/datetime/datetime.ty@from_unix`), and `now()` — the builtin at
  `src/tychoc.c:4518`, wall-clock UNIX seconds — is whole seconds, so the fixture
  can compare the two directly. Nothing in the tree could consume more.

  *The fixture, and what a golden can honestly hold.* `corelib/test/io/main.ty`
  gained a section and `corelib/test/io.out` two lines. No exact epoch is
  asserted — the file is written by the run, so the value differs every time. The
  four stable properties are: the call succeeds; `s1 > 1600000000` (2020-09-13,
  so a status code leaking through the return as 0/2/3 fails); `s1 <= now()`, the
  same clock, so a bad cast or a nanosecond field read as seconds fails; and a
  rewrite is `>= ` the first reading, which is what proves the value tracks the
  file rather than being constant. `>=` and not `>` on purpose: both writes land
  inside one second and `st_mtime` is truncated to seconds. Error path:
  `mtime_missing=NotFound`. Non-error path: `mtime_dir=Ok`.

  *The FFI shape was unproven before this and is now.* `inout int` on an extern
  had only ever been used with a `bytes` return —
  `corelib/io/io.ty@iox_read_file` and `corelib/net/net.ty:102` were the only two
  in the corelib. An `inout int` alongside a plain `int` return is new here; it
  compiles and runs, which the lane below demonstrates.

  *Gates, both foreground.*

  ```
  $ make test
  passed: 560   failed: 0
  all green
  ```

  560 before, 560 after — correct and expected: the change adds lines to an
  existing `corelib/test/` lane, not a new `tests/` fixture, so this gate covers
  the compiler's handling of the new FFI shape without its count moving.

  ```
  $ make -s corelib
  ok   io
  corelib: all green (tychoc matches goldens)
  ```

  *One out-of-scope touch, caused by this phase and repaired in it.* Inserting
  `mtime` into `corelib/io/io.ty` pushed `is_dir` down the file and reddened
  `python3 scripts/check_citations.py`, which was green at `edb7f78`:

  ```
  STALE  server/README.md:152  `corelib/io/io.ty:133` -> lines 133-133 of
         corelib/io/io.ty do NOT contain 'is_dir'; it appears at :28, :138, :157
  ```

  That block is quoted with **the anchor dropped from the stale ref** — it read
  `:133@is_dir`. Reproducing it whole made the gate parse its own error message
  as a live citation and redden on this evidence block, the failure it had just
  reported one file over. The number is the record and stays; the anchor was a
  live promise about the tree you are reading now, which is the half a record
  never protected.

  `server/` is phase 2's scope and `server/README.md` is phase 5's, so this is
  noted rather than absorbed silently — but it is not new work, it is drift my
  own edit created, and leaving it would hand phase 2 a red gate it did not
  cause. The repair is the one CLAUDE.md prescribes for exactly this event
  ("convert an old one when it next breaks"): drop the line number, keep the
  symbol, `corelib/io/io.ty@is_dir`. The line is live narrative prose, not a
  record line — no arrow-joined refs, not a table row — so nothing was
  falsified. One token changed in `server/README.md`; the gate is green again.

  **No phase 6 was added.** The only discovery is the drift above, and it is
  fixed. The tempting generalisation — sweep the remaining line-numbered refs
  into `corelib/io/io.ty` to the `path@SYMBOL` form before phase 3 grows the file
  again — is the hand sweep CLAUDE.md has declined three times with measurements,
  and it says in as many words that converting correct old-form refs is not work.
  The note under phase 3 below is the whole of the mitigation.

- [ ] **Phase 2 — `Last-Modified` and `If-Modified-Since`**
  - Scope: `corelib/httpd/httpd.ty` for the header handling, `server/main.ty`,
    and `server/run.sh`.
  - **Check the date format question from Pre-flight first** and report what
    `corelib/datetime/` can actually do. If it cannot produce an IMF-fixdate, say
    so before building — that changes the size of this phase.
  - The comparison must be conservative: `304` only when the file's mtime is
    **not newer** than the client's timestamp. An unparseable or absent header
    means `200`, never `304`.
  - Done when: a conditional GET with a current timestamp gets `304` **with no
    body**, a conditional GET with an older timestamp gets `200` **with** the
    body, an unparseable `If-Modified-Since` gets `200`, and every response
    carries `Last-Modified`.
  - Verify: `make -s server-check` with the new assertions, each proven to FAIL
    on the pre-change binary; then the 10-run loop; then `make test`.

- [ ] **Phase 3 — `io.read_at` through the shim**
  - Scope: `corelib/io/io_shim.c`, `corelib/io/io.ty`, and a fixture.
  - `pread(2)` is the call — it does not disturb a file offset and needs no
    `lseek`. Bounds are the interesting part: a read starting past EOF, a length
    running past EOF, and a negative offset are three different answers and the
    fixture must pin all three.
  - Done when: a fixture reads a known slice of a known file and matches a
    golden; the three boundary cases each have an asserted answer.
  - **Run `python3 scripts/check_citations.py` before committing** (<1s). Phase 1
    grew `corelib/io/io.ty` and that alone reddened a line-numbered ref to
    `is_dir` from `server/README.md`; adding `read_at` to the same file will move
    the same definitions again. Repair a break by dropping the line number and
    keeping the symbol — not by repointing the number.
  - Verify: `make test`, then `make -s corelib`.

- [ ] **Phase 4 — `Range` and `206`**
  - Scope: `corelib/httpd/httpd.ty`, `server/main.ty`, `server/run.sh`.
  - `Range: bytes=A-B`, `bytes=A-` and `bytes=-N` (suffix) are the three forms
    worth supporting. A syntactically invalid range is `200` with the whole file;
    a range that cannot be satisfied is `416` with `Content-Range: bytes */LEN`.
    Multipart is out of scope and the response is `200`.
  - `206` carries `Content-Range: bytes A-B/LEN` and a `Content-Length` of the
    slice, not the file.
  - Done when: each of the three forms returns the right bytes — proven by `cmp`
    against a slice of the file on disk, not by length alone — plus the `416` and
    the invalid-range-is-200 cases.
  - Verify: `make -s server-check`, the 10-run loop, `make test`.

- [ ] **Phase 5 — spec, README, and the sweep**
  - Scope: `docs/spec/` for the two new `core:io` calls,
    `docs/guides/corelib.md`, `server/README.md`, and `make ci`.
  - `server/README.md` lists both features under "Deliberately not implemented".
    They move, with the date, following the convention the file already uses for
    the two limitations that were closed on 2026-07-26.
  - Done when: both calls are specified with provenance, the README describes
    what the server now does, and `make ci` is green with the exit code observed.
  - Verify: the three doc gates, then `make ci` once, waited on in-turn.

## Out of scope

- **Multipart ranges.** Stated in phase 4 and recorded in the README.
- **`ETag`.** The other half of conditional requests; `sha256.hex` already exists
  so it is cheap, but it is a separate feature and the costing ranked only the
  `If-Modified-Since` half.
- **The six other excluded features** (TLS, HTTP/2, compression, virtual hosts,
  directory listings, pipelining). The costing rejected all six with reasons.
