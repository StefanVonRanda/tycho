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

## Batch error reporting -- parse errors

Statement-level recovery landed 2026-08-19: several errors in one proc are all
reported now. What remains is PARSE errors, which fire before any recovery point
exists, so `die_at` flushes and exits on the first one.

- **Scope:** Mojo's shape (`KGEN/lib/MojoParser/ParserBase.cpp@skipUntilIndentation`):
  on error skip to a line at or below the failed construct's indentation,
  tracking bracket depth, suppressing diagnostics while skipping.
- **Done when:** a file with two malformed `fn` headers reports 2. Today it
  reports 1 (measured 2026-08-19).
- **Verify:** `make test` (expect 731). **Gates:** `make test`,
  `sh scripts/asan_self.sh`, `sh scripts/entrypoints.sh`.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
