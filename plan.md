# What the next program says the language needs

> This plan is a fresh clone, 2026-08-04: the completed tycho-sat plan lives
> at `docs/internals/plan-tycho-sat-DONE.md`. The rule from that plan holds
> here: *does anything that is not the program written to want it need this?*
> A finding becomes a phase only when a second, independent caller exists.

## The program -- (to be chosen)

Eight programs in, the stress axes used are: data structures (tycho-kv B+
tree), bits (tycho-chess bitboards), arithmetic (tycho-rsa bignum),
networking + shared-state concurrency (tycho-kvsrv's actor store),
algorithmic depth (tycho-sat's CDCL). The axis still untouched:
**systems-y I/O** -- spawning subprocesses, reading the filesystem's
metadata, reacting to signals, wiring a pipeline of external tools.

Candidates, honestly scored:

- **A build tool.** A make-like dependency graph: source files, outputs,
  mtime-based up-to-date checks, a DAG of build steps, and -- the untried
  part -- PARALLEL execution of independent steps via the concurrency model
  (a step that spawns its dependents, a channel for results). It is
  dogfood-adjacent: this repo's own Makefile is the reference for what a
  Tycho build tool must express. The differential is a fixture tree: a
  small project with known artifacts, built twice (second build must be a
  no-op -- the mtime check), and a forced rebuild after touching a source.
  Stresses os.run (untouched), io metadata, and the conc model again.
- **A terminal roguelike.** A turn-based game with an ANSI-rendered grid on
  stdout: keyboard input, a game loop, procedural generation. Stresses
  terminal I/O and a stateful game loop; the differential is deterministic
  seeds making the render output golden-lockable. Weaker than the build
  tool on the systems axis.
- **A second solver-adjacent tool.** A Sudoku/graph-coloring constraint
  engine over the sat machinery -- re-proves tycho-sat; weakest.

The plan's default is the build tool: it is the one untouched axis, its
differential (the fixture tree, the no-op second build) is hermetic, and
its parallel step execution gives the concurrency model a second real
workout after the kv store's pscan and the chess engine's parsearch.

## Findings

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
