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

- [x] **Phase 2 — the corelib surface**
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

  ### Evidence — phase 2

  New package `corelib/signal/` — `signal.ty` over a pure-libc `signal_shim.c`,
  no `deps` file — plus the fixture `corelib/test/signal/main.ty` and its golden
  `corelib/test/signal.out`. Nothing else in the tree changed; `server/main.ty`,
  `server/run.sh` and `docs/spec/` were not touched.

  #### The width: narrow, two functions, and no Tycho code in handler context

      signal.on_shutdown(fd) -> bool          install SIGTERM+SIGINT -> shutdown(fd)
      signal.shutdown_requested() -> bool     read the flag the handler set

  `signal.on(sig, handler)` was rejected, and the reason is not "smaller is
  nicer". A Tycho function called from a handler has to be re-entrant against two
  things this tree really has: the arena allocator, in which every Tycho value
  lives and which is not re-entrant, and the scheduler's mutex-guarded queues — a
  handler that interrupts the lock holder and then touches a queue deadlocks the
  process, which is the same class of failure as the Worst case in the Pre-flight
  but harder to see. It is also not the *narrow* option's problem to solve which
  thread receives the signal: phase 1 measured `pthread_create` with a NULL
  attribute and no `pthread_sigmask` (`runtime/tycho_rt.c:577`), so delivery is to
  an arbitrary thread, and a Tycho-level handler would inherit that.

  Recorded so the question is not pretended away — a future wide version needs,
  at minimum: (1) a signal-safe hand-off out of handler context, self-pipe or
  `signalfd`, read by a dedicated thread, so the Tycho callback runs on an
  ordinary stack rather than inside the handler; (2) a rule keeping the handler
  itself at a `sig_atomic_t` store, unchanged from what ships here; (3) a
  `pthread_sigmask` policy, since "an arbitrary worker runs your callback" is not
  a contract anyone can code against; (4) a spec section for the re-entrancy
  contract. None of that is needed to reach `server/main.ty:617`, and the tree has
  exactly one caller. The header of `corelib/signal/signal.ty` carries the same
  list so the next reader does not have to find this file.

  Two functions rather than one: `on_shutdown` is what phase 3 needs, and
  `shutdown_requested` is what makes the fixture able to prove the handler *ran*
  rather than only that `accept` later failed. It costs one `sig_atomic_t`.

  #### The handler, statement by statement, against the POSIX safe list

  `corelib/signal/signal_shim.c:77-85`. `man 7 signal-safety` on this system is
  the list being cited.

  | line | statement | why it is safe |
  |---|---|---|
  | `corelib/signal/signal_shim.c:79` | `int saved = errno;` | reads a thread-local `int`; no call. Saved because `shutdown()` may set `errno` and the interrupted thread is entitled to its own value |
  | `corelib/signal/signal_shim.c:81` | `sigx_flag = 1;` | store to a `volatile sig_atomic_t` — the one object type POSIX permits a handler to write while the rest of the program reads it |
  | `corelib/signal/signal_shim.c:82` | `int fd = (int)sigx_fd;` | load from a `volatile sig_atomic_t`, same guarantee. Not a TLS read: phase 1's `__thread` histogram was instrumentation and is deliberately not shipped |
  | `corelib/signal/signal_shim.c:83` | `shutdown(fd, SHUT_RDWR)` | **on the POSIX async-signal-safe list.** A bare syscall: allocates nothing, takes no userspace lock the interrupted thread could already hold |
  | `corelib/signal/signal_shim.c:84` | `errno = saved;` | store to a thread-local `int` |

  No `malloc`, no stdio, no `pthread_*`, no arena touch — so there is no lock for
  the handler to deadlock the interrupted thread against, which was the stated
  constraint rather than a formality. `sigemptyset`/`sigaction`/`memset`
  (`corelib/signal/signal_shim.c:104-110`) run in `sigx_on_shutdown`, ordinary
  code, not in handler context.

  Two orderings that are load-bearing and are commented at the site:

  - The fd is stored (`corelib/signal/signal_shim.c:103`) **before** the first
    `sigaction`, so no signal can find a handler installed and a stale or absent
    descriptor. `-1` means "registered nothing", and the handler then only sets
    the flag.
  - `sa.sa_flags = 0` (`corelib/signal/signal_shim.c:108`) — **no `SA_RESTART`**,
    per phase 1's settled fact 1: with `SA_RESTART` the receiving thread's
    `accept` is restarted and it does not wake at all. `shutdown` releases the
    other loops either way; the extra `EINTR` is free and gets the receiver out
    marginally earlier.

  Fail-closed, matching `core:net`: `sigx_on_shutdown` returns 0 on a negative or
  `> INT_MAX` fd and on a failed `sigaction`, so a caller that ignores the result
  keeps the default disposition — dying on SIGTERM, today's behaviour — never a
  half-armed one.

  #### Where it lives, and what it had to be wired into: nothing

  Checked rather than assumed. `corelib/run.sh:21-32` discovers tests by globbing
  `corelib/test/*/main.ty`, greps the fixture's own `import` lines for `core:X`,
  and picks up `corelib/$mod/${mod}_shim.c` and `corelib/$mod/deps` by path.
  `tychoc` auto-discovers the shim the same way. So **there is no list of corelib
  packages in the build or the compiler that a new one must join** — verified by
  `grep -rln 'core:net' src scripts Makefile tools editors docs/spec`, which
  matches only `src/tychoc.c:10534` (a comment), `scripts/entrypoints.sh:7` and
  `scripts/ci.sh:103` (both comments), and `docs/spec/appendix-e-conformance.md`.
  The one real enumeration is `docs/spec/18-library.md`, whose §32 lists the
  core-tier packages one heading each — `net` is §32.24 at
  `docs/spec/18-library.md:263`, the last core-tier entry is `decimal` at
  `docs/spec/18-library.md:285`. `signal` has no section there yet; adding one is
  **phase 4's** scope and was deliberately left alone. Note for phase 4: the
  headings are the enumeration, and `docs/spec/appendix-e-conformance.md` is the
  other file that names `core:X` packages.

  `core:signal` therefore lands with zero build changes: `make -s corelib` picked
  it up on the first run. The extern prefix is `sigx_`, for `core:net`'s reason at
  `corelib/net/net.ty:91-92` — `signal` and `sigaction` are libc symbols and a
  bare name would bind to the kernel wrapper.

  Incidental: `extern fn sigx_requested() -> int`
  (`corelib/signal/signal.ty:56`) is the first **zero-argument** extern in
  `corelib/` — `grep -rn "extern fn [a-zA-Z_]*()" corelib tests docs/spec ffi`
  returns nothing else. It compiles and runs. The form is documented at
  `docs/reference/ffi.md:13` (`extern fn getpid() -> int`), so this is a first
  use of a specified feature, not an unspecified one — no phase filed.

  #### The fixture, and why a golden is achievable here

  `corelib/test/signal/main.ty`, golden `corelib/test/signal.out`. It lives in
  `corelib/test/` and not `tests/`: every corelib package has exactly one
  `corelib/test/<name>/` directory, that is the convention, and it keeps
  `make test` at 560 rather than inflating the corpus with a package-surface test.

  The awkward part of a signal test is timing, and it is avoidable here. The
  fixture sends `kill -TERM $PPID` through `os.system`, so the target is its own
  process. `kill(2)` makes the signal pending before it returns; the fixture is
  meanwhile blocked in `waitpid(2)` inside `system(3)`; the kernel runs a pending
  unblocked signal's handler before returning to user space. **The handler has
  therefore run by the time `os.system` returns** — no sleep, no poll, no race to
  lose. Single-threaded, so there is no second thread to be delivered to.

  SIGTERM and not SIGINT, deliberately: glibc's `system(3)` sets SIGINT to
  `SIG_IGN` in the caller for the duration of the call, so a `kill -INT` issued
  this way would be swallowed by `system` rather than by anything in
  `core:signal`. Both signals are installed
  (`corelib/signal/signal_shim.c:109-110`); SIGINT gets its real exercise in
  phase 3's server gate.

  What the fixture proves, and what it does not. It proves the handler installs,
  fails closed on a bad fd, actually fires, and leaves the listener in a state
  where `accept` returns `Err` instead of blocking — which is exactly the arm
  `server/main.ty:493-494` already winds down on. It does **not** prove all N
  accept loops are released; that is a property of `shutdown()` rather than of
  this API, it is measured at N=4 in phase 1's table, and phase 3 must still prove
  it end to end in the server as the Pre-flight requires.

  Stability: the built fixture was run 20 times and compared with `cmp` against
  the golden — **20/20 byte-identical**, no `DIFF` line printed.

  ```
  armed_bad=false
  requested_before=false
  listen_ok=true
  armed=true
  accept_before=true
  kill_code=0
  requested_after=true
  accept_after=Failed
  ```

  #### Gates — real output

  `make test` — 560 before this phase, 560 after. **Accounted for: this phase adds
  no `tests/` fixture at all.** The one test it adds is `corelib/test/signal`,
  which `corelib/run.sh` scores, not `tests/run.sh`; a corpus fixture would have
  been the wrong home for a package-surface test and would have moved this number.

  ```
  passed: 560   failed: 0
  all green
  ```

  `make -s corelib` — 38 ok, 1 skip (`image`, missing libpng, pre-existing), and
  the new line:

  ```
  ok   sha256
  ok   signal
  ok   sort
  ...
  corelib: all green (tychoc matches goldens)
  ```

  Ordering note, so this is not overstated: `make test` ran once, before a later
  comment-only edit to `signal_shim.c`'s header. That edit cannot move it — no
  fixture under `tests/` imports `core:signal`, so `tests/run.sh` never compiles
  the file. `make -s corelib`, which does compile it, was re-run afterwards and is
  the output above.

  `python3 scripts/check_citations.py`:

  ```
  citation check: ok (168 anchored contain the token they name, 2706 bare in bounds,
  140 source->doc citations resolve, 233 source->source in bounds, 12 source->source anchored)
  ```

