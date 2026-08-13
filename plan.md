# Close the last two friction items, then audit the last two packages

## Goal
`docs/internals/FRICTION.md` has exactly two open items after 2026-08-13's
triage and audit: #33 (`_` is not a discard, and the diagnostic does not say so)
and open-list item 8 (no direct spelling for N workers, costed in
`docs/rfc/parallel-for-width.md`). Then the audit method that produced #29-#33
runs again against the last two corelib packages with no consumers.

## Pre-flight
- Worst case: a language change (phase 2) accepts a program it should refuse, or
  silently ignores the width and every gate stays green. Mitigation: phase 2's
  fixture COUNTS workers; a width that does nothing must redden it.
- Reversibility: phases 1 and 2 touch `src/tychoc.c`; each is one commit and
  revertible. Phase 3 adds files only.
- Verified 2026-08-13 at `cb148ad2`: `_ = f(1)` and `_ = r(1)` both die
  "assignment to unknown variable '_'"; `[spawn work(1), spawn work(2)]` is still
  refused; `core:sqlite` and `core:testing` have zero consumers outside corelib/.
- Assuming: the RFC's three call sites are still the only ones. Phase 2 re-reads
  them before editing.

## Phases

- [ ] **Phase 3 — audit `core:sqlite` and `core:testing`**
  - Same method as `tools/tycho-snap`: one real program, findings recorded as they
    are hit, positive controls where a claim is made.
  - `core:testing` is the sharper subject — a test framework nothing tests.
  - New findings go in as FRICTION #34+; a package that behaves gets a line under
    "What did not go wrong".
  - Verify: whatever the program touches, plus `make corelib` if a package changes.
