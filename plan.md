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

## Next agent probe -- generics, newtypes, or the error paths

`docs/internals/probe-procedure.md` has the setup, the brief shape and how to
read the result. The 2026-08-19 FFI run is recorded in
`docs/internals/probe-ffi-2026-08-19.md`; its two findings are fixed and the
program was thrown away, the record being the artifact.

- **Aim:** measured over the existing records, ZERO non-author programs touch
  generics, newtypes, `subscript`, `bounded[N]`, `select`, or enum/Option/Result
  error paths. Channels have one. Pick one of the zeros.
- **Two things the last run taught, both in the procedure:** build the compiler
  from `main` rather than the v0.7.0 tarball, or part of the report describes
  diagnostics already fixed; and run the agent's own code under the sanitizers,
  because the best finding last time was a use-after-free in its C shim that its
  log never mentioned.
- **Done when:** a record lands under `docs/internals/`, and anything actionable
  in it is either fixed or written here.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