- [x] **Phase 3 — the server shuts down cleanly, and the gate proves it**
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

  ### Evidence — phase 3

  Two files changed: `server/main.ty` (one import, one guarded call, a comment)
  and `server/run.sh` (the gate). `corelib/signal/` and `docs/` untouched.

  #### The server change: one call, and the argument for its position

  `import "core:signal"` at `server/main.ty:61`, and the arm itself at
  `server/main.ty:635-637` — `if not signal.on_shutdown(srv)`, warning on stderr
  and continuing, because a server that could not arm its handler is still a
  working server and `on_shutdown` already fails closed to the old behaviour.

  No new control flow, exactly as the phase brief predicted. The chain is
  entirely pre-existing: handler → `shutdown(srv, SHUT_RDWR)` → every blocked
  `net.accept` returns `Err` → `accept_loop`'s `Err` arm at
  `server/main.ty:493-494` sets `running = false` → `worker` unwinds through
  `wait(peer)` → the stopped line, now at `server/main.ty:646`.

  **Why there and nowhere else**, the three constraints written at the site:

  - **After `net.listen`** (`server/main.ty:605-607`) — `on_shutdown` takes the
    listening fd; before that call there is no fd to register.
  - **Before the fan-out** (`server/main.ty:645`). Phase 1's correction to the
    thread model is the whole reason this needs saying: `main` *calls*
    `worker(cfg, srv, 1, cfg.workers)` rather than spawning it, and `worker`
    (`server/main.ty:499-504`) spawns peer k+1 then runs accept loop k itself. So
    main **is** accept loop 1 and does not return from that call until the entire
    pool has wound down. Everything below it is shutdown code; there is no later
    point in `main` that runs at startup.
  - **Before the banner** (`server/main.ty:639-643`), which is the readiness
    signal `server/run.sh` polls for. Arming after it leaves a window where a
    reader has been told "serving" while SIGTERM still has its default
    disposition — a rare, confusing gate flake, bought for nothing.

  One call arms the whole pool: the handler is per-process, not per-thread, and
  it acts on the shared listener. Which thread the kernel picks does not matter,
  which is precisely why phase 1 chose `shutdown()` over `close()`.

  #### The gate: three kill cases where there was one

  `server/run.sh` asserted wait status 143 — the absence of clean shutdown. It
  now asserts three separate things, and the SIGKILL case is kept rather than
  replaced because it is the control.

  | case | signal | asserts |
  |---|---|---|
  | 1 | SIGTERM | exit status **0** (not 143, not the watchdog's 137); the `stopped after N requests` line present; **N equals the access-log line count**; the log's last line is the shutdown line, not a request line |
  | 2 | SIGINT | exit status 0 **and** the stopped line — the same handler, on the signal `core:signal`'s own fixture provably cannot test |
  | 3 | SIGKILL | wait status **137**, and **no** stopped line: uncatchable, so nothing winds down |

  Case 3 earns its place as case 1's control. Without it, "exit 0 and a line" is
  consistent with the process having done that on the way out of any signal at
  all; with it, the clean exit is demonstrably the handler's doing.

  Case 2 is here because phase 2's evidence promised it and nothing else can
  keep the promise: glibc's `system(3)` sets SIGINT to `SIG_IGN` in the caller
  for the duration of the call, so `corelib/test/signal/main.ty`'s
  `kill -INT $PPID` would be swallowed by `system` rather than by anything under
  test. SIGTERM and SIGINT are installed together at
  `corelib/signal/signal_shim.c:109-110`; this gate is the only exercise the
  second one gets.

  A 10 s watchdog SIGKILLs case 1's server. That is not belt-and-braces: without
  it a regression that leaves one loop blocked would **hang** this gate rather
  than redden it, and a hung gate is the failure mode hardest to attribute.

  #### The Worst case, proved four ways

  Scratch script, `--workers 4`, its real output:

  ```
  server pid=2452586 pgid=2452586  (own session; pgid == pid means the group is the server alone)
  bound port=37409
  requests answered 200: 8/8
  --- workers that served, from the access log ---
  w1 w2 w3 w4
  --- threads in the process before SIGTERM (/proc/2452586/task) ---
  2452586 2452590 2452591 2452592
  task count: 4
  === RESULTS ===
  exit status: 0            (want 0; 143 = old behaviour, 137 = watchdog SIGKILL i.e. hang)
  shutdown wall: 1 ms   (phase 1 measured 4/4 release in C at 552 ms)
  stopped line: tycho-httpd: stopped after 8 requests
  last stderr line: tycho-httpd: stopped after 8 requests
  --- process group 2452586 after shutdown (ps -o pid,stat,comm -g) ---
      PID STAT COMMAND
  (ps reports no such process group)
  processes left in group: 0
  /proc/2452586 exists: no
  ```

  The four readings, and why each one is needed:

  1. **Four loops really were blocked.** Eight sockets connected before any of
     them speaks; a worker is inside `serve_conn` until its connection closes, so
     with four accept loops all four must take one. `w1 w2 w3 w4` in the access
     log, and `/proc/<pid>/task` shows exactly 4 threads. Without this the rest
     proves nothing about N=4.
  2. **Exit status 0 *is* the all-exited assertion.** `worker` returns
     `n + wait(peer)` (`server/main.ty:499-504`) and `main` calls it directly, so
     the stopped line is reachable only after every spawned peer is joined. One
     loop still in `accept(2)` means `wait()` never returns — no line, no exit,
     and the watchdog's 137 instead. This is the reading the naive stdout grep
     misses: the line is printed by the main thread, so grepping for it alone
     cannot distinguish 4/4 from 1/4. Exit 0 can.
  3. **Nothing is left.** `pgid == pid` because the server is launched into its
     own session; the group is then the server alone and `ps -o pid= -g` is
     empty, `/proc/<pid>` gone.
  4. **1 ms.** Measured from `kill` to `wait` returning.

  **A first attempt at reading 3 was wrong and is recorded rather than quietly
  fixed.** It used `set -m` to put the server in its own process group; under a
  shell with no controlling tty that prints `can't access tty; job control turned
  off` and *proceeds*, so the server inherited the harness shell's group and the
  check reported `processes left in group: 6` — the harness's own `zsh`, `sh`,
  `sleep`, `sed` and `grep`. A check that counts its own observer is worse than
  no check. The shipped form calls `setsid(2)` through a `python3` that
  immediately `execv`s, so `$!` still names the server and the group contains it
  alone.

  #### Timing, against phase 1's 0.552 s

  Not slower — and the two numbers measure different spans, so the comparison is
  spelled out rather than rounded off. Phase 1's 0.552 s is the C probe's
  **whole wall-to-exit**, including its setup; its actual release finding was
  "all three released in the same millisecond". The 1 ms here is `kill(2)` to
  `wait(2)` returning, i.e. the shutdown itself. They agree: release is immediate
  in both, and Tycho adds nothing measurable over the C probe.

  One case is materially slower, it was found by measurement after the guess that
  produced it turned out wrong, and it is **bounded, not a hang**:

  ```
  0 idle keep-alive clients (all 4 loops in accept): rc=0  shutdown=1 ms     [1 stopped line]
  1 idle keep-alive client  (1 loop in serve_conn): rc=0  shutdown=1 ms     [1 stopped line]
  4 idle keep-alive clients (ALL 4 in serve_conn): rc=0  shutdown=5141 ms  [1 stopped line]
  ```

  With `--idle-ms 5000` and every worker parked in `serve_conn` on an idle
  keep-alive connection, shutdown takes one idle timeout. The 1-client row is the
  instructive one: it was expected to be slow and is not, because phase 1's
  settled fact 2 has SIGTERM delivered to the main thread, main is accept loop 1,
  and it was the loop holding that connection — `EINTR` released it directly.
  With four held connections the other three get no such rescue and wait out
  `SO_RCVTIMEO`. Still exit 0, still the line, every time. Filed as **phase 15**.

  #### Gates — real output

  `make -s server-check`: green. Assertion count **52 → 57** (+5: SIGTERM exit 0,
  the stopped line, count-equals-log-lines, SIGINT, and SIGKILL's two, less the
  one 143 assertion removed).

  ```
    ok   SIGTERM: exit status 0 (clean; every accept loop returned and was joined)
    ok   SIGTERM: tycho-httpd: stopped after 84 requests
    ok   SIGTERM: served count == access log lines
    ok   access log: last line is the shutdown line (stopped after serving)
    ok   SIGINT: exit status 0 and the stopped line (same handler as SIGTERM)
    ok   SIGKILL: wait status 137 (128+SIGKILL), uncatchable by design
    ok   SIGKILL: no stopped line, nothing wound down (the control for case 1)
  server: OK
  ```

  Ten consecutive runs of the final file — the assertions changed *and* the
  program under test changed, so the previous plan's 10/10 does not carry:

  ```
  run 1: rc=0 ok=57 fail=0  server: OK
  run 2: rc=0 ok=57 fail=0  server: OK
  run 3: rc=0 ok=57 fail=0  server: OK
  run 4: rc=0 ok=57 fail=0  server: OK
  run 5: rc=0 ok=57 fail=0  server: OK
  run 6: rc=0 ok=57 fail=0  server: OK
  run 7: rc=0 ok=57 fail=0  server: OK
  run 8: rc=0 ok=57 fail=0  server: OK
  run 9: rc=0 ok=57 fail=0  server: OK
  run 10: rc=0 ok=57 fail=0  server: OK
  GREEN: 10/10
  ```

  `make test` — 560, undisturbed:

  ```
  passed: 560   failed: 0
  all green
  ```

  Honest ordering note: `make test` ran against `server/main.ty` in its final
  state but against the gate *before* the SIGINT case was added. It cannot move —
  no fixture under `tests/` builds `server/run.sh` or the server — and
  `make -s server-check` plus the 10-run loop above are both the final file.

- [x] **Phase 4 — spec and docs**
  - Scope: `docs/spec/` for the new surface, `server/README.md` for the shutdown
    behaviour, and `FRICTION.md` — the "signal handling is absent" observation
    becomes either closed or narrowed.
  - Provenance discipline: single-line refs anchored `path:N@token`, ranges bare.
  - Done when: the surface is specified, the README describes shutdown, and the
    doc gates are green.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`,
    `sh scripts/spec_check.sh`.

  ### Evidence — phase 4

  Three files: `docs/spec/18-library.md` (new §32.27), `server/README.md` and
  `FRICTION.md`. No source file was touched — `corelib/signal/`, `server/main.ty`
  and `server/run.sh` are byte-identical to phase 3.

  #### The spec: §32.27, and where the safety contract went

  Checked before writing rather than assumed: §32's per-package headings really
  are the enumeration (`docs/spec/18-library.md:86` opens the section,
  `docs/spec/18-library.md:263` is `net`, and `docs/spec/18-library.md:285` is
  `decimal`, the last core-tier entry), and the *newer* entries —
  `net`, `bignum`, `decimal`, `compress`, `image`, `tls` — close with
  **`Source <path>`** rather than the `docs/guides/corelib.md:N` pointer the older
  ones use. §32.27 follows the newer form, which is also the honest one: the guide
  has no `core:signal` entry to point at (verified — `grep -n signal
  docs/guides/corelib.md` returns exactly one hit, `128+signal` at
  `docs/guides/corelib.md:301`, in `os.run`'s exit-code description). Filed as
  phase 16.

  The catalog paragraph matches the neighbours in length and shape. The
  safety contract does not fit that shape, so it is a `####` subsection —
  a depth the spec already uses once, at `docs/spec/00-conventions.md:43`, for
  exactly this purpose (a note that qualifies the section above it). Nothing was
  added to `docs/spec/appendix-e-conformance.md`: its E.2 matrix is keyed by
  normative *clause*, has no §31–33 block at all, and its one sentence about the
  library chapters (`docs/spec/appendix-e-conformance.md:401-402`) scopes itself
  to the `deps` tier, which `core:signal` is not.

  **What was made normative**, i.e. the sentences a second implementation is bound
  by rather than informed of:

  - `on_shutdown` **MUST** fail closed — a descriptor it cannot register or a
    handler it cannot install leaves the default disposition in place, never a
    half-armed state.
  - An implementation **MUST NOT** run Tycho code in handler context, with the
    reason attached to two verified facts rather than to taste: values live in a
    bump-allocated non-re-entrant arena (§31.1), and channel operations park behind
    a mutex — `runtime/tycho_rt.c:657` is `pthread_mutex_t mu`, taken at
    `runtime/tycho_rt.c:693`.
  - The handler's **five** actions, each named with its justification, anchored one
    per line into the shim (`corelib/signal/signal_shim.c:81@sigx_flag`,
    `:82@sigx_fd`, `:83@shutdown`) so the citation gate re-checks the mapping on
    every future edit to that file.
  - Both load-bearing orderings **MUST** be preserved: the descriptor registered
    before the first `sigaction` (`corelib/signal/signal_shim.c:103@sigx_fd`), and
    `sa_flags = 0` (`corelib/signal/signal_shim.c:108@sa_flags`).
  - **The `SA_RESTART` clause is the one worth having.** Phase 1 measured it as a
    contrast (0/4 woken with `SA_RESTART`, 1/4 without) and the tempting way to
    write that up is "so we do not set it". What the spec says instead is what a
    *program* can rely on: an interrupted `accept` **MUST** be observable as a
    failed one rather than as a silently restarted call. That is a statement about
    the language surface, not about a flag, and it is the half a reimplementer
    would otherwise have to guess. The measured numbers stayed out of the spec —
    they are evidence for the rule, not the rule.
  - The non-portability is spelled out as an obligation: `shutdown()` waking a
    thread blocked in `accept` on a *listening* socket is Linux behaviour, backlog
    connections are dropped rather than drained, and another platform **MUST**
    re-establish the released-every-thread property rather than assume it —
    with `close(fd)`'s two observed failures named so it is not retried as the
    obvious substitute.

  #### `server/README.md`: the shutdown story was wrong in four places, not one

  Read whole rather than appended to, as the brief required, and the stale surface
  was wider than the one paragraph:

  1. **"One rough edge that is still here — there is no graceful shutdown"** (the
     whole block) is now the third entry in the section above it, which meant
     re-titling that section ("Two rough edges" → "Three") and splitting its
     "both were closed on 2026-07-26" opener, since the third closed on a different
     date and was a *language* gap rather than a corelib one. The replacement keeps
     the old behaviour in the past tense — 143, no line, a log ending on a request —
     because that section's stated purpose is that the history is worth as much as
     the current truth.
  2. **The "What it does" table had no shutdown row.** It has one now: exit 0 and
     the count line for `SIGTERM`/`SIGINT`, `SIGKILL` still uncatchable.
  3. **"Two socket fixes this program forced into `core:net`"** opened by asserting
     that neither property is settable from Tycho *because* "signal disposition is
     process-wide". Half of that reason has expired. Corrected in place rather than
     deleted: the conclusion still holds — `core:signal` cannot set a disposition,
     so `MSG_NOSIGNAL` is still the only fix for `SIGPIPE` — and the sentence now
     says so instead of resting on a premise that stopped being true.
  4. **"Verifying it"** described the gate as killing the server "on every exit
     path", which was accurate when the kill was teardown and is not now that it is
     three assertions. It names all three cases, says why `SIGKILL` is the control
     rather than redundancy, and gives the 57.

  **The `--idle-ms` measurement went into Usage, under the flag itself**, not into
  the shutdown section — a reader choosing that number is looking at the flag list,
  and the finding *is* about the flag: it bounds the worst-case `SIGTERM`, not only
  how long a silent peer may pin a worker. 1 ms at 0 or 1 held connections, 5141 ms
  at four, exit 0 and the line in every case, with the phase 15 pointer and the
  reason the 1-connection case is fast (routing luck, not design).

  **Six citations repaired while in there, all of them staled by this plan.**
  Phase 3's `import "core:signal"` at `server/main.ty:61` shifted every line below
  it by one: `:342-352` → `:343-353` (the log line), `:301` → `:302` (`is_dir`),
  `:368` → `:369` (`peer_addr`), `:494` → `:494-495` (the wind-down arm, which the
  old single-line ref no longer covered), `:617` → `:646` (the stopped line) and
  `:610-614` → `:639-643` (the banner). Three were re-anchored `@token` while being
  fixed, so the next shift reddens the gate instead of drifting silently. Also
  `FRICTION.md:432` → `:601`, which had been stale since before this plan. The
  same +1 hit seven comments in `server/run.sh`; that file is outside this phase's
  scope, so it is filed as phase 17 rather than swept here.

  #### `FRICTION.md`: the item the brief named does not exist, and the real one is a
  narrowing

  **Checked before scoring: there is no "signal handling is absent from the
  language" entry.** `grep -in signal FRICTION.md` returned exactly **one** line
  before this phase — `FRICTION.md:601`, the `SIGPIPE` item — and no entry anywhere
  in the file asks for `SIGTERM` handling or graceful shutdown. So there was
  nothing to strike through, and inventing an entry in order to close it would have
  been the worst available outcome in a file whose whole value is that it was
  written before the fix. What the tree actually had was the *plan's* Goal, not a
  friction item.

  Scored as **two** things, following the file's own conventions:

  - **A re-score in place on `FRICTION.md:601`**, not a strike-through. That entry
    is about `SIGPIPE`, its fix (`MSG_NOSIGNAL`) shipped long ago, and its
    conclusion is untouched — but the *general* claim it rests on, "signal
    disposition is process-wide with no Tycho surface", has become false in its
    first half. The note says what is now true, that `core:signal` cannot set a
    disposition, and that the useful restatement is "no Tycho surface for signal
    **disposition**". A strike-through would have claimed this phase closed a
    `SIGPIPE` item it did not touch.
  - **A new dated section**, matching how the file already records a plan's
    outcome. It scores the shutdown case **CLOSED** with the measurement, the
    general handler **STILL OPEN, narrowed** with the four things a wide version
    needs (carried from `corelib/signal/signal.ty:19-31` so the list has one home
    and not two), the `--idle-ms` latency as **NEW, measured**, the Linux-only
    mechanism as **NEW, small**, and the zero-build-wiring finding under what went
    right. "Closed for the shutdown case, narrowed to: no general handler" is the
    verdict the brief predicted, and it survived contact with the file.

  Append-only plus one in-place line edit, deliberately: seven `FRICTION.md:N`
  citations live in `src/tychoc.c`, `corelib/`, `tests/` and `server/README.md`,
  every one of them at `:432` or below, so nothing above line 601 moved and none of
  them drifted.

  #### Gates — real output

  Markdown-only change, so the three doc gates and nothing else (`CLAUDE.md`'s
  budget table: `make test` cannot observe a change that reaches no compiled
  artifact). No `.ty` file was added, so `scripts/spec_examples.sh`'s corpus is
  unchanged at 9.

  ```
  $ python3 scripts/check_citations.py
  citation check: ok (186 anchored contain the token they name, 2741 bare in bounds,
  140 source->doc citations resolve, 233 source->source in bounds, 12 source->source anchored)

  $ sh scripts/check_links.sh
  link check: ok (136 markdown files, no dead relative links)

  $ sh scripts/spec_check.sh
  spec-check: Appendix A grammar matches §3/§4 (ok)
  spec-check: all Appendix E fixture citations resolve (ok)
  spec-examples: ok docs/spec/03-types.md:155 (tychoc)
  ...
  spec-examples: 9 runnable example(s), all pass
  ```

  The anchored population moved **168 → 186** across the three files and this
  evidence block. Preferring the anchored form was the point: a bare ref into
  `corelib/signal/signal_shim.c` would have stayed green while the handler drifted
  under it, and the spec's five-action table is worth nothing if it names the wrong
  five lines.

  **The gate caught this write-up before it caught anything else, which is the
  note worth leaving.** The first run of `check_citations.py` after the plan edit
  came back `FAILED (2 stale citation(s))`: `plan.md:664`'s `` `:263` `` and
  `` `:285` `` were bare continuations of a `docs/spec/18-library.md` named on the
  *previous* line, and a `docs/` path does not carry across lines — precisely the
  trap `CLAUDE.md`'s Citations section says has reddened four separate phases on
  their own evidence. Fixed by spelling both paths in full; it is the fifth.

## Status — PLAN COMPLETE

Four phases, four commits:

| phase | commit | what shipped |
|---|---|---|
| 1 | `b4a41a8` | the mechanism settled by C probe: `shutdown(listen_fd, SHUT_RDWR)` from the handler, 4/4 loops released, with the three rejections recorded as observations |
| 2 | `1f177e9` | `corelib/signal/` — `signal.ty` over a pure-libc `signal_shim.c`, fixture `corelib/test/signal` and its golden |
| 3 | `5428fa1` | `server/main.ty` arms it in one call; `server/run.sh` asserts clean exit on `SIGTERM` and `SIGINT` with `SIGKILL` as the control, 52 → 57 assertions |
| 4 | *this commit* | `docs/spec/18-library.md` §32.27 with the async-signal-safety contract, `server/README.md`'s shutdown story, `FRICTION.md` re-scored |

**What shipped.** `server/main.ty:646` — `tycho-httpd: stopped after N requests`,
unreachable when this plan opened — now prints on `SIGTERM` and on `SIGINT`, with
exit status 0, every worker joined and nothing left in the process group. The
surface is two functions (`signal.on_shutdown(fd)`, `signal.shutdown_requested()`)
over 126 lines of C, needed **zero** build wiring, and the gate that used to assert
wait status 143 — the absence of clean shutdown — now asserts its presence.

**What remains open**, all of it filed, none of it blocking: phases 6–12 and 14–17
below. The two this plan created are **phase 15** (shutdown latency is bounded by
`--idle-ms`, measured at 5141 ms with four held keep-alive connections — the API to
fix it exists and has no caller) and **phase 14** (`accept_loop` cannot tell a
transient `accept` failure from a closed listener, because `netx_accept` collapses
every errno to `-1`; pre-existing, found while proving `EINVAL` reaches the
wind-down arm). Phases 16 and 17 are this phase's own out-of-scope findings.

## Carried forward

Unclosed discoveries from the previous plan; none blocking.

- [ ] **Phase 6** — 110 references to "`plan.md` phase N" across 42 files point at
      the wrong plan; `plan.md` rotates on archive and the citation gate cannot
      see them (no line number). `server/main.ty` alone has 13.
- [ ] **Phase 7** — `ncpu()`'s spec definition (`docs/spec/16-builtins.md:251`,
      "the `parallel for` fan-out width") is false above 64, measured.
