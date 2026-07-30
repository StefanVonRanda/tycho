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

- [ ] **Phase 1 — `server/run.sh`: start it, talk to it, tear it down**
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

## Out of scope

- **The concurrency pair (`FRICTION.md` items 3 and 4).** Named above as context
  for phase 4's recommendation; not to be implemented in this plan.
- **The eight excluded HTTP features.** Phase 4 decides whether any is worth a
  future plan; none is built here.
- **`compiler/tychoc0.ty`.** Frozen, its lanes retired 2026-07-29, unaffected.
