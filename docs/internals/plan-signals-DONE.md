# Signal handling, driven by the server's unreachable shutdown

Previous plan complete and archived at
[plan-prunner-DONE.md](plan-prunner-DONE.md)
(four phases plus Phase 13; `make test-fast` ships as an advisory lane). Its
seven still-open phases are carried forward at the bottom of this file.

## Goal

`server/main.ty@stopped` prints `"tycho-httpd: stopped after N requests"` and is
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
  `server/main.ty:493-494` [SUPERSEDED: construct deleted in batch A — do not repoint] sets `running = false` in the `Err` arm of accept,
  commented "listener closed: wind down". The served count returns at
  `server/main.ty@stopped`. **No new control flow is needed in the server** — the
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
  N workers are pthreads. In fact `server/main.ty@total` calls `worker(...)`
  *directly*, and `worker` at `server/main.ty@remaining` spawns worker k+1 and then
  runs its own `accept_loop`. So with `--workers 4` the process has **the main
  thread running accept loop 1 plus three spawned threads** — not four spawned
  threads with an idle main. This changes the answer to both settled facts
  below, so the probe was rewritten to match. The probe's shape is otherwise
  copied from the tree: one shared listening fd (`server/main.ty:605-612`), a
  bare `accept()` with no EINTR retry (`corelib/net/net_shim.c:162-167`),
  `pthread_create` with a NULL attribute and no `pthread_sigmask`
  (`runtime/tycho_rt.c:577`), and SIGTERM delivered process-directed from a
  forked child rather than raised by the thread that would then handle it.

  The probe's only non-server thread is a watchdog that reports and `_exit`s
  when a loop hangs; SIGTERM is blocked in that thread alone so the scaffolding
  cannot become a candidate receiver and perturb the measurement. Hangs are
  detected with `pthread_timedjoin_np` and a 3 s deadline — the server's main
  thread really does join its peers (`server/main.ty@wait`), so a loop that never
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
  at `server/main.ty:493-494` [SUPERSEDED: construct deleted in batch A — do not repoint]. `shutdown()` is on this system's
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
    `server/main.ty:493-494` [SUPERSEDED: construct deleted in batch A — do not repoint] sets `running = false` on any `Err`. **Every worker
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
    choice; the smallest thing that makes `server/main.ty@stopped` reachable is the
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
  contract. None of that is needed to reach `server/main.ty@stopped`, and the tree has
  exactly one caller. The header of `corelib/signal/signal.ty` carries the same
  list so the next reader does not have to find this file.

  Two functions rather than one: `on_shutdown` is what phase 3 needs, and
  `shutdown_requested` is what makes the fixture able to prove the handler *ran*
  rather than only that `accept` later failed. It costs one `sig_atomic_t`.

  #### The handler, statement by statement, against the POSIX safe list

  `corelib/signal/signal_shim.c:144-156`. `man 7 signal-safety` on this system is
  the list being cited.

  | line | statement | why it is safe |
  |---|---|---|
  | `corelib/signal/signal_shim.c:146@clobber` | `int saved = errno;` | reads a thread-local `int`; no call. Saved because `shutdown()` may set `errno` and the interrupted thread is entitled to its own value |
  | `corelib/signal/signal_shim.c:148@sigx_flag` | `sigx_flag = 1;` | store to a `volatile sig_atomic_t` — the one object type POSIX permits a handler to write while the rest of the program reads it |
  | `corelib/signal/signal_shim.c:149@sigx_fd` | `int fd = (int)sigx_fd;` | load from a `volatile sig_atomic_t`, same guarantee. Not a TLS read: phase 1's `__thread` histogram was instrumentation and is deliberately not shipped |
  | `corelib/signal/signal_shim.c:150@SHUT_RDWR` | `shutdown(fd, SHUT_RDWR)` | **on the POSIX async-signal-safe list.** A bare syscall: allocates nothing, takes no userspace lock the interrupted thread could already hold |
  | `corelib/signal/signal_shim.c:155@saved` | `errno = saved;` | store to a thread-local `int` |

  No `malloc`, no stdio, no `pthread_*`, no arena touch — so there is no lock for
  the handler to deadlock the interrupted thread against, which was the stated
  constraint rather than a formality. `sigemptyset`/`sigaction`/`memset`
  (`corelib/signal/signal_shim.c:201-207`) run in `sigx_on_shutdown`, ordinary
  code, not in handler context.

  Two orderings that are load-bearing and are commented at the site:

  - The fd is stored (`corelib/signal/signal_shim.c:200@sigx_fd`) **before** the first
    `sigaction`, so no signal can find a handler installed and a stale or absent
    descriptor. `-1` means "registered nothing", and the handler then only sets
    the flag.
  - `sa.sa_flags = 0` (`corelib/signal/signal_shim.c:205@sa_flags`) — **no `SA_RESTART`**,
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
  (`corelib/signal/signal_shim.c:206-207`); SIGINT gets its real exercise in
  phase 3's server gate.

  What the fixture proves, and what it does not. It proves the handler installs,
  fails closed on a bad fd, actually fires, and leaves the listener in a state
  where `accept` returns `Err` instead of blocking — which is exactly the arm
  `server/main.ty:493-494` [SUPERSEDED: construct deleted in batch A — do not repoint] already winds down on. It does **not** prove all N
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
    `server/main.ty:493-494` [SUPERSEDED: construct deleted in batch A — do not repoint] and the count already returns at `server/main.ty@stopped`.
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
  `server/main.ty:742-744` — `if not signal.on_shutdown(srv)`, warning on stderr
  and continuing, because a server that could not arm its handler is still a
  working server and `on_shutdown` already fails closed to the old behaviour.

  No new control flow, exactly as the phase brief predicted. The chain is
  entirely pre-existing: handler → `shutdown(srv, SHUT_RDWR)` → every blocked
  `net.accept` returns `Err` → `accept_loop`'s `Err` arm at
  `server/main.ty:591-592` sets `running = false` → `worker` unwinds through
  `wait(peer)` → the stopped line, now at `server/main.ty@stopped`.

  **Why there and nowhere else**, the three constraints written at the site:

  - **After `net.listen`** (`server/main.ty:712-714`) — `on_shutdown` takes the
    listening fd; before that call there is no fd to register.
  - **Before the fan-out** (`server/main.ty@total`). Phase 1's correction to the
    thread model is the whole reason this needs saying: `main` *calls*
    `worker(cfg, srv, 1, cfg.workers)` rather than spawning it, and `worker`
    (`server/main.ty:606-611`) spawns peer k+1 then runs accept loop k itself. So
    main **is** accept loop 1 and does not return from that call until the entire
    pool has wound down. Everything below it is shutdown code; there is no later
    point in `main` that runs at startup.
  - **Before the banner** (`server/main.ty:746-750`), which is the readiness
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
  `corelib/signal/signal_shim.c:206-207`; this gate is the only exercise the
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
     `n + wait(peer)` (`server/main.ty:606-611`) and `main` calls it directly, so
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
    per line into the shim (`corelib/signal/signal_shim.c:148@sigx_flag`,
    `:149@sigx_fd`, `:150@shutdown`) so the citation gate re-checks the mapping on
    every future edit to that file.
  - Both load-bearing orderings **MUST** be preserved: the descriptor registered
    before the first `sigaction` (`corelib/signal/signal_shim.c:200@sigx_fd`), and
    `sa_flags = 0` (`corelib/signal/signal_shim.c:205@sa_flags`).
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

**What shipped.** `server/main.ty@stopped` — `tycho-httpd: stopped after N requests`,
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

- [x] **Phase 6** — 110 references to "`plan.md` phase N" across 42 files point at
      the wrong plan; `plan.md` rotates on archive and the citation gate cannot
      see them (no line number). `server/main.ty` alone has 13.
      *(batch C: 167 refs over 43 files, not 110 over 42 — the count missed the
      backticked spelling. Each rewritten to name the plan it meant; the rule
      written into `CLAUDE.md` — see below)*
- [x] **Phase 7** — `ncpu()`'s spec definition (`docs/spec/16-builtins.md:251`,
      "the `parallel for` fan-out width") is false above 64, measured.
      *(batch B: definition corrected and the 64 cap written down — see below)*
- [x] **Phase 8** — every `tests/reject/` fixture carrying a `package` header is
      scored against the whole directory; affects `tests/run.sh` equally.
      *(batch C: measured — **0 of 249** carry one, so nothing is passing for the
      wrong reason today. The arrangement that prevents it is now enforced in
      both runners rather than merely described — see below)*
