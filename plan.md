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

- [x] **Phase 4 — `Range` and `206`**
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

  **Evidence (2026-07-31).**

  *Where the parse lives, and the three-valued answer that is the whole design.*
  `server/main.ty@parse_range` takes the header value **and the file length** and
  returns `server/main.ty@Rng` — `kind`, `start`, `end`. **Three outcomes, not
  two**, and every pairwise conflation of them is a wire-visible bug:
  `RANGE_NONE` is "there is no usable range here", which is a **200 with the
  whole file**; `RANGE_OK` is a satisfiable slice; `RANGE_UNSAT` is a **416**.
  The length is a parameter rather than something the caller applies afterwards
  because satisfiability is not a property of the header — `bytes=-64` has no
  start until the length is known, and `bytes=900-` is a 206 on a 1 KB file and
  a 416 on a 500-byte one. Splitting "parse" from "resolve" would mean two
  functions that are each wrong alone.

  *The parse rules, in the order they are applied.*

  | input | answer | why |
  |---|---|---|
  | length unknown (`io.size` failed) | NONE → 200 | a range cannot be checked, let alone clamped, without it |
  | not `bytes=` (absent, or `items=0-9`) | NONE → 200 | RFC 7233 §2.1: ignore a unit you do not understand |
  | `bytes=0-99,200-299` | NONE → 200 | multipart, out of scope by decision |
  | `bytes=A-B`, `B >= A`, `A < LEN` | OK, `A`..`min(B, LEN-1)` | **inclusive at BOTH ends**; B past EOF is CLAMPED, not refused |
  | `bytes=A-B`, `B < A` | NONE → 200 | RFC 7233 §2.1 calls the spec invalid, so the header is ignored |
  | `bytes=A-`, `A < LEN` | OK, `A`..`LEN-1` | open-ended |
  | `bytes=A-` / `bytes=A-B`, `A >= LEN` | UNSAT → 416 | first-byte-pos at or past EOF |
  | `bytes=-N`, `0 < N`, `LEN > 0` | OK, `max(0, LEN-N)`..`LEN-1` | suffix; `N > LEN` is the whole file, per RFC 7233 §2.1 |
  | `bytes=-0` | UNSAT → 416 | asks for nothing; an empty 206 would be a lie |
  | any range, `LEN == 0` | UNSAT → 416 | `Content-Range: bytes */0` |
  | non-digits anywhere (`bytes=abc`, `bytes= 0-9`, `bytes=0x10-`, `bytes=1-2-3`) | NONE → 200 | the grammar is `1*DIGIT` and nothing else |

  The unit is compared case-insensitively (`server/main.ty@ci_prefix`) because a
  range unit is a **token** (RFC 7233 §2.2). `server/main.ty@parse_pos` reports a
  digit run longer than 15 characters as `HUGE_POS` (1e15) rather than handing it
  to `strings.parse_int`: a Range header is attacker-controlled,
  `bytes=0-99999999999999999999999999` costs nothing to send, and past ~19 digits
  the value does not fit an int. `HUGE_POS` is larger than any file this program
  can open, so it lands in **exactly the arm the true value would have** — 416 as
  a first-byte-pos, clamped as a last-byte-pos. Both are asserted.

  *Bytes are never read to learn a length, which is what phases 3 and 6 bought.*
  The serve path is `io.size` (one `stat(2)`) then `io.read_at` (one `pread(2)`)
  — **never `read_bytes`**. A 416 opens nothing at all. Phase 6's brief asked
  which way this phase would go and this is it, unchanged from what phase 6
  predicted.

  *The off-by-one, named in the code and pinned by its own assertion.*
  `bytes=A-B` is **inclusive at both ends**, so the count is `end - start + 1`.
  The suite asserts `bytes=0-0` is **one** byte with body `disk[0:1]`, which is
  the case that separates the two: a server computing `end - start` sends 99
  plausible-looking bytes for `bytes=0-99` and **zero** here.

  *Content-Range reports what was READ, not what was asked for.* `last` is
  computed from `len(part)` after the pread, not from `rg.end`. The `io.size`
  → `io.read_at` window is racy in one direction (the file shrinks), and a short
  read described as the full slice makes a client reassembling a download stitch
  a hole into it. A read that comes back **empty** becomes a 416 rather than an
  empty 206 — by then no byte of the requested range exists, which is what 416
  means.

  ***Interaction 1 — `Range` + `If-Modified-Since`: the 304 wins, and nesting is
  what enforces it.*** RFC 7232 §6 orders the two, evaluating the conditional
  before `Range`. The range work is **inside the `else` of the 304 test** in
  `server/main.ty@serve_conn`, not in a flat `elif` chain over `(ims, range)` —
  a flat chain leaves the order as a property of statement sequence that the next
  edit can silently invert, where nesting makes it structural. A 206 there would
  hand back bytes the client already holds **and** drop the 304's promise that
  what it holds is current. Four assertions pin it (304 status, no body, no
  `Content-Range`, no `Accept-Ranges`), and two more pin the converse — the same
  request with a stamp one second older is a **206**, so the ordering is not
  implemented as "a Range suppresses ranges whenever a conditional is present".
  Falsified by mutant D.

  ***Interaction 2 — `Range` + `HEAD`: a third bodyless case that needs no third
  rule, which is the finding.*** Phase 2 found HEAD and 304 suppress bodies for
  different reasons and split them with `if head_only and not
  httpd.bodyless(out.status)`. A 206 is **not** bodyless, so it takes the HEAD
  arm, and that arm reports `len(out.body)` — which is the **slice**, because the
  206 is built with the slice as its body. So `HEAD` + `Range: bytes=0-99` gets
  206, the same `Content-Range`, `Content-Length: 100` and no bytes: exactly the
  head its GET would have produced. **The guard holds unchanged and no code was
  added for this case.** The alternative considered and rejected — skip the
  `pread` on HEAD and set the length by arithmetic — would be a second,
  never-exercised way to compute the one number this whole feature turns on.

  *`Accept-Ranges: bytes` is sent, on exactly two responses.* **200 for a file
  and 206.** RFC 7233 §2.3 says a server supporting ranges SHOULD send it, and
  the reason it is worth the 22 bytes is that it is the only way a resuming
  client learns the answer **without spending a probe request** to find out.
  **Not on 304** — RFC 7232 §4.1 lists what a 304 should carry and this is not on
  it, and a 304 describes no representation. **Not on an error page**, whose
  generated body has no ranges anyone wants. Both absences are asserted, so this
  is a pinned decision and not an oversight.

  *The corelib half.* `corelib/httpd/httpd.ty@reason_phrase` gained **206
  Partial Content** and **416 Range Not Satisfiable** — RFC 7233 §4.4's spelling,
  not RFC 2616's longer "Requested Range Not Satisfiable". Without them both
  rendered as the placeholder `"Status"`, the same defect phase 2 fixed for 304.
  `corelib/httpd/httpd.ty@bodyless` is **unchanged and correct**: neither 206 nor
  416 is bodyless, and both describe a real body.

  *The assertions assert BYTES.* `server/run.sh` went **90 → 173 assertions**,
  83 new. Every 206 case compares the body against the same slice taken from the
  file on disk with python's own slicing — `Content-Length: 100` is satisfied by
  a server returning the **wrong** hundred bytes, and three of the mutants below
  do exactly that. `img/logo.png` is the subject because it is **binary**: a
  wrong offset into a text file still looks like text. One new fixture, a
  zero-length `empty.txt` in the copied document root, for the case a repo of
  real assets cannot carry — every range over a 0-byte file is unsatisfiable and
  `bytes */0` is the only thing a 416 can say about it.

  *Pre-change failure, and the mutants for what it structurally cannot reach.*
  **45 of the 83 new assertions fail on the pre-change binary** (`de3fccb`, a
  `git worktree`, the new `server/run.sh` and the current `./tychoc` copied in;
  the run scores 128 ok / 45 FAIL against 173 / 0 here). The rest cannot, for
  phase 2's structural reason: a server that ignores `Range` trivially satisfies
  "200 with the whole body". So seven mutants of the **finished** code, each one
  edit, each verified green in that worktree before mutation:

  | variant | the bug it embodies | reddens |
  |---|---|---|
  | pre-change `de3fccb` | feature absent | 45 of 83 |
  | A: `rg.end - rg.start` | off-by-one on the inclusive end | 24 |
  | B: suffix returns `(0, n-1)` | `bytes=-N` read from the FRONT | 3 |
  | C: explicit `Content-Length: str(flen)` on the 206 | length describes the FILE, not the slice | 5 |
  | D: 304 also requires no `Range` header | `Range` outranks the conditional | 4 |
  | E: `Accept-Ranges` in `emit` | advertised on every response, 304 included | 3 |
  | F: `RANGE_NONE` → `RANGE_UNSAT` after the unit check | an unusable range REJECTED, not ignored | 20 |
  | G: 206 arm drops `Content-Type` | a 206 that does not say what it is | 1 |

  **77 of the 83 redden under at least one variant.** Mutant B reddening only 3
  is not weakness: those three are `bytes=-64`'s `Content-Range`, its body
  against `disk[-64:]`, and its body **not** equal to `disk[0:64]` — the last
  written precisely so that reading the suffix from the wrong end cannot pass by
  looking plausible.

  **The six that no variant can falsify are labelled as controls in
  `server/run.sh`**, on phase 2's precedent that a control and a proof look
  identical from the pass line:

  - **`200 unusable Range (no unit)` and `(unknown unit)`**, both halves of each.
    A header whose unit this server does not recognise leaves `parse_range` at
    the **same** `return RANGE_NONE` as a request with no `Range` at all — they
    are one input, so no mutant can separate them from an ordinary 200. This is
    also why mutant F is applied only *after* the unit check: mutating that
    return turns every plain GET in the suite into a 416.
  - **`HEAD 206 no body`** — HEAD suppresses a body on its own, exactly phase 2's
    `HEAD 304 no body`. What it does prove is that the 206 did not slip past the
    HEAD arm, which its `Content-Length` assertion cannot say alone.
  - **`200 the 0-byte file itself`** — a control on the fixture, so that the two
    416s over `empty.txt` are read against a file that is served correctly
    without a range.

  *Gates, all foreground, one command each.*

  ```
  $ make -s server-check
  server: OK                                  (173 ok, 0 FAIL; was 90)

  $ for i in $(seq 1 10); do make -s server-check; done
  run 1..10: server: OK  ok=173 FAIL=0        (10/10, no flake)

  $ make test
  passed: 560   failed: 0
  all green

  $ python3 scripts/check_citations.py
  citation check: ok
  ```

  560 before, 560 after — expected, for the reason phases 1, 3 and 6 recorded:
  nothing was added under `tests/`, and this gate is here to prove the
  `corelib/httpd/` change broke no compiler or golden behaviour.

  *Citation drift: the anchored refs held, and the BARE ones are the finding.*
  Phase 2 grew `server/main.ty` by ~110 lines and broke **32** refs; this phase
  grew it by ~200 and `python3 scripts/check_citations.py` was **green on the
  first run**. That is phase 2's `path@SYMBOL` conversion paying off exactly as
  it predicted, and it is also **not the whole story** — phase 2 wrote down that
  a bare **range** is never content-checked, so it rots silently. Two such ranges
  were phase 2's own repair, verified by reading at that commit:
  `server/run.sh`'s header cited `server/main.ty:812-816` for the bind and
  `server/main.ty:846-850` for the banner. My insertions moved both. (Both
  numbers are reproduced whole, which phases 1–3 could not do with theirs. The
  difference is the anchor: theirs carried one, and an anchor is a live promise
  about *today's* tree that would have reddened this block. These two are bare
  ranges — bounds-checked and nothing else — so quoting them asserts nothing
  about the current file, which is the same property that let them rot in the
  first place. They are a record of what the header said at `de3fccb`, so they
  stay.) Repaired the prescribed way — not repointed — to
  `server/main.ty@port_of` and `server/main.ty@banner`, which survive the next
  insertion by construction. **No sweep**; only the refs this phase broke were
  touched.

