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

- [ ] **Phase 1 — `io.mtime` through the shim**
  - Scope: `corelib/io/io_shim.c`, `corelib/io/io.ty`, and a fixture.
  - The `stat(2)` is already there. Decide whether to extend
    `iox_stat_kind` or add a sibling — read its comment about the shared
    return-code space with `iox_read_file` first, because that is the constraint
    that shaped it. A `Result(int, IoErr)` returning epoch seconds is the obvious
    surface; justify whatever you choose.
  - Done when: a fixture reads the mtime of a file it just wrote and the value is
    sane, with a golden; the error path (missing file) returns an `Err`.
  - Verify: `make test`, then `make -s corelib`.

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
