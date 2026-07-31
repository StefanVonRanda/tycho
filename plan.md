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
  `corelib/io/io_shim.c@iox_stat_kind` calls `stat(2)` into a local
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

- [x] **Phase 2 — `Last-Modified` and `If-Modified-Since`**
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

  **Evidence (2026-07-31).**

  *The Pre-flight question, answered before anything was built: `corelib/datetime/`
  cannot produce or read an IMF-fixdate, and it did not need to.* The package
  formats ISO-8601 (`corelib/datetime/datetime.ty@format_iso`) and parses
  ISO-8601 and the Common Log Format (`corelib/datetime/datetime.ty@parse_clf`);
  `grep -n "^fn " corelib/datetime/datetime.ty` lists no RFC 1123 / RFC 7231
  entry point in either direction. **But the risk the Pre-flight named — "the
  feature needs a formatter written from scratch" — did not materialise, because
  every PIECE is there and already tested:** `weekday_name` gives `Sun`..`Sat`,
  `month_name` gives `Jan`..`Dec`, `month_num` is its inverse, `pad2`/`pad4` do
  the zero padding, `from_unix` supplies the civil fields *including the weekday*,
  and `to_unix` goes back. So the phase was the size it was costed at.

  *And the server already had half of it.* `server/main.ty@http_date` existed and
  emitted exactly this format for the `Date` header — its comment called it "RFC
  1123", which is the same production RFC 7231 renamed IMF-fixdate. The phase
  brief did not know that and neither did the Pre-flight; it was found by reading
  the file. So the formatter was not written, it was **generalised**: `http_date()`
  became `http_date_at(secs)` plus a one-line `http_date()` calling it with
  `now()`. Two callers, one format, no second spelling to drift.

  *Where the two new functions live, and why not in `corelib/datetime/`.* Both
  are in `server/main.ty`. `corelib/datetime/` is out of this phase's scope, but
  scope is not the only reason: IMF-fixdate is an HTTP wire format, not a
  calendar fact, and `core:datetime`'s header states its two parser dialects
  deliberately. Adding a third belongs to whoever specs it (phase 5's territory),
  not to a server phase that needs it working today.

  *What `parse_http_date` accepts, and the one thing it delegates.* IMF-fixdate
  only — 29 characters, fixed positions. It validates the punctuation and the
  literal `GMT` itself, then **rearranges the fixed fields into the CLF shape
  `dd/Mon/yyyy:HH:MM:SS` and hands them to `datetime.parse_clf`**, which already
  does the digit scanning, the month name, and the range checks (13th month, 31st
  of February, hour 24) with a fail-closed sentinel. A second copy of that logic
  was the alternative and it would have been the copy that rots.

  RFC 7231 §7.1.1.1's two obsolete forms — rfc850 and asctime — are **not**
  parsed, deliberately. They fall out as `-1`, which means `200`, which is the
  safe direction: the cost is one needless full response for a client that sends
  one, never a stale `304`. Every client that sends us an `If-Modified-Since` is
  echoing back the `Last-Modified` we wrote, which is an IMF-fixdate by
  construction. Both forms are asserted in the gate as `200`-with-body, so this
  is a pinned decision rather than an omission.

  *The date math, checked against an independent implementation before it was
  wired to anything.* A throwaway program carrying copies of both functions,
  compiled with `./tychoc`:

  ```
  fmt   Sun, 06 Nov 1994 08:49:37 GMT     <- python: email.utils.formatdate(784111777, usegmt=True)
  parse 784111777                          <- python: calendar.timegm(parsedate(...))
  roundtrip 4000/4000                      <- parse(format(s)) == s over ~12 years of s
  bad-len -1  bad-punct -1  bad-month -1  bad-day -1  bad-hour -1  garbage -1  empty -1
  ```

  Both directions agree byte-for-byte with python's, which is the point: a
  formatter checked only against its own parser proves nothing.

  *The comparison rule as implemented.* In `server/main.ty@serve_conn`:
  `if lm >= 0 and ims >= 0 and lm <= ims:` → `304`. Three separate ways to not
  know the answer all fall through to `200`: `io.mtime` failed (`lm < 0`, and no
  `Last-Modified` is sent either — a header we cannot fill is worse than none);
  no or unparseable `If-Modified-Since` (`ims < 0`); or the file is newer.
  **`<=` and not `<` is load-bearing** — equal means the client holds exactly this
  version — and the equality case is asserted, because `<` is a silent
  one-character regression that a suite testing only "conditional GET → 304"
  would never catch. mtime is whole seconds (`corelib/io/io.ty@mtime`, phase 1)
  and so is an IMF-fixdate, so the two compare with no rounding on either side.

  *Phase 1's cost claim, confirmed rather than assumed.* The `304` arm builds its
  response and never calls `io.read_bytes`, so the hot conditional path is one
  `stat(2)` and no file read — cheaper than the serve it replaces.

  *The HEAD/304 interaction, which is a real interaction and not a formality.*
  Both suppress the body and they are **not the same suppression**.
  `server/main.ty@emit`'s HEAD arm suppresses the body while *keeping* the
  `Content-Length` a GET would have reported — that is the whole point of HEAD,
  and the explicit header is what stops `render_head` recomputing `0` from the
  emptied body. A `304` has no representation to describe at all. Run the HEAD
  arm on a `304` and it stamps `Content-Length: 0`, which is precisely the value
  RFC 7230 §3.3.2 forbids there: the only length a `304` may carry is the one the
  `200` would have sent, and this server deliberately never learns it. So the arm
  is guarded `if head_only and not httpd.bodyless(out.status)`. A bodyless
  response already has an empty body, so nothing is lost by skipping it.

  *The corelib half: `corelib/httpd/httpd.ty@bodyless`.* `304` was missing from
  `corelib/httpd/httpd.ty@reason_phrase` and rendered as the placeholder
  `"Status"`; it is in the table now. The larger change is that
  `corelib/httpd/httpd.ty@render_head` no longer *synthesises* `Content-Length`
  or `Content-Type` for a bodyless status (1xx, 204, 304 — RFC 7230 §3.3.3 rule
  1). This is not the same question as "is the body empty": a `404` with an empty
  body still describes a body of zero bytes and `Content-Length: 0` is the right
  thing to say about it. An explicit header set by a caller is still emitted
  verbatim in every case — the rule governs only what gets invented. 1xx is
  included for completeness; nothing in the package generates one.

  *The four required cases, and 25 more.* `server/run.sh` went **61 → 90
  assertions**. The document root is a copy, so the runner pins one file's mtime
  with `os.utime` to a fixed epoch — which turns `Last-Modified` from "some
  plausible string" into an exact comparison against a date python formatted
  independently. The four the brief named: current timestamp → `304` with no
  body; **older timestamp → `200` with the body**; unparseable → `200`;
  `Last-Modified` on the plain `200` (asserted on two different files).

  *Pre-change failure, and where the honest answer differs from the brief's.*
  **10 of the 29 fail on the pre-change binary** — built by `git worktree add`
  at `74fd4c7` with the new `server/run.sh` copied in. The other 19 **cannot**,
  and that is structural rather than a gap: a server with no conditional-GET
  support trivially satisfies "serve `200` with the body". The negative
  assertions exist to catch a server that is wrongly *permissive*, which the
  absent implementation is not. So they were proven against three mutants of the
  finished code instead — the same worktree, one `sed` each:

  | variant | the bug it embodies | reddens |
  |---|---|---|
  | pre-change `74fd4c7` | feature absent | 10 of 29 |
  | A: `if header(...) != ""` | `304` whenever the header is present | 14 of 29 |
  | B: `lm < ims` | off-by-one on "not newer" | 6 of 29 |
  | C: `lm >= 0 and (ims < 0 or lm <= ims)` | fail **open**: unevaluable counts as satisfied | 16 of 29 |

  **28 of the 29 redden under at least one variant.** Mutant C also breaks 12
  *pre-existing* assertions, which is its own finding: failing open turns the
  whole server into a `304` machine, and the suite says so loudly.

  The 29th, `HEAD 304 no body`, is **not falsifiable by any variant I could
  construct** — HEAD suppresses the body on its own, so no mutation of the
  conditional logic can redden it. It is kept and **labelled in `server/run.sh`
  as a control**, because from the pass line a control and a proof look identical.
  The 304-has-no-body *claim* is carried by the GET form, which reddens on the
  pre-change binary and on mutants A and B.

  *Gates, all foreground, one command each.*

  ```
  $ make -s server-check
  server: OK                      (90 ok, 0 FAIL; was 61)

  $ for i in $(seq 1 10); do make -s server-check; done
  run 1..10: server: OK  ok=90 FAIL=0        (10/10, no flake)

  $ make test
  passed: 560   failed: 0

  $ python3 scripts/check_citations.py
  citation check: ok
  ```

  560 before, 560 after — expected: no fixture was added, and `make test` is here
  to prove the corelib change broke no compiler or golden behaviour.

  *The out-of-scope touch this phase caused, at 16x phase 1's scale.* Phase 1
  grew `corelib/io/io.ty` and broke **one** line-numbered ref. This phase grew
  `server/main.ty` by ~110 lines and broke **32**, across `FRICTION.md`,
  `server/README.md`, `corelib/signal/signal_shim.c`, `server/run.sh`,
  `server/main.ty` itself, and two frozen archives. Every one of the 32 targets
  `server/main.ty`; the gate was green at `74fd4c7`, so all 32 are this phase's
  drift and none is pre-existing.

  This is repaired here rather than deferred, on phase 1's precedent — leaving it
  would hand phase 3 a red gate it did not cause — and CLAUDE.md authorises both
  halves in as many words: "convert an old one when it next breaks", and, for an
  anchor inside a frozen record, "the anchor rule wins on the anchor". **Two
  different repairs, chosen per line by shape, applied line-targeted and never by
  blanket `sed`** (three of the files carry the same ref text on both kinds of
  line):

  - **8 refs → drop the anchor, keep the number.** Five are repair-log lines
    (`old` → `new`, two refs joined by an arrow) in
    `docs/internals/plan-citation-gate-DONE.md`; three are frozen prose that
    states outright that its numbers are a past record ("the line numbers here
    are the post-batch-E ones"). The number is data and moving it would falsify
    evidence; the anchor was a live promise bolted onto a dead number.
  - **24 refs → the `path@SYMBOL` form, line number dropped.** These are live
    pointers at named constructs. One was a **bare** `:753`, anchored on
    `stopped` — with no path of its own, so it bound to whatever path that
    document named last. Naming the path is strictly better and the binding was
    invisible. (That ref is written here with **its anchor dropped**, for the
    reason phase 1 records one line further down: quoted whole, it is a live
    bare citation inside this evidence block, and it would resolve against
    whichever file this paragraph mentioned last — passing or failing for
    reasons that have nothing to do with the repair being described.)

  *And two the gate could not have caught.* `server/run.sh`'s own header cited
  `server/main.ty:712-716` and `:746-750` as bare **ranges** for the bind and the
  banner. Bare ranges are not content-checked, so both were silently pointing at
  `usage()` text after my insertions and the gate stayed green. Repointed to
  `812-816` and `846-850`, verified by reading both. A bare range is the one form
  where being wrong costs nothing at the gate and everything to a reader — and it
  caught me twice in this phase: correcting `server/main.ty`'s own header (which
  still listed conditional requests under "NOT IMPLEMENTED", false as of this
  commit) shifted the file another 7 lines *after* the first repoint, and the
  gate stayed green through it. The `path@SYMBOL` refs needed no second visit.

  **No sweep was performed and none is recommended.** Only broken refs were
  touched. The remaining line-numbered refs into `server/main.ty` are correct
  today, and CLAUDE.md says converting correct old-form refs is not work — but
  note that **phase 4 grows `server/main.ty` again** for `Range`, so expect this
  same class of break, and repair it the same two ways rather than repointing
  numbers.

- [x] **Phase 3 — `io.read_at` through the shim**
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

  **Evidence (2026-07-31).**

  *The FFI shape, and the precedent fitted exactly.* `corelib/io/io_shim.c@iox_read_at`
  is `(path, off, n, status, out, outlen)` — the payload in the `bytes` return, the
  status in an `inout int`. This is `corelib/io/io_shim.c@iox_read_file`'s case and
  not `corelib/io/io_shim.c@iox_stat_mtime`'s, for the reason phase 1 recorded: the
  assignment is decided by which half *can* occupy the return, and a `bytes` payload
  has no choice. Verified in the compiler rather than assumed —
  `src/tychoc.c@ffi_scalar_type` is the predicate, and it rejects a `bytes` `inout`
  with a message naming `bytes` outright. So the status took the `inout`. **No third
  pattern was invented and none was needed.**

  *No separate count out-param.* A short read is reported by `len(b)`. The one
  temptation here was a `got` out-param beside the payload; it would have been a
  second spelling of something `bytes` already carries.

  *The parameter order was read, not guessed.* An `inout` lowers to a `tycho_int*`
  **in its declared position**, with the `bytes` return's two out-params appended
  after — which is why `(path, off, n, status)` on the Tycho side is
  `(path, off, n, status, out, outlen)` in C. Confirmed against `iox_read_file`'s
  two declarations side by side before writing the C.

  *The three boundaries, each a different answer.*

  | case | answer | why |
  |---|---|---|
  | `off` past EOF | `Ok`, `len == 0` | pread(2)'s own answer, and complete |
  | `n` past EOF | `Ok`, short, unpadded | `len(b)` is the count |
  | `off` or `n` negative | `Err(Failed)` | refused before `open(2)` |

  **Past EOF is `Ok(empty)`, not an `Err`, and this is the decision worth
  defending** — it looks like the ambiguous-empty defect this whole plan exists to
  remove. It is not the same shape. `read_bytes`' empty was ambiguous because *three
  different world states* produced one value with no way to tell them apart. Here
  the caller **chose the offset**, so "zero bytes at offset 900" is a complete
  answer to the question actually asked, and the caller already holds the input that
  disambiguates it. Inventing an `Err` would also mean `read_at` disagreeing with
  pread(2) for no gain.

  **A negative offset is refused in the shim, ahead of `open(2)`** — not in Tycho
  and not by letting pread(2) return EINVAL. It has to be a guard rather than a
  consequence: a negative `tycho_int` cast to a 32-bit `off_t` is
  implementation-defined, and the value must never reach pread(2) at all, where a
  sign-extended offset is a wild read. Negative `n` is refused in the same line.
  Both are `Err(Failed)` and **neither got a new `IoErr` variant** — they are
  unreachable from correct code (a `Range:` parser only ever produces digits), and
  adding a variant would change a public enum whose payload-free shape `io.ty`'s
  header pins deliberately.

  *Zero length is `Ok(empty)`: asking for nothing succeeds at giving nothing.*

  *The untrusted-length decision — clamp to the file, impose no cap.* The
  allocation is `min(n, size - off)` from an `fstat` on the already-open fd,
  **never `n`**. So a `Range:` header naming a terabyte allocates what the file
  holds beyond `off`. I considered a fixed cap on top and **rejected it**: a cap is
  a policy number with no principled value, it would make `read_at` unable to read a
  large file a caller legitimately wants, and it adds nothing here — an attacker
  cannot drive the allocation above the size of a file they could already have
  fetched whole with `read_bytes`. The clamp removes the attack; a cap would only
  add a knob. The clamp is also what makes the `off_t` cast provably safe: after it,
  `off < size`, and `size` came from an `off_t`.

  The fstat/pread window is racy and is safe in both directions by construction: a
  file that shrank yields a short read (already legal), one that grew yields less
  than `n` (also already legal). `EINTR` retries; a `pread` returning 0 breaks to a
  short read rather than spinning.

  *Regular files only.* A directory is `Err(IsDir)`, matching `read_bytes`. A fifo,
  socket or device is `Err(Failed)` — `st_size` is meaningless for them and pread(2)
  fails ESPIPE on anything unseekable, so "the byte at offset N" is a question they
  have no answer to.

  *The fixture asserts letters, not lengths.* `corelib/test/io/main.ty` writes a
  26-byte file where byte i is `'A'+i`, so a wrong offset prints the wrong letters
  instead of the right count — which is the failure a length-only assertion passes.
  `corelib/test/io.out` gained six lines covering an interior slice (`FGHIJ`), the
  whole file through the offset path, all three boundaries, zero length, the
  terabyte-length clamp (asserted by its *contents*, so an absent clamp is a 1 TB
  malloc rather than a quiet pass), and the missing/directory paths. The golden
  matched on the first run.

  *Gates, all foreground, one command each.*

  ```
  $ make -s corelib
  ok   io
  corelib: all green (tychoc matches goldens)

  $ python3 scripts/check_citations.py
  citation check: ok

  $ make test
  passed: 560   failed: 0
  all green
  ```

  560 before, 560 after — expected for the same reason phase 1 recorded: this
  extends an existing `corelib/test/` lane rather than adding a `tests/` fixture, so
  the gate proves the compiler still handles the FFI shape without its count moving.

  *The drift this phase caused, repaired here.* Inserting `iox_read_at` above
  `iox_stat_kind` moved it from line 149 to 239 and reddened one ref — in this
  file's own Pre-flight, exactly the class phase 3's brief predicted:

  ```
  STALE  plan.md:37  `corelib/io/io_shim.c:149` -> lines 149-149 of
         corelib/io/io_shim.c do NOT contain 'iox_stat_kind'
  ```

  Quoted above with **the anchor dropped from the stale ref**, on phase 1 and 2's
  precedent: reproduced whole it is a live citation inside this evidence block and
  the gate would redden on the report of the failure it had just fixed. The line is
  live narrative prose — not a repair log, not a before/after row — so the
  prescribed repair applied unchanged: drop the number, keep the symbol,
  `corelib/io/io_shim.c@iox_stat_kind`. One token, gate green again.

  The new `src/tychoc.c@ffi_scalar_type` ref above was written in the `path@SYMBOL`
  form from the start, so it cannot drift when `src/tychoc.c` next moves.

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

- [ ] **Phase 6 — `io.size`, and it must land BEFORE phase 4**
  - Found by phase 3, not absorbed by it: phase 3's scope was `read_at`, and adding
    a second public call to spare a later phase a discovery is the silent absorption
    `CLAUDE.md` forbids. Recorded here instead, with the ordering it needs.
  - **Phase 4 cannot be written without a file length that does not read the file.**
    All three of its named cases need one: `416` must emit
    `Content-Range: bytes */LEN`; a suffix range `bytes=-N` needs LEN to compute its
    start; `bytes=A-` needs LEN for the end of `Content-Range: bytes A-END/LEN`.
  - **Verified absent, not assumed.** `grep -rn "st_size\|fn size\|file_size"` over
    `corelib/` and `src/tychoc.c` returns only `corelib/raster/raster.ty`'s BMP
    header field and the `st_size` phase 3 just added *inside*
    `corelib/io/io_shim.c@iox_read_at`, which is local to that function and reaches
    no caller. There is no size builtin. So today the only way to get a file's
    length is `len(io.read_bytes(p))` — reading the whole file to learn how big it
    is, which is precisely the defect phase 3 exists to remove. A `Range` server
    built on that would allocate the 1 GB phase 3 just stopped allocating.
  - Scope: `corelib/io/io_shim.c`, `corelib/io/io.ty`, and the same fixture.
  - The shape is already settled by phase 1 — a scalar payload, so `io.size` is
    `Result(int, IoErr)` and mirrors `corelib/io/io.ty@mtime` exactly, down to
    reusing `stat(2)`. Whether it is a third `stat` sibling or an extra `inout` on
    `iox_stat_mtime` is the one open question; phase 1's reasoning for keeping
    `mtime` separate from `iox_stat_kind` applies and probably settles it.
  - Note the honest alternative before building: phase 4 could call
    `io.read_at(p, off, n)` and infer a short read from `len`, which covers
    `bytes=A-B` without any new call. It does **not** cover `416` or `bytes=-N`,
    which need the length up front. Say which way phase 4 went.
  - Done when: a fixture reads the size of a file it just wrote, matches it against
    `len(io.read_bytes(p))` for the same file, and pins the missing-file `Err`.
  - Verify: `make test`, then `make -s corelib`, then
    `python3 scripts/check_citations.py` — expect drift into `corelib/io/io.ty`
    again and repair it by dropping the number, not repointing it.

## Out of scope

- **Multipart ranges.** Stated in phase 4 and recorded in the README.
- **`ETag`.** The other half of conditional requests; `sha256.hex` already exists
  so it is cheap, but it is a separate feature and the costing ranked only the
  `If-Modified-Since` half.
- **The six other excluded features** (TLS, HTTP/2, compression, virtual hosts,
  directory listings, pipelining). The costing rejected all six with reasons.