- [x] **Phase 5 — spec, README, and the sweep**
  - Scope: `docs/spec/` for the two new `core:io` calls,
    `docs/guides/corelib.md`, `server/README.md`, and `make ci`.
  - `server/README.md` lists both features under "Deliberately not implemented".
    They move, with the date, following the convention the file already uses for
    the two limitations that were closed on 2026-07-26.
  - Done when: both calls are specified with provenance, the README describes
    what the server now does, and `make ci` is green with the exit code observed.
  - Verify: the three doc gates, then `make ci` once, waited on in-turn.

  **Evidence (2026-07-31).**

  *Three calls, not two, and phase 6 is why this phase knew.* The scope line
  above says "the two new `core:io` calls" because it was written before phase 6
  existed. Phase 6's hand-off names the residual risk in as many words — an agent
  reading only its own brief documents two of three — and `corelib/io/io.ty@size`
  is the one that is easy to miss. All three are specified: `io.mtime` (phase 1),
  `io.read_at` (phase 3), `io.size` (phase 6).

  *The spec, in §32's per-package form and not a new one.* `docs/spec/18-library.md`
  §32.21 was read against its neighbours first. §32's preamble says each
  subsection gives a **realization kind** and the **key exports**, and §32.10
  `time` and §32.11 `datetime` are the two entries that carry a normative clause
  list — "Three semantics are normative for the package" — plus a trailing shim
  citation. This entry follows that shape exactly: catalog paragraph, then "Five
  semantics are normative for the three", then
  `docs/guides/corelib.md:267-298`; shim `corelib/io/io_shim.c`.

  *Two sentences in the old §32.21 were wrong, and a wrong normative sentence is
  worse than a missing one.* It said the package was **"pure Tycho"** and that it
  **"composes `core:strings` and `core:path`"**. Both were false before this plan
  and neither is a drift this plan caused: `corelib/io/io.ty`'s header records
  that the `core:path` dependency went on 2026-07-26 when `exists` became
  `stat`-backed, and its only `import` is `core:strings`; and `corelib/io/io_shim.c`
  has existed since the `getline` streaming reader, so the realization kind is the
  same "pure core plus a **libc-only shim**" that §32.10 and §32.11 state for
  their own packages. Repaired in place, because the three calls this phase
  specifies are shim calls and the entry could not name them while claiming the
  package makes no syscalls. **No count was written into that sentence** — the
  guide's "**Six** calls go past the builtins" is a figure the guide already
  frames and defends; restating it in the spec would be a second copy to rot.

  *The five normative clauses, each read out of the source rather than the
  evidence above it.* Every claim was checked against
  `corelib/io/io_shim.c@iox_read_at`, `@iox_stat_size` and `@iox_stat_mtime` and
  against the `Result` mapping in `corelib/io/io.ty@read_at`, `@size` and
  `@mtime`:

  | clause | what the source actually says |
  |---|---|
  | directory is `Ok` for `mtime`, `Err(IsDir)` for `size` | `iox_stat_mtime` has no `S_ISDIR` arm at all and returns `TY_RF_OK`; `iox_stat_size` returns `TY_RF_DIR` before it ever reads `st_size` |
  | `size` succeeds on exactly the paths `read_at` can read | both refuse `!S_ISREG` with the same `TY_RF_ERR` → `Err(Failed)`, and both answer `TY_RF_DIR` on a directory |
  | `Ok(0)` is a success, never the failure value | the status rides the shim's **return**, the payload an `inout`, so `0` is reachable only through `TY_RF_OK` |
  | the allocation is bounded by the FILE | `want = n < avail ? n : avail` where `avail = size - off` from the `fstat` on the open fd, and `malloc(want)` — never `malloc(n)` |
  | at-or-past EOF is `Ok` with zero bytes | the guard is `if (off >= size) { *status = TY_RF_OK; ... }`, so **at** EOF as well as past it — the spec says "at or past", which the phase brief's wording did not |

  The last row is the one worth flagging: the brief said "a read starting past
  EOF". `off == size` takes the same arm, so the spec sentence says **at or
  past**. Stating only "past" would have left the boundary case unspecified,
  which is exactly the gap a reader fills in with a guess.

  *How the guide was grown without shifting a single cited range — the
  measurement that decided it.* `docs/guides/corelib.md`'s `io` bullet was
  **rewritten at an identical line count**, 32 lines, occupying 267-298 before and
  after; `docs/guides/corelib.md` is 473 lines before and after, and `- **`os`**`
  still begins at 299. This was not stylistic. Phase 6's brief said ~30 refs in
  `docs/spec/18-library.md` cite this file by range, and grepping every
  line-numbered ref to it found **12 that begin past line 298** and would have
  moved: `docs/spec/18-library.md`'s `os` entry, three ranges in
  `docs/spec/appendix-h-differences.md`, six in
  `docs/internals/plan-signals-DONE.md`, one in
  `docs/internals/plan-friction-DONE.md`, and **one in `FRICTION.md`**. That last
  one is the whole argument: `FRICTION.md` is the file a person edits by hand and
  the one CLAUDE.md protects explicitly, so it could not be repaired here — and
  every one of the 12 is a **bare range**, which phase 4 established is never
  content-checked and therefore rots in silence with the gate green. Growing the
  bullet would have broken twelve pointers, repaired ten, and left two knowingly
  false. So the budget was fixed and the text was reflowed to fit it: the new
  material on the three calls was paid for by compressing the `make_dir`/`remove`
  prose and dropping one sentence about payload-free variants that the **previous
  bullet already states**.

  *One rotted range repaired, in scope.* §32.21's citation read
  `docs/guides/corelib.md:266-297`. Line 266 is the tail of the *previous* bullet
  and 298 the tail of `io`'s — off by one, wrong before this plan, and invisible
  to the gate for the bare-range reason above. Repointed to `267-298`, verified by
  reading both boundary lines. A range has no `path@SYMBOL` form, so repointing is
  the only repair available; there is nothing to convert it to.

  *The README was stale in six places where the brief warned of one.* Phase 4's
  note said an earlier phase found four; this found six, and only two of them are
  the ones the brief named:

  | # | what it said | what is true |
  |---|---|---|
  | 1 | byte ranges and conditional requests under "Deliberately not implemented" | both implemented — moved, with the date |
  | 2 | "57 assertions in total" | **173**, counted from the runner's own output; and 57 was already stale before this plan, since Pre-flight measured 61 at `edb7f78` |
  | 3 | `Statuses \| 200, 301, 400, 403, 404, 405, 408, 431` | 206, 304 and 416 were missing |
  | 4 | `make server-check  # ~4s` | **8.5 s measured** here; written as `~8s` |
  | 5 | "`io.is_dir` a fourth in `core:io`" | four `core:io` additions this program forced, not one |
  | 6 | "### Three rough edges that were here" | five |

  Items 3-6 are the ones the brief's "check the whole file" caught. Item 5 is the
  one a reader would have been most misled by: that parenthesis exists to count
  what running this server has forced into the corelib, and it is the sentence
  this plan's entire premise is written on.

  *What the README now records, and the exclusion that is a decision.* Two new
  rows in "What it does" — **Conditional GET** (`Last-Modified` on every file
  `200`; `304` only when the mtime is *not newer*; absent, unparseable, an
  obsolete date form or an unreadable mtime all `200`) and **Byte ranges** (the
  three forms, the inclusive-both-ends clamp, `416` with `bytes */LEN`, and
  `Accept-Ranges` on exactly the `200` for a file and the `206`). Two new bullets
  under the rough edges, with commits (`74fd4c7`; `cf0c0f3` and `de3fccb`, served
  by `7552384`) and `path@SYMBOL` citations throughout, so they survive the next
  insertion into either file.

  **Multipart ranges moved into "Deliberately not implemented" with the reason
  written down**, which phase 4 required and `server/main.ty@parse_range`'s own
  comment promises the README carries: `bytes=0-99,200-299` is answered **`200`
  with the whole file**. The two alternatives are named and rejected — `416` would
  be false, because those ranges *are* satisfiable and only this server's response
  format is missing; and returning the first range alone would misdescribe what
  was sent. `ETag` stays excluded and now says why it is the *validator half* of
  conditional requests rather than reading as a synonym for the half that shipped.

  *One figure was written qualitatively on purpose.* The controls — assertions no
  mutation of the feature can redden — are labelled in `server/run.sh` in
  comments, not in a countable form: one comment reads "the first two are
  CONTROLS". Phases 2 and 4 between them describe seven. Rather than publish a
  number no command reproduces and nothing checks, the README says "a handful …
  labelled as controls in the script" and points at the script. This is the
  "never copy a figure the gate prints into prose" rule applied one step earlier:
  to a figure nothing prints at all.

  *Gates, all foreground, one command each, in the brief's order.*

  ```
  $ python3 scripts/check_citations.py
  citation check: ok

  $ sh scripts/check_links.sh
  link check: ok (138 markdown files, no dead relative links)

  $ sh scripts/spec_check.sh
  spec-check: Appendix A grammar matches §3/§4 (ok)
  spec-check: all Appendix E fixture citations resolve (ok)
  spec-examples: 9 runnable example(s), all pass
  ```

  **No ref was broken and none needed repairing** — the first phase here of which
  that is true without a repair paragraph following it. The guide did not shift a
  line, the spec's own growth is cited only by bare bounds-checked refs, and every
  citation written by this phase is in the `path@SYMBOL` form, which survives
  insertion by construction. (The gate's `path@SYMBOL` count rises across this
  commit; the number is not quoted here, per CLAUDE.md — run
  `python3 scripts/check_citations.py --stats` for today's.)

  *The sweep, run once, at the end, with the exit code observed.*

  ```
  $ make ci > ci.log 2>&1; echo "CI_EXIT=$?" >> ci.log
  CI_EXIT=0                                   (1100 s wall = 18m20s)
  ```

  All thirteen steps green, and the four the brief singled out as never having
  seen this work are in that run by name: `[2b/13] make ilp32` (the fixture suite
  rebuilt under `-m32`, which is the lane that would catch a `tycho_int` /
  `off_t` / `st_size` width mistake in the three new shim functions),
  `[2c/13] make asan-self` (`compiled: 577  failed: 0`), `[3c/13] make
  server-check`, and the three fuzz lanes — `[6] ok=200`, `[7] accepted=34
  rejected=166`, `[8] ok=150`, all `FAIL=0`. `make test` reported `passed: 560
  failed: 0`, unchanged across all six phases for the reason each of them
  recorded: the corelib work extends an existing `corelib/test/` lane rather than
  adding a `tests/` fixture.

  **This was the plan's only `make ci`.** Six phases, one sweep, and it confirmed
  rather than discovered — which is what CLAUDE.md asks of it.

  *What the sweep did and did not cover, stated exactly.* All three documentation
  files were already written when it started, so `[12] spec-check`,
  `[12b] docs-fences` and `[13] check-links` ran over them. **This evidence block
  was written afterwards** and is not in that run — deliberately, since it is
  Markdown and CLAUDE.md's gate table says Markdown cannot affect a compiled
  artifact. It is covered instead by re-running the two doc gates after writing
  it, which is what caught the four bare refs in phase 8's table below: reproduced
  the way the citing files spell them, they bound to the wrong document and
  reddened the gate on the report of the defect. Both gates are green above at
  the state being committed.

- [x] **Phase 6 — `io.size`, and it must land BEFORE phase 4**
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

  **Evidence (2026-07-31).**

  *A third `stat(2)` sibling, and the caller count that decided it.*
  `corelib/io/io_shim.c@iox_stat_size` is `(const char *path, tycho_int *size)`
  returning the status — `corelib/io/io_shim.c@iox_stat_mtime`'s shape exactly, for
  phase 1's reason: a scalar payload cannot share the 0..3 code space (a 2-byte file
  would be indistinguishable from `TY_RF_DIR`) and a scalar *can* be an `inout`, so
  the payload takes the `inout` and the status keeps the return. **No new FFI
  pattern was invented; this is the second use of the one phase 1 proved.**

  Phase 1 settled sibling-versus-fold by counting callers who would pay for a value
  they discard, and the same count decides this — **it just lands differently, and
  the honest reading is that the count alone does not settle it here.**
  `iox_stat_kind` has **three** callers (`corelib/io/io.ty@is_dir`,
  `corelib/io/io.ty@exists`, and `corelib/io/io_shim.c@iox_make_dir`, which calls it
  from C). `iox_stat_mtime` has **one** (`corelib/io/io.ty@mtime`) — verified by
  grep, not assumed. So folding a size into it costs one discard, not three, and on
  phase 1's arithmetic that is a much weaker objection.

  What settles it is that the fold **creates a second discard immediately**:
  `io.size` would then call `iox_stat_mtime` and throw away the mtime, so *both*
  public calls would be discarding half of a merged fetch, and the merged function
  could no longer be called `iox_stat_mtime` — a rename of a shim phase 1 shipped
  four commits ago. Two discards and a rename to save one `stat(2)` for a caller
  that wants a size *and a date together*, and no such caller exists or is coming: a
  `Range` request wants a size and a **slice**.

  *And it is not shared with `corelib/io/io_shim.c@iox_read_at`'s `fstat` either,
  which is the case phase 1 did not have.* The phase brief is right that phase 4
  will often want both the size and a slice of the same file, so this was the real
  candidate. It does not work, and the reason is **ordering, not cost**: that
  `fstat` runs on an fd opened, read and closed inside one call, and all three
  cases needing a length need it *before* they know what to read — a `416` emits
  `Content-Range: bytes */LEN` and reads **nothing at all**, `bytes=-N` needs LEN to
  compute its start, and `bytes=A-` needs LEN to name its end. A size handed back
  out of `read_at` arrives after the decision that needed it. So the cost of the
  split is one extra `stat(2)` on the path that then also reads, and **zero** on the
  `416` path, which stats once and opens nothing.

  *Which way phase 4 goes, since the brief asked this phase to say.* The alternative
  it named — call `io.read_at` and infer a short read from `len` — covers
  `bytes=A-B` and nothing else, which is the half phase 4 already had. `io.size`
  now exists, so phase 4 takes the length up front for all three forms and the
  `416`, and `io.read_at` for the slice. Two calls, one `stat` and one `pread`,
  and the file is never read to learn its length.

  *The directory decision, which deliberately DIFFERS from `mtime`'s and is the
  case where phase 1's reasoning does not carry.* `corelib/io/io.ty@size` returns
  **`Err(IsDir)`** for a directory. Phase 1 returned `Ok(secs)` for a directory's
  mtime on the grounds that erroring would discard a field the kernel had already
  supplied — and that argument is sound *there*, because a directory's `st_mtim`
  **is** the modification time being asked for. A directory's `st_size` is not the
  byte length being asked for: it is the size of the directory's own on-disk entry
  structure (4096 on ext4, filesystem-dependent elsewhere, 0 on some), and no read
  can ever produce that many bytes. So this is not withholding an answer, it is
  declining to report a number that is not one — and the caller who would be misled
  is precisely the one this call exists for, which would answer a `416` with
  `Content-Range: bytes */4096` for a path holding no bytes at all.

  The rule that keeps the two calls honest with each other, and the one I would
  defend if only one line survived: **`io.size` succeeds on exactly the paths
  `io.read_at` can read.** Regular file → both `Ok`; directory → both `Err(IsDir)`;
  fifo, socket or device → both `Err(Failed)`, since `st_size` is meaningless for
  them and pread(2) fails `ESPIPE` on anything unseekable. `size(p)` is the length
  `read_bytes(p)` would return, on every path, or neither answers. A size no read
  could produce is worse than no size.

  *Zero length: `Ok(0)`, and 0 is structurally not the failure value.* This is the
  case the brief singled out and it needs no special handling — it is **bought by
  the FFI shape**, which is worth stating because it is the payoff for the split
  above. The status rides the shim's *return*, so a failure arrives as an `Err` and
  can never present as a small number; `0` is reachable only through `TY_RF_OK`.
  Contrast the defect this whole plan exists to remove, where `len(read_bytes(p))`
  gave `0` for an empty file, a missing file **and** a directory. The golden prints
  the pair rather than the number alone (`size_empty=Ok n=0`) so that an `Err`
  collapsing through `unwrap_or` would read `Failed n=-1`, and a status leaking into
  the payload would read `Ok n=1`. Only `Ok n=0` passes. A static server meets this
  file for real and owes it a `200` with `Content-Length: 0`, not a `500`.

  *The fixture, and why it is the inverse of phase 3's.* Phase 3 asserted letters so
  a wrong offset prints wrong letters instead of a right count. Here the **number is
  the assertion**, so the length has to be unambiguous *in the source* rather than a
  property of the machine: `corelib/test/io/main.ty` reuses the 26-byte alphabet
  file phase 3 writes, so `26` is a fact of the fixture. It is **also** cross-checked
  against `len(read_bytes)` on the same file — the read-the-whole-thing route this
  call replaces — so a size disagreeing with the bytes actually present fails even
  if the literal `26` were itself wrong. Both must hold. `corelib/test/io.out` gained
  three lines; `mwhy` was reused unchanged, and its `IsDir` arm — written by phase 1
  as an unreached guard against a silent change — **is now a reached arm.**

  ```
  size=Ok n=26 is26=1 agrees=1
  size_empty=Ok n=0 wrote=1 readlen=0
  size_missing=NotFound size_dir=IsDir
  ```

  The golden matched on the first run: the diff against the recorded file was
  exactly these three added lines and no others, checked before recording.

  *Gates, all three foreground, one command each.*

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

  560 before, 560 after — expected for the reason phases 1 and 3 both recorded: this
  extends an existing `corelib/test/` lane rather than adding a `tests/` fixture, so
  the gate proves the compiler still handles the FFI shape without its count moving.

  *No citation drift, and that is a result rather than luck.* Phase 1 broke one ref
  by growing `corelib/io/io.ty`; phase 3 broke one by moving `iox_stat_kind` 90 lines
  down `corelib/io/io_shim.c`. This phase inserted into **both** files and moved
  `iox_make_dir` and `iox_remove` further still, and the gate was green on the first
  run with nothing to repair. The reason is that the refs those phases repaired were
  converted to `path@SYMBOL`, which survives insertion by construction — the
  mitigation CLAUDE.md prescribes, paying off on the third consecutive phase to grow
  these files. Every citation written above is in that form for the same reason, so
  phase 4 can grow these files again without reddening this block.

  **Hand-off to phase 5, and no phase 7 was added.** The only discovery outside
  `corelib/io/` is that **phase 5's scope line now under-counts: it says "the two new
  `core:io` calls" and there are three** — `io.mtime`, `io.read_at` and `io.size`.
  That is a wrong number in a brief, not new work: phase 5 already owns documenting
  whatever `core:io` gained, and filing a phase 7 to say "phase 5 should also
  document the third one" would be a phase whose whole content is a correction to
  another phase's word. Phase 1 set this precedent when its only discovery was drift
  it had itself caused. **The residual risk is real and named rather than papered
  over:** a phase 5 agent that reads its own brief and not this block documents two
  of three. Whoever writes phase 5's instructions should say "three", and
  `corelib/io/io.ty@size` is the one that is easy to miss because it arrived after
  the sentence was written.

