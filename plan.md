# What the next program says the language needs

> This plan is a fresh clone, 2026-08-04: the completed tycho-rsa plan lives
> at `docs/internals/plan-tycho-rsa-DONE.md`. The rule from that plan holds
> here: *does anything that is not the program written to want it need this?*
> A finding becomes a phase only when a second, independent caller exists.

## The program -- (to be chosen)

Six programs in, the stress axes used are: data structures (tycho-kv B+
tree), bits (tycho-chess bitboards), value semantics / inout threading (all),
concurrency (kv pscan's four named spawns; chess's parallel-for root),
arithmetic (tycho-rsa on bignum). The two axes the tools have NOT touched:
**networking** (only the repo's examples — tycho-httpd, weblog — run over
core:net) and **systems-y I/O** (sockets, timers, processes).

Candidates, honestly scored:

- **An HTTP key-value server.** A second program over core:net after
  tycho-httpd — so any core:net finding has a second caller BY CONSTRUCTION,
  the demand bar the plan exists to police. A concurrent request loop (a
  listener task, a worker task per connection, a shared store), GET/PUT/DELETE
  against the tycho-kv-style store, keep-alive, status codes. Risks: it must
  not be a copy of tycho-httpd (which already serves files); the interesting
  part is the shared-state concurrency, which the store's pscan probed only
  read-only.
- **A DPLL/CDCL SAT solver.** Boolean constraint solving: watched literals,
  decision heuristics (VSIDS), conflict-driven clause learning with an
  implication graph, clause deletion. The differential is ground truth: SAT
  competition instances have published SAT/UNSAT verdicts. Algorithmic depth
  on top of bit-level and heap machinery the language has shown — but it
  re-uses more than it stresses.
- **A small relational database.** A SQL-subset parser + planner over the
  tycho-kv B+ tree — re-proves the store and adds a parser; the weakest new
  stress.

The plan's default is the HTTP KV server: it pushes the two untouched axes
at once (networking + shared-state concurrency), and its findings meet the
demand rule by construction.

## Findings

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