- [x] **Phase 9** — *(closed by the previous plan's phase 4; kept for the record)*
- [x] **Phase 14** — filed by phase 1. `accept_loop` treats *every* `Err` from
      `net.accept` as "listener closed" and sets `running = false`
      (`server/main.ty:493-494` [SUPERSEDED: construct deleted in batch A — do not repoint]), because `netx_accept`
      (`corelib/net/net_shim.c:162-167`) collapses every failure to `-1` and the
      errno is not carried into Tycho. A transient, per-call error — `EMFILE`,
      `ENFILE`, `ECONNABORTED`, `EPROTO`, all of which POSIX says to retry —
      therefore **retires that worker permanently and silently**, and the server
      keeps running with fewer accept loops than it reports in its banner
      (`server/main.ty@banner`). Under fd exhaustion the workers would drain one at
      a time until the process quietly stops serving. This is pre-existing and
      not caused by the signal work; phase 1 found it while establishing that
      `EINVAL` reaches the wind-down arm, which is the same collapse working *in
      our favour*. Fixing it means carrying errno across the shim boundary, which
      is a wider change than this plan.

- [x] **Phase 15** — filed by phase 3, measured not guessed. Shutdown is clean but
      its **latency is bounded by `--idle-ms`, not by the signal**, when workers
      are busy. `accept_loop` (`server/main.ty:547-603`) can only notice a
      shutdown between accepts; a worker inside `serve_conn`
      (`server/main.ty:375-516`) stays in its keep-alive loop until `alive` goes
      false, and for an idle client that means waiting out `SO_RCVTIMEO` and
      taking the `Err(httpd.Timeout)` arm at `server/main.ty:442-443`. Measured
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

- [x] **Phase 16** — filed by phase 4. `core:signal` is **absent from
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
      *(batch D: done. The two `docs/guides/corelib.md` ranges above are the
      AS-FOUND ones — the entry was appended at the END of the `## Packages`
      list, so that list is now `docs/guides/corelib.md:54-416` and the C-shim
      section `docs/guides/corelib.md:416-433`. The "third libc-only shim after
      `os` and `net`" framing is FALSE and was not written: there are seven.
      See the evidence block below.)*

- [x] **Phase 17** — filed by phase 4, and caused by this plan. Phase 3's
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

- [x] **Phase 10, 11, 12** — filed by the previous plan's phase 3 from the
      concurrency write-up: spec §22 never describes `send` from a `parallel for`
      body, `docs/guides/concurrency.md:104` points at a fixture that contradicts
      it, and `corelib/iter/iter.ty:8`'s `map` is `[$T] -> [$T]` so it cannot
      change element type.
      *(batch B: §22.1 added, the guide re-pointed at `tests/conc/parfor_chan.ty`,
      `map` widened to `[$T] -> [$U]` with a fixture. The three line numbers above
      are the AS-FOUND ones and no longer resolve to the quoted text — batch B
      moved all three; see the evidence block below for where each went.)*

## Out of scope

- **A general signal API.** If phase 2 concludes the narrow surface is right, a
  full `signal.on(sig, handler)` is a separate plan with its own re-entrancy and
  threading questions.
- **Windows.** The tree is POSIX-only throughout; the shim follows suit.
- **`make test-fast`'s hung-job blind spot**, recorded in the previous plan: a
  hung fixture gives `rc=124` with nothing printed. Unrelated to signals in the
  server, though the same mechanism might eventually help it.

- [x] **Phase 18 — the zed README's corpus count has now reddened `make ci` four
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

### Batch A evidence — phases 14 and 15, 2026-07-31

Two files: `server/main.ty` and `server/run.sh`. `corelib/net/` was **not**
touched, and the next paragraph is why.

#### The errno-surface decision: none, and the reason is not thrift

Phase 14's entry says fixing it "means carrying errno across the shim". It does
not. The obvious change — an `inout status` on `netx_accept` the way
`netx_read` already has one (`corelib/net/net.ty:102`), new `NetErr` variants,
a spec entry for each — was rejected because **the server does not need to know
which error it got.** It needs one bit: is this the shutdown, or is it not?

That bit already had an API and no caller. `signal.shutdown_requested()`
(`corelib/signal/signal.ty:64-65`) reads the flag the handler sets, and
`corelib/signal/signal_shim.c:148-150` sets that flag **before** it calls
`shutdown(fd)` — so an accept released by the shutdown, or interrupted by
`EINTR` on the way to it, observes the flag already true. The discriminator is
exact for the case that matters, and it costs no FFI surface, no `NetErr`
variant and no spec text. `server/main.ty@shutdown_requested` is the whole decision.

The residual risk is honest and is insured rather than ignored: the flag is a
`volatile sig_atomic_t` read from a thread other than the one the handler ran
on, which is not by itself a cross-thread ordering guarantee. If a worker ever
read a stale 0 it retries, the next accept on a shut-down listener fails at once
(phase 1 measured `EINVAL` there), and the flag is re-read every time round —
so the retry cap at `server/main.ty@MAX_ACCEPT_FAILS` guarantees the wind-down even if the
fast path were missed. Cap and backoff are `server/main.ty:82-83`: 100 failures
20 ms apart, i.e. two seconds of unbroken failure before a loop retires, and it
retires *loudly* now. Sustained fd exhaustion past two seconds still drains the
pool; that is a bound, not a fix, and it is stated rather than papered over.

#### Where the phase 15 check went, and why there

`server/main.ty@shutdown_requested` — `serve_conn`'s loop condition, and nowhere else in that
function. It is the one point where nothing is in flight: the previous response
is written and logged, the next blocking read has not started. Testing there
means a worker never *commits* to another `read_request_capped` once shutdown is
under way, which is the entire cost, because that read blocks for up to
`cfg.idle_ms` and the loop cannot be re-entered until it returns. Deeper would
abandon a request mid-answer; shallower (`accept_loop` only, which also tests it
now) never sees a kept-alive connection at all.

#### Both measurements

Phase 15, same method as phase 3 — `kill(2)` to `wait(2)` returning, `--workers 4
--idle-ms 5000`:

```
                                              before      after
0 idle keep-alive clients                       1 ms       1 ms
4 idle keep-alive clients (phase 3's case)   4924 ms    4733-4901 ms   UNCHANGED
4 BUSY keep-alive clients (100ms drip)     102215 ms       6-8 ms
```

**The parked-idle row did not move, and the entry's headline claim was
incomplete.** A worker already blocked in `read_request_capped` when the signal
lands stays blocked until `SO_RCVTIMEO` expires: nothing wakes a `recv` on a
*connection* fd, because the handler shuts down the *listener*
(`corelib/signal/signal.ty:38-43`). The loop condition cannot run until that read
returns, so no test of it can help this case. Filed as phase 19 below rather than
claimed. Phase 3's 5141 ms reproduced here at 4924 ms on the unpatched binary.

What the check did find is a worse case than the one it was filed for: a client
that keeps its connection *busy* held the worker for **102215 ms** — `MAX_REQS`
requests served at the client's own pace, because nothing told the loop to stop.
That is 102 s of shutdown latency hiding behind the 5 s that was reported, and it
is now 6-8 ms.

Phase 14, against the unpatched and patched binaries, 3 runs each. A transient
`EMFILE` window is induced with `prlimit(2)` on the live process and then lifted:

```
unpatched:  process alive after the window = False   serves a 200 = no
            exit status without any signal = 0       "stopped after N requests"
patched:    process alive after the window = True    serves a 200 = yes
            then SIGTERM: rc=0, shutdown = 1 ms
```

Unpatched, the whole pool retires on a transient error and the server walks out
of its own bottom **with no signal sent and nothing reported** — which is exactly
the silent drain the entry predicted. Patched, it survives and a real shutdown
still works, which is the half a retry loop can quietly cost.

**A storm of connections cannot induce this and it is worth recording why.** With
four accept loops the server never holds more than four connections at once, so
its own fd budget is never what runs out; the first attempt at this measurement
used 80 sockets and proved nothing. Nor is lowering the limit under four *parked*
accept loops enough: `__sys_accept4` reserves the descriptor with
`get_unused_fd_flags()` and only then blocks in `do_accept()`, so those four calls
have already passed the rlimit check and complete normally — measured, they
install fds 4..7 over a soft limit of 4. `EMFILE` is reached only when a loop
enters `accept(2)` *afresh*, one idle timeout later. That is why the gate case
runs at `--idle-ms 200` with a 0.8 s window, and why a 400 ms window against
`--idle-ms 500` reddens for neither binary.

#### Gates — real output

`server/run.sh` grew **three** assertions, 57 → 60, and each was proved against
the unpatched binary first: without `server/main.ty`'s change, `transient-accept`
and `after EMFILE` and `SIGTERM under load` all FAIL, twice out of two.

```
  ok   transient accept failure (EMFILE, window lifted): server still serving
  ok   after EMFILE: SIGTERM still exits 0 with the stopped line (retry did not eat the shutdown)
  ok   SIGTERM under 4 busy keep-alive clients: exit 0 well inside the 10s watchdog
server: OK
```

- `make -s server-check`: **server: OK**, 60 assertions.
- 10-run loop: **10/10 OK**, 60 assertions every run.
- `make test`: **passed: 560   failed: 0** — unchanged, as it must be; nothing
  outside `server/` moved.
- `python3 scripts/check_citations.py`: ok (186 anchored, 2743 bare in bounds,
  140 source→doc, 240 source→source in bounds, 12 source→source anchored).

`make ci` was deliberately **not** run: per `CLAUDE.md` it is the closing sweep,
not a per-batch confirmation.

**Gate cost.** `sh server/run.sh` went 4.2 s → 7.0 s. The two new cases are
sleep-bound and cannot be made much cheaper: one has to outlast an idle timeout
to reach `accept(2)` at all, the other has to let four workers pick up a
connection before the signal. Recorded rather than hidden, because
`CLAUDE.md`'s gate table quotes `make server-check` at ~4s and that number is now
wrong by 3 s.

- [x] **Phase 19** — filed by batch A, measured. Shutdown latency for a worker
      **already parked in a blocking read** is still one `SO_RCVTIMEO`: 4733 ms
      with `--idle-ms 5000`, unchanged by phase 15, because
      `server/main.ty@shutdown_requested`'s check cannot run until `read_request_capped`
      returns and nothing wakes a `recv` on a connection fd — the handler shuts
      down the *listener* only (`corelib/signal/signal.ty:38-43`). Two ways out,
      neither small enough for batch A. (a) Let the handler shut down accepted
      connection fds too, which means an async-signal-safe fd table in
      `corelib/signal/signal_shim.c` and a way for `serve_conn` to register and
      retire each fd. (b) Slice the read: arm `min(idle_ms, poll)` and re-check
      the flag per slice, keeping a cumulative idle budget. (b) was prototyped
      on paper and rejected for now — `read_request_capped` returns its partial
      `raw` and cannot be resumed, so a head split across two slices would be
      re-parsed from the second slice, and a slice timeout with bytes in `raw`
      turns into a 408 after the *slice* rather than after the idle timeout,
      which `server/run.sh:262`'s "408 fired near the idle timeout" assertion
      pins at 400..3000 ms. Bounded and exit-0 either way, so this is still slow,
      not hung.

- [x] **Phase 20** — filed by batch A, out of scope and pre-existing. The
      watchdog at `server/run.sh:346` is `( sleep 10; kill -KILL ... ) &`, and
      `kill "$WD"` reaps the subshell but not the `sleep` it is blocked in. The
      orphan keeps the stdout this script inherited, so a caller that *captures*
      output — `out=$(sh server/run.sh)`, which is how a CI step collecting a log
      would do it — blocks until the sleep expires rather than until the script
      exits. Measured: 4.4 s per run direct, 34.5 s per run captured when a 30 s
      watchdog was briefly in the file. Batch A's own three watchdogs are already
      written `) >/dev/null 2>&1 &`, which costs nothing and removes the orphan's
      grip; `server/run.sh:346` was left alone only because it is not batch A's
      scope. One redirect closes it.

### Batch B evidence — phases 7, 10, 11 and 12, 2026-07-31

Six files: `docs/spec/16-builtins.md`, `docs/spec/13-concurrency.md`,
`docs/spec/appendix-f-impl-defined.md`, `docs/spec/18-library.md`,
`docs/guides/concurrency.md`, `docs/guides/corelib.md`, plus the one code change
in `corelib/iter/iter.ty` with its fixture. Every claim below was produced by
compiling a program, not by reading a document — which is how three of the four
items were found in the first place.

#### Phase 7 — `ncpu()` is not the fan-out width, and the cap was written nowhere

**What the compiler does.** A scratch program printing `ncpu()` and running a
`parallel for i in 0..<1000` reduction, transpiled and run:

    $ ./p7                       ncpu=16    sum=499500
    $ TYCHO_THREADS=100 ./p7     ncpu=100   sum=499500

and the emitted C for that same statement, three consecutive lines:

    tycho_int _pk = tycho_ncpu();
    if (_pk > _phi - _plo) _pk = _phi - _plo;
    if (_pk < 1) _pk = 1; if (_pk > 64) _pk = 64;

So `ncpu()` returned **100** while the fan-out was **64**. The generator lines
are `src/tychoc.c:10038`, `src/tychoc.c:10039` and `src/tychoc.c:10040`; the
value `ncpu()` returns is `runtime/tycho_rt.c:847-852` (`TYCHO_THREADS` first,
else `sysconf(_SC_NPROCESSORS_ONLN)`). The reason the number is 64 and not
something else is one line further on: `src/tychoc.c:10041` emits a fixed
`HTask *_pts[64]` chunk-handle array.

**The cap was undocumented — verified, not assumed.** `grep -n '64'` over
`docs/spec/13-concurrency.md` and `docs/spec/16-builtins.md` returned only
unrelated hits (`to_i64`, `base64`, provenance line numbers), and the same
search over `docs/guides/concurrency.md` and `docs/reference/concurrency.md`
returned nothing at all. The clamp existed in `src/tychoc.c` and in no document
in the tree.

**Decision: correct the definition AND document the cap. Neither of the other
two options was available to this batch, and one of them is wrong anyway.**

- *Make `ncpu()` return the clamped value* — a `src/tychoc.c` change, outside
  batch B's scope, and **wrong on the merits**: `ncpu()` also answers "how many
  CPUs does this box have", which is what `tools/prunner/main.ty` and every
  hand-rolled `spawn` pool want. Clamping it to a `parallel for` implementation
  limit would make the builtin lie in the other direction.
- *Lift the 64* — also `src/tychoc.c`, and not free: `src/tychoc.c:10041` is a
  fixed-size array in emitted C, so lifting it means a heap allocation and a
  free path per `parallel for` site. A real proposal, not a docs fix.
- *Say what is true* — chosen. `docs/spec/16-builtins.md:251` now defines
  `ncpu()` as online CPUs / `TYCHO_THREADS`, states that this is the
  **requested** width and not necessarily the achieved one, and says a program
  sizing anything from `ncpu()` MUST NOT assume that many chunks run.
  `docs/spec/13-concurrency.md:81-84` states the chunk count as `min(ncpu(), N)`
  with an implementation-MAY upper bound, reference = 64.
  `docs/spec/appendix-f-impl-defined.md:46` — which already listed the worker
  count as implementation-defined — now also requires the bound to be documented.

#### Phase 10 — §22 said the opposite of what the compiler does

**What the compiler does, part 1: a captured channel is not copied.** A scratch
program with 200 `send`s from a `parallel for` body into one channel drained by
a spawned task:

    parallel for i in 0..<200:
        send(out, i * i)
    ...
    total=2646700

2646700 is exactly `sum(i*i, i=0..199)`, so all 200 items crossed **one** queue.
The emitted C shows why, verbatim: the channel capture is `_sa->a2 = h_out;` —
the raw handle, with no `copy_into` wrapper, unlike every heap capture.
`src/tychoc.c:10051` copies a capture only `if (type_is_heap(ct))`, and
`type_is_heap` (`src/tychoc.c:1318-1340`) has no channel arm, so it returns 0
for `Channel(T)`.

**What the compiler does, part 2: `recv` works too, plainly.** A second program
with a bare `match recv(jobs):` in a `parallel for` body over 64 produced items
printed `s=2016 n=64` — `sum(0..63)`, every item taken exactly once. So the
construct is not select-only; the guide's worked idiom just happened to use
`select`.

**What the document said.** `docs/spec/13-concurrency.md:81-82` (as found):
"Each chunk's captured values are deep-copied into it." Read as written, that
gives every chunk a private queue and makes both programs above impossible. The
correct rule existed only in the non-normative guide
(`docs/guides/concurrency.md:154-155` as found, now
`docs/guides/concurrency.md:168`).

**Decision: carve the exception into the normative sentence, and promote the
guide's rule to a numbered subsection.** The deep-copy sentence at
`docs/spec/13-concurrency.md:86-90` now states the `Channel(T)` exception and
says an implementation MUST NOT deep-copy a captured channel — phrased as a
requirement rather than a description, because a second implementation reading
only the spec is exactly the reader who got this wrong. New
`docs/spec/13-concurrency.md:121@### 22.1` covers `send` from the body (not an
outer-scope write, not subject to the reduction rule), `recv` from the body
(exactly-one-chunk, hence deterministic integer reductions), the `parallel for x
in ch:` equivalence including the producer's obligation to `close`, and that
§22's fail-closed exits are unchanged inside such a body.

**A stale provenance range was fixed on the way past.** §22's provenance cited
the fan-out as `src/tychoc.c:9995-10006`; `gen_parfor` is at
`src/tychoc.c:10030-10069` and the fan-out is `src/tychoc.c:10038-10039`. The
old range passed the gate because a bare range is only bounds-checked. It now
cites the three lines it means, anchored where a single line is named.

#### Phase 11 — the guide pointed at the desugaring, and said so itself

`docs/guides/concurrency.md:104` (as found) closed the bounded-fan-out section
with "Worked example: `tests/conc/workers.ty`". `tests/conc/workers.ty:2` says
of itself that it is "the pattern `parallel for x in ch:` sugars over" — the
manual form, written with `select`/`recv` and a `0..<100` the reader has to size
by hand. The fixture that actually demonstrates the sugar is
`tests/conc/parfor_chan.ty`, whose `parallel for x in jobs:` is at
`tests/conc/parfor_chan.ty:16`; the guide never named it. This is not
hypothetical damage: the previous plan's phase 1 read `workers.ty` and copied
the manual idiom into `tools/prunner/main.ty`.

**A correct worked example does exist** — checked, not assumed. Both fixtures
are live in `tests/conc/` and both are scored by `make test` (560/0 below).
`docs/guides/concurrency.md:108-112` now names `tests/conc/parfor_chan.ty` as
the worked example of the sugar and demotes `tests/conc/workers.ty` to "the
manual form the sugar replaces — read it to see what the sugar saves", citing
the fixture's own header line so the next reader cannot repeat the mistake. The
same section also gained the `send`-from-the-body paragraph (phase 10's guide
half) and phase 7's correction to the `ncpu()` parenthetical, which previously
called it "the fan-out width" in the same words the spec did.

#### Phase 12 — a FIX, not a limitation: the second type variable already works

**The unknown named by the original entry was "whether inference reaches a
function-typed `fn($T) -> $U` parameter". It does.** Tested before touching the
library, with a standalone program that does not import `core:iter` at all:

    fn map2(xs: [$T], f: fn($T) -> $U) -> [$U]: ...
    ys := map2([1, 2, 3], to_str)      # to_str: fn(int) -> string
    println(ys[0] + ys[2])             # printed: 13

It compiled and ran first time. So this was never a compiler item, and the
earlier note that it *might* be one is now answered: the blocker was one type
variable in one signature in `corelib/iter/iter.ty`, nothing more.

**Decision: widen it.** `corelib/iter/iter.ty:14` is now
`fn map(xs: [$T], f: fn($T) -> $U) -> [$U]`. Same-type calls are unaffected —
inference binds `$U` to `$T` — and that is proven rather than argued: the seven
pre-existing lines of `corelib/test/iter.out` are byte-identical and
`examples/corelib/iter.out` `cmp`s clean untouched. The change is a pure append
of three lines to the golden.

**The fixture is deliberately three cases, in `corelib/test/iter/main.ty`.** A
named fn (`itos`, `int -> string`), a closure to a different scalar
(`int -> float`), and the reverse direction (`string -> int`) — so a regression
to one type variable cannot pass any of them, and the "named fn" case is the one
the original report measured failing:

    i2s=1,2,3,4,5
    i2f=0.5,1.0,1.5,2.0,2.5
    s2i=2,0,2,0

`filter`, `reduce`, `count` and `any` were left alone: `filter`'s predicate is
already `fn($T) -> int` and the others are genuinely type-preserving.
`docs/spec/18-library.md:137-141` and `docs/guides/corelib.md:89-90` now say
which of the five may change element type and which may not.

**`docs/guides/corelib.md` was edited to keep its line count exactly the
same** (1 line replaced by 1 line, `git diff --numstat` = `1 1`). This was
deliberate: ~30 refs in `docs/spec/18-library.md` and two in
`docs/spec/appendix-h-differences.md` cite that file by line range, and a
three-line insertion would have silently invalidated every one below the
insertion point while still passing the gate, since bare ranges are
bounds-checked only.

#### Gate output — all five, foreground, one per command

    $ python3 scripts/check_citations.py
    citation check: ok (190 anchored contain the token they name, 2752 bare in
    bounds, 140 source->doc citations resolve, 240 source->source in bounds,
    12 source->source anchored)

    $ sh scripts/check_links.sh
    link check: ok (136 markdown files, no dead relative links)

    $ sh scripts/spec_check.sh
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 9 runnable example(s), all pass

    $ make -s corelib
    corelib: all green (tychoc matches goldens)

    $ make test
    passed: 560   failed: 0
    all green

560 as expected — no fixture gained or lost. `make test` and `make -s corelib`
were run because `corelib/iter/` changed; the three doc gates cover everything
else. `make ci` was **not** run: the user runs the closing sweep after the
batches.

**One cheap extra check, outside the five.** `examples/corelib/iter/main.ty`
calls `iter.map` twice and is scored by `make corelib-examples`, not by any of
the five gates above. Rather than spend the whole lane, that one program was
compiled and `cmp`ed against `examples/corelib/iter.out` directly: identical.

- [x] **Phase 21** — filed by batch B, and caused by it. Batch B's insertions
      moved line numbers in four spec/guide files, and **exactly 20 citations
      from OUTSIDE batch B's scope now point at the wrong lines**. None of them
      redden `scripts/check_citations.py`, because a bare range is only
      bounds-checked — which is precisely why this needs a phase rather than
      being left to the gate. The shift bands are mechanical:
      `docs/guides/concurrency.md` old ≥105 → **+13**;
      `docs/spec/13-concurrency.md` old 83..112 → **+8**, old 113..115 →
      **+36**, old ≥119 → **+41**; `docs/spec/16-builtins.md` old ≥261 →
      **+3**; `docs/spec/18-library.md` old ≥139 → **+3**. The 20 were
      enumerated by script, not estimated, and this is the whole list — citing
      file, then the ref it carries and the delta to add:

      | citing site | ref as written | + |
      |---|---|---|
      | `FRICTION.md:317` | `docs/spec/13-concurrency.md:86` | 8 |
      | `FRICTION.md:342` | `docs/spec/13-concurrency.md:91-92` | 8 |
      | `FRICTION.md:423` | `docs/spec/13-concurrency.md:100` | 8 |
      | `FRICTION.md:425` | `docs/spec/13-concurrency.md:110-111` | 8 |
      | `FRICTION.md:433` | `docs/spec/13-concurrency.md:127` | 41 |
      | `FRICTION.md:449` | `docs/spec/13-concurrency.md:91-92` | 8 |
      | `FRICTION.md:455` | `docs/guides/concurrency.md:154-155` | 13 |
      | `FRICTION.md:520` | `docs/spec/13-concurrency.md:165-167` | 41 |
      | `docs/rfc/ffi-threading-design-review.md:62` | `docs/guides/concurrency.md:137-139` | 13 |
      | `docs/rfc/ffi-threading-design-review.md:64` | `docs/guides/concurrency.md:141` | 13 |
      | `docs/rfc/ffi-threading-design-review.md:268` | `docs/guides/concurrency.md:137-139` | 13 |
      | `docs/rfc/ffi-threading-design-review.md:273` | `docs/guides/concurrency.md:114-117` | 13 |
      | `docs/rfc/ffi-threading-design-review.md:277` | `docs/guides/concurrency.md:122-132` | 13 |
      | `docs/rfc/ffi-threading-design-review.md:319` | `docs/guides/concurrency.md:141` | 13 |
      | `plan.md:336` | `docs/spec/18-library.md:263` | 3 |
      | `plan.md:337` | `docs/spec/18-library.md:285` | 3 |
      | `plan.md:664` | `docs/spec/18-library.md:263` | 3 |
      | `plan.md:664` | `docs/spec/18-library.md:285` | 3 |
      | `tools/prunner/main.ty:87` | `docs/spec/13-concurrency.md:100` | 8 |
      | `tools/prunner/main.ty:378` | `docs/spec/13-concurrency.md:127` | 41 |

      Two notes the next agent should not have to re-derive.
      `tests/diag/parfor_expr_source.ty:7` cites
      `docs/guides/concurrency.md:102`, which is **below** the insertion point
      and is still correct — checked, not skipped. And the four `plan.md` rows
      **feed phase 16**: it adds a `core:signal` section to
      `docs/spec/18-library.md` at exactly the coordinates those rows name, so
      fixing them before phase 16 runs is worth more than fixing them after.
      Not absorbed into batch B because `FRICTION.md`, `docs/rfc/`,
      `tools/` and `tests/` are all outside its scope lock. Verify:
      `python3 scripts/check_citations.py` plus a spot-read of three moved refs
      against the text they quote. Not `make test` — `tools/prunner/main.ty`'s
      two are inside comments, so no compiled artifact can move.

- [x] **Phase 22** — filed by batch B, out of scope and pre-existing.
      `docs/reference/builtins.md:75` carries the **same false `ncpu()`
      claim** batch B just corrected in the spec — "`parallel for` fan-out
      width (online CPUs; `TYCHO_THREADS` overrides)", the same assertion in
      slightly different words — and
      `docs/reference/concurrency.md:55` has "K = ncpu() chunk tasks
      (TYCHO_THREADS overrides)" in a code comment with no mention of the 64
      cap. `docs/reference/` is hand-written, not generated from
      `docs/spec/` (checked: no `docs/reference` target in `Makefile` or
      `scripts/`), so the correction does not propagate on its own. Left alone
      only because batch B's scope lock names `docs/spec/` and `docs/guides/`.
      Two sentences. Verify: the two doc gates.

- [x] **Phase 23** — filed by batch B, a stale source→source comment.
      `src/tychoc.c:3339` tells the reader the chunker is at
      "gen_parfor, `src/tychoc.c:9932`"; `gen_parfor` is at
      `src/tychoc.c:10030-10069`, so the pointer is 98 lines short and lands in
      unrelated codegen. It is bounds-valid, so the citation gate's
      source→source lane passes it. Same class as batch D's phase 17 (stale
      source→source comments in `server/run.sh`) but in a different file, so it
      is filed separately rather than folded in. One number. Verify:
      `python3 scripts/check_citations.py`.