- [ ] **Phase 8** — every `tests/reject/` fixture carrying a `package` header is
      scored against the whole directory; affects `tests/run.sh` equally.
- [x] **Phase 9** — *(closed by the previous plan's phase 4; kept for the record)*
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

- [ ] **Phase 15** — filed by phase 3, measured not guessed. Shutdown is clean but
      its **latency is bounded by `--idle-ms`, not by the signal**, when workers
      are busy. `accept_loop` (`server/main.ty:477-495`) can only notice a
      shutdown between accepts; a worker inside `serve_conn`
      (`server/main.ty:364-473`) stays in its keep-alive loop until `alive` goes
      false, and for an idle client that means waiting out `SO_RCVTIMEO` and
      taking the `Err(httpd.Timeout)` arm at `server/main.ty:400-401`. Measured
      with `--workers 4 --idle-ms 5000`: 0 or 1 held keep-alive clients → 1 ms;
      **4 held clients → 5141 ms**. (The 1-client case is fast only by luck of
      routing — phase 1's settled fact 2 delivers SIGTERM to the main thread,
      which is accept loop 1, so `EINTR` released the one busy loop directly.)
      Exit is still 0 with the served-count line in every case, so this is slow,
      not hung, and `MAX_REQS` plus the idle timeout bound it. The fix already
      has its API: `signal.shutdown_requested()`
      (`corelib/signal/signal.ty:64-65`) exists for exactly this and **has no
      caller in the tree** — testing it in `serve_conn`'s loop condition and in
      `accept_loop`'s would cut a busy shutdown to one in-flight request. Left
      out of phase 3 deliberately: it changes the serving path, which is a wider
      blast radius than installing a handler, and the gate would need a held-open
      client to prove it.

- [ ] **Phase 16** — filed by phase 4. `core:signal` is **absent from
      `docs/guides/corelib.md`**, the non-normative companion the spec's older
      §32 entries cite by line. Verified rather than assumed: `grep -n signal
      docs/guides/corelib.md` returns exactly one hit, `128+signal` at
      `docs/guides/corelib.md:301`, inside `os.run`'s exit-code description — no
      package entry. The guide's `## Packages` list
      (`docs/guides/corelib.md:54-393`) and its C-shim section
      (`docs/guides/corelib.md:393-410`, which names `regex` as the libc-only
      example) both stop short of it. §32.27 was therefore written to close on
      `Source corelib/signal/signal.ty`, the form the newer entries (`net`,
      `bignum`, `decimal`, `compress`, `image`, `tls`) already use, so the spec
      does not cite a paragraph that does not exist. Not blocking: the guide is
      non-normative and the package's own header is the fullest description in
      the tree. Two paragraphs, plus a line in the C-shim section noting that
      `signal` is the third libc-only shim after `os` and `net`.

- [ ] **Phase 17** — filed by phase 4, and caused by this plan. Phase 3's
      `import "core:signal"` at `server/main.ty:61` shifted every line below it by
      one, staling bare citations into that file. `server/README.md`'s six were
      repaired in phase 4 (three of them re-anchored `@token` so the next shift
      reddens the gate); **seven comments in `server/run.sh` were not**, because
      that file is outside phase 4's scope: `server/run.sh:10` (`:604-614`),
      `:13` (`:513`), `:20` (`:616`), `:211` (`:426-436`), `:336` (`:499-504`),
      `:374` and `:392` (both `:342-352`). Each is a source→source citation, which
      `scripts/check_citations.py` checks for **bounds only**, so all seven are
      green while pointing one line high — the exact silent-drift class the gate's
      own docstring calls out. The repair is mechanical (+1, then re-anchor the
      single-line ones); the reason it is a phase and not a footnote is that
      `server/run.sh` is the server gate and a comment that misnames a line is how
      the next reader mis-attributes a failure.

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

- [ ] **Phase 18 — the zed README's corpus count has now reddened `make ci` four
      times, and the fix is always the same two keystrokes.**
  - Firings: 462→813 (2026-07-29), 837→845, 845→846, 846→848 (2026-07-31, this
    plan's phase 2 adding `corelib/signal/signal.ty` and
    `corelib/test/signal/main.ty`). The lane is **correct every time** — that is
    not the problem.
  - Every firing had the same cause: ordinary work added a `.ty` file. Nobody
    adding a corelib package thinks to update a number in an editor plugin's
    README, and nobody ever will. `scripts/editors_check.sh:17-21` already says
    this in its own words — "a number a human must remember to update is not a
    verified claim, it is a decaying one" — and then requires exactly such a
    number.
  - **The claim's value does not depend on N.** What a reader wants to know is
    "the grammar was verified over the whole tracked corpus", which is true at
    813 and at 848. The number adds precision nobody needs and a maintenance
    obligation everybody forgets.
  - Two honest fixes, and the second is probably right:
    1. Have the script **rewrite** the README line when it disagrees, rather than
       failing. Turns a red build into a diff — but a gate that edits the tree it
       checks is a new precedent in this repo and worth thinking about.
    2. Change the claim to name **no number**, and have the gate assert the
       sentence exists and that the corpus parsed. Needs a matching change to
       `scripts/editors_check.sh:70-81`, which currently errors with
       `NO COUNT FOUND` and instructs the reader not to reword it.
  - Not done here: this is a gate design decision, not a count fix, and this
    plan's phase 4 was documentation-only. Cost either way: under an hour.

## Cleanup batches — how the remaining nine phases are being run, 2026-07-31

Phase 9 was already closed by the previous plan and only its checkbox was open;
ticked, not worked. The nine real items are grouped into **four batches**, each
independently verifiable and each committing once. Every phase keeps its own
entry and checkbox; a batch ticks what it closes.

| batch | phases | subject |
|---|---|---|
| A | 14, 15 | **the server's two measured behaviour gaps** — `accept_loop` retiring a worker on *any* `Err` so a transient `EMFILE` silently drains the pool, and shutdown waiting a full `--idle-ms` (5141 ms measured) when workers are parked in `serve_conn` |
| B | 7, 10, 11, 12 | **spec and guide corrections from the concurrency work** — `ncpu()`'s definition false above 64, §22 never describing `send` from a `parallel for` body, the guide pointing at a fixture that contradicts it, and `iter.map`'s single type variable |
| C | 6, 8 | **the two structural items** — 110 "`plan.md` phase N" references pointing at the wrong plan across 42 files, and `tests/reject/` fixtures with a `package` header scored against the whole directory |
| D | 16, 17, 18 | **documentation and gate hygiene** — `core:signal` missing from the corelib guide, seven stale source→source comments this plan caused in `server/run.sh`, and the zed README count that has now reddened CI four times |

**Order is deliberate.** A first because it is real behaviour and the rest is
description; D last because phase 18 is a gate *design* decision and the tree
should be otherwise settled before a gate changes shape.
