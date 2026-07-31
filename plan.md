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

- [x] **Phase 1 — settle the mechanism by experiment, in C, before any Tycho**
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

  ### Evidence — phase 1

  One throwaway C probe, `probe.c`, built with
  `gcc -O1 -g -Wall -Wextra -pthread`, run outside the repo (scratch only; the
  only repo file this phase changed is `plan.md`). Platform: Linux
  `7.0.3+deb14-amd64`, gcc 15.3.0, glibc 2.42.

  **The Pre-flight's model of the server's shape was wrong in one load-bearing
  way, and the first probe run reproduced the wrong shape.** The Pre-flight says
  N workers are pthreads. In fact `server/main.ty:616` calls `worker(...)`
  *directly*, and `worker` at `server/main.ty:499` spawns worker k+1 and then
  runs its own `accept_loop`. So with `--workers 4` the process has **the main
  thread running accept loop 1 plus three spawned threads** — not four spawned
  threads with an idle main. This changes the answer to both settled facts
  below, so the probe was rewritten to match. The probe's shape is otherwise
  copied from the tree: one shared listening fd (`server/main.ty:497-504`), a
  bare `accept()` with no EINTR retry (`corelib/net/net_shim.c:162-167`),
  `pthread_create` with a NULL attribute and no `pthread_sigmask`
  (`runtime/tycho_rt.c:577`), and SIGTERM delivered process-directed from a
  forked child rather than raised by the thread that would then handle it.

  The probe's only non-server thread is a watchdog that reports and `_exit`s
  when a loop hangs; SIGTERM is blocked in that thread alone so the scaffolding
  cannot become a candidate receiver and perturb the measurement. Hangs are
  detected with `pthread_timedjoin_np` and a 3 s deadline — the server's main
  thread really does join its peers (`server/main.ty:503`), so a loop that never
  returns is a process that never exits.

  #### Mechanism × outcome, N=4, faithful shape, handler without `SA_RESTART`

  | mech | woke | `accept()` return / errno | hung | wall to exit | observed |
  |---|---|---|---|---|---|
  | 0 — signal only (baseline) | 1/4 | main: `-1` `EINTR`(4) | **3** | 3.552 s | loops 1,2,3 still blocked after 3 s, `accept` never returned |
  | 1 — `SO_RCVTIMEO` + polled flag | 4/4 | main: `-1` `EINTR`(4); others `-1` `EAGAIN`(11) after 3 spins each | 0 | 0.611 s | clean, but every idle loop spins at the timeout forever |
  | 2 — `shutdown(fd, SHUT_RDWR)` | 4/4 | main: `-1` `EINTR`(4); others `-1` `EINVAL`(22), 0 spins | 0 | 0.552 s | all three released in the same millisecond; `shutdown` returned 0 |
  | 3 — `close(fd)` | 1/4 | main: `-1` `EINTR`(4) | **3** | 3.552 s | loops 1,2,3 hung; **and the fd number was handed straight back out** |
  | 4 — eventfd + `poll` before `accept` | 4/4 idle, **2/4 under traffic** | main: `-1` `EINTR`(4); loop3 woken by eventfd; loops 1,2 hung | **2** | 3.653 s | see below — reproduced 2 of 3 runs |
  | 5 — mech 4 + `O_NONBLOCK` listener | 4/4 | main: `-1` `EINTR`(4); others woken by eventfd, `accept` not called | 0 | 0.652 s | clean, and this is what mech 4 costs to make correct |

  Under live traffic (the child opens two connections before the kill) every
  mechanism that woke 4/4 also accepted both connections normally — the wake
  machinery does not break the serving path. Mechanism 2 was re-run 20 times
  with traffic: **20/20 `woke=4/4 hung=0`**.

  #### Chosen: mechanism 2, `shutdown(listen_fd, SHUT_RDWR)` from the handler

  It is the only candidate that releases every loop with no change to
  `corelib/net/net_shim.c`'s accept, no polling in the steady state, and no fd
  lifetime hazard. `netx_accept` (`corelib/net/net_shim.c:162-167`) returns `-1`
  on `EINVAL`, which becomes `Err` in Tycho, which is already the wind-down arm
  at `server/main.ty:493-494`. `shutdown()` is on this system's
  async-signal-safe list (`man 7 signal-safety`). The fd is never closed, so
  nothing can reuse the number while a loop is still in `accept`. Measured
  release: 0.000 s of extra latency, all four loops at 0.552 s, the same
  millisecond the signal landed.

  Its cost, recorded honestly: `shutdown` on a *listening* socket is a Linux
  behaviour, not a POSIX guarantee, and pending connections in the backlog are
  dropped rather than drained. Both are acceptable for a shutdown path; neither
  is portable. The tree is POSIX-only-in-practice and already Linux-tested, so
  this is the same class of bet `SO_RCVTIMEO`-on-`accept` would have been.

  #### Why the other three lost — observations, not man-page arguments

  - **Mechanism 1 (`SO_RCVTIMEO`) — rejected, and it is the runner-up.** It does
    work: 4/4, 0 hung. Two observed costs. First, the spin counts in the table
    are real work: each idle loop woke 3 times in 0.6 s and would keep doing that
    forever at 200 ms granularity, for four workers, whether or not a shutdown is
    ever requested. Second and decisively, it cannot be adopted without changing
    the shim: with a timeout armed on the listener, a plain idle tick returns
    `-1`/`EAGAIN`, `netx_accept` (`corelib/net/net_shim.c:162-167`) collapses
    that to `-1` exactly as it collapses a real failure, and
    `server/main.ty:493-494` sets `running = false` on any `Err`. **Every worker
    would retire on its first idle timeout.** So mechanism 1 costs a shim change
    *and* a new "was that a timeout or a failure?" surface, to buy a slower
    version of what mechanism 2 does for free.
  - **Mechanism 2 is not free of the same trap** — see phase 14 below.
  - **Mechanism 3 (`close`) — rejected on the hang and on the fd hazard.** Three
    of four loops were still blocked in `accept` 3 seconds after the fd was
    closed; `accept` never returned. Closing an fd does not wake a thread already
    blocked on it. Worse, the probe then called `open("/dev/null")` three times
    from another thread and got **listen fd 3 back**, while three loops were
    still blocked on that number — the exact fd-reuse corruption the Pre-flight
    flagged as a possibility, observed rather than theorised.
  - **Mechanism 4 (eventfd + `poll`) — rejected as specified; correct only in a
    larger form.** With an idle listener it looked clean, 4/4. **Under live
    traffic it hung 2 of 4 loops, reproduced in 2 of 3 runs.** The failure is a
    thundering herd: one incoming connection makes `poll` return `POLLIN` on the
    listener for *all* waiting loops, they all fall through to `accept`, one wins,
    and the losers block inside `accept` — past the readiness gate, where the
    eventfd write can no longer reach them. The gate only works if the accept
    behind it cannot block. Mechanism 5 in the table is that corrected version
    (`O_NONBLOCK` on the listener plus an `EAGAIN` retry) and it is clean, 4/4 —
    but it means a non-blocking listener, a retry loop inside `netx_accept`, and
    a `poll` before every accept in the tree's hot path. That is the largest
    change of the four, to reach the same outcome mechanism 2 reaches with a
    one-line handler.

  #### Settled fact 1 — a handler without `SA_RESTART` does interrupt `accept()`

  Confirmed by contrast, mechanism 0, same probe, only the flag varying:

  - `sa_flags = 0`: `woke=1/4`, the receiving loop returned `-1` with
    `errno=4` (`EINTR`). 
  - `sa_flags = SA_RESTART`: `woke=0/4 hung=4` — **no loop woke at all**, the
    kernel restarted the `accept` under the receiver too.

  So the Pre-flight is right that `corelib/net/net_shim.c:162-167` needs no
  retry-loop change *for the receiving thread*, and it is right for the reason
  given. But the practical consequence is smaller than it reads: `EINTR` releases
  exactly one loop out of four, which is the Worst case in the Pre-flight, not a
  partial solution. The handler must still be installed without `SA_RESTART`
  (mechanism 2 works with `SA_RESTART` too — re-measured, 4/4 — but the extra
  `EINTR` wake costs nothing and gets the main thread out of `accept` a hair
  earlier).

  #### Settled fact 2 — the main thread receives it, deterministically here

  70 runs, no variation:

  - Faithful shape (main runs loop 1), 30 runs: **30/30 delivered to the main
    thread.**
  - Main thread idle, four spawned loops, 20 runs: **20/20 delivered to the main
    thread** — it is not "whichever thread is not busy".
  - SIGTERM blocked in the main thread only, the three spawned loops eligible,
    20 runs: **20/20 delivered to the first-created spawned loop.**

  Method: a `__thread` index stamped by each loop, recorded in the handler into a
  `volatile sig_atomic_t`, histogrammed over repeated whole-process runs (a
  process can only be killed once). Reading a thread-local from a handler is not
  on the safe list; it is instrumentation, not the shipped design.

  So on this kernel delivery goes to the lowest-numbered eligible thread, main
  first. **That is an implementation detail of Linux's signal routing, not a
  contract, and the chosen mechanism does not depend on it** — mechanism 2 wakes
  all four loops regardless of which one ran the handler, which is exactly why it
  was chosen over anything that has to be delivered to the right thread.

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
- [ ] **Phase 14** — filed by phase 1. `accept_loop` treats *every* `Err` from
      `net.accept` as "listener closed" and sets `running = false`
      (`server/main.ty:493-494`), because `netx_accept`
      (`corelib/net/net_shim.c:162-167`) collapses every failure to `-1` and the
      errno is not carried into Tycho. A transient, per-call error — `EMFILE`,
      `ENFILE`, `ECONNABORTED`, `EPROTO`, all of which POSIX says to retry —
      therefore **retires that worker permanently and silently**, and the server
      keeps running with fewer accept loops than it reports in its banner
      (`server/main.ty:612`). Under fd exhaustion the workers would drain one at
      a time until the process quietly stops serving. This is pre-existing and
      not caused by the signal work; phase 1 found it while establishing that
      `EINVAL` reaches the wind-down arm, which is the same collapse working *in
      our favour*. Fixing it means carrying errno across the shim boundary, which
      is a wider change than this plan.

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