- [ ] **Phase 7 — the bare ranges into `server/main.ty` that were ALREADY wrong
  before this plan started**
  - Found by phase 4 while repairing its own two, and **not absorbed by it**:
    phase 4's scope was three files, these reach six, and none of them is drift
    phase 4 caused. Recorded here rather than swept, which is what CLAUDE.md
    prescribes.
  - **This is not the hand sweep CLAUDE.md has declined three times.** That
    refusal is about converting refs that are *correct*. Every ref below was
    verified **wrong at `de3fccb`**, before this plan touched `server/main.ty`,
    by reading the cited line out of that commit — `git show de3fccb:server/main.ty`
    piped through `sed -n '<N>p'`, one ref at a time. Not one of them names what
    its prose says it names; they are all somewhere inside an unrelated comment
    block. So this is a set of **live pointers that are already false**, and the
    gate is structurally unable to say so.
  - **Why the gate is green over them, which is the reusable finding.** All are
    bare **ranges** or bare single refs with no `@token`. `scripts/check_citations.py`
    bounds-checks those and nothing more, so a ref stays green as long as the
    file is long enough — and `server/main.ty` only ever gets longer. Phase 2
    wrote this down after being caught by it twice in one phase; phase 4 was
    caught by the same two refs a third time. **The third occurrence is what
    makes this worth a phase.**
  - Verified stale (the cited line, in that commit, is not what the prose
    claims): `server/README.md:57` and `server/README.md:197` for the stopped
    line, `server/README.md:59` for `log_req`, `server/README.md:182` for the
    accept-loop `Err` arm, `server/README.md:260` for the banner, `Makefile:250`
    for the banner, `corelib/signal/signal_shim.c:97` for the accept/serve/
    retire/close sequence, `server/run.sh` at three places for `log_req`,
    `worker` and the `bad_len` refusal, `server/main.ty:617` for its own
    idle-expiry arm, and `FRICTION.md` at five places. **Count them again before
    starting** — the list above is what one grep found on 2026-07-31 and phase 5
    may move `server/README.md` under its own feet.
  - Repair by **naming the construct**, not by repointing: `server/main.ty@stopped`,
    `@log_req`, `@worker` and so on. A repointed number is wrong again the next
    time anyone adds a paragraph, which is precisely the history above.
  - **`FRICTION.md`'s five are the exception and must be left alone** unless read
    individually: that file is edited by hand, several of its entries are
    struck-through closed records quoting line counts as *evidence for a
    decision*, and CLAUDE.md protects those numbers explicitly.
  - Scope: whichever of the files above still hold a stale ref. Not
    `scripts/check_citations.py` — making bare ranges content-checkable is a
    different, larger question and would redden the whole tree at once.
  - Verify: `python3 scripts/check_citations.py` (<1s) and
    `sh scripts/check_links.sh`. **Not `make test`, not `make ci`** — comments and
    Markdown cannot affect a compiled artifact. `make -s server-check` only if
    `server/run.sh` is touched.