### Batch C evidence — phases 6 and 8, 2026-07-31

Two structural items. Neither is a line-of-code bug; both are about how
something is arranged, and in both cases the honest first step was to **count**,
because the plan's own numbers were wrong in phase 6's case and phase 8's
premise turned out not to hold at all.

#### Phase 6 — the decision: name the archived document, not a convention

The item offered two end states. **Option (a) was taken**: every reference is
rewritten in place to name the archived plan it actually meant. The reasoning is
four measurements, not a preference.

**1. The population is bigger than recorded, and heterogeneous.** The item says
110 refs over 42 files. The real figure is **172 refs over 44 files**: the
original count matched `plan.md phase N` and missed the backticked spelling
`` `plan.md` phase N `` (66 of them) and the plural `` `plan.md` phases 1 and 2 ``
(5 more, found only because a stale line number in this very evidence block
forced a re-check). More important than the size is
the shape — those 167 refs mean **eight different plan documents**:

| refs | document |
|---:|---|
| 72 | `docs/internals/plan-loops-cleanup-DONE.md` |
| 52 | `docs/internals/plan-friction-DONE.md` |
| 27 | `docs/internals/plan-option-result-DONE.md` |
| 9 | `docs/internals/plan-prunner-DONE.md` |
| 5 | `plan.md` (this plan — correct as written, left alone) |
| 3 | `docs/internals/plan-postfreeze-rawstring-DONE.md` |
| 3 | `docs/internals/plan-webserver-gate-DONE.md` |
| 1 | `docs/internals/plan-webserver-DONE.md` |

