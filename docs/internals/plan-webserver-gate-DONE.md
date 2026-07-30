# The web server: a real gate, and a README that tells the truth

Previous plan complete and archived at
[plan-loops-cleanup-DONE.md](plan-loops-cleanup-DONE.md)
(67 phases, all closed, `make ci` green). `FRICTION.md` was re-scored against the
tree at `945acfa` and its ten numbered items all reproduce.

## Goal

`server/main.ty` is the largest program in the tree — 341 code lines, a real HTTP
server — and **nothing verifies it.** `make server` builds it and asserts
nothing, and says so in its own comment (`Makefile:227-229`). Its README then
documents two limitations the program no longer has.

Done looks like: a runner that starts the server, talks to it, asserts what it
answers, and tears it down; that runner in `make ci`; and a README describing the
program as it is.

This is the same hole `examples/fetch/run.sh` sat in — red for weeks, unnoticed,
because no aggregate lane ran it. That was closed on 2026-07-30. The server is
the last program still in it, and it is the biggest.

## Pre-flight

- **Worst case:** a gate that passes without asserting. A daemon test that starts
  the server, fails to reach it, and treats "no response" as "nothing to check"
  is worse than no gate — it makes the hole invisible. Every assertion in this
  plan must be shown failing on a deliberately broken server before it is
  believed.
- **Reversibility:** fully. New files plus a CI step; no data touched, no
  language change.
- **Verified — nothing runs it.** `Makefile:226-232` is a build-only target whose
  comment states it is "deliberately NOT in `make ci` and asserts nothing,
  because the thing it produces is a long-running network daemon, not a fixture
  with a golden". `server/README.md:142-144` repeats it.
- **Verified — the tree has no pattern for this.** Every `examples/*/run.sh` is
  batch-and-compare: run a program to completion, diff against a golden. Checked
  all eleven. None starts a background process and talks to it, so the readiness
  / teardown / port-collision pattern has to be established here rather than
  copied. `examples/fetch/run.sh` supplies the *conventions* — `set -u`,
  `TYCHOC=./tychoc`, `T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT` at `:49`, and a
  dependency skip that exits 0 at `:32`.
- **Verified — port collision is already solved and nobody used it.**
  `server/main.ty:513` documents `--port N  port to bind, 0 = pick free`, and
  `:604-614` binds, reads the actual port back with `net.port_of`, and prints it
  in a startup banner on **stderr**:
  `tycho-httpd: serving <root> on http://<host>:<bound>/  workers=N idle=Nms`.
  So the runner can start with `--port 0`, poll the banner out of a log file for
  both readiness *and* the port, and never pick a number that might be taken.
  **No `sleep` is needed and none should be used.**
- **Verified — the assertions already exist, written down twice.**
  `server/README.md:146-154` gives the short transcript (headers, a binary `cmp`
  of a PNG, the `--path-as-is` traversal 403, keep-alive reuse). The signed-off
  long form is under Phase 7 of
  `docs/internals/plan-friction-DONE.md`: 200/301/403/404/405/408/431/400, a
  20 KiB header, binary junk, a partial-head stall, keep-alive 3-on-one-fd,
  the access log's four workers and peer address, `--help` exit 0, `--bogus`
  exit 1, and SIGTERM wait status 143.
- **Verified — the README is stale about two things, both of which the program
  now does.** It claims an empty directory is answered 404 because "there is no
  `stat` or `is_dir` in the corelib" and that the access log has no client
  address because `getpeername` is not exposed. Both are false:
  `corelib/io/io.ty:133` is `fn is_dir(p: string) -> Result(bool, IoErr)` over a
  real `stat(2)` and `server/main.ty:301` matches on it;
  `corelib/net/net_shim.c:193` is `getpeername` + `inet_ntop` and
  `server/main.ty:333` uses `net.peer_addr` for the log field the README says is
  unreachable.
- **Assuming — the banner is a sound readiness signal.** It is printed at
  `server/main.ty:614`, *after* `net.listen` returns at `:604` but *before*
  `worker()` starts accepting at `:616`. A connect between those points is queued
  by the kernel rather than refused, because the socket is already listening — so
  the banner should be safe as "connectable". **Risk if wrong:** a flaky gate
  that fails under load, which is the worst kind. Phase 1 must prove it by
  hammering the runner repeatedly, not by reasoning about it.
- **Assuming — `curl` is the right client.** The abuse cases (a 20 KiB header, a
  deliberate stall, raw garbage) may need a raw socket rather than `curl`. If so,
  say what you used and why. A skip when the client is unavailable must exit 0
  and print a SKIP line, as `examples/fetch/run.sh:32` does.

## Phases

