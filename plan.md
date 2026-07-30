# The web server: a real gate, and a README that tells the truth

Previous plan complete and archived at
[docs/internals/plan-loops-cleanup-DONE.md](docs/internals/plan-loops-cleanup-DONE.md)
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

- [ ] **Phase 2 — wire `server-check` into `make ci`**
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

- [ ] **Phase 3 — `server/README.md` describes the program as it is**
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

- [ ] **Phase 4 — with a gate in place, what is worth building next**
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

## Out of scope

- **The concurrency pair (`FRICTION.md` items 3 and 4).** Named above as context
  for phase 4's recommendation; not to be implemented in this plan.
- **The eight excluded HTTP features.** Phase 4 decides whether any is worth a
  future plan; none is built here.
- **`compiler/tychoc0.ty`.** Frozen, its lanes retired 2026-07-29, unaffected.
