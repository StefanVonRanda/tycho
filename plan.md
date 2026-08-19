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

## Parse errors inside an unclosed bracket

Parse errors batch now (2026-08-19) -- EXCEPT when the failed construct leaves a
bracket open. Then only one is reported, and the cause is in the LEXER, not the
parser.

**Measured, from a token dump.** `fn a(:` never closes its `(`, and the lexer
suppresses NEWLINE/INDENT/DEDENT while `bracket_depth > 0`
(`src/tychoc.c@bracket_depth`) -- correct for line continuation. The
consequence is that the whole rest of the file becomes one logical line: the
dump showed zero layout tokens after the unclosed `(`. No parser-level resync
can recover that, because the block structure is gone before the parser runs.
This is why the first attempt looked like a resync bug and was not one.

**What would fix it:** in the lexer, treat a line beginning in column 1 with a
RESERVED declaration keyword (`fn`, `struct`, `enum`, `handle`, `type`) as
proof the bracket was never closed -- report the unclosed bracket at its opening
line and reset `bracket_depth`. Those five are reserved
(see `~/.claude/skills/tycho-syntax`), so none can legally begin a continuation
line; the contextual words (`const`, `import`, `package`, `extern`) must NOT be
used, since a variable may be named after them.

- **Done when:** two `fn a(:`-style headers report 2, and the error names the
  unclosed bracket's own line rather than a confused downstream one.
- **Verify:** `make test` (expect 732). **Gates:** `make test`,
  `make editors-check` (it parses every `.ty` in the tree, so a lexer change is
  exactly what it exists to catch), `sh scripts/entrypoints.sh`.

## Parse-error batching -- reverted twice, and why

Type errors batch (per statement). PARSE errors still report one. Two attempts
are now recorded so a third does not repeat either.

**Attempt 1 (column-based resync).** Reported both errors and invented a third
at the body of every later declaration. Cause: INDENT/DEDENT are lexed tokens
carrying `col = 0`, so a column scan walks past them and loses the DEDENT that
closes the aborted body.

**Attempt 2 (depth-tracking resync + lexer unclosed-bracket detection).** Got
all four batching classes correct and passed make test 733/0, asan-self,
editors-check, entrypoints, corelib and fuzz-reject at 40 seeds -- then
`make ci` ran fuzz-reject at **200** seeds and found a heap-buffer-overflow in
tychoc on seed 95 (`fuzz/findings/reject_seed_95.ty`, kept). Reverted in
`78335b81` / `ab976f03`.

**What is known about the crash**, so the next attempt starts here:

- ASan: `heap-buffer-overflow` in `at()` (`src/tychoc.c@at`), called from
  `parse_program`'s declaration-loop condition; the allocation trace points at
  `tv_push`'s realloc of the token vector.
- `origin/main` on the same input reports only a pre-existing 96-byte leak, no
  overflow -- so it was introduced by the parse recovery, not latent.
- THREE fixes were tried and none removed it: clamping the resync to a
  precomputed EOF index; making `ProcVec out` static (it is written between
  setjmp and longjmp, so as a plain local its fields are indeterminate after
  recovery -- that reasoning is sound and the change should be kept in any
  retry, it simply was not the whole cause); and bounding the loop condition
  itself by that EOF index.
- Untested hypothesis, and the most promising: `force_type_decl`
  (`src/tychoc.c@force_type_decl`) parses a type declaration OUT OF ORDER from
  inside another declaration's parse, over `g_tdecl_toks`. A longjmp out of that
  nested parse unwinds through a second, inner Parser -- so `decl_start` and
  `ps.p` belong to different parsers. Verify that before writing more code.
- 40 seeds was not enough to see it; use `python3 fuzz/run_reject.py 200`.

- **Done when:** two malformed `fn` headers report 2, and
  `python3 fuzz/run_reject.py 200` is FAIL=0.
- **Gates:** `make test`, `sh scripts/asan_self.sh`,
  `python3 fuzz/run_reject.py 200`, `make editors-check`.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
