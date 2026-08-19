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

## From the generics probe (2026-08-19)

Record: `docs/internals/probe-generics-2026-08-19.md`. Each rebuilt on `main`
before being written here; the program was thrown away.

**Items 1-3 fixed 2026-08-19.** A NULL corelib path no longer reaches the `cc`
line; `-> [T]` after a `where` clause parses as an array; and the generic
type-argument rule is documented with a message that describes it. What is left:

### 4 and 5 -- documentation only

`fn name$(K,V)()` is not the declaration form (the reference shows `name$(T)`
only at calls; the guide has the declaration form and they do not cross-
reference). And `docs/reference/packages.md` buries "a file with an `import` is a
package, so give it its own directory" in a last paragraph, where it reads as
style advice rather than the cause of a collision in a file you did not name.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
