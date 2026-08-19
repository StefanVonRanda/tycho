# `select` probe, 2026-08-19 — a select that takes no arm at all

A fresh agent built a 474-line parallel source auditor: 5 channels, 3 `select`
sites, 4 scanner and 2 aggregator tasks, shutdown by close-ordering. **The
program is not the artifact — this is.** It was thrown away.

## The finding: `recv` + `default` with no `closed:` arm goes dead on close

After the channel is closed and drained, a `select` with a `recv` arm and a
`default:` arm takes **neither**. Measured here, 100 iterations of that shape
against a closed channel holding one buffered value:

```text
recvs=1 defaults=0 total=1
```

One iteration received the buffered value. The other 99 selected nothing at all.
A loop of that shape with no iteration bound spins forever, doing no work, with
no crash and no diagnostic.

`default` exists precisely to mean "nothing is ready, proceed anyway", so a
closed channel is the one case where it must fire. The docs describe `default`
and `closed:` as independent arms and do not say a `recv` arm goes dead on close.
The agent also probed the precedence question and found `closed:` outranks
`default:` when both are present — also undocumented.

Its workaround was to drain the channel while still open, which is not something
a reader would arrive at from the reference page.

## Also reported

- **No top-level `X := 6`** — `const NAME = expr` is the form, stated in spec
  §12.2 but not on the reference page. The error is `expected 'fn'`, the only
  diagnostic in the run that did not name its own fix.
- `tychoc` compiles every `.ty` beside the entry file as one package, so two
  programs cannot share a directory. Third probe in a row to hit this.
- The deadlock lint is a genuine true positive: it warned about a missing
  `close(ch)`, and the built program really did hang.
- `sort.by_key`'s deprecation warning printed the exact `sort_by` rewrite.

## What went right

Determinism held where it matters: **identical output over 10 consecutive runs
and at `TYCHO_THREADS=1`, `2` and `32`** — 13 runs, one md5. After the corpus
changed, five more runs agreed again. The agent found no nondeterminism.

Its first 60-line concurrency probe compiled and ran correctly with zero
iterations. Tasks returning aggregates — a struct holding arrays of structs
holding arrays — needed nothing annotated. Tuple returns, recursive `inout`
parameters, and `push(map[k], v)` on a `[string: [string]]` all worked first
try. The corelib-not-found message named both search paths and the override
variable, which is the diagnostic fixed earlier the same day.
