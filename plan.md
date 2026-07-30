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

- [x] **Phase 1 — the pool, on a toy workload**
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

  **Evidence (2026-07-30).** New file: `tools/prunner/main.ty`. Built with
  `./tychoc tools/prunner/main.ty -o <tmp>/prunner`; no Makefile target, no gate
  wiring — phase 4 owns that.

  **Directory: `tools/prunner/`, and it had to be a directory.** `tools/` holds
  the Tycho-written tooling, but its three programs (`tools/tychofmt.ty`,
  `tools/lsp.ty`, `tools/tycho.ty`) sit flat in it. This program must
  `import "core:os"`, so it must carry a `package` header, and a `package` header
  is exactly what switches tychoc into the directory scan — `src/tychoc.c:7757`
  says the scan "is entered only when the entry file declares a `package`
  header". A `package main` file dropped straight into `tools/` therefore
  compiles all three siblings with it and dies on a duplicate `main`. Observed
  the same mechanism first-hand while probing: a probe file in a scratch
  directory was compiled together with an unrelated stray `a.ty` and reported
  that file's syntax error under the name of a file I had not asked for. The
  diagnostic at `src/tychoc.c:7779` exists for precisely this and its advice is
  `mkdir`. `tools/prof/` is the existing subdirectory precedent.

  **`core:os` call: `os.system`, not `os.run`.** `corelib/os/os.ty:32` inherits
  the child's stdout/stderr and returns only the exit code; `corelib/os/os.ty:36`
  adds a `popen(3)` pipe plus a captured string per job. This phase needs the
  code and nothing else, `true`/`false` are silent, and interleaved capture is a
  phase-2 problem — that is where a fixture's output has to be compared and
  ordering starts to matter.

  **No losses — 10 consecutive runs, K=243** (the fixture count, so the number is
  the one phase 2 will face). The check is not a count: every result carries its
  job index, and `tally` asserts index *i* arrived exactly once for every *i*, so
  a lost job and a duplicated job are distinguishable. All ten runs identical:

      run1   mode=pool n=243 results=243 unique=243 missing=0 dupes=0 ok=162 fail=81 wall_ms=19 maxconc=16 ncpu=16 NOLOSS
      run2   ... wall_ms=18 maxconc=16 ncpu=16 NOLOSS
      run3   ... wall_ms=18   run4 ... 17   run5 ... 18   run6 ... 18
      run7   ... wall_ms=18   run8 ... 17   run9 ... 17   run10 ... 19
      # every run: results=243 unique=243 missing=0 dupes=0 ok=162 fail=81 NOLOSS

  **Timing, sequential vs pool. ncpu = 16** (`nproc`, and `ncpu()` agrees).

      A: 243 trivial cmds   seq wall_ms=266  maxconc=1   |  pool wall_ms=18   maxconc=16   -> 14.8x
      B: 64 cmds x 50ms     seq wall_ms=3453 maxconc=1   |  pool wall_ms=212  maxconc=16   -> 16.3x

  **Observed worker count: 16 = ncpu**, and it is measured, not inferred. Each
  result carries the monotonic `clock()` interval it occupied; `max_overlap`
  counts intervals live at each interval's start (for intervals the maximum is
  always attained at some start), so `maxconc` is the peak simultaneous
  commands. Case B is the honest one: 64 items at 50ms over 16 workers is four
  rounds = 200ms ideal, observed 212ms. Nothing is starved.

  **The `min(N, ncpu)` / 64-chunk assumption: checked, and it holds.** The clamp
  is `src/tychoc.c:10040`. It cannot bite at ncpu=16, so it was forced with
  `TYCHO_THREADS`, K=200 at 50ms:

      TYCHO_THREADS=32    maxconc=32  ncpu=32   wall_ms=371  NOLOSS
      TYCHO_THREADS=64    maxconc=64  ncpu=64   wall_ms=217  NOLOSS
      TYCHO_THREADS=100   maxconc=64  ncpu=100  wall_ms=218  NOLOSS

  The cap is real and exactly 64, but it **limits width, it does not starve**:
  200 items still came back 200, unique, at every setting. The Pre-flight's
  "risk if wrong" — the cap interacting with the channel drain to starve workers
  — did not happen. Note the third row: `ncpu()` reported 100 while the fan-out
  was 64. See Phase 7.

  **Running notes: what writing concurrent Tycho was actually like.** Raw
  material for phase 3; phase 3 decides what earns a `FRICTION.md` entry.

  - *The pool compiled first try, and that was the surprise.* The whole program —
    two channels, a spawned producer, a spawned collector, a `parallel for` with
    `select`/`recv`, `os.system` inside the body — built with no errors and no
    warnings on the first `./tychoc`. Expectation going in, set by `FRICTION.md`,
    was a fight.
  - *The one real design constraint was not the affine channel.* It was
    `docs/spec/13-concurrency.md:100`: the only outer-scope write a `parallel for`
    body may perform is a `+`/`*` reduction on `int`/`float`. So results cannot be
    `push`-ed anywhere from the body — `tests/conc/reject/parfor_push.ty` says
    "parallel for cannot mutate captured variable 'xs' in place". A worker pool
    whose results are per-item and not a scalar sum therefore **must** route them
    out through a second channel. That is the shape of `run_pool`, and it was not
    a choice.
  - *`docs/spec/13-concurrency.md:127` cost less than the Pre-flight expected.*
    The channel-cannot-escape rule forces the pool to be a scope rather than an
    object, but `run_pool(cmds) -> [Res]` still reads as an ordinary function:
    the channels are interior, the array of results comes out. Nothing had to be
    contorted. The friction would start if two different call sites wanted to
    share one pool.
  - *A spawned task returning a heap array works and is the piece that makes this
    tractable.* `collect(ch, n) -> [Res]` runs concurrently with the workers and
    is `wait`-ed for its array. Without it, the results channel would have to be
    sized to the whole job count — an unbounded buffer inside the thing whose
    entire point is boundedness.
  - *A struct passes through a channel cleanly.* `channel(Res, 16)` carrying
    `{idx, code, t0, t1}` needed no string encoding and no parsing back. This was
    probed before it was relied on, expecting `int` only.
  - *The best diagnostic of the phase came from a warning, not an error.* An early
    probe with a write-only results channel got: "nothing ever receives from
    channel 'res', so a send parks once its buffer fills" — naming the variable
    and the consequence. That is the deadlock, caught at compile time.
  - *The worst diagnostic came from the directory scan* — see the directory
    choice above. A stray sibling's error, reported against a file that had no
    such line. `src/tychoc.c:7779` improves the duplicate-`main` case but not the
    syntax-error case, which still names only the sibling's path.
  - *Measuring concurrency needed no language support and there is no worker
    identity.* Nothing exposes which chunk an iteration ran in, so "how many
    workers actually ran" is not directly askable. Timestamping each item and
    computing max interval overlap answers it exactly and cost nine lines. Worth
    noting as a non-problem before someone proposes a thread-id builtin.
  - *`ncpu()` is the only knob and `TYCHO_THREADS` is the only override.* There is
    no way to ask for "8 workers here, 32 there" from inside the language; a
    program that wants a narrower pool than ncpu has to be launched with the
    environment variable set. For a fixture runner that will eventually want
    `-j`, that is the next thing to hurt.

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

