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

## §1 program 3 -- the FFI / soa / handle surface

Brief to hand an LLM agent verbatim. It must not read `src/tychoc.c`, and must
not be given `~/.claude/skills/tycho-syntax` -- that file is the author's scar
tissue and handing it over defeats the probe.

```
Install: gh release download v0.7.0 -R StefanVonRanda/tycho, extract, run ./tychoc
Read: README.md and docs/ only. Never src/tychoc.c.
Build ONE real program, ~300-500 lines, that MUST use all three:
  - `extern fn` against a C library (libc is fine: stat, opendir, readdir)
  - a `handle` type with a free function, for the resource it opens
  - `soa` for the bulk records it collects
Keep a log: every place the docs did not answer, or the compiler surprised you.
  Record the .ty snippet, what you expected, what happened, which doc page you
  checked FIRST, and what went RIGHT -- a log that only complains is not evidence.
Do not read the compiler source to unblock yourself. Stuck IS the finding.
Return: the program, and the log.
```

- **Why this surface:** tycho-diff covered algorithms, tycho-hash concurrency,
  the markov run maps/structs. FFI, `soa`, `handle` and newtypes have never been
  touched by a non-author program, and `ROADMAP.md` §1 now asks for different
  halves rather than more programs.
- **Done when:** the program and a `FRICTION-OUTSIDE.md` land under
  `tools/tycho-<name>/`, matching how tycho-diff and tycho-hash are filed.
- **Gates:** `sh scripts/entrypoints.sh` (it compiles everything under `tools/`),
  plus a `run.sh` and lane if the program is worth keeping.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
