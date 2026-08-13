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
- Verified 2026-08-13: `core:os` CAN run a subprocess and return its exit code
  and its captured stdout (`os.run`), including 12 at once from inside a
  `parallel for` at three pool widths. The recipes are real; the fallback to an
  internal action was not needed.

## Phases

- [ ] **Phase 3 — the affine-handle diagnostic has no source location**
  - `tychoc: a task handle cannot be stored in a container or aggregate --
    wait(t) first` prints with no `path:line`, unlike every other diagnostic.
    Probed 2026-08-13 with `hs := [spawn work(1), spawn work(2)]`.
  - Done when: it names the file and line of the offending spawn, with a reject
    fixture pinning the text.
  - Verify: `make test`, baseline 670.