- [x] **Phase 1 — `server/run.sh`: start it, talk to it, tear it down**
  - Scope: new `server/run.sh`, a `server-check` target in `Makefile`, and
    whatever fixture files under `server/www/` the assertions need. **Not** the
    CI wiring (phase 2), **not** `server/README.md` (phase 3).
  - Establish the daemon pattern the tree lacks: `--port 0`, poll the stderr
    banner for readiness and the bound port, `trap` teardown that kills the
    server on every exit path including failure, and a temp dir that is always
    cleaned. No `sleep` as a readiness mechanism.
  - Assert the documented behaviour: the short transcript in
    `server/README.md:146-154`, plus as much of Phase 7's long form in
    `docs/internals/plan-friction-DONE.md` as can be asserted without a raw
    socket. State plainly which cases you could not cover and why.
  - **Every assertion must be shown to fail on a broken server.** Break one thing
    (a wrong root, a corrupted asset, a stubbed status) per assertion class, show
    it reddening, restore it. An assertion never observed failing is not an
    assertion.
  - Done when: `sh server/run.sh` is green, `make server-check` runs it, the
    deliberate-break proofs are recorded, and running it ten times in a row is
    green ten times — the flakiness check the Pre-flight's readiness assumption
    requires.
  - Verify: `sh server/run.sh`, then `make server-check`, then the ten-run loop,
    then the break proofs. **Not `make ci`** — that is phase 2.

  **Evidence (2026-07-30).** New `server/run.sh` (52 assertions), new
  `server-check` target at `Makefile:226-233` plus its name in `.PHONY`
  (`Makefile:16`). `server/main.ty` untouched. ~4.1s per run.

  `sh server/run.sh` → `EXIT=0`, `server: OK`, 52 `ok` lines, 0 `FAIL`.
  `make -s server-check` → `MAKE_SERVER_CHECK_EXIT=0`, 52 `ok`, `server: OK`.

  *Readiness, which was the Pre-flight's one unverified assumption.* No `sleep`
  is used to decide the server is up. The runner starts it with `--port 0`
  (`server/main.ty:513`), redirects stderr to a file, and polls that file every
  20 ms for up to 10 s for the banner `server/main.ty:610-614` prints after
  `net.listen` at `server/main.ty:604` and `net.port_of` at `server/main.ty:608`.
  The regex is anchored on the whole line, so the same match yields readiness,
  the bound port, and the root/workers/idle values, which are then asserted
  against what was asked for. Measured: the banner appears in 0–2 ms and the
  first connect always succeeds. The assumption that a connect between
  `net.listen` (`:604`, i.e. `server/main.ty:604`) and the first `accept` in
  `worker()` (`server/main.ty:616`) is queued rather than refused held in
  **10/10** runs.

  *Teardown.* `trap cleanup EXIT INT TERM`, where `cleanup` kills the server,
  reaps it, and removes the temp dir. Proved on three paths, each checked with
  `pgrep -a tycho-httpd` afterwards: success (`exit=0`, no stray), failure (a
  wrong-`--root` copy, `exit=1`, no stray), and interrupt (`SIGINT` 2 s into a
  run, no stray). `/tmp/tmp.*` count 345 before and 345 after. This is not
  hypothetical: an early hand probe of mine that had no trap *did* leave a
  `tycho-httpd` bound and running, and it had to be killed by hand — exactly the
  poisoning the trap exists to prevent.

  *Client: raw sockets via python3, not curl.* Three assertions cannot be
  written with curl — the 20 KiB-header 431, the deliberately unfinished head
  behind the 408, and bytes that are not HTTP at all behind the 400 — so having
  paid for a raw client, everything uses it and there is one failure vocabulary.
  It also removes the need for curl's `--path-as-is` in the traversal case: a
  raw socket has no normalizer to switch off. Skips with a `SKIP` line and
  exit 0 if `python3` is absent, as `examples/fetch/run.sh:32` does for libcurl.

  *Covered (52).* Banner/port/root/workers/idle · 200 for `/img/logo.png`
  (status, `Content-Type: image/png`, `Content-Length`, `Cache-Control`, and the
  body **byte-identical** to the 7883-byte file on disk) · 200 for `/` serving
  `index.html` verbatim · `text/css` for `/style.css` · HEAD (200, full
  `Content-Length`, zero-byte body) · 301 `/about` → `/about/` and the 200
  behind it · 301 `/emptydir` → `/emptydir/` then 404 for `/emptydir/`, a
  directory git cannot store so the runner makes it · 403 for two traversals and
  for a hidden segment, plus a check that the 403 body does not contain the file
  · 404 · 405 for POST and DELETE with `Allow: GET, HEAD` · 400 four ways
  (binary junk, absolute-form target, `%00` in the path, and the
  `Content-Length: -5` smuggling primitive refused at `server/main.ty:426-436`)
  · 431 against `MAX_HEAD` (`server/main.ty:69`) · 408 with the elapsed time
  bounded around `--idle-ms` · keep-alive, 3 requests on one fd with all 3
  bodies compared to disk · survival of 50 hostile mid-response disconnects (the
  `SIGPIPE` case; `MSG_NOSIGNAL` at `corelib/net/net_shim.c:41-53`) · 8
  concurrent connections all answered · the access log showing **all four**
  workers and a peer address on every line (`net.peer_addr`,
  `server/main.ty:342-352`) and at least one line for each of
  200/301/400/403/404/405/408/431 · `SIGTERM` → shell wait status 143 · `--help`
  exit 0, `--bogus` exit 1 naming the option, `--port 70000` exit 1.

  *Not covered, and why.* (a) The concurrency **measurements** from the earlier
  phase 7 (222/433/853 ms for 4/2/1 threads) — wall-clock on a shared box is the
  flake this runner exists to avoid; what is asserted instead is the structural
  fact that all four workers take traffic. (b) The `TCP_NODELAY` 620×
  measurement, same reason. (c) The "stopped after N requests" line — see the
  finding below; it cannot be reached. (d) HTTPS/HTTP2 etc. are not implemented,
  so there is nothing to assert.

  *Deliberate-break proofs — 13 classes, each observed red then restored.* The
  shipped `server/run.sh` was never edited for these; each break was a scratch
  copy with one perturbation, so "restore" is "discard the copy". Every one
  exited 1 and named the failure:

  | # | break | what reddened |
  |---|---|---|
  | 1 | server cannot start (`--workers 999`) | `FAIL readiness: no startup banner on stderr within 10s` (+13 more) |
  | 2 | one byte appended to the served `logo.png` | `FAIL 200 /img/logo.png body == file on disk`, both `Content-Length` checks |
  | 3 | wrong document root | 22 FAILs, from the banner check through every status |
  | 4 | `--quiet` | all 12 access-log assertions |
  | 5 | `--workers 1` | `got 'w1 ', want 'w1 w2 w3 w4 '` |
  | 6 | `--idle-ms 1` | `FAIL 408 fired near the idle timeout` |
  | 7 | keep-alive fd not reused | `got: [True, 'connection not reused for style.css', 'BrokenPipeError on data.json']` |
  | 8 | `SIGKILL` instead of `SIGTERM` | `FAIL SIGTERM: wait status 137, want 143` |
  | 9 | `--bogus` expected to exit 0 | `FAIL --bogus: exit 1` |
  | 10 | 431 header shrunk to 8 KiB (under `MAX_HEAD`) | `got HTTP/1.1 200 OK, want 431`; also `no line with status 431` |
  | 11 | traversal target swapped for a file inside the root | `got HTTP/1.1 200 OK, want 403 Forbidden` |
  | 12 | the 408's head completed | `got HTTP/1.1 200 OK, want 408`; `0ms, want 400..3000ms` |
  | 13 | the 400's junk replaced with a valid request | `got HTTP/1.1 200 OK, want 400 Bad Request` |

  Break 6 is the one that earned its keep: `--idle-ms 1` reddened the 408 timing
  but **not** the keep-alive assertion, because the original read loop spun on
  `recv() == b""` instead of failing. That was fixed (`pump()` treats a closed
  peer as a failure) and break 7 then reddened with a message naming which of
  the three requests lost the connection. An assertion that cannot fail is not
  an assertion, and that one could not.

  *Flakiness.* Ten consecutive runs of the final file: **10 green, 0 red, 52 ok
  per run**, 41 s total, no stray process afterwards. (An earlier ten-run loop
  was also 10/10, but on the pre-`pump()` file, so it is the second loop that
  counts.)

  *Collateral, and it reddened a gate.* Inserting the `server-check` target
  pushed every later `Makefile` line down by 8, and four anchored citations
  pointed at the old numbers: `python3 scripts/check_citations.py` failed with
  four `STALE  ... Makefile:270@SKIPPED -> ... it appears at :278`. Repointed to
  `:278` in `scripts/asan_self.sh:11`, `scripts/asan_self.sh:72`,
  `scripts/editors_check.sh:29` and `scripts/check_citations.py:247` (the last
  is that gate's own worked example). Gates after the fix: citations `ok (0
  stale)`, links `ok (134 markdown files)`.

  While there: the bare `,:246` beside the first of those was **already wrong at
  `230653e`** — line 246 of `Makefile` was blank there, and a bare `:N` binds to
  the previously named path, so it resolved to nothing. The gate never caught it
  because an unanchored ref is unverifiable. It meant the `TYCHO_NO_ASAN=1`
  recipe line, now `Makefile:279`, and was corrected to `,:279` in the same
  edit rather than left beside a number that had just been proved wrong.

- [x] **Phase 2 — wire `server-check` into `make ci`**
  - Scope: `scripts/ci.sh` and the `Makefile` comment at `:226-229` that
    currently says the server is deliberately out of CI — that sentence becomes
    false with this phase and must be rewritten, not left.
  - Follow the shape of the lanes added on 2026-07-30: a `step "[Nx/13] ..."`
    label and `make -s <target>`, numbered as a sub-lane rather than renumbering
    the thirteen. `scripts/ci.sh:62-70` is the freshest example and explains its
    own numbering choice.
  - Note `CLAUDE.md`'s gate table lists what each `make ci` step maps to; a new
    step needs a row there too.
  - Done when: `make ci` runs the server lane and is green, with the observed
    exit code.
  - Verify: `make ci`, once, waited on in-turn, exit status **observed** not
    derived. This is the phase that earns the sweep — it adds a CI step.

  **Evidence (2026-07-30).** Four files: `scripts/ci.sh` (the step),
  `Makefile` (two comments), `CLAUDE.md` (two table rows), plus four citation
  repairs. `server/run.sh` and `server/main.ty` untouched.

  *Where the step went, and why.* `[3c/13]`, immediately after `[3b]
  entrypoints` (`scripts/ci.sh:93-94`), following the `[2d]` precedent at
  `scripts/ci.sh:62-70`: a sub-lane letter, not a fourteenth number, because the
  `/13` denominator counts numbered steps. Two reasons for that slot, both about
  which gate reports the cheapest true thing first. **Dependency:** `[3b]`
  compile-checks `server/main.ty` in milliseconds, so a server that does not
  build reddens there with a compile error rather than arriving here as `FAIL
  readiness: no startup banner on stderr within 10s` — a message that describes
  a symptom four steps downstream of its cause. **Cost:** ~4s, so it belongs
  ahead of the minute-scale fuzz/tools lanes; there is no argument for making a
  4-second lane wait behind a fuzz campaign. It is a sub-lane of `3b` rather
  than a sixth dogfood inside `[3]` because everything in step 3 diffs stdout
  against a recorded golden and this one talks HTTP to a live daemon — different
  kind of assertion, so a different lane.

  Also corrected in `scripts/ci.sh:85-88`: the `[3b]` comment listed `server/`
  among the runners "outside this file", which this phase makes false. The
  webserver/weblog/sqlite half of that sentence is still true and stays.

  *What the `Makefile` comment says now.* `Makefile:226-236`. The old text —
  "deliberately NOT in `make ci` and asserts nothing, because the thing it
  produces is a long-running network daemon, not a fixture with a golden" — was
  wrong in both halves and, worse, stated a false general principle. The
  replacement records the non-obvious part: **the daemon shape is what makes it
  gateable.** Binding `--port 0` and printing the bound port in a startup banner
  (`server/main.ty:610-614`) hands a runner readiness *and* the port on one
  line, so there is no `sleep` and no fixed port to collide on; a `trap` covers
  teardown. What is genuinely unassertable is wall-clock (the concurrency and
  `TCP_NODELAY` numbers), not behaviour. `Makefile:245-246` now records the
  membership and the step number beside the `server-check` target itself.

  *`CLAUDE.md`.* Two rows. The step→gate map gets `[3c] server-check` →
  `make server-check`, noting that a red there is a behaviour change in
  `server/main.ty` or `core:net` rather than a build break, since `[3b]` would
  have caught that first. The gate-budget table gets `make server-check` at ~4s,
  because that table is where "cheapest gate that can redden" is looked up and
  `server/` previously had no row at all — the rule listed `.ty` fixtures and
  corelib but nothing covering this program.

  *Citation fallout, as phase 1 predicted.* The `Makefile` comment rewrite added
  8 lines above the `ilp32` recipe and shifted `SKIPPED` from `:278` to `:286`;
  the citation gate failed with the same four `STALE` lines phase 1 saw, in the
  same four places. Repointed `278 -> 286` in `scripts/asan_self.sh:11`,
  `scripts/asan_self.sh:72`, `scripts/editors_check.sh:29` and
  `scripts/check_citations.py:247`, and the bare `,:279` beside the first (the
  `TYCHO_NO_ASAN=1` recipe line) to `,:287`, verified by reading
  `Makefile:286-287` rather than by arithmetic. Nothing cited a `scripts/ci.sh`
  line below the insert: the refs that exist are in `docs/internals/plan-*-DONE.md`,
  which `scripts/check_citations.py:316` exempts by name as frozen evidence — so
  the insert moved no live anchor. Gates after the repair: citations `ok (168
  anchored, 2523 bare in bounds, 138 source->doc, 191 source->source in bounds,
  12 source->source anchored)` — the source→source count is one higher than
  before, which is the new `server/main.ty:610-614` in the `Makefile` comment —
  and links `ok (134 markdown files)`. `sh -n` and `dash -n` both accept
  `scripts/ci.sh`; the step string carries no backtick, which is the syntax
  error `[12b]` shipped with once (`scripts/ci.sh:159-163`).

  *The sweep.* `make ci` run **once**, at the end, after the targeted gates were
  green — not as a feedback loop. Observed `CI_EXIT=0` and `CI GREEN -- tree is
  good`, with `>>> [3c/13] make server-check` in the log followed by 52 `ok`
  lines and `server: OK`. Wall clock was not instrumented at the start; bounded
  by the two in-turn polls at **still running at 580 s, finished by 1160 s**,
  consistent with the ~19 min the gate table quotes. The lane's own cost is the
  ~4s measured in phase 1, so `make ci` is not measurably longer for it.

  *No out-of-scope discovery to file.* The absolute-path citations in the
  archived plans looked like one until checked: `scripts/check_citations.py:316`
  exempts `docs/internals/plan-*-DONE.md` deliberately, and the file's own header
  (`scripts/check_citations.py:109-122`) explains why. Verified before filing,
  not after.

- [x] **Phase 3 — `server/README.md` describes the program as it is**
  - Scope: `server/README.md` only.
  - The two false limitations under "Deliberately not implemented" go, replaced
    by what the program actually does now, with provenance. Where the limitation
    was real and was fixed, say so with the date rather than deleting the history
    — that is the convention the rest of the tree follows.
  - "Verifying it" (`:140-158`) currently says "There is no `make` gate for
    this". After phase 2 that is false; it should point at the gate and keep the
    by-hand transcript as the manual path.
  - Check the rest of the file against the program while you are in it. Two
    stale claims were found by reading two sections; the other six sections have
    not been checked.
  - Done when: every claim in the README is true at HEAD, and the citations
    resolve.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.
    Markdown only — not `make test`, not `make ci`.

  **Evidence (2026-07-30).** One file changed: `server/README.md`.
  `server/main.ty`, `server/run.sh`, `scripts/ci.sh` and `Makefile` untouched.
  Gates: citations `ok (168 anchored, 2544 bare in bounds, 138 source->doc, 191
  source->source in bounds, 12 source->source anchored)` — the bare count is
  2523 -> 2544, i.e. the 21 new `path:line` refs this rewrite added and no stale
  one; links `ok (134 markdown files)`.

  *How the empty-directory answer was settled: by running it, not by reading it.*
  Started the HEAD binary on a `mktemp -d` copy of `server/www` with an added
  empty `emptydir/`, `--port 0 --workers 4 --idle-ms 2000`, read the bound port
  out of the banner, and drove raw sockets at it. Observed:
  `GET /emptydir` -> `HTTP/1.1 301 Moved Permanently`, `Location: /emptydir/`;
  `GET /emptydir/` -> `HTTP/1.1 404 Not Found`. So the README's claim was false
  in the way that matters — the redirect it said was withheld is **issued** —
  and the surviving `404` is a different and correct answer (no `index.html`
  behind the slash form), not a residue of the old bug. Source agrees after the
  fact: `server/main.ty:301` matches `io.is_dir`, `:307` returns the 301 for the
  no-slash case and `:306` the 404 for the slash form. Fix dated from the commit
  itself, not from prose: `git log -S` puts `io.is_dir` and its adoption in
  `resolve()` both in `4fa192d`, **2026-07-26**.

  *The second false claim, read rather than assumed.* `corelib/net/net_shim.c:204`
  is `netx_peer_addr` (`getpeername` + `inet_ntop`, `__thread` buffer),
  `corelib/net/net.ty:143` is `fn peer_addr(fd: int) -> Result(string, NetErr)`,
  and `server/main.ty:368` calls it once per connection. The same live run
  printed `w1 127.0.0.1 GET //etc/passwd 403 629 0.054ms` — worker, **peer**,
  method, target, status, bytes, duration. Dated `7b76fcd`, **2026-07-26**.

  *Per-section verdict — all eight checked.*

  | § | verdict |
  |---|---|
  | header block | **corrected.** Asset list omitted `img/dot.png` and the font licence. Everything else held under measurement: `logo.png` is 480x270 (IHDR), `favicon.ico` is a 32x32 ICO whose payload starts `\x89PNG`, `quicksand-regular.ttf` is 95440 B with sfnt tag `00010000`, `app.js:8` really does `fetch("/data.json")`, `about/index.html` is the subdirectory index. |
  | Usage | **checked, accurate.** Byte-for-byte against `usage()` (`server/main.ty:508-518`): every flag, every default. `--port 8080` / `--port=8080` both work via `cli.parse_spec` (`:536`); `--port 0` prints the bound port, observed in the live banner above. |
  | What it does | **corrected, one row.** `Logging` listed "worker, method, target, ..." and omitted the client address — the same stale fact as the second false limitation, in a second place. Now names the peer field, gives a real line, and records the ` write-failed` tail (`server/main.ty:342-352`). The other six rows checked and accurate: 405 + `Allow: GET, HEAD` (`:437-440`, `:211-212`); `bytes` end to end; `ctype_for` (`:158-169`) and `application/octet-stream` never `text/plain` (`corelib/httpd/httpd.ty:428`); index resolution (`:269-271`, `:307`); keep-alive with `MAX_REQS = 1024` (`:70`) and HTTP/1.0 defaulting closed (`corelib/httpd/httpd.ty:309-311`); `SO_RCVTIMEO` (`server/main.ty:488` -> `corelib/net/net_shim.c:307`); the status set and the silent-close-on-`Closed` rule (`server/main.ty:390-393`). |
  | Concurrency | **corrected, two things.** The code block still matches `server/main.ty:499-504` exactly and the affine-handle reasoning holds. But "`plan.md` phase 1 measured the alternatives" pointed at *this* plan's phase 1, which is `server/run.sh` — the measurements are Phase 1 of `docs/internals/plan-webserver-DONE.md`. Repointed, and the throughput table labelled a **recorded measurement of 2026-07-26** (`docs/internals/plan-webserver-DONE.md:814-816`) with an explicit note that `make server-check` asserts no wall-clock. |
  | Path traversal | **corrected, one real error.** Steps 1-6 match `resolve()` (`server/main.ty:254-309`) in content *and* order. But the vector list put `/%00` among things answered `403`, and it is **`400`** — control-byte rejection at `server/main.ty:265-267` fires before `safe_join` can refuse anything. Confirmed live: the six traversal vectors returned `403`, `/%00` returned `400`. Split the list in two and wrote down why the split exists. |
  | Deliberately not implemented | **rewritten.** The eight excluded features are accurate. The two "known rough edges" were both false; replaced with dated fixed-then history per the tree convention, plus a new bullet for the shutdown gap phase 1 found. |
  | Two fixes into `core:net` | **checked, accurate; provenance added.** `MSG_NOSIGNAL` at `corelib/net/net_shim.c:41-53` and `:151-152`, `TCP_NODELAY` at `:154-155`, both real, both unreachable from Tycho. The 100-disconnect claim is `FRICTION.md:432`; the 620x figures are `docs/internals/plan-webserver-DONE.md:855`. Both labelled recorded measurements. Retitled "Two **socket** fixes" — with `getpeername` now documented above as a third `core:net` addition this program forced, a bare "Two fixes this program forced into `core:net`" had become false by arithmetic. |
  | Verifying it | **rewritten, as briefed.** Opened with `make server-check` and the `[3c/13]` lane (`Makefile:247-248`, `scripts/ci.sh:111`), kept the old false sentence as dated history because the *principle* it stated was the worse error, and kept the curl transcript verbatim under a `By hand` subhead — it is still correct. Added one line the transcript needed: it backgrounds a server with no `trap`, which is exactly how phase 1 left a stray `tycho-httpd` bound. |

  *This write-up reddened the gate on its first pass, which is worth recording.*
  Three `STALE`, all in the evidence block above, none in `server/README.md`:
  `:488` and `:390-393` meant `server/main.ty` but followed a line naming
  `corelib/net/net_shim.c`, so they bound to that and went out of bounds; and
  `:814-816` after a `docs/` path was refused outright, because the gate does not
  let a `docs/` path carry past the line that names it — a number landing inside
  a prose document is checked by nothing. All three spelled out in full and the
  gate is green. `CLAUDE.md` says four separate phases have reddened on their own
  write-ups this way; this is the fifth, in the phase whose entire subject was
  unverified claims.

  *One claim deliberately not made.* The README never asserted a clean-shutdown
  behaviour, so there was nothing to retract there; the new bullet states the
  absence instead, which is a claim the tree can check against
  `server/main.ty:617`.