- [ ] **Phase 8 — the bare refs into `docs/spec/18-library.md`, wrong before this
  plan and now further from their targets**
  - Found by phase 5 while checking what its own insertion would move, and **not
    absorbed by it**: phase 5's scope was the `io` entry, these reach four other
    files, and none of them is drift phase 5 caused. This is phase 7's class in a
    second document, which is the reason it is worth its own phase rather than a
    line in phase 7 — the *files* differ, so the "count them again" step differs.
  - **Verified wrong at `edb7f78`**, before this plan touched anything, by reading
    each cited line out of that commit (`git show edb7f78:docs/spec/18-library.md`
    piped through `sed -n '<N>p'`, one ref at a time). Not one names what its prose
    claims:

    | the citing line | the ref it holds | its prose claims | that line at `edb7f78` actually is |
    |---|---|---|---|
    | `docs/internals/spec-plan.md:429` | `docs/spec/18-library.md:254` | §32.24 `net` | inside §32.22 `os` |
    | `docs/internals/spec-plan.md:429` | `docs/spec/18-library.md:265` | §32.25 `bignum` | a blank line |
    | `docs/internals/spec-plan-audit-2026-07-24.md:109` | `docs/spec/18-library.md:254` | §32.24 `net` | inside §32.22 `os` |
    | `docs/internals/plan-signals-DONE.md:336` | `docs/spec/18-library.md:263` | `net` | inside §32.23 `regex` |
    | `docs/internals/plan-signals-DONE.md:337` | `docs/spec/18-library.md:285` | `decimal` | inside §32.25 `bignum` |

    (Each `docs/spec/18-library.md:N` above is written with its path spelled out.
    Reproduced the way the citing files write them — bare `:254`, `:265` — they
    would bind to whichever document *this* table row named last, which reddens
    the gate on the very defect being reported. That is the trap this phase
    exists to close, and it caught this write-up first.)

  - **This phase's own edit moved them a further +41 lines.** `docs/spec/18-library.md`
    grew from 434 to 475 lines, all of it inside §32.21, so every section from
    §32.22 down shifted by 41 and every ref in the table above is 41 lines further
    from its subject than it was this morning. That is disclosed rather than
    repaired here because repairing it is a different file set from this phase's
    scope, and because the refs were already false — the shift makes a wrong
    pointer wronger, not a right one wrong.
  - **Why the gate is green over all of them**, which is phase 7's finding
    reproduced in a second file: every one is a **bare** ref with no `@token`, so
    `scripts/check_citations.py` bounds-checks it and nothing more. A ref stays
    green as long as the file is long enough, and `docs/spec/18-library.md` only
    ever gets longer.
  - **Two of the five are inside record shapes and must be left alone**, which is
    why this is not a sweep: `docs/internals/plan-signals-DONE.md:1406-1409` is a
    six-row before/after table whose cells *quote* the two
    `docs/spec/18-library.md` line numbers above as what a ref said at a past
    moment, and `docs/internals/spec-plan.md:429`'s ref sits inside
    a `**[CLOSED — …]**` marker, which is a closure record. Read each one
    individually before touching it; the number on a record line is data.
  - Scope: whichever refs are still both live pointers and wrong. Not
    `scripts/check_citations.py`.
  - Verify: `python3 scripts/check_citations.py` and `sh scripts/check_links.sh`.
    **Not `make test`, not `make ci`** — Markdown cannot affect a compiled
    artifact.

