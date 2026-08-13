# tycho-make — a dependency-driven build system

## Phases

- [ ] **Phase 1 — FRICTION #27 is wrong; replace the wavefront with a work queue**
  - `docs/internals/FRICTION.md:3348` claims bounded parallelism over a runtime
    graph "has exactly one expressible shape". Probed false 2026-08-13:
    - 4 `parallel for` bodies each `recv` from one shared channel → `total 56`;
    - two spawned workers sharing one jobs channel → `total 56`, split **2+6**
      (uneven = pulled on demand, not statically partitioned);
    - a full coordinator loop over a diamond DAG → `order 0123`, no deadlock.
    The entry looked for a design where WORKERS share a mutable indegree table.
    Go's does not: the table lives in the one coordinator thread, workers are
    stateless pullers. Neither the affinity rule nor the capture rule applies.
  - Scope: rewrite `tools/tycho-make/build/build.ty@run_level` as a coordinator
    plus N workers over a jobs channel; correct FRICTION #27 to record what is
    actually expressible and what the wavefront cost was.
  - Done when: a node becomes runnable as soon as its own deps finish, not when
    its whole level does — asserted by a DAG where one long chain sits beside a
    wide level, with the runner naming a node that must start before the level
    it follows completes. The wavefront cannot pass that assertion.
  - Verify: `make make-check`; log byte-identical over two runs and at
    `TYCHO_THREADS=1` and `2`, which is what ordered reassembly is for.
