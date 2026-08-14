<!-- Thanks for contributing to Tycho! A few quick things before you open this PR. -->

## What & why

<!-- What does this change do, and why? Link any related issue. -->

## Checklist

- [ ] `make ci` passes locally (the full gate — there is no hosted CI).
- [ ] If this touches the language: `src/tychoc.c` is the only compiler you need
      to change. `compiler/tychoc0.ty` was FROZEN on 2026-07-29 and cut from every
      gate, so the second-implementation check no longer exists — do not try to
      keep it in step, and do not run `make fixpoint` (the target is gone).
- [ ] New behavior is covered by a test (a golden in `tests/`, or a `tests/reject/`
      case for something that must fail).
- [ ] Docs updated if the change is user-visible (`docs/reference/` for behavior).

<!-- New to the codebase? CONTRIBUTING.md explains the build, the gate, and the parity rule. -->
