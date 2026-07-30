# A parallel fixture runner, written in Tycho

Previous plan complete and archived at
[docs/internals/plan-webserver-gate-DONE.md](docs/internals/plan-webserver-gate-DONE.md)
(four phases plus Phase 7, `make ci` observed green). Its Phases 5 and 6 are
carried forward at the bottom of this file.

## Goal

`tests/run.sh` compiles and runs **243 fixtures one at a time**. `scripts/asan_self.sh`
does 561 compiles the same way. Both are minutes long, both are embarrassingly
parallel, and together they are most of why `make ci` takes ~19 minutes — which
cost a full working session's patience on 2026-07-30.

Write the fixture-running workload as a Tycho program with a bounded worker pool,
and make it produce a report identical to the sequential one. Two things fall out,
and **both are the point**:

1. A gate that takes a fraction of the wall-clock. Wanted for its own sake.
2. A real concurrent Tycho program of non-trivial size, written to find out what
   actually hurts — rather than what a costing exercise predicted would hurt.

## Pre-flight

- **Worst case, and it is specific:** a parallel runner that reports green while
  silently skipping fixtures. A sequential runner that dies takes the report with
  it; a parallel one can lose a work item and still print a tidy summary. **The
  count is the invariant** — if the parallel runner does not report exactly 243
  results, it has failed, no matter what those results say.
- **Reversibility:** total. This is a new program alongside `tests/run.sh`, which
  is not touched until the new one has been proven equal on the same corpus.
- **Verified — the workload is sequential today.** `grep -nE "parallel|jobs|&$|xargs.*-P|wait" tests/run.sh`
  returns nothing. `fuzz/run.py:29` imports `ProcessPoolExecutor` and `:130` uses
  it, so the fan-out shape is already proven *in Python* on this exact machine;
  what is missing is a Tycho expression of it.
- **Verified — the language can already express a bounded worker pool, and this
  corrects the assumption this work was proposed under.** `tests/conc/workers.ty`
  is that pattern, tested: `channel(int, 16)` for bounded backpressure
  (`tests/conc/workers.ty:18`), `spawn produce(jobs, 100)` for the producer
  (`:19`), and `parallel for i in 0..<100:` with a `select`/`recv(jobs, x)` arm
  (`:22-24`) fanning out ncpu workers that share the one channel, each job
  consumed exactly once. `FRICTION.md`'s item about unstorable task handles is
  **sidestepped entirely** by this shape — it stores no handles.
- **Verified — a Tycho program can drive external commands.** `corelib/os/os.ty:5`
  is `os.system(cmd) -> int` over libc `system(3)`, and the same package wraps
  `popen(3)` returning an `Output` carrying the exit code. So compiling and
  running a fixture from inside Tycho needs no new FFI.
- **Verified — the constraint that shapes the design.**
  `docs/spec/13-concurrency.md:127` states a `Channel(T)` "cannot be returned,
  stored in a container". Channels are affine like task handles. So the pool
  cannot be built as a reusable library object that hands out channels; it has to
  be written in the shape `tests/conc/workers.ty` uses, with the channel created
  and consumed in one scope.
- **Assuming — determinism is the hard part, not parallelism.** `tests/run.sh`
  prints one line per fixture in corpus order and its output is compared by eye
  and by CI. A parallel runner completes work out of order, so it must buffer and
  re-sort, or emit only a summary. **Risk if wrong:** an unstable report that
  differs run to run, which is unusable as a gate and indistinguishable from a
  real failure. Phase 2 must produce byte-identical output across repeated runs
  before anything else is believed.
- **Assuming — `min(N, ncpu)` and the 64-chunk cap are tolerable here.**
  `FRICTION.md`'s item records `parallel for` fanning out `min(N, ncpu)` chunk
  tasks with an undocumented hard cap of 64 at `src/tychoc.c:10040`. For 243
  fixtures on a 16-core box that is ncpu workers, which is what is wanted anyway.
  **Risk if wrong:** the cap interacts with the channel drain in a way that
  starves workers. Phase 1 must measure the actual concurrency achieved, not
  assume it.
- **Open, and deliberately not decided in advance:** whether this replaces
  `tests/run.sh`, supplements it, or stays a demonstration. That depends on
  whether phase 2 proves equality, and phase 4 decides it on evidence.

## Phases

