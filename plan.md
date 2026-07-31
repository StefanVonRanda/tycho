# Signal handling, driven by the server's unreachable shutdown

Previous plan complete and archived at
[docs/internals/plan-prunner-DONE.md](docs/internals/plan-prunner-DONE.md)
(four phases plus Phase 13; `make test-fast` ships as an advisory lane). Its
seven still-open phases are carried forward at the bottom of this file.

## Goal

`server/main.ty:617` prints `"tycho-httpd: stopped after N requests"` and is
**unreachable**. Nothing installs a SIGTERM or SIGINT handler, because Tycho has
no signal surface at all. `make server-check` currently asserts the server dies
with wait status 143 — that is, it asserts the *absence* of clean shutdown.

Done looks like: `SIGTERM` makes every worker stop accepting, the process prints
its served count and exits 0, and `server/run.sh` asserts that line rather than
asserting the kill.

## Pre-flight

- **Worst case:** a shutdown path that hangs. The server has N workers blocked in
  `accept()`; a design that wakes one and leaves the rest blocked turns
  `SIGTERM` into "hangs until something kills it harder", which is worse than
  today's clean `143`. **Phase 3 must prove all workers exit, not just that the
  line prints** — one worker printing while three hang would satisfy a naive
  check.
- **Reversibility:** full. New shim + corelib surface; the server change is
  additive and `server/run.sh` is the gate that proves it.
- **Verified — there is no signal support.** `grep -rln "signal(\|sigaction\|SIGTERM\|SIGINT" corelib/ runtime/ src/`
  matches only `runtime/tycho_rt.c`, and both hits are false positives: a
  `SIGFPE` comment at `runtime/tycho_rt.c:103` and `pthread_cond_signal` at
  `runtime/tycho_rt.c:707`.
- **Verified — the wind-down is already `Err`-driven and already written.**
  `server/main.ty:493-494` sets `running = false` in the `Err` arm of accept,
  commented "listener closed: wind down". The served count returns at
  `server/main.ty:617`. **No new control flow is needed in the server** — the
  path exists and nothing reaches it.
- **Verified — EINTR already propagates.** `corelib/net/net_shim.c:162-167` is a
  bare `accept()` with **no EINTR retry loop**: on interrupt it returns -1, which
  becomes `Err` in Tycho, which is exactly the arm above. So a handler installed
  *without* `SA_RESTART` needs no change to `netx_accept` to wake the thread that
  receives the signal.
- **Verified — and this is the whole difficulty.** `runtime/tycho_rt.c:577` calls
  `pthread_create` with a NULL attribute and there is **no `pthread_sigmask`
  anywhere in the tree**, so a process-directed signal is delivered to one
  arbitrary thread. EINTR therefore wakes exactly one worker. The other N-1 stay
  blocked in `accept()` and the process does not exit.
- **Verified — there is precedent for bounding a blocking socket call.**
  `corelib/net/net_shim.c:19` includes `<sys/time.h>` "for `SO_RCVTIMEO`" and
  `:63` includes `<errno.h>` to tell `EAGAIN`/`EWOULDBLOCK` from a real error;
  the per-connection tuning helper is at `:124`. That machinery exists for
  connected sockets and the server already exposes `--idle-ms` on top of it.
- **Assuming — one of four mechanisms will work, and I have not tested any of
  them on this platform.** They are not equal and the choice is phase 1's whole
  job:
  1. **`SO_RCVTIMEO` on the listener + a polled flag.** Strongest in-tree
     precedent, portable in spirit. On Linux `SO_RCVTIMEO` does apply to
     `accept()`, but that is a platform behaviour, not POSIX.
  2. **`shutdown(listen_fd, SHUT_RDWR)` from the handler.** On Linux this wakes
     blocked `accept()` calls; it is not portable and `shutdown` on a listening
     socket is not specified to do this.
  3. **`close(listen_fd)` from the handler.** `close` is async-signal-safe, but
     closing an fd another thread is blocked in `accept()` on is **not
     well-defined** — it can hang or, worse, the fd number can be reused.
  4. **Self-pipe or `eventfd` + `poll` before `accept`.** Correct and portable,
     and the largest change: it puts a readiness step in front of every accept.
  **Risk if wrong:** the hang described in Worst case. Phase 1 settles this by
  *experiment on this machine*, not by reasoning.
- **Assuming — the feature belongs in a new `corelib/signal/` package.**
  `corelib/os/` and `corelib/net/` are the conventions to follow (a `.ty` surface
  over a `_shim.c`). The alternative is the runtime, which would make signals a
  language builtin rather than a library. **Phase 2 decides and states why**; the
  deciding question is whether a Tycho program should be able to install a
  handler for an arbitrary signal, or only ask "was shutdown requested?".

## Phases

