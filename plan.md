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

Type errors batch (per statement, landed 2026-08-19). PARSE errors still report
exactly one: they fire before any recovery point, so `die_at` flushes and exits.

**An attempt on 2026-08-19 was built, measured and REVERTED.** Recording why, so
the next attempt does not repeat it. A `setjmp` in `parse_program`'s declaration
loop plus a resync that skipped forward to the next token in column 1 did report
both of two malformed `fn` headers -- and added a spurious third error at the
body of every declaration after the first failure.

**Cause, from a token dump at the landing point:** `TK_INDENT` and `TK_DEDENT`
are lexed tokens carrying `col = 0` (`src/tychoc.c@TK_INDENT`), so a
column-based scan walks straight past them. The `DEDENT` closing the aborted
body gets skipped, the block nesting the parser is counting never rebalances,
and the next declaration is parsed against a stream missing its `NEWLINE`/
`INDENT` pair -- the dump showed `fn main ( ) :` followed directly by the body's
first identifier.

**What a working attempt needs:** resync on the INDENT/DEDENT structure rather
than on columns -- track the depth the failed construct opened and consume
exactly the DEDENTs that close it, which is what Mojo's
`ParserBase::skipUntilIndentation` does with its `openBrackets` vector. Clearing
`Parser.depth` alone is not enough; that was tried and changed nothing.

- **Done when:** two malformed `fn` headers report 2, and a file with one
  malformed header followed by valid declarations reports exactly 1.
- **Verify:** `make test` (expect 731). **Gates:** `make test`,
  `sh scripts/asan_self.sh`, `sh scripts/entrypoints.sh`.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
