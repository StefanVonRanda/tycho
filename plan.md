# What the next program says the language needs

> This plan is a fresh clone, 2026-08-04: the completed tycho-kvsrv plan
> lives at `docs/internals/plan-tycho-kvsrv-DONE.md`. The rule from that plan
> holds here: *does anything that is not the program written to want it need
> this?* A finding becomes a phase only when a second, independent caller
> exists.

## The program -- (to be chosen)

Seven programs in, the stress axes used are: data structures (tycho-kv B+
tree), bits (tycho-chess bitboards), arithmetic (tycho-rsa bignum),
networking + shared-state concurrency (tycho-kvsrv's actor store). The axis
still untouched: **algorithmic depth** — a solver or planner with real
heuristics and backtracking, where the differential is a corpus of instances
with known verdicts rather than a hand-checkable vector.

Candidates, honestly scored:

- **A DPLL/CDCL SAT solver.** Boolean constraint solving: unit propagation
  with watched literals, VSIDS decision heuristics with an occurrence heap,
  conflict-driven clause learning (implication graph), clause deletion,
  restarts. The differential is ground truth: the SATLIB and SAT competition
  corpora carry published SAT/UNSAT verdicts for thousands of instances, and
  the classic pigeonhole instances (`PHP(n)`) have exact unsatisfiability
  proofs the solver must rediscover. The deepest algorithmic workout the
  language has faced — and the concurrency model could even be probed again
  (parallel cube-and-conquer is a known technique), though that is optional.
- **A small relational database.** A SQL-subset parser + a naive planner over
  a persisted store — re-proves the kv B+ tree and adds a parser; the
  weakest new stress of the three.
- **A git-style content-addressed object store.** blobs/trees/commits hashed
  with core:sha256, pack files, delta compression — re-proves the archive
  machinery tycho-ar already showed; overlapping.

The plan's default is the SAT solver: it is the one axis (algorithmic
depth) the language has not been pushed on, its differential is a corpus
rather than a vector, and its heuristics (watched literals, VSIDS, clause
learning) are the kind of intricate stateful code the value-semantics model
has not yet been asked to carry.

## Findings

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