- [ ] **Phase 1 — the pool, on a toy workload**
  - Scope: a new `tools/prunner/` (or similar — pick and say why) containing a
    Tycho program that takes a list of work items, fans them out over a bounded
    channel with `parallel for` + `select`/`recv` as `tests/conc/workers.ty:18-24`
    does, and collects results. Work item: run a shell command via `core:os`,
    capture its exit code. **No fixture logic yet** — a list of `true`/`false`
    commands is enough.
  - **Measure the concurrency actually achieved.** Not "it compiled" — a timing
    comparison against the same list run sequentially, plus the worker count
    reached. The Pre-flight flags the `min(N, ncpu)` / 64-chunk behaviour as
    assumed-tolerable; this is where that gets checked.
  - Done when: the toy pool runs K commands, returns K results with no losses
    across 10 runs, and the measured speedup is reported with the machine's ncpu.
  - Verify: run it; the 10-run no-loss check; the timing comparison. Not `make ci`.

- [ ] **Phase 2 — the real corpus, byte-identical report**
  - Scope: teach the program the actual fixture workload — compile each `.ty`,
    run it, compare against its golden — mirroring what `tests/run.sh` does.
    Read that script properly first; it handles `tests/*.in` stdin, the
    `tests/pkg/*/` package loop, and the reject/abort/diag/warn lanes, and not
    all of those need to be in scope. **Say which lanes you covered and which you
    deliberately left to the sequential runner.**
  - **The count is the invariant.** The report must name exactly as many results
    as the sequential runner does over the same lane, and the pass/fail verdict
    per fixture must agree with it fixture by fixture — not just in total.
  - Output must be **byte-identical across repeated runs**. Buffer and sort, or
    emit a canonical summary; either is fine, an unstable report is not.
  - Done when: over the covered lanes, the parallel runner's verdicts agree with
    `tests/run.sh`'s fixture-for-fixture, and three consecutive runs are
    byte-identical to each other.
  - Verify: the agreement check against `sh tests/run.sh`, the three-run identity
    check, and `make test` still green (you have not modified it yet).

- [ ] **Phase 3 — what writing it actually taught**
  - Scope: a written account appended to this plan, and any `FRICTION.md` entries
    it earns. **This is the phase the whole exercise exists for.**
  - Record what was awkward, what was impossible, and what was easier than
    expected. Be specific: a line you could not write and the diagnostic you got,
    a shape you had to contort, a place the affine-channel rule
    (`docs/spec/13-concurrency.md:127`) forced a design. Where an existing
    `FRICTION.md` item was confirmed, sharpened, or **disproved**, say so — the
    file was re-scored at `945acfa` and its concurrency items were written from
    the web server, which `grep parallel server/main.ty` shows never used
    `parallel for` at all.
  - Done when: the account exists, and every `FRICTION.md` change it makes is
    backed by something that happened while writing the program.
  - Verify: the two doc gates.

- [ ] **Phase 4 — does it replace `tests/run.sh`?**
  - Scope: a decision, with the evidence from phases 2 and 3, and its
    implementation if the answer is yes: wiring into the `Makefile` and
    `scripts/ci.sh`, or a recorded decision not to.
  - The bar is not "it is faster". It is: does it agree fixture-for-fixture, is
    its report stable, does it fail loudly when a fixture fails, and does it
    degrade sanely when something goes wrong mid-run? A gate that is fast and
    occasionally wrong is worse than the slow one it replaces.
  - Note the Pre-flight's worst case: a parallel runner reporting green while
    losing work. Whatever ships must make that impossible to do quietly.
  - Done when: the decision is written with its reasoning, and if it ships, the
    lane is in `make ci` and green.
  - Verify: if it ships, `make ci` once, observed. If it does not, the two doc gates.

## Carried forward

- [ ] **Phase 5 — `server/main.ty:617` is unreachable.** `"stopped after N requests"`
  never prints: nothing installs a SIGTERM or SIGINT handler, and signal handling
  is absent from the language. The wind-down path it would trigger already exists
  at `server/main.ty:494`. Phase 4 of the previous plan ranked this second of the
  things worth building.
- [ ] **Phase 6 — 110 references to "`plan.md` phase N" across 42 files point at
  the wrong plan.** `plan.md` rotates on archive, so cited phase numbers run to 63
  while the live plan starts again at 1. The citation gate cannot see them: there
  is no line number to check. `server/main.ty` alone has 13.

## Out of scope

- **The `min(N, ncpu)` spec fix** (`src/tychoc.c:10040` vs
  `docs/spec/13-concurrency.md:78-82`). ~1 line, real, and its own change — phase 1
  measures the behaviour but does not correct the document.
- **Storable task handles or channels.** If phase 3 finds a genuine need, it files
  it. This plan does not attempt a type-system change.
- **`scripts/asan_self.sh`.** The same parallelisation would help it and it is a
  bigger win; it is deliberately not in scope until the fixture runner proves the
  pattern.
