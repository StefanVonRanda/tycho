# What comes next

> 2026-08-04: the generic-hash phase is complete (60e3601, goldens 8b9fc88) and
> the trie follow-up is measured and committed (cd2b37d). New owner-directed
> phase: **the build tool** — the long-shelved standing candidate, un-shelved by
> the owner's explicit request (the "second caller" the demand rule asks for).
> Validated before admission: every primitive it needs exists in Tycho today —
> `core:os.system` (subprocess + exit code), `core:io.mtime` (st_mtime via the
> io shim), `read_file`/`write_file`, and the concurrency model (`spawn`/`wait`,
> `channel`/`send`/`recv`) — so the tool is pure Tycho, no new shim.

## Phase 1 — `tycho-build`: a make-like build tool in Tycho

The last untested axis: systems-y I/O — subprocesses, file metadata, a parallel
dependency graph. `tools/tycho-build/main.ty`, a make-lite:

- **Build-file format**: `target: dep1 dep2` rules + indented recipe lines (each
  a shell line via `os.system`, run in order, first non-zero exits the rule);
  `#` comments; the first rule is the default target; a dep that names no rule
  is a plain source file.
- **Up-to-date rule** (mtime-based): a target rebuilds iff its output file is
  missing, or any dep's mtime is newer, or any dep was **rebuilt in this run**
  (the second-granularity tie-breaker make needs — two files written in the
  same second would otherwise stall a chain). Recipe-less rules are pass-through
  groupers (like `all:`), silent.
- **Parallel execution** via the concurrency model: a bounded worker pool
  (`spawn` N workers, each looping `recv` on a job channel, `send`ing
  `(rule, code)` on a done channel), and a scheduler that dispatches newly-ready
  rules in topological order. Status lines (`build <target>`, `FAILED <target>
  (exit N)`) print from the scheduler thread in DAG order — deterministic for a
  fixed starting state; recipe stdout is inherited live (like make -j, parallel
  interleaving is not ordered). A failed rule skips its dependents; the build
  exits 1.
- **CLI**: `tycho-build [buildfile] [target]` (buildfile defaults to
  `buildfile`, target to the first rule); exit 0 success/no-op, 1 build failure,
  2 usage/parse/io error.
- **Hermetic differential** (`tools/tycho-build/run.sh`): a fixture tree built
  in a temp dir — a chain (`src → out1/out2 → final`) plus an independent
  branch — and legs for: [1] first build runs everything, build lines locked to
  `expected.out`, outputs exist; [2] **second build is a NO-OP** (empty stdout,
  exit 0 — the differential); [3] `touch` a source rebuilds only its
  dependents; [4] a failing recipe exits 1, prints `FAILED (exit N)`, and its
  dependents are skipped (outputs absent); [5] two clean builds are
  byte-identical (determinism); [6] bad buildfile / missing buildfile / unknown
  target exit 2. No temp path or host detail reaches the golden.
- **Wiring**: `make build-check` lane + ci step `[3o/21]`, following the
  ar-check/q-check shape.

Gate: `make build-check` (the only lane that runs the tool). The tool is pure
Tycho over existing corelib (no shim → no shim-check; no golden in the suite →
no goldens-check). Doc gates for plan.md. `make tools-check` untouched
(tychofmt + lsp only). `make test` cannot redden — nothing under `tests/`
changes.

## Not in scope

- Dependency discovery / globbing (no wildcards; explicit deps only).
- Phony-target declaration syntax (a recipe-less rule is the group).
- Shell-out escaping (recipes are shell lines by design, like make).
- The trie and the build-tool candidates are both closed; the plan is empty
  after this phase unless the owner files something new.

> Phase 1 evidence — 2026-08-04: all gates green. `make build-check` all 6
> legs (first-build golden, no-op differential, touch-only-dependents,
> failure-skips-dependents, determinism, exit-2 errors), doc gates ok,
> goldens-check ok. Nothing in `tests/` or `corelib/` moved, so `make test`
> and `make corelib` cannot redden for this phase.
>
> Implementation notes: the tool is pure Tycho over existing surface —
> `core:os.system` (recipes), `core:io.mtime` (up-to-date checks), `spawn` +
> `parallel for` + channels (a bounded worker pool — each fan-out chunk is a
> worker looping on a job channel, sending `(rule, code)` on a done channel;
> the scheduler is a spawned task). Three bugs found and fixed during the
> work, each a real Tycho learning: (1) `while` does not exist (`for cond:`);
> (2) my post-order DFS reversal was wrong — post-order push is already
> deps-before-dependents; (3) the scheduler's stall check fired when the
> build COMPLETED (remaining hit 0 mid-pass) and, before the fix, a stalled
> scheduler never sent the worker sentinels, hanging the `parallel for` —
> the sentinel-send is now on both the success and the stall path. Also
> learned: the tie-breaker is load-bearing — two outputs written in the same
> second compare equal by mtime, so the rebuilt-in-this-run flag (not mtime)
> drives chained rebuilds; the touch leg needed a `sleep 1` for the same
> reason.
