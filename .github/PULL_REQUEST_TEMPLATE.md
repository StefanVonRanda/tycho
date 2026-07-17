<!-- Thanks for contributing to Tycho! A few quick things before you open this PR. -->

## What & why

<!-- What does this change do, and why? Link any related issue. -->

## Checklist

- [ ] `make ci` passes locally (the full gate — there is no hosted CI).
- [ ] If this touches the language: the change works in **both** compilers
      (`src/tychoc.c` and `compiler/tychoc0.ty`) — otherwise `make fixpoint` goes red.
- [ ] New behavior is covered by a test (a golden in `tests/`, or a `tests/reject/`
      case for something that must fail).
- [ ] Docs updated if the change is user-visible (`docs/reference/` for behavior).

<!-- New to the codebase? CONTRIBUTING.md explains the build, the gate, and the parity rule. -->