- [x] **Phase 4 — with a gate in place, what is worth building next**
  - Scope: a written recommendation appended to this plan. **No implementation.**
  - The README lists eight deliberately-excluded features (TLS, HTTP/2, byte
    ranges, compression, conditional requests, virtual hosts, directory
    listings, pipelining). With a gate now catching regressions, re-ask which — if
    any — is worth having, judged by what it would teach about the *language*
    rather than by HTTP completeness. This program exists to put pressure on
    Tycho, not to be a web server.
  - Weigh that against `FRICTION.md`'s items 3 and 4, which this program is what
    found: `parallel for` silently capping at `min(N, ncpu)` — measured live at
    222/433/853 ms for 4/2/1 threads — with an undocumented 64-chunk cap at
    `src/tychoc.c:10040` contradicting `docs/spec/13-concurrency.md:78-82`, and
    no spelling for N workers because task handles are affine and unstorable.
    **Do not attempt those here.** They want a type-system answer and belong in
    their own plan; the question for this phase is only whether the server is the
    right program to force them.
  - Done when: a recommendation is written with its reasoning, naming what it
    would cost and what it would prove.
  - Verify: the two doc gates.

  **The recommendation (2026-07-30).** Nothing was implemented. Every claim below
  was checked by opening the file named, not by recall; where a claim is inherited
  from `FRICTION.md` rather than re-derived here it says so.

  *One naming note before anything else.* The brief called the concurrency pair
  `FRICTION.md` items 3 and 4; at HEAD the re-scored list numbers them **7 and 8**
  (`FRICTION.md:299` and `FRICTION.md:320`). The brief also dated the re-score
  `945acfa` and the file says `afa67da` (`FRICTION.md:211`) — both are right and
  they name different things: `945acfa` is the commit that performed the re-score,
  `afa67da` is the tree it was scored against. Item numbers below are the file's.

  ### 1. The eight excluded features, ranked by language pressure

  **Build — 1st: conditional requests, the `If-Modified-Since` half only.**
  This is the only one of the eight that is blocked on something the tree does not
  have, and it is blocked in exactly the shape this program has produced twice
  before. `corelib/io/io_shim.c:149` is `iox_stat_kind`: it calls `stat(2)` at
  `corelib/io/io_shim.c:152` and then **discards the entire `struct stat` except
  `S_ISDIR`** at `corelib/io/io_shim.c:153`. Grepped `mtime` across `corelib/`,
  `src/tychoc.c`, `runtime/` and `docs/spec/`: **no hits anywhere.** A file's
  modification time is therefore unreachable from *any* Tycho program, not merely
  from this one — the same sentence that was true of `getpeername` before
  2026-07-26 and of `is_dir` before `4fa192d`. Everything downstream already
  exists: `corelib/datetime/datetime.ty:77` (`from_unix`),
  `corelib/datetime/datetime.ty:123` (`pad2`), `corelib/datetime/datetime.ty:140`
  (`weekday_name`), `corelib/datetime/datetime.ty:146` (`month_name`) and
  `corelib/datetime/datetime.ty:320` (`parse_clf`) supply every part of an RFC 1123
  date except the seconds to feed them.
  The friction is a *design* question with a real caller asking it: what does an
  `io.stat` return? A `Result(Stat, IoErr)` carrying size, mtime and kind makes
  `corelib/io/io.ty:133`'s `is_dir` a one-line wrapper and retires the status-code
  channel `iox_stat_kind` invented — a multi-field syscall result is a shape
  `core:io` has never had to express.
  **Cost:** ~10 lines of shim, ~15 in `corelib/io/io.ty`, an
  `http_date`/`parse_http_date` pair in `core:datetime` ~30, ~20 in
  `server/main.ty`, ~6 assertions in `server/run.sh`. Half a day.
  **Proves:** whether Tycho's `Result` + struct return carries a multi-field
  syscall cleanly, and closes a corelib gap every Tycho program has.
  **Do not build the `ETag` half.** `corelib/sha256/sha256.ty:150` is `hex(msg)`,
  so an ETag is one call over a body already in memory. It teaches nothing and it
  is strictly worse than mtime — it reads the whole file to answer "unchanged?".

  **Build — 2nd: byte ranges, single range only.**
  `corelib/io/io.ty:107`'s `read_bytes` reads the **whole** file:
  `corelib/io/io_shim.c:96` is `iox_read_file`, an `fopen` plus the `fread` loop at
  `corelib/io/io_shim.c:108`. Grepped `pread(`/`lseek(`/`fseek(` across `corelib/`:
  the only hit is a comment. So `Range: bytes=0-1023` against a 1 GB file means
  allocating 1 GB. `bytes` slicing landed in `b823bc8`, which is the interesting
  part: the feature is **expressible today and wrong**, which is a sharper lesson
  than one that will not compile. Behind it sits the bigger gap it makes visible —
  `corelib/httpd/httpd.ty:466`'s `write_response` takes a `Response` whose body is
  wholly in memory, so there is no way to send a file the process cannot hold.
  **Cost:** shim + `io.read_range` ~20, `Range` parsing ~30, 206 +
  `Content-Range` ~20, ~8 assertions. Half a day.
  **Proves:** whether "part of a file" is expressible without inventing a handle
  type, and it is the cheapest program that makes the missing streaming-response
  API concrete rather than theoretical.
  **Stop at one range.** `multipart/byteranges` is where this stops teaching and
  starts being HTTP; say so in `server/README.md` rather than building it.

  **Build — 3rd, and only if someone wants the answer: pipelining.**
  The least useful HTTP feature of the eight and the sharpest *language* question,
  which is the trade this program exists to make. It is also **not
  "unimplemented" — it is silently lossy**, and that was found by reading
  `corelib/httpd/httpd.ty:242-296` for this phase rather than assumed:
  `read_request_capped` is a free function over an fd with no state between calls.
  Before the head terminator is found it reads in 4096-byte gulps
  (`corelib/httpd/httpd.ty:266`), so one `recv` can pull the opening bytes of a
  *second* pipelined request into `buf`; `need` is then computed at
  `corelib/httpd/httpd.ty:262` and the loop exits at `corelib/httpd/httpd.ty:263`
  on `len(buf) >= need` — **with the bytes past `need` still in `buf`**. They come
  back inside the second tuple element and the caller drops them:
  `server/main.ty:375` binds `raw` and uses it only to name the request in a log
  line. Nothing corrupts, because the server closes on a bodied request
  (`server/main.ty:424-425`), but the boundary is not tracked.
  Fixing it needs a buffered reader that survives the call, and that is the
  question worth asking: Tycho has structs and `inout` — `corelib/net/net.ty:102`
  already passes `status: inout int` through an extern — so a `Reader` struct
  threaded `inout` is expressible; but the tree's *existing* answer to precisely
  this problem is `corelib/io/io.ty:202-211`, `open_lines`/`read_line`/
  `close_lines`, which escapes to an opaque C `ptr`. **Can a stateful reader be
  written in Tycho, or does the corelib reach for `ptr` every time it needs one?**
  **Cost:** a day, nearly all design. **Risk, stated plainly:** the honest outcome
  may be "the `ptr` handle was right", in which case the day buys a second data
  point for a conclusion the tree already reached once. That is worth less than the
  first two and more than the five below.

  **Never — compression.** The clearest no on the list.
  `corelib/compress/compress.ty:1-4` is gzip RFC 1952 over zlib in the `bytes`
  domain and `corelib/compress/compress.ty:17` is the call. `Content-Encoding:
  gzip` is an `Accept-Encoding` substring test and one line. The hard part is
  finished, in C, and was written for someone else. Zero pressure.

  **Never — virtual hosts.** Maps are a first-class type
  (`docs/spec/03-types.md:293`, `docs/spec/12-aggregates.md:457`). A host→root map
  is a subscript. It exercises nothing that is not already exercised.

  **Never — directory listings.** `corelib/io/io.ty:235` is `list`,
  `corelib/sort/sort.ty:42` is a stable `asc` over `comparable`, string building is
  routine. The only thing it adds is an HTML-escaping surface, inside a program
  whose gate exists to assert that **no byte of a refused path leaks**
  (`server/README.md:110-114`). Negative value, not merely zero.

  **Never here — TLS**, and the reason is worth writing down because it is not
  "too hard". `core:tls` is **client-only**: `connect`, `write`, `read`,
  `close_conn`, `ok` at `corelib/tls/tls.ty:24-36`, with no accept side and no
  cert/key loading. Server TLS is a new OpenSSL shim *plus* making every I/O site
  polymorphic over an `int` fd and a `ptr` conn — and the pressure that would apply
  is "does Tycho have interfaces?", which is answerable **by reading**:
  `docs/spec/03-types.md:316-320` gives nominal sum types, so
  `enum Conn { Plain(int), Tls(ptr) }` and a `match` at two sites is the whole
  design, and it costs a match rather than a language feature. Writing it would
  confirm a conclusion already in hand. (`corelib/tls/tls_shim.c` is also one of
  the two files that fails `-std=c11`, `FRICTION.md:232-247` item 1 — fix that
  first regardless.) If server TLS is genuinely wanted, the demand-gated thing is a
  TLS *server* in `corelib/tls` driven by a program that needs one; not this one,
  which `server/README.md:118` tells you to terminate in front of.

  **Never — HTTP/2, emphatically.** HPACK with static and dynamic tables, Huffman
  coding, flow control, stream multiplexing. The multiplexing half *would* press
  hard on the concurrency gap — but it presses through several thousand lines of
  framing that teach nothing, and it needs readiness multiplexing `core:net` does
  not expose: the only `poll(` in `corelib/net/net_shim.c` is at
  `corelib/net/net_shim.c:44` and it is **inside a comment**. You would build a
  `poll` exposure, a stream table and HPACK in order to arrive at a question a
  hundred-line work queue asks directly. Worst ratio on the page.

  ### 2. The concurrency pair — the server is the wrong vehicle

  **Item 7 (`parallel for` caps at `min(N, ncpu)`): the server cannot exercise it
  at all.** Verified, not assumed — `grep parallel server/main.ty` is **empty**.
  The pool is one `spawn` at `server/main.ty:501` and one `wait` at
  `server/main.ty:503`. No server feature above would introduce a `parallel for`
  either. And the item's actionable half wants no program: the undocumented
  64-chunk ceiling at `src/tychoc.c:10040` contradicts
  `docs/spec/13-concurrency.md:78-82`, and `FRICTION.md:315-317` already says that
  half "is a ~1-line spec fix and should be split out and taken". **Take the spec
  fix. It is the cheapest true thing named anywhere in this phase.** The warning
  half stays open and should: `N` is a runtime expression
  (`docs/spec/13-concurrency.md:86`), so there is nothing static to warn from.

  **Item 8 (no spelling for N workers): the server is a *weak* vehicle, and that
  is the finding.** Its N tasks are identical, live until process exit, never
  complete out of order, and are waited exactly once in order by the recursion at
  `server/main.ty:499-504`. A program whose handles form a **chain** never needs a
  **container** of handles — it needs a chain, and it has one, in six lines that
  work. Adding HTTP features to make it press harder would be building the wrong
  shape harder.
  The shape that forces item 8 is a **bounded worker pool draining a channel**: M
  jobs, N < M workers, completions out of order, results collected. There you hold
  N handles simultaneously, the count is data-dependent, and the recursive dodge
  stops being six lines. `send`/`recv`/`close` exist as builtins already
  (`FRICTION.md:264-276` item 4 pins them at `src/tychoc.c:5609`,
  `src/tychoc.c:5618`, `src/tychoc.c:5624`).
  **The concrete program, if one is written: a parallel link checker over the
  tree's own Markdown.** `scripts/check_links.sh` is a sequential shell script
  today; `core:net`, `core:http` and `core:tls` supply the client; the job count is
  data-dependent and far larger than `ncpu`. It clears `ROADMAP.md:23`'s demand
  gate — a real program that wants the feature — and it turns item 7's chunk cap
  from a microbenchmark into a **user-visible stall**, because one hung host blocks
  every job chunked behind it. A parallel test/CI driver is the same shape with a
  larger payoff (`make ci` is ~19 min, `CLAUDE.md:18`) and a larger blast radius;
  the link checker is the safer first one.
  **Caveat, stated rather than buried:** neither program *fixes* item 8. An array
  of handles is a type-system change — `FRICTION.md:320-327` reproduces the refusal
  at `src/tychoc.c:639` (`task_container_err`) — and a program can only demonstrate
  the need, which `server/main.ty:497-498` already does in prose. Write it if you
  want a second shape's design pressure before touching the affine rule; do not
  write it expecting the change to fall out.

  ### 3. Signal handling (Phase 5): its own bucket, and here the server *is* right

  It is not a concurrency item, and the difference is not cosmetic. Verified for
  this phase: grepping `sigaction|SIGTERM|SIGINT|signal(` across `corelib/`,
  `runtime/` and `src/` matches **one file**, `runtime/tycho_rt.c`, and both hits
  are false — a comment about `SIGFPE` at `runtime/tycho_rt.c:103` and
  `pthread_cond_signal` at `runtime/tycho_rt.c:707`. **No signal handler is
  installed anywhere in the tree.** That is a runtime gap, not a type-system one.
  The server is the right vehicle because the wind-down already exists and only its
  trigger is missing: `accept_loop` sets `running = false` on `Err` from
  `net.accept` at `server/main.ty:494`, and `worker()` returns the summed count at
  `server/main.ty:503` into the line at `server/main.ty:617`. So the entire feature
  is "make the blocked `accept` return", and the design question is small and
  sharp. A Tycho-level handler is a function callable on any thread at any
  instruction, which the language has no way to type — so the shape to build is
  almost certainly **not** that. It is the self-pipe (the runtime's handler writes
  a byte; Tycho reads an fd) or, for this program alone, the runtime calling
  `shutdown(2)` on the listener. Both `close()` and `shutdown()` are on POSIX's
  async-signal-safe list; **whether a thread blocked in `accept(2)` actually
  returns is platform-dependent and must be measured, not reasoned about** — and
  that measurement is most of the value.
  **Cost:** ~half a day for the narrow version. `server/run.sh` has already had the
  assertion written and observed failing, per Phase 1's evidence above.
  **Proves:** whether the runtime can hand an async event to Tycho code without
  inventing a new function colour. Cheaper and far less speculative than item 8,
  and the only item here where the gate already knows what right looks like.

  ### 4. "Do nothing more here" — it very nearly wins

  Stated honestly, because it is the option a list-driven answer would skip. The
  server has done its job: it produced `MSG_NOSIGNAL` and `TCP_NODELAY`
  (`server/README.md:170-192`), `getpeername`, `io.is_dir`, both concurrency items,
  and now a 52-assertion gate inside `make ci` at `[3c/13]`. `ROADMAP.md:23` says
  library work is "built against a real program that needs it, never ahead of one"
  — and **nobody needs this server.** Every feature above would be built to make it
  press on the language, which is that policy read backwards.
  It loses on one narrow point: **two of the three recommended items are not server
  features.** A file's mtime and an offset read are `core:io` gaps that every Tycho
  program has; this server is simply the cheapest program in the tree that notices
  them. That is the demand gate working, not being dodged. If those two land in
  `core:io` for their own sake, the server's use of them is ~40 lines and a handful
  of assertions on a runner that already exists.

  ### The answer, in order

  1. **The 1-line spec fix** for the 64-chunk cap (`src/tychoc.c:10040` vs
     `docs/spec/13-concurrency.md:78-82`). Minutes. Not a server change.
  2. **Signals** (Phase 5), narrow version. Half a day. The server is the vehicle.
  3. **`io` mtime** → conditional requests. Half a day. Corelib gap first.
  4. **`io` offset read** → single-range 206. Half a day. Corelib gap first.
  5. **Pipelining**, only if the stateful-reader question is wanted. A day.
  6. **A worker-pool program** (link checker) if item 8 is to be pushed — *not* a
     server feature. Uncosted; it demonstrates, it does not fix.

  **Build nothing else in `server/` ever.** TLS, HTTP/2, compression, virtual hosts
  and directory listings are struck permanently, with the reason recorded above so
  the question is not re-asked. And if none of 1–6 is picked up, **declaring the
  server finished is a defensible outcome** and a better one than building
  compression to make a list shorter.

  *Gates.* Markdown only, as briefed: `python3 scripts/check_citations.py` and
  `sh scripts/check_links.sh`. Neither `make test` nor `make ci` was run; this
  phase changed no code. Results recorded under "Status" below.