**12 of the 44 files are heterogeneous** — their own references mean two to six
different plans. `src/tychoc.c` spans 2, `server/main.ty` 4, `FRICTION.md` 6.
This is what decides the question: a convention stated once cannot resolve a
reference whose meaning depends on which commit wrote it, so option (b) would
have left every reader running `git blame` on every citation.

**2. This repo already tried option (b), and it did not hold.** `FRICTION.md`
carried a key added 2026-07-30 asserting that all of its "`plan.md` phase N"
refs meant `docs/internals/plan-friction-DONE.md`, and arguing "one line here
makes 51 claims true; rewriting all 51 in place would not make them truer."
Measured, its 53 such refs span six plans, and they are interleaved *line by
line* — `FRICTION.md` line 545 is friction, 546 and 547 are loops-cleanup, 548
is option-result — because successive plans re-scored the same list. That is the
exact shape a single key cannot describe. The key has been replaced with a
record of what it claimed and what the measurement found.

**3. Attribution is derivable and checkable, not guessed.** The item warns that
a confidently wrong plan name is worse than an ambiguous one, and that an
earlier phase made that mistake. The method used here has an independent check
built into it:

- **Rotation boundaries** come from `git log --diff-filter=A --format=%at --`
  over each `docs/internals/plan-*-DONE.md`. The commit that *adds*
  `plan-X-DONE.md` is the instant X stopped being live, and it is the same
  commit that opens X+1, so the windows tile with no gap and no overlap.
