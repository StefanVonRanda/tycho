# Open

One phase, not completable inside a coding session. Everything else from the
2026-08-15 sweep is done and in `git log` — evidence lives in the commit
messages, per the rule in `CLAUDE.md`. Publishing 0.7.0 was phase 1 and is
deleted rather than ticked, per the same rule.

## Get the external review (ROADMAP §7)

- **Scope:** send `docs/internals/audit-brief.md` to a reviewer outside the project.
- **Done when:** someone who did not write this code has reported findings, or
  reported which classes they looked for and found nothing in — the second is
  worth as much and the brief asks for it.
- **Verify:** nothing to run. This phase has no gate by construction.
- **Why it cannot be absorbed into a coding phase:** FRICTION #77. The
  interior-NUL rule is normative in `docs/spec/14-ffi.md`, a deliberate sweep ran
  for it on 2026-08-13, and three packages carry guards naming each other — yet
  the two highest-severity sites were never on that list, one collapsing two
  passwords into a single derived key. A sweep covers the sites its author has in
  mind, which are the ones already fixed.

## Seven dead functions blessed by a baseline

`scripts/entrypoints.warn` locks the exact warning set the tree emits, and on
2026-08-18 seven `is never called` warnings were RECORDED into it rather than
acted on. A baseline is the right instrument for "this is the known state", but
it also means these seven are now permanently blessed and nothing says the
cleanup is owed.

- `examples/raytrace/main.ty@v_mul`, `tools/tycho-chess/main.ty@popcnt`,
  `tools/tycho-ed/buf/buf.ty@text`, `tools/tycho-make/graph/graph.ty@order`,
  `tools/tycho-sheet/cell/fold.ty@_num_of`, `tools/tycho-stat/num/num.ty@sum`,
  `tools/tycho-tmpl/doc/doc.ty@count`.
- **Why it was not just done:** deleting one exposes the next. `tycho-vm` had 11
  and removing two surfaced two more, so this is a cascade, not seven edits, and
  it belongs in its own pass rather than smuggled into a compiler change.
- **Done when:** each is deleted or given a caller, and `entrypoints.warn` has
  no `is never called` line left.
- **Verify:** `sh scripts/entrypoints.sh`. Each tool also has its own lane
  (`make vm-check`, `ed-check`, `make-check`, `sheet-check`, `stat-check`) --
  run the one for whatever gets deleted; a golden moves if the deletion was
  wrong.
- **Not a compiler question.** The warning is doing its job; this is the debt it
  found.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
