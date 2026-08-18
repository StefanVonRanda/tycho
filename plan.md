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

## Batch error reporting (`src/tychoc.c@die_at`)

- **Scope:** `die_at` calls `exit(1)`, so a file with three type errors reports
  one. Measured 2026-08-18: three `"x" + 1` errors -> 1 reported.
- **Done when:** that fixture reports 3, and `tests/reject/` + `tests/diag/`
  still report exactly one error each where they pin one.
- **Two halves, and only one is hard.** Read out of Mojo's frontend
  (`KGEN/lib/MojoParser`, sparse clone, 2026-08-18):
  - *Parse errors:* copy `ParserBase::skipUntilIndentation(minIndent, ...)` --
    on error, skip tokens until a line at or below the failed construct's
    indentation, tracking bracket depth, then resume at the next declaration.
    Mojo is indentation-structured like Tycho, so this maps directly. It
    suppresses diagnostics WHILE skipping, which is the cascade fix.
  - *Type errors:* needs a poison type. Tycho has `T_PENDING` and `T_UNBOUND`
    but neither survives resolve, so neither works as one.
- **Copy the exit policy too:** Mojo emits every diagnostic, then checks
  `diags.isErrorEmitted()` once at the end and refuses to emit IR
  (`KGEN/lib/MojoParser/EntryPoint.cpp`). Tycho must likewise never write C
  after any error.
- **Verify:** `make test` (expect 728). **Gates:** `make test`,
  `sh scripts/entrypoints.sh`. Not `make ci`.
- **Not doing:** the `--allow-unused` downgrade flag, decided 2026-08-18.

## Attached notes on diagnostics (`src/tychoc.c@die_at`)

- **Scope:** every Tycho diagnostic names ONE line. Mojo attaches a second
  location to the same error -- "declared here", "the other argument is here".
  Counted 2026-08-18: 149 `attachNote` sites in `KGEN/lib/MojoParser`, against
  1 `note:` in `src/tychoc.c`.
- **Where it pays first:** the errors whose interesting location is a
  DECLARATION, not the use -- the newtype refusals (`make ledger-check`), the
  affine-handle refusals (`make fh-check`), and the per-file import gate added
  on this branch, which says a file did not import a package without pointing at
  where a sibling did.
- **Done when:** at least those three carry a note naming the other line, and a
  diag fixture pins the two-line shape.
- **Independent of batch reporting** -- do either first.
- **Verify:** `make test` (expect 728; `tests/diag/*.err` goldens move by
  design). **Gates:** `make test`, `make ledger-check`, `make fh-check`.
- **Unchecked:** me read the note call sites, not `SharedState.cpp` (4,080 lines)
  where their diagnostic engine lives, so "cheap" is an estimate.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