- **`git blame --line-porcelain`** on the citing line puts it in exactly one
  window.
- **The check:** the phase number cited must be a phase the mapped document
  actually declares. **167 of 167 passed; 0 unverifiable**, and the 5 plural refs were
  attributed and checked one at a time. A misdated line
  would have to land in a window whose plan happens to declare the same phase
  number to survive this, and the plans differ sharply in length (option-result
  declares 5 phases, friction 10, loops-cleanup 67).
- **A second, independent corroboration:** for 98 of the 167, the blame commit's
  *own subject* names a phase number ("feat(corelib): phase 6 — core:cli learns
  --flag VALUE"), and in **98 of 98** that number is declared by the document the
  window mapped to — e.g. that commit maps to `plan-friction-DONE.md`, whose
  phase 6 is "core:cli and args()". **Zero contradictions.** The other 69 have
  subjects with no phase number ("chore: batch 11 …") and rest on the window
  alone.

**4. The spelling is the house style, not a new one.** `server/README.md:165`
already writes `` `docs/internals/plan-friction-DONE.md` phase 5 ``. The full
repo-relative path was chosen over a stem shorthand for that reason; it costs
about 34 columns per site and the citing lines were not reflowed.

**What was deliberately NOT rewritten.**

- **5 refs that mean the live `plan.md`** and are therefore correct today:
  `FRICTION.md:739`, `server/main.ty:741`, and `server/run.sh`
  at lines 328, 479 and 602 (batch C recorded the last three as 468 and 591 and
  `server/main.ty:708`; batch E moved them, and the line numbers here are the
  post-batch-E ones). Batch E then wrote a **sixth**, `server/run.sh:650`, after
  this count was taken. These are the ones the next archive must sweep, which is
  why the rule below has an archiving half.
- **4 refs inside `docs/internals/plan-*-DONE.md`** (3 in
  `plan-webserver-gate-DONE.md`, 1 in `plan-friction-DONE.md`). Those files are
  frozen records — `scripts/check_citations.py` states the rule that they must
  never be edited — and inside one of them "`plan.md` phase N" self-refers
  unambiguously anyway.

**Recurrence is the actual defect, so it is addressed directly.** Rewriting 167
references (158 same-line, 4 that wrapped across two lines, 5 plural) fixes today; it does not stop the next plan from creating a fresh
batch. `CLAUDE.md` gained a "`plan.md` rotates, so never leave "`plan.md` phase
N" behind" section under "Plans", stating both halves: cite the archived name
when the plan is already archived, and **the commit that archives a plan
rewrites the references that plan created**, using the boundary/blame/check
method above. It also records why no gate catches this —
`scripts/check_citations.py` only recognises refs of the form `path:N`, and a
plan reference has no line number.

Four cross-line references (the path at end of line, "phase N" wrapping onto the
next) were invisible to the line-based rewrite and were repaired by hand:
`examples/corelib/result/main.ty`, `server/main.ty`, `tools/lsp.ty` and
`CLAUDE.md`'s own `make test-fast` paragraph.

#### Phase 8 — the premise does not hold, and the reason is worth keeping

The item says reject fixtures with a `package` header "may be passing for the
wrong reason". Measured first, as instructed:

```
flat tests/reject/*.ty fixtures:        249
of those, declaring a package header:  0
therefore scored against a sibling:     0
package-mode reject cases, isolated:    1 (tests/reject/pkg/privacy_cross/ )
any occurrence of the word package:     0
```

**Zero.** Not one flat reject fixture declares a `package` header, so not one is
being scored against a sibling's error, and the earlier phase's observation was
a deliberate probe rather than a fixture in the tree. The mechanism is real and
was re-derived rather than taken from the item: `detect_package`
(`src/tychoc.c:12316-12322`) returns the leading `package <name>` of the entry
file's token stream, and `src/tychoc.c:12713` branches on it into
`compile_package` — a whole-directory merge. The line the item names has moved: `src/tychoc.c:7757` is now a
line of the comment block above `dup_other_file`, not the scan trigger.

The reason the count is zero is that the arrangement is already right: the one
package-mode reject case lives in its own directory, `tests/reject/pkg/`, run by
a separate lane, and `tests/run.sh:186-190` already says why. **What was missing
was enforcement.** Nothing stopped the next author dropping a `package`-headed
fixture into the flat directory, where it would compile all 249 siblings, be
refused for whichever sibling errors first in sort order, and be scored `ok` by
a lane that asserts only "nonzero exit plus a non-empty diagnostic".

**The fix** is a guard in the flat reject lane in both runners —
`tests/run.sh:170-177` and `tools/prunner/main.ty`'s `judge` — with the same
`grep -q '^package [A-Za-z_]'` predicate and a byte-identical FAIL reason, so
the two reports stay identical as `CLAUDE.md` requires. It is fail-closed
(RULE 7): a fixture it cannot clear is failed, not compiled. It was chosen over
"assert the diagnostic names the fixture's own file" because the directory scan
is the *only* route to a wrong-reason refusal — a single-file compile has no
sibling to be refused for — so guarding the header is exactly the defect and
nothing wider.

**The guard was proven to fire, not assumed to.** A deliberately valid program
carrying a `package` header was dropped into `tests/reject/` and the real lane
body run over the directory:

```
FAIL  reject_zz_guard_probe  (declares a package header -- it would be compiled against every sibling in tests/reject/; move it to tests/reject/pkg/<name>/)
pass=249 fail=1 fails= reject_zz_guard_probe
```

249 real fixtures pass, the probe fails, and the probe was removed. **The count
does not move: 560 before, 560 after**, because the guard adds no test name — it
is an extra branch inside the existing per-fixture scoring.

Inserting the guard shifted `tests/run.sh` by 12 lines at the loop header and 16
below it, invalidating every citation into the file. **41 refs in 6 live files**
were repaired (`tools/prunner/main.ty` 34, `scripts/asan_self.sh` 2,
`tests/diag/range_removed.ty` 2, and one each in three `tests/reject/`
fixtures), and each was re-checked against the construct it names rather than
bumped blindly — the naive uniform +16 was wrong for the four refs to the loop
header and was corrected to +12. The archived plans and the two dated audit
documents under `docs/internals/` were left alone: **0 of their refs cite a line
at or past the insertion point**, so nothing there went stale.

#### Gates

Markdown, comments and two shell/Tycho runners, plus `src/tychoc.c` comments and
`.ty` fixture comments — so `make test` is the right gate and `make ci` was not
run.

```
$ python3 scripts/check_citations.py
citation check: ok (191 anchored contain the token they name, 2812 bare in bounds, 245 source->doc citations resolve, 245 source->source in bounds, 12 source->source anchored)

$ sh scripts/check_links.sh
link check: ok (136 markdown files, no dead relative links)

$ make test
passed: 560   failed: 0
all green

$ make test-fast        # tools/prunner/main.ty was edited, so it must still build and agree
passed: 560   failed: 0
all green
```

Two comment edits (the plural references) landed in `server/main.ty` and
`tests/conc/bare_for_arrarith.ty` after `make test` had run, and neither file is
covered by it, so their own gates were run rather than assumed:

```
$ make server-check
server: OK

$ make conc
conc: passed 38   failed 0
```

560 before, 560 after, both lanes. Every unit of change is accounted for: the
phase 8 guard adds a branch, not a test name, and no fixture trips it. Every
edit in this batch is a comment, a Markdown paragraph, or the reject-lane guard;
no compiled behaviour changed, which is why `make ci` was not run.

- [ ] **Phase 24** — filed by batch C. **Nothing mechanical prevents the phase 6
      defect from recurring.** 167 references rotted invisibly because
      `scripts/check_citations.py` recognises only refs of the form `path:N`, and
      "`plan.md` phase N" has no line number, so the gate skips it entirely. The
      cure batch C shipped is a written rule in `CLAUDE.md`'s "Plans" section,
      which is worth having but is enforced by nobody. A gate is cheap and the
      predicate is exact: **outside `plan.md` itself and the frozen
      `docs/internals/plan-*-DONE.md`, no file may contain "`plan.md` phase N"**
      — it is either a live-plan reference that the archiving commit must sweep,
      or it is already stale. Today the tree holds 5 such refs (`FRICTION.md`,
      `server/main.ty`, `server/run.sh` ×3), all legitimately about this plan, so
      the gate cannot be turned on as a hard failure until this plan is archived
      and they are rewritten — which is precisely the archiving discipline it
      would enforce. Sequence it that way: land the check, let the archive commit
      be its first customer. Out of scope for batch C, which was told to pick an
      end state for the references, not to add a gate. Verify:
      `python3 scripts/check_citations.py`.

- [x] **Phase 25** — filed by batch C, out of scope and pre-existing. The
      package-mode comment above `dup_other_file` cites two sites and **both are
      wrong**. `src/tychoc.c:7751` says the directory scan sorts at
      "`scan_pkg_files` qsorts, `:11759`" — `src/tychoc.c:11759` is a
      `fprintf` of `"    TychoArrC%d r = src;\n"` inside array-copy codegen;
      `scan_pkg_files` is at `src/tychoc.c:12371`. `src/tychoc.c:7759` says the
      scan "is entered only when the entry file declares a `package` header
      (`:12069-12071`)" — `src/tychoc.c:12069` is a comment line inside enum
      copy/eq body generation; the header is detected by `detect_package` at
      `src/tychoc.c:12316-12322` and acted on at `src/tychoc.c:12713`. Both are
      bounds-valid, so the citation gate's source→source lane passes them — the
      documented blind spot. Found while re-deriving the scan trigger for phase
      8; the comment's *prose* is correct and only its two numbers are stale.
      Same class as phase 17 and batch B's phase 23 but a different file, so
      filed separately. Two numbers plus one range. Verify:
      `python3 scripts/check_citations.py`.

### Batch D evidence — phases 16, 17 and 18, 2026-07-31

Five files: `docs/guides/corelib.md`, `docs/spec/appendix-h-differences.md`,
`server/run.sh`, `editors/zed/README.md`, `scripts/editors_check.sh`, plus the
`CLAUDE.md` timing correction batch A filed. No compiled behaviour changed, so
`make test` was not run; `make ci` was left for the closing sweep.

#### Phase 18 — the decision, and the tree-editing-gate question asked plainly

**Option 2 was taken: the number is gone from the claim, and the gate asserts the
claim is present.** The count lane is replaced by a phrase check; the CORPUS lane
that actually proves the grammar parses the tree is untouched.

The first option — have the script rewrite `editors/zed/README.md` when it
disagrees — was rejected, and the precedent question is worth answering rather
than dodging, because on cost alone it is the tempting one (it never fails, and
nobody ever retypes a number again). Two reasons it is wrong *here*:

1. **A gate that repairs its own subject asserts nothing.** The README's number
   would become, by construction, whatever the script just computed. There is no
   tree in which the claim is false, so the check has no discriminating power —
   it is a code generator wearing a gate's clothes. The four firings are cited as
   evidence the lane works; they are equally evidence that the *only* thing it
   ever caught was its own maintenance burden. It never once caught a wrong claim
   about the grammar.
2. **It would make `make ci` mutate tracked files.** `scripts/editors_check.sh`
   is step `[9b]`. A developer running the suite would get a dirty working tree
   as a side effect of *checking*; on a clean CI checkout the rewrite is computed,
   written, and thrown away with the container, so the claim is "kept true" by an
   edit nobody reads or reviews. Nothing else in `scripts/ci.sh` writes to a
   tracked file, and this is not the change that should introduce it.

**What was deliberately NOT weakened.** The thing that matters is that the
grammar still parses the corpus, and that lane is byte-for-byte unchanged: the
two-directional sorted diff at `scripts/editors_check.sh:110-137` still fails on
a newly-failing file *and* on a known-bad file that starts parsing. Only the
hand-typed integer is gone. Proven below by breaking it.

**A second defect surfaced while rewriting the sentence, and nothing gated it.**
The README claimed the corpus "reports exactly ONE `ERROR` node ... That one is
`tests/reject/rawstring_unterminated.ty`". The known-bad set has been **two**
files since the raw-string work — the gate's own list at
`scripts/editors_check.sh:122-125` names `tests/reject/hex_escape_one_digit.ty`
too, and `make editors-check` prints both. So the old lane was policing the one
number in that sentence that did not matter while the substantive claim beside it
was wrong. The rewritten sentence names both files.

#### Phase 18 — break proof, both directions

The claim lane, reworded so the gated phrase is absent (`every tracked` →
`all the tracked source files`), restored immediately after:

    >>> editors: zed README corpus claim
        CLAIM MISSING from editors/zed/README.md -- expected the phrase
        "every tracked `.ty` file". Reword it and this lane must be updated in step.

The lane that matters, with an unparseable fixture dropped into the corpus
(`tests/reject/zz_batchd_breakproof.ty`, an unterminated raw string), removed
immediately after:

    >>> editors: zed README corpus claim
        ok  claim present; tree has 849 .ty files (reported, not asserted)
    >>> editors: zed grammar over the corpus (849 .ty files)
        CORPUS PARSE MISMATCH ('<' expected to fail but parsed, '>' newly failing):
          2a3
          > tests/reject/zz_batchd_breakproof.ty
    editors-check: FAIL
    make: *** [Makefile:64: editors-check] Error 1

Both halves of the design are visible in that one run: **the corpus lane still
reddens** on a grammar failure, and **the claim lane stayed green at 849** — the
850th `.ty` file will not redden `make ci`, which was the entire point.

Green after restoring both:

    >>> editors: zed README corpus claim
        ok  claim present; tree has 848 .ty files (reported, not asserted)
        src/ matches grammar.js byte for byte (parser.c, grammar.json, node-types.json, tree_sitter/)
    >>> editors: zed grammar over the corpus (848 .ty files)
        848 files parsed; the only failure is the enumerated known-bad set (tests/reject/hex_escape_one_digit.ty tests/reject/rawstring_unterminated.ty )
    editors-check: ok

`scripts/editors_check.sh` is **the same 140 lines before and after**, on purpose:
nine refs cite it by line (`scripts/editors_check.sh:24`, `:29`, `:57-58`,
`:57-59`, `:76-85`, `:78-85`, `:86-88`, `:92`, `:97`, across four archived
`plan-*-DONE.md` records). The header
entry was rewritten in 5 lines and the lane in 21 — the counts they replaced — so
not one of those refs moved. Repairing frozen records is worse than not moving
them.

#### Phase 17 — the refs were re-derived, and "+1" was wrong for all seven

The phase entry predicted a uniform `+1` from phase 3's `import "core:signal"`.
Every one of the seven is wrong by more than that, because phases 3, 14 and 15
also rewrote `net.listen` into a `match` and inserted the ~28-line signal-arming
block into `main()`. Each was re-derived by reading the current construct, not
shifted:

| comment | as-found | re-derived | drift |
|---|---|---|---|
| `server/run.sh:10` | `server/main.ty:604-614` | `server/main.ty:679-683` bind + `net.port_of`, **and** `server/main.ty:713-717` banner | split |
| `server/run.sh:13` | `server/main.ty:513` | `server/main.ty:588` | +75 |
| `server/run.sh:20` | `server/main.ty:616` | `server/main.ty:719` | +103 |
| `server/run.sh:211` | `server/main.ty:426-436` | `server/main.ty:463-473` | +37 |
| `server/run.sh:336` | `server/main.ty:499-504` | `server/main.ty:574-579` | +75 |
| `server/run.sh:374` | `server/main.ty:342-352` | `server/main.ty:354-364` | +12 |
| `server/run.sh:392` | `server/main.ty:342-352` | `server/main.ty:354-364` | +12 |

`server/run.sh:10` is the one a uniform shift could not have fixed at all: the as-found
`604-614` was a single span covering bind → banner, and the signal-arming block
now sits **inside** it, so the honest repair is two tight ranges rather than one
39-line range that is mostly an unrelated comment.

The two single-line refs are now **anchored** (`@pick`, `@worker`), so the next
shift reddens the gate instead of rotting silently — the same treatment phase 4
gave three of `server/README.md`'s six. Ranges stay bare, per `CLAUDE.md`. The
source→source anchored count moved 12 → 14. `server/run.sh` is **666 lines before
and after**: `plan.md`'s phases 19 and 20 cite `server/run.sh:262` and `:346`, and
both still resolve.

#### Phase 16 — the entry, and where the shift was paid

The `signal` bullet was appended at the **end** of the `## Packages` list
(`docs/guides/corelib.md:392-414`), which is the placement that disturbs the
fewest citations: of the ~30 refs into that file, **only three end past line 391**
and therefore moved. Checked mechanically rather than by eye — every ref in the
tree was extracted and its end line compared against the insertion point:

- `docs/spec/appendix-h-differences.md:27` (row H7, compress/image/tls):
  `412-449` → **`435-472`**, repaired.
- `plan.md:920` `54-393` and `plan.md:921` `393-410` — the phase 16 entry's own
  AS-FOUND refs. Left as found and annotated in place with the new numbers
  (`:54-416`, `:416-433`), following the precedent batch B set for phases 10-12:
  a filing record says what was found, and rewriting it silently would erase the
  defect it exists to describe.

Everything else into that file ends at or below `:353` and did not move.

**The phase's own premise was wrong and was not copied.** It asked for a line
saying `signal` is "the third libc-only shim after `os` and `net`". Enumerated
instead of assumed — every `corelib/*/` with a `<name>_shim.c` and no `deps`
manifest — there are **seven**: `datetime`, `io`, `net`, `os`, `regex`, `signal`,
`time`. `corelib/signal/signal.ty:3-4` only ever claimed "the same self-contained
model as core:os and core:net", which is a model, not a ranking. The guide now
says `core:regex`, `core:os`, `core:net` and `core:signal` are "among" the
libc-only shims, which is true and does not decay into a count — the same lesson
phase 18 is about.

#### Gates

    citation check: ok (191 anchored contain the token they name, 2826 bare in bounds,
      248 source->doc citations resolve, 244 source->source in bounds,
      14 source->source anchored)
    link check: ok (136 markdown files, no dead relative links)
    spec-check: Appendix A grammar matches §3/§4 (ok)
    spec-check: all Appendix E fixture citations resolve (ok)
    spec-examples: 9 runnable example(s), all pass          [exit 0]
    editors-check: ok

`CLAUDE.md`'s `make server-check` cost corrected `~4s` → `~7s` in **both** places
it appears (the gate table and the `make ci` step table), per batch A's measured
7.0s. The two tables disagreeing is how a corrected number half-rots.

- [x] **Phase 26** — filed by batch D, out of scope and pre-existing. The CORPUS
      lane's own comment miscounts the set it guards: `scripts/editors_check.sh:115`
      reads "Exactly one reject fixtures are LEXICAL ones" while the heredoc
      immediately below it (`scripts/editors_check.sh:122-125`) enumerates **two**
      — `tests/reject/hex_escape_one_digit.ty` and
      `tests/reject/rawstring_unterminated.ty` — and `make editors-check` prints
      both. The number is wrong and the agreement ("one ... are") is broken, which
      together suggest the line was edited from a one-file set and not finished.
      Nothing gates a comment, so it is invisible; it is one word. Found while
      rewriting the README lane in the same file, and deliberately not absorbed
      into phase 18, which owned the count claim and not the corpus lane. Verify:
      `make editors-check`.

### Batch E evidence — phases 19 and 20, 2026-07-31

Three files carry the change: `corelib/signal/signal_shim.c` (the registry),
`corelib/signal/signal.ty` (its two-call surface), `server/main.ty` (register and
retire), `server/run.sh` (the redirect, and one new assertion). Every number
below came from running something.

#### Phase 20 first, because it makes every other timing in this batch honest

`server/run.sh:357` was `( sleep 10; kill -KILL ... ) &`. `kill "$WD"` reaps the
subshell; the `sleep` it is blocked in survives, and it holds the stdout this
script inherited. `$(...)` does not wait for the script, it waits for the write
end of the pipe to close everywhere — so a caller capturing the log waits out the
orphan.

Both directions, same box, same binary:

```
                       direct     captured
before the redirect    7089 ms    14280 ms
after  the redirect    7141 ms     7100 ms
```

The 7.2 s gap is the remainder of a 10 s watchdog armed part-way through the run;
batch A measured the same effect as 34.5 s when a 30 s watchdog was briefly in
the file. `) >/dev/null 2>&1 &` closes it, which is how the other three watchdogs
(`server/run.sh:512`, `server/run.sh:586`, `server/run.sh:632`) were already
written. Final numbers with phase 19's extra case in the file: **7338 ms direct,
7361 ms captured** — captured and direct now agree, which is the whole point.

#### Phase 19 — route (a), and why (b) stayed rejected

(b) was re-read, not re-litigated, and the entry's reasoning holds:
`read_request_capped` hands back a partial `raw` with no way to resume it, so a
head split across two slices is re-parsed from the second, and a slice timeout
holding bytes answers 408 after the *slice* — which `server/run.sh:262` pins at
400..3000 ms. Nothing found to contradict that, so route (a) it is.

**The registry.** `corelib/signal/signal_shim.c:117-118`: a fixed
`static volatile sig_atomic_t sigx_conns[256]`, one slot per worker, `0` meaning
empty and any other value meaning `fd + 1`. The safety argument, statement by
statement:

1. **The handler writes nothing but `sigx_flag`.** It only *reads* the slots
   (`corelib/signal/signal_shim.c:151-154`). So no handler-side store to a slot
   exists to be interleaved with anything.
2. **Each slot has exactly one writer.** Slot `i` is written only by worker `i`,
   from ordinary context. Workers never touch each other's slots, so the program
   contains no write/write pair on a slot and there is nothing to serialise —
   which is why there is **no lock**, rather than a lock chosen carelessly. A
   mutex is not on the `man 7 signal-safety` list and a handler blocking on one
   the interrupted thread already holds deadlocks the process; the design removes
   the need for mutual exclusion instead of trying to make taking it safe.
3. **The one concurrent pair is single-writer/single-reader on a
   `volatile sig_atomic_t`** — precisely the object POSIX defines for a handler
   to share with the rest of the program. The reader observes the old value or
   the new one, never a torn one.
4. **`fd + 1`, not `fd`**, so the whole table is already correct at static
   zero-initialisation: there is no init pass for a signal to race with during
   arming, and `0` — a legal descriptor — is not confusable with "empty".
5. **Every range check is outside the handler**
   (`corelib/signal/signal_shim.c:169-170` and `corelib/signal/signal_shim.c:180`).
   The handler does no validation, so
   its body stays loads, branches and `shutdown()`.
6. **The handler's loop is bounded and branch-only**: 256 loads, each into a
   local *before* it is tested, so the value range-checked is the value passed to
   `shutdown()` and no slot is read twice.
7. **The listener still goes first**, then the connections, and `sigx_flag`
   before both — so any thread woken by either `shutdown()` finds the flag set.

**The retire path, which matters as much as registering.** `server/main.ty:590`
calls `signal.retire_conn(slot)` **before** `net.close_fd(conn)` at
`server/main.ty:591`, never after. Reversed, a handler reading the still-live
slot would target a number the kernel has already handed back out. In the order
shipped, a stale read requires the handler to observe a value the owning thread
overwrote strictly earlier, and even then `shutdown()` on the wrong number is
benign: closed → `EBADF`, reused by another connection → that connection is being
shut down anyway, reused by a regular file → `ENOTSOCK`. `shutdown()` closes
nothing, frees nothing and discards no written data. That is the same property
that made it the right call over `close()` for the listener in phase 1, and it is
what makes this race benign rather than merely unlikely.

**Sizing is derived, not picked.** `server/main.ty:672` rejects `--workers`
outside 1..256, and an accept loop holds at most one connection at a time
(`server/main.ty:557-591` is one sequential body: accept, register, serve, retire,
close). One slot per worker is therefore sufficient. `slot = wid - 1` because
worker ids run 1..N and the table is indexed from 0. `register_conn` **fails
closed**: an out-of-range slot or fd is refused, and a refused connection keeps
exactly the pre-phase-19 behaviour — one `SO_RCVTIMEO` — rather than putting a
wrong descriptor in the table.

#### The measurement, and the three things that must not regress

Method as phases 3 and 15 used it: `kill(2)` to `wait(2)` returning,
`--workers 4 --idle-ms 5000`. "before" is a binary built from a clean worktree at
`61a66b0`, so both columns are this box on this day:

```
                                       before      after
0 idle clients (phase 3)                 1 ms       1 ms   rc=0, stopped line
4 PARKED idle clients (phase 19)      4878 ms       1 ms   rc=0, w1..w4 all served
4 BUSY clients, 100ms drip (phase 15)    5 ms       1 ms   rc=0, stopped line
```

`4878 ms` reproduces batch A's 4733-4901 ms band on the unpatched tree, so the
baseline is the same phenomenon. The parked case was repeated: **1 ms, 5 runs out
of 5.** Phase 3's clean-shutdown invariants held in every run — exit 0, the
`stopped after N requests` line, and `w1,w2,w3,w4` all present in the access log,
which is the "all workers released" assertion.

#### The new assertion, proved against the unpatched tree first

`server/run.sh` case 6, 60 → **61 assertions**: four clients each complete one
request, read the answer and go quiet, which parks all four workers inside the
next read; then SIGTERM under a **3 s** watchdog with `--idle-ms 8000`. The
numbers are chosen so it cannot be a coin flip — the old behaviour cannot finish
under 8 s by any path and comes back 137; the new one takes about a millisecond.
Copied into a worktree at `61a66b0` and run there:

```
  FAIL SIGTERM with parked readers: wait status 137, want 0 (137 = it waited out SO_RCVTIMEO)
server: FAIL
```

twice out of two, and only that assertion. On the patched tree:

```
  ok   SIGTERM with 4 parked keep-alive readers: exit 0 well inside the 3s watchdog (idle is 8s)
server: OK
```

#### Gates — real output

- `make -s server-check`: **server: OK**, **61** assertions (was 60).
- 10-run loop: **10/10 OK, 61 every run** — run twice, before and after the
  comment-only edits, 10/10 both times.
- `make test`: **passed: 560   failed: 0** — unchanged.
- `make corelib`: **all green (tychoc matches goldens)**. Run because
  `corelib/signal/` changed and `corelib/test/signal/main.ty` compiles the shim;
  `make test` does not cover that package.
- `python3 scripts/check_citations.py`: **ok** (191 anchored, 2844 bare in bounds,
  248 source→doc, 247 source→source in bounds, 16 source→source anchored).
- `sh scripts/check_links.sh`: ok. `sh scripts/spec_check.sh`: 9 runnable
  examples, all pass. Both run because `docs/spec/18-library.md` was touched.
- `make ci` deliberately **not** run: per `CLAUDE.md` it is the closing sweep.

**Gate cost.** `sh server/run.sh` 7.0 s → 7.3 s. The new case is one respawn plus
a 0.2 s settle; its watchdog only costs 3 s when it *fails*.

#### Four files outside the stated scope were edited, and why

Inserting comment blocks moved lines, and 15 **anchored** citations elsewhere in
the tree pointed into the three edited files by line number. `check_citations.py`
failed on all 15. Re-pointing them is not scope creep, it is the change's own
cleanup: `FRICTION.md` (2), `server/README.md` (1), `docs/spec/18-library.md`
(5), `plan.md`'s own phase 2 evidence (5), plus two in `server/run.sh`'s header
(`server/main.ty:621`, `server/main.ty:752`) and the readiness spans
above them. Nothing but the line numbers changed in any of them. The two refs
this batch itself wrote into `server/main.ty` and `corelib/signal/signal_shim.c`
were switched to the **anchored** `path:N@token` form on purpose, so the next
edit that moves them reddens the gate instead of drifting silently.

- [ ] **Phase 27** — filed by batch E, out of scope and pre-existing, and it is
      the *other half* of the citation problem `CLAUDE.md` already documents.
      `scripts/check_citations.py` verifies an **anchored** ref
      (`path:N@token`) against the token it names, but a **bare** `path:N` is only
      **bounds-checked** — it passes as long as the file has that many lines.
      2848 refs are in that category, and they drift silently. Batch E's own
      edits moved lines in `corelib/signal/signal_shim.c` and `server/main.ty`
      and the gate caught **15 anchored** refs while catching **none** of the
      bare ones that describe the same code. Concretely, in this file:
      `docs/internals/plan-signals-DONE.md:296` read
      "`corelib/signal/signal_shim.c:81` | `sigx_flag = 1;`" while line 81 of
      that file had become the middle of a comment — the statement it names is
      at `corelib/signal/signal_shim.c:148@sigx_flag`. The same was true of
      `docs/internals/plan-signals-DONE.md:290`'s handler span, and of a long
      tail of `server/main.ty:N` refs in this file's phase 1-3 evidence.
      **All 38 were repaired by phase 1 of the plan that followed this one**, as
      the stated one-time exception to the frozen-record rule: they were already
      false at the moment this file froze, which the rule does not protect.
      Two candidate treatments and they are not equivalent: (i) leave
      historical evidence blocks alone as frozen records and say so in
      `CLAUDE.md` — cheap, but it means a live `plan.md` contains refs that
      resolve to the wrong lines until it is archived; (ii) teach the gate to
      require an anchor for **source→doc single-line** refs the way it already
      does inside `> Provenance:` blocks, which would redden ~2848 refs at once
      and needs a migration rather than a phase. **Do not start (ii) without
      counting the real blast radius first.** Verify either way:
      `python3 scripts/check_citations.py`.

---

## Annotation added 2026-07-31: `[SUPERSEDED]`, and what it does NOT mean

This record is frozen. The tag `[SUPERSEDED: …]` was added to six lines of it
**after** freezing, and it is the only post-freeze change to this document's
prose. It is not a correction. Nothing above it was renumbered, reworded or
removed; each tag sits beside the citation it qualifies and the citation still
reads exactly as it was written.

**What the six tagged lines say.** Each cites `server/main.ty:493-494` and
states that those lines set `running = false` in the `Err` arm of `accept`.
When this plan was written that was true, and it was the load-bearing fact
behind phases 1, 3 and 14.

**Why they are tagged rather than repaired.** Batch A of this very plan
**deleted** that construct. It did not move. There is therefore no line to
repoint to, and the nearest true statement in today's file is a *different*
rule — one that only winds down when `signal.shutdown_requested()` agrees —
which batch A adopted precisely because retiring a worker on any `Err` was
wrong. Repointing these six refs at it would make a frozen record assert, in
today's line numbers, the opposite of what this plan concluded. A wrong write
is worse than a skipped one, so nothing was written.

**Where the replacement reasoning lives.** `server/main.ty` carries a comment
block headed `WHY THE ERR ARM IS NOT `running = false`, AND WHY THAT NEEDED NO
ERRNO.` — grep that heading rather than trusting a number; it was at
`server/main.ty:520` on the day this note was written, and the point of the note
is that such numbers move.

**The rule this establishes, for the next reader who greps `493-494`:** a
citation in a frozen record whose *construct* was deleted is not a citation to
repair. It is a claim about superseded behaviour. Tag it where it stands, name
what deleted it, and point at the reasoning that replaced it. Do not repoint it,
and do not delete it — the claim is part of the evidence for why the change was
made.

**Why the tag is prose and not a gate feature**, and the reason is stronger than
it first looked. `scripts/check_citations.py` never asked for a repair here and
never will — but not because these six are merely bare. They are not checked at
all: `server/` is absent from that file's `SRC_PREFIX`, so a Markdown citation
into the server is skipped before bounds, existence or anchors are considered.
That was measured while this note was being written, and the note's first draft
said "bounds only", which was wrong. 179 refs into `server/` are in that state,
and 751 across all trees outside the prefix; it is filed as its own phase and is
not this annotation's business.

So the exposure here was never the gate, in either direction. It was a human
sweeping for stale-looking numbers — which is exactly what a marker a human
reads can stop, and what a marker only a gate reads could not have.