- [ ] **Phase 1 — settle the mechanism by experiment, in C, before any Tycho**
  - Scope: throwaway C probes under the scratch directory. **No repo files
    change in this phase** except `plan.md`.
  - Build a minimal reproduction of the server's shape: a listening socket, N
    pthreads each blocked in `accept()`, and a `SIGTERM` handler. Then measure
    each of the four candidate mechanisms in Pre-flight: which threads wake, what
    `accept` returns, whether any thread hangs, and what happens to an fd that is
    closed under a blocked `accept()`.
  - Report a table: mechanism × (threads woken, return value, errno, hang?).
    **Include the failure modes, not just the winner** — the next person needs to
    know why three options were rejected.
  - Done when: one mechanism is chosen with measured evidence, and the three
    rejected ones have a recorded reason that is an observation rather than an
    argument.
  - Verify: the probe programs and their real output, in the evidence.

- [ ] **Phase 2 — the corelib surface**
  - Scope: the chosen mechanism as a shim plus a `.ty` surface. Follow
    `corelib/net/net_shim.c` and `corelib/os/os.ty` for conventions — a header
    comment stating what the package is for, the shim pure libc, no external
    dependency.
  - **Decide and state where it lives and how wide it is.** A narrow
    `signal.shutdown_requested() -> bool` is very different from a general
    `signal.on(SIGTERM, handler)`; the narrow one is what the server needs and
    the wide one is a language feature with re-entrancy rules. Justify the
    choice; the smallest thing that makes `server/main.ty:617` reachable is the
    default answer.
  - Async-signal-safety is a real constraint, not a formality: whatever the
    handler does must be on the POSIX safe list. Say what it does and why that is
    safe.
  - Done when: a Tycho fixture in `tests/` (or `corelib/test/`, whichever matches
    the convention — check) demonstrates the surface working, with a golden.
  - Verify: `make test`, then `make -s corelib`. Not `make ci`.

- [ ] **Phase 3 — the server shuts down cleanly, and the gate proves it**
  - Scope: `server/main.ty` and `server/run.sh`.
  - The server change should be small — the wind-down path already exists at
    `server/main.ty:493-494` and the count already returns at `:617`.
  - **`server/run.sh` currently asserts wait status 143**, i.e. that the server
    is killed. That assertion must change to assert clean exit and the printed
    count. Do not delete the abrupt-kill case — SIGKILL should still be tested,
    it just is not the same case any more.
  - **Prove every worker exits, not just that the line prints.** Run with
    `--workers 4`, send `SIGTERM`, and show the process group is gone and the
    exit status is 0. A single worker printing while three hang is the Worst case
    and a naive check passes it.
  - Done when: `SIGTERM` produces exit 0 and the served-count line, `make -s server-check`
    is green with the strengthened assertions, and the 10-run stability check
    phase 1 of the previous plan established still holds.
  - Verify: `make -s server-check`, the 10-run loop, and an explicit
    all-workers-exited check with its output.

- [ ] **Phase 4 — spec and docs**
  - Scope: `docs/spec/` for the new surface, `server/README.md` for the shutdown
    behaviour, and `FRICTION.md` — the "signal handling is absent" observation
    becomes either closed or narrowed.
  - Provenance discipline: single-line refs anchored `path:N@token`, ranges bare.
  - Done when: the surface is specified, the README describes shutdown, and the
    doc gates are green.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`,
    `sh scripts/spec_check.sh`.

## Carried forward

Unclosed discoveries from the previous plan; none blocking.

- [ ] **Phase 6** — 110 references to "`plan.md` phase N" across 42 files point at
      the wrong plan; `plan.md` rotates on archive and the citation gate cannot
      see them (no line number). `server/main.ty` alone has 13.
- [ ] **Phase 7** — `ncpu()`'s spec definition (`docs/spec/16-builtins.md:251`,
      "the `parallel for` fan-out width") is false above 64, measured.
- [ ] **Phase 8** — every `tests/reject/` fixture carrying a `package` header is
      scored against the whole directory; affects `tests/run.sh` equally.
- [ ] **Phase 9** — *(closed by the previous plan's phase 4; kept for the record)*
- [ ] **Phase 10, 11, 12** — filed by the previous plan's phase 3 from the
      concurrency write-up: spec §22 never describes `send` from a `parallel for`
      body, `docs/guides/concurrency.md:104` points at a fixture that contradicts
      it, and `corelib/iter/iter.ty:8`'s `map` is `[$T] -> [$T]` so it cannot
      change element type.

## Out of scope

- **A general signal API.** If phase 2 concludes the narrow surface is right, a
  full `signal.on(sig, handler)` is a separate plan with its own re-entrancy and
  threading questions.
- **Windows.** The tree is POSIX-only throughout; the shim follows suit.
- **`make test-fast`'s hung-job blind spot**, recorded in the previous plan: a
  hung fixture gives `rc=124` with nothing printed. Unrelated to signals in the
  server, though the same mechanism might eventually help it.
