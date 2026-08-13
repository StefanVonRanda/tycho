# tycho-make — a dependency-driven build system

## Goal
A build tool: parse a rulefile, build a DAG, decide staleness from content
hashes and mtimes, execute in dependency order with bounded parallelism, and
print a deterministic build log. Chosen to stress affine task handles under a
real scheduler, which is the constraint no existing program has had to design
around.

## Pre-flight
- Worst case: it duplicates tycho-flow's pipeline work and finds nothing.
  Mitigation: flow's stages are a fixed linear chain; here the graph is data
  read at runtime, so the scheduler cannot be written as a chain of spawns.
- Reversibility: new files under `tools/tycho-make/` only.
- Verified 2026-08-13: `a := spawn f(1)` then `wait(a)` works; spawn+wait paired
  inside a loop works; `[spawn f(1), spawn f(2)]` is REFUSED — "a task handle
  cannot be stored in a container or aggregate". So a ready-queue of handles is
  not expressible; fan-out must be `parallel for`, paired spawn/wait, or
  channels. Design the scheduler around that, do not fight it.
- Assuming: `core:os` can run a subprocess and return its exit code and output.
  NOT probed. If it cannot, the "recipe" becomes an internal action and the
  program still stands — say so rather than working around it silently.

## Phases

- [ ] **Phase 1 — graph/ + the lane, together**
  - Scope: `tools/tycho-make/graph/` (rulefile parse, DAG, topological order,
    cycle detection that NAMES the cycle), a thin driver, `run.sh`, golden,
    `make make-check`, a `scripts/ci.sh` step, both gate tables.
  - Done when: `make make-check` is green from a clean checkout and reddens for
    a dropped edge.
  - Verify: `make make-check`, `make goldens-check`, `sh scripts/entrypoints.sh`.
  - The lane ships WITH slice 1. It lagged the program on four of five previous
    programs; tycho-sim is the only one that got this right.

- [ ] **Phase 2 — staleness and the executor**
  - Scope: content hash + mtime staleness, bounded-parallel execution honouring
    dependencies, the build log.
  - Done when: log byte-identical over two runs and at two `TYCHO_THREADS`
    values; a no-op rebuild does zero work and says so; touching one input
    rebuilds exactly its dependents, asserted against literals in the runner.
  - Verify: `make make-check`.

- [ ] **Phase 3 — the affine-handle diagnostic has no source location**
  - `tychoc: a task handle cannot be stored in a container or aggregate --
    wait(t) first` prints with no `path:line`, unlike every other diagnostic.
    Probed 2026-08-13 with `hs := [spawn work(1), spawn work(2)]`.
  - Done when: it names the file and line of the offending spawn, with a reject
    fixture pinning the text.
  - Verify: `make test`, baseline 670.
