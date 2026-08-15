# Open

Two phases, neither completable inside a coding session. Everything else from
the 2026-08-15 sweep is done and in `git log` — evidence lives in the commit
messages, per the rule in `CLAUDE.md`.

## 1. Publish 0.7.0

- **Scope:** tag, push, create the release. Nothing else.
- **Done when:** `gh release list` shows v0.7.0 as a prerelease.
- **Verify FIRST — a checksum proves intact, never current:** `make release-check`,
  then extract the tarball and run `tests/reject/` through the *shipped* `tychoc`
  plus a positive control that must compile. That check has caught a stale
  artifact twice, including one shipping two known crypto defects under a version
  whose CHANGELOG said they were fixed.
- **Gates:** `make release-check` only. Do not run the sweep for this.
- **Not a build step.** ROADMAP §6 calls it a decision. It was offered in-session
  on 2026-08-15 and deferred, so it needs an explicit go-ahead, not an assumption.

## 2. Get the external review (ROADMAP §7)

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

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