- [ ] **Phase 5 — the shutdown line at `server/main.ty:617` is unreachable**
  - Found by phase 1, outside its scope (which forbade touching `server/main.ty`),
    so it is filed rather than fixed.
  - `server/main.ty:616-617` runs the worker pool and then prints
    `tycho-httpd: stopped after N requests`. **That line never executes.**
    Nothing in `server/main.ty` installs a `SIGTERM` or `SIGINT` handler —
    grepped for `signal|sigaction|SIGTERM|SIGINT|SIGPIPE` across
    `server/main.ty`, `corelib/net/net.ty` and `corelib/net/net_shim.c`, and the
    only signal anywhere in reach is `SIGPIPE`, suppressed per-send with
    `MSG_NOSIGNAL` (`corelib/net/net_shim.c:41-53`). The default disposition
    therefore terminates the process where it stands, the accept loops never
    wind down, and the count is never printed. Observed: `server/run.sh` asserted
    the line and failed; `kill -TERM` gives wait status 143 and a log whose last
    entry is a request line.
  - The accept loop *does* have a wind-down path — `accept_loop` sets
    `running = false` on `Err(e)` from `net.accept` (`server/main.ty:494`) —
    so the machinery to return a count exists and only the signal that would
    trigger it is missing.
  - Two honest resolutions, and the choice is a language question, not a server
    one: expose signal handling from `core:net` (or a new corelib module) so the
    program can close the listener and let `worker()` return, which is the
    feature the tree does not have; or delete the line and say in a comment that
    a static server has no graceful shutdown. **Do not** assert its absence in
    `server/run.sh` — that would redden the day it is fixed.
  - Verify: whichever way it goes, `make server-check` (~4s) plus `make test` if
    corelib changes. Not `make ci`.

