# What the next program says the language needs

> This plan is a fresh clone, 2026-08-04: the completed tycho-kvsrv plan
> lives at `docs/internals/plan-tycho-kvsrv-DONE.md`. The rule from that plan
> holds here: *does anything that is not the program written to want it need
> this?* A finding becomes a phase only when a second, independent caller
> exists.

## The program -- the DPLL/CDCL SAT solver  (tools/tycho-sat/)

The language stress is algorithmic depth: watched-literal unit propagation,
VSIDS decisions with an activity heap, first-UIP conflict analysis and
clause learning, restarts -- the intricate stateful machinery (per-literal
watch lists, an implication graph walk, a mutable clause database) that the
value-semantics model has not yet been asked to carry. DPLL/CDCL is
iterative (no deep recursion), so the recursion guard stays idle.

The differential is a corpus, not a vector, and it is hermetic:
- UNSAT ground truth, self-generated: the pigeonhole instances PHP(n) are
  constructible in a few lines of CNF and unsatisfiability is a published
theorem for every n.
- SAT ground truth, self-generated with a witness: plant a random
  assignment, generate clauses satisfied by it -> guaranteed SAT, and the
  solver's printed model is verified by the runner's OWN independent clause
  checker (a few lines of python that cannot be fooled by a wrong model).
- Determinism: the search is deterministic (no randomness; fixed restart
  schedule), so two runs are byte-identical.
- The learning claim is printed, not asserted: solving one instance with
  and without learning reports both conflict counts; if CDCL ever stops
  beating DPLL there, the golden reddens and a human looks.

Scope, honestly sized: clause deletion and phase saving are the classic
next refinements and are deliberately absent; a geometric restart schedule
carries the restarts. The solver reads DIMACS CNF from a file or stdin and
offers a built-in PHP(n) generator (`--php N`) so the theorem-based ground
truth needs no file plumbing.

## Phases

### Phase 1 -- the core: parser, BCP, VSIDS, CDCL  [DONE 2026-08-04]

The solver is ~480 lines: a DIMACS parser, first-UIP conflict analysis and
clause learning, VSIDS decisions with an activity heap (the max-heap carries
all the assigned/unassigned bookkeeping), geometric restarts, a
chronological --no-learn mode for the comparison, and a built-in PHP(n)
generator so the theorem-based ground truth needs no file plumbing. The
search is iterative; the recursion guard stays idle as promised.

Two design decisions were forced by experience, both recorded honestly:
- **The BCP is the naive full scan, not watched literals.** The first cut
  used the classic two-watch scheme; its invariants (watch lists must never
gain a stale false entry, and a clause whose watch dies must be revisited at
the level where the SECOND watch died) proved brittle to write correctly in
the first pass, so the propagation was replaced by a per-assignment scan of
the whole clause database -- correct BY CONSTRUCTION, one scan per trail
entry, fine at the gate's sizes. Watched literals are the classic
performance refinement, deferred.
- **The one-line bug behind the whole debugging saga: `backtrack` never
  reset `s.cur_level`.** Every analysis after a backtrack used a stale,
  too-high decision level, so the conflict clause's literals all appeared
  below the current level and first-UIP could not resolve them -- the
  solver alternately crashed, claimed false conflicts, and (with an early
  unsound fallback) answered PHP(3) SAT. The fix was one line
  (`s.cur_level = to_level`); the corpus gate caught it at every stage,
  which is the point of the ground-truth differential.

### Phase 2 -- the corpus gate  [DONE 2026-08-04]

`make sat-check` (ci step [3m/19], ~4s): PHP(2..9) all UNSAT (the
published theorem, rediscovered every run), three planted instances
(50/100/150 vars, fixed seeds) SAT with the runner's OWN independent clause
checker verifying the printed model clause by clause, determinism
(byte-identical repeats), and the learning comparison recorded in the
golden: CDCL 4 conflicts vs chronological DPLL 5 on the 100-var instance --
the learning claim is printed, not asserted, so the golden reddens if CDCL
ever stops beating DPLL. Measured: PHP(8) 13ms, PHP(9) 1.7s, PHP(10) 2.7s
-- the exponential resolution proofs showing up as expected.

**No language findings** -- the eighth program with zero compiler/runtime
defects. The Solver struct (eleven array fields, threaded inout through
the whole search) held up; the value-semantics model carried the mutable
clause database and the activity heap without complaint.

## Findings

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
