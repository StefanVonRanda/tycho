# Open

One phase. Everything else from the 2026-08-15 sweep is done and in `git log` —
evidence lives in the commit messages, per the rule in `CLAUDE.md`. Publishing
0.7.0 was phase 1 and is deleted rather than ticked, per the same rule.

## Rewrite the bootstrap compiler in Tycho (`compiler/`)

- **Scope:** a second Tycho compiler written in Tycho, targeting the whole
  locked language, not a subset. Seven packages under `compiler/`: `lex parse
  ast types lower emit driver`, entry `compiler/main.ty`, `make tychoc1` ->
  `./tychoc1`. `runtime/tycho_rt.c` is unchanged and stays C; `tychoc1` emits
  against the same runtime ABI.
- **Order:** `print(1)` end to end through lex->parse->emit before `types/`
  is written. Then climb the `make test` pass count.
- **Done when:** `TYCHOC=./tychoc1 make test` is green at the same count as
  `./tychoc`, and the fixpoint holds -- `tychoc1` built by tychoc, then
  self-built twice, the two emitted `.c` identical.
- **Verify:** no new harness. 55 `run.sh` files hard-code `TYCHOC=./tychoc`
  (`tests/run.sh:33`); change each to `TYCHOC="${TYCHOC:-./tychoc}"` and the
  existing lanes become the differential. Not checked: whether every one of
  those 55 routes all invocations through `$TYCHOC` rather than an inline
  `./tychoc`. If any does not, the override tests the wrong binary silently.
- **The cost is diagnostics, not codegen.** Goldens in `tests/` pin message
  text, batching and the second "declared here" location. Compiling the corpus
  correctly still leaves lanes red until the wording matches.
- **Decide before starting:** a fresh clone has no `tychoc1`, so either
  `src/tychoc.c` stays the bootstrap forever or a generated `.c` is committed.
  This changes the release story.