- [ ] **Phase 6 — 110 references to "`plan.md` phase N" point at the wrong plan**
  - Found by phase 3. Two of them were in `server/README.md` and were fixed there
    because they were in scope; the other 108 are not, so they are filed.
  - **The defect.** `plan.md` is a *rotating* file: when a plan closes it is
    archived to `docs/internals/plan-<name>-DONE.md` and a new plan takes the
    name. Every comment that says "plan.md phase 4" was true when written and
    silently retargets the day the plan rotates. It is now pointing at whatever
    phase 4 of the *current* plan happens to be about.
  - **Measured at HEAD**, excluding `docs/internals/plan-*-DONE.md` (which
    `scripts/check_citations.py:316` exempts as frozen evidence, and where the
    refs are internally consistent): **110 references across 42 files.** The
    citation gate cannot see any of them — it checks `path:line`, and
    "plan.md phase 4" carries no line number, so this is an unverifiable
    reference by construction, the same class as the bare `:246` phase 1 found.
  - **It is already wrong, not merely fragile.** The live `plan.md` has 5 phases.
    The cited phase numbers run to **63**. The refs belong to at least four
    different archived plans — `plan-webserver-DONE.md`, `plan-option-result-DONE.md`,
    `plan-friction-DONE.md`, `plan-loops-cleanup-DONE.md` — all spelled `plan.md`.
    Worst concentrations: `server/main.ty` **13**, `corelib/test/io/main.ty` 7,
    `corelib/io/io.ty` 6, `corelib/httpd/httpd.ty` 6, `corelib/result/result.ty` 4,
    `FRICTION.md` 4, `Makefile` 3.
  - Phase 3 resolved the two it owned by reading the target: "phase 1 measured
    the concurrency alternatives" is Phase 1 of `docs/internals/plan-webserver-DONE.md`,
    and "Phase 7" is that same file's Phase 7 at `docs/internals/plan-webserver-DONE.md:748`
    and `docs/internals/plan-webserver-DONE.md:814-816`. The other 108 each need the same treatment — `git log -S` on
    the thing being described, then the archived plan it lands in. **This is not
    mechanical**; a blind rewrite would invent provenance, which is worse than a
    stale pointer because it reads as verified.
  - Worth deciding as part of it: whether `scripts/check_citations.py` should
    reject the bare "plan.md phase N" spelling outright and require the archived
    filename, so this cannot re-accumulate. That is the only thing that stops the
    next rotation from re-breaking whatever this phase fixes.
  - **Note:** `server/README.md`'s one *surviving* `plan.md` reference — "Filed
    as phase 5 of `plan.md`", the shutdown gap — is correct at HEAD and points at
    this plan deliberately. It will need rewriting to the archived name when this
    plan closes, which is the same defect seen from the other end.
  - Scope: comments and prose only; no behaviour. Verify: the two doc gates,
    plus `make test` only if a `.ty` comment edit is somehow not a comment edit.

