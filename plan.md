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

### 1. A NULL path reaches the `cc` command line

`cc ... -I(null) ...` then `sh: 1: Syntax error: "(" unexpected`, when the
compiler cannot resolve its corelib. `%s` on a NULL. The user is shown an error
about their shell, on the first command they run.
- **Done when:** an unresolvable corelib path is a diagnostic naming corelib and
  `TYCHO_CORELIB`, and no `(null)` can reach a command line.
- **Reproduce:** move `tychoc` away from `corelib/`, unset `TYCHO_CORELIB`,
  compile a program with no imports.
- **Gates:** `make test`, `sh scripts/entrypoints.sh`.

### 2. `-> [T]` misparses, but only with a `where` clause

`fn f(xs: [$T]) -> [T] where comparable(T):` gives "a fixed-size array length
must be an integer literal". Without the `where` the same signature compiles and
runs; with `-> [$T]` it compiles either way. The error names fixed-size arrays,
which the author never used.
- **Done when:** the three forms agree, or the refusal names the real cause.
- **Gates:** `make test` -- add a reject or diag fixture pinning whichever way it
  is resolved.

### 3. Generic struct type arguments: all-parameter or all-concrete

`Index($K, [$V])` AND `Index($K, [float])` are both refused with "may not
partially mention a type parameter", though the second mentions none partially.
The rule exists only in the error text -- `docs/reference/generics.md` does not
state it.
- **Done when:** the rule is in `generics.md`, and the message describes what it
  actually enforces.
- **Gates:** the two doc gates; `make test` if the check changes.

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