- [ ] **Phase 7 — `ncpu()` reports a width the fan-out will not use.**
  `docs/spec/16-builtins.md:251` defines `ncpu()` as "The `parallel for` fan-out
  width (online CPUs; overridable by `TYCHO_THREADS`)". Phase 1 measured
  `TYCHO_THREADS=100`: `ncpu()` returned 100 and the peak simultaneous
  iterations were 64, because `src/tychoc.c:10040` clamps the chunk count to 64.
  So the builtin's stated meaning is false above 64 — a program that sizes a
  buffer or a work split from `ncpu()` over-provisions silently. This is
  **distinct from** the `min(N, ncpu)` item already in "Out of scope": that one
  is about the chunk count versus the spec's fan-out prose, this one is about
  `ncpu()`'s own definition. Fix is one of: document the cap on the builtin,
  make `ncpu()` return the clamped value, or lift the 64 (`HTask *_pts[64]` at
  `src/tychoc.c:10041` is why it is 64). Deciding which is the work.

## Out of scope

- **The `min(N, ncpu)` spec fix** (`src/tychoc.c:10040` vs
  `docs/spec/13-concurrency.md:78-82`). ~1 line, real, and its own change — phase 1
  measures the behaviour but does not correct the document.
- **Storable task handles or channels.** If phase 3 finds a genuine need, it files
  it. This plan does not attempt a type-system change.
- **`scripts/asan_self.sh`.** The same parallelisation would help it and it is a
  bigger win; it is deliberately not in scope until the fixture runner proves the
  pattern.