## Out of scope

- **The concurrency pair (`FRICTION.md` items 3 and 4).** Named above as context
  for phase 4's recommendation; not to be implemented in this plan.
- **The eight excluded HTTP features.** Phase 4 decides whether any is worth a
  future plan; none is built here.
- **`compiler/tychoc0.ty`.** Frozen, its lanes retired 2026-07-29, unaffected.

## Status — PLAN COMPLETE

Four phases, four commits, one per phase, in order:

| phase | commit | what shipped |
|---|---|---|
| 1 | `937758e` | `server/run.sh` — 52 assertions over a live daemon; `server-check` at `Makefile:245-248`; 13 deliberate-break classes each observed red; 10/10 flake-free |
| 2 | `af5474d` | the lane in `make ci` as `[3c/13]` (`scripts/ci.sh:111`); two `Makefile` comments rewritten; two `CLAUDE.md` gate-table rows; `CI_EXIT=0` observed once |
| 3 | `8e01e4a` | `server/README.md` true at HEAD — two false limitations retired with dates, one real error found (`/%00` is 400, not 403), all eight sections checked |
| 4 | *this commit* | the recommendation above. No code, no script, no other document touched. |

Phase 4's gates, Markdown only: citations `ok (168 anchored, 2609 bare in bounds,
138 source->doc, 191 source->source in bounds, 12 source->source anchored)` — the
bare count is 2544 -> 2609, i.e. the 65 new `path:line` refs this write-up added
and no stale one; links `ok (134 markdown files)`. `git status --short` before the
commit was one line, ` M plan.md`.

**What the plan set out to do, and did.** `server/main.ty` was the largest program
in the tree with nothing verifying it. It now has a gate that starts the real
binary, talks HTTP to it over raw sockets, asserts 52 things, and tears it down on
every exit path — inside `make ci`, costing ~4s. Its README no longer documents
two limitations the program does not have. The pattern the tree lacked (a daemon
under test: `--port 0`, banner-as-readiness, `trap` teardown, no `sleep`) is
established and written down.

**What remains open, both filed by this plan's own phases:**

- **Phase 5** — the unreachable shutdown line at `server/main.ty:617`. Found by
  phase 1, out of its scope. Phase 4 recommends taking it, narrow version, and
  places it *second* on the list: it is a runtime gap (no signal handler exists
  anywhere in the tree, re-verified in phase 4), it is cheap, and the wind-down
  machinery it needs already exists at `server/main.ty:494`.
- **Phase 6** — 110 "`plan.md` phase N" references across 42 files pointing at a
  rotated plan. Found by phase 3. Not mechanical: each needs `git log -S` against
  the archived plan it belongs to. Note the recursion — **this plan's own
  `server/README.md:168` "Filed as phase 5 of `plan.md`" becomes an instance of the
  defect the moment this file is archived.**
- **Phase 7** (below) — a latent `core:httpd` defect found by phase 4 while
  costing pipelining.

Phase 4 additionally recommends, outside this plan: the ~1-line spec fix for the
undocumented 64-chunk cap (`src/tychoc.c:10040` vs
`docs/spec/13-concurrency.md:78-82`), an `io` mtime exposure, an `io` offset read,
and — if `FRICTION.md` item 8 is to be pushed — a worker-pool program that is
**not** this server.

## Phases filed after the plan closed

- [x] **Phase 7 — `read_request_capped` discards bytes it has already taken off
  the socket**
  - Found by phase 4 while costing pipelining; a correctness finding, not a
    recommendation, so it is filed rather than absorbed.
  - **The defect.** `corelib/httpd/httpd.ty:242-296`. Until the head terminator is
    found the loop reads in 4096-byte gulps (`corelib/httpd/httpd.ty:266`), so a
    single `recv` can pull in the opening bytes of a *following* request. `need` is
    then set at `corelib/httpd/httpd.ty:262` and the loop exits at
    `corelib/httpd/httpd.ty:263` on `len(buf) >= need` — leaving the surplus bytes
    in `buf`, returned as the second tuple element and dropped by the only caller
    (`server/main.ty:375` uses `raw` solely to name a request in the log).
  - **Consequence.** A client that pipelines two GETs on one connection gets the
    first answered and the second silently swallowed: its bytes are consumed from
    the socket and discarded, and the server then blocks until the idle timeout.
    Nothing corrupts — a bodied request already forces close at
    `server/main.ty:424-425` — but the function's contract is stated nowhere and is
    not what a reader would assume.
  - **Not yet reproduced.** This is read from source, not observed on the wire.
    **The phase's first action is to prove it**, with a raw socket sending two
    pipelined GETs in one `send`, before anything is changed. If it does not
    reproduce, say why and close the phase.
  - **Two honest resolutions**, and the cheap one is probably right: document the
    contract on `read_request_capped` — "reads at most one request; surplus bytes
    already taken from the socket are returned in the second element and are the
    caller's problem" — and have `server/main.ty` close the connection when surplus
    is present, which is correct behaviour rather than a swallow. The expensive one
    is the stateful buffered reader phase 4 costs at a day; do not start there.
  - Scope: `corelib/httpd/httpd.ty` and, if the close is added, `server/main.ty`
    plus an assertion in `server/run.sh`. Verify: `make server-check` (~4s), plus
    `make test` if `corelib/httpd/httpd.ty` behaviour changes. Not `make ci`.

  **Evidence (2026-07-30).** Comment-only change to `corelib/httpd/httpd.ty`. No
  behaviour changed, so `server/main.ty` and `server/run.sh` are untouched and
  `server-check` stays at 52 assertions.

  *Outcome 1: it reproduces exactly as the entry describes.* Server built from
  `server/main.ty`, started `--port 0 --workers 1 --idle-ms 1000`, banner polled
  for readiness as `server/run.sh` does. One `sendall` of 73 bytes:

      b'GET /style.css HTTP/1.1\r\nHost: t\r\n\r\nGET /index.html HTTP/1.1\r\nHost: t\r\n\r\n'

      ready on port 41407
      elapsed until peer close/eof: 1.013s   peer closed=True   bytes=1917
      responses seen: 1
      HTTP/1.1 200 OK
      Content-Type: text/css; charset=utf-8
      Cache-Control: no-cache
      Connection: keep-alive
      Content-Length: 1726
      ... (the 1726 bytes of style.css)

  The second GET is answered by nothing. The connection does not hang without
  bound and it does not close early: it idles for **1.005–1.013s** across two
  runs, i.e. exactly the `--idle-ms 1000` keep-alive deadline, and is then closed
  quietly — the `Err(Timeout)` arm at `server/main.ty:399-403` with `len(raw) == 0`,
  so no 408. The access log holds **one** line and it is correct:

      w1 127.0.0.1 GET /style.css 200 1726 0.137ms

  So: no corruption, no hang, no mis-attributed bytes. The consequence in the
  entry is right as written.

  *Where the surplus goes — measured, not read.* `corelib/httpd/httpd.ty:151-156`
  splits at the FIRST terminator, so the surplus is handed to the parsed request's
  `body`. A throwaway program calling `httpd.parse_request` on the same two-GET
  buffer printed:

      path        = /style.css
      body len    = 37
      body        = GET /index.html HTTP/1.1\r\nHost: t\r\n\r\n
      content-len = []

  37 bytes is the second request verbatim, and no `Content-Length` accompanies it.
  That absence is the only way a caller can tell surplus from a real body, and it
  was not written down anywhere before this phase.

  *Resolution: document the contract, no behaviour change.* The observed behaviour
  is not worse than the entry describes, which is the condition the brief set for
  reaching past the cheap fix. Adding the close in `server/main.ty` would change
  the `Connection:` header of a served response and need a 53rd assertion in
  `server/run.sh` — both outside this phase's named scope — to save a client one
  idle period while doing something `server/README.md:116-121` lists as
  deliberately not implemented. It would not make pipelining work. So the defect
  fixed here is the stated one: the contract was nowhere. A `SURPLUS BYTES` block
  now sits on `read_request_capped` in `corelib/httpd/httpd.ty` naming both places
  the bytes land (the 2nd tuple element and `Request.body`), the `Content-Length`
  tell, the fact that they never come back, and the wire behaviour with its
  measured 1.005s. The note on the `read_request` wrapper in the same file was
  tightened to say it DROPS them and to point at that block.

  *Gates.* `make -s server-check` → `MAKE_SERVER_CHECK_EXIT=0`, `server: OK`,
  `grep -c "^  ok "` = **52** (unchanged). `make test` → `passed: 560 failed: 0`,
  `all green`. `make -s corelib` → `corelib: all green (tychoc matches goldens)`.
  `python3 scripts/check_citations.py` → green. `make ci` not run: this phase adds
  no CI step and changes no compiled behaviour.