## Out of scope

- **Multipart ranges.** Stated in phase 4 and recorded in the README.
- **`ETag`.** The other half of conditional requests; `sha256.hex` already exists
  so it is cheap, but it is a separate feature and the costing ranked only the
  `If-Modified-Since` half.
- **The six other excluded features** (TLS, HTTP/2, compression, virtual hosts,
  directory listings, pipelining). The costing rejected all six with reasons.

## Status — PLAN COMPLETE

Six phases, six commits, one `make ci`:

| phase | commit | what shipped |
|---|---|---|
| 1 | `74fd4c7` | `io.mtime` — `stat(2)` mtime in whole seconds; a directory is `Ok` |
| 2 | `ca716a2` | `Last-Modified` / `If-Modified-Since`, `304`; `httpd.bodyless`, `304` in `reason_phrase` |
| 3 | `cf0c0f3` | `io.read_at` — `pread(2)`, allocation clamped to the file |
| 6 | `de3fccb` | `io.size` — a length without a read; a directory is `Err(IsDir)` |
| 4 | `7552384` | `Range` / `206` / `416`, `Accept-Ranges`; `206` and `416` in `reason_phrase` |
| 5 | this commit | the spec, the guide, the README, and the sweep |

Phase 6 was filed by phase 3 and had to land **before** phase 4, so the commit
order is 1, 2, 3, 6, 4, 5 rather than the numbering.

**What the goal asked for, and whether it happened.** A conditional GET answers
`304` with no body; a `Range` request answers `206` with `Content-Range`; both
are asserted by `make server-check`, which went **61 → 173 assertions**. `core:io`
gained not two capabilities but **three** — `mtime`, `read_at` and `size` — all
three usable by any Tycho program and all three specified in
`docs/spec/18-library.md` §32.21. `make ci`: **CI_EXIT=0**, observed, 1100 s.

**Still open, and deliberately.** `ETag`, multipart ranges, and the six other
excluded features — all under "Out of scope" above and all recorded in
`server/README.md` with their reasons. Two documentation phases are filed and
unstarted: **phase 7** (bare refs into `server/main.ty`, wrong before phase 4)
and **phase 8** (the same class in `docs/spec/18-library.md`, found by phase 5).
Neither blocks anything; both are pointers that are already false and that no
gate can see.

**One observation this phase cannot explain and did not cause.** An untracked
`new_ideas.md` was present in the working tree at the start of this phase's
session and is absent now. This phase ran no `rm`, `git clean` or `git checkout`,
and `grep -n "git clean\|rm -rf\|distclean" scripts/ci.sh Makefile` returns
nothing, so `make ci` did not remove it either. Recorded rather than guessed at.
