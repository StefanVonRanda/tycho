# Friction writing `tycho-diff`, from outside

`ROADMAP.md` §1 asks for programs written by someone who did not write the
compiler, with the friction recorded. This is one such program: a Myers O(ND)
line differ with unified output, ~180 lines of Tycho across two packages, written
2026-08-14 against `docs/` only — the tutorial, `docs/reference/builtins.md` and
the corelib sources — without reading `src/tychoc.c` for any of it.

**It is one program by one non-author.** §1 asks for three by two people. This
does not close it; it is a down payment, and the findings below are the point.

Everything here is a first-contact reaction, kept even where the answer turned
out to be documented somewhere I did not look. That is the data §1 is after: an
author knows where to look.

**All four diagnostics below were fixed the same day**, each with a fixture under
`tests/diag/`. The wording quoted in each section is what the compiler said
BEFORE; the reports are left in the past tense they were written in, because the
point of this file is the first-contact experience, not the changelog.

---

## 1. In `package main`, the `die` warning offers a remedy the next line forbids

Naming a helper `fail`-like is the first thing a CLI program does. `die` is a
builtin, and the compiler says so extremely well — then contradicts itself:

```
warning: `die` collides with the builtin of the same name -- every unqualified
  `die(...)` calls the BUILTIN, so this procedure is unreachable by that name
  (§3.7). Rename it, or call it qualified as `pkg.die(...)`.
error: 'die' is already defined
```

The warning's second remedy is impossible here. Measured both ways: in a
**library** package the declaration is accepted, only warns, and `lib.die("x")`
really does call mine. In `package main` it is a hard error — `main` has no
qualified name to reach itself by, so "call it qualified" cannot be done.

The warning is right in general and wrong in the one package where every
newcomer writes their first program. Cost: two minutes, but the two minutes are
spent doubting a diagnostic, which is worse than being told plainly. Suggested
fix: in `package main`, drop the qualified-call half of the remedy. **Done** — the
explanation is kept in full and only the impossible remedy is replaced, since for
`len` (which warns and compiles) "unreachable by that name" is the load-bearing
half. `tests/diag/shadow_builtin_main.ty` pins the warning AND the error together,
because it was the pair that was wrong.

## 2. A missing import is reported as a missing symbol

```
error: package 'arrays' has no symbol 'fill'
```

`arrays.fill` exists (`corelib/arrays/arrays.ty@fill`). The actual mistake was
that I had not written `import "core:arrays"` in that file. The message names
the symbol as the problem, so I went and re-read the corelib source to check I
had the name right — the one place that could not tell me anything.

Suggested fix: when the package prefix is not imported in this file, say so
first. **Done** — appended rather than substituted, so a self-qualified name
(which is not "imported" either) keeps its old message.
`tests/diag/qualified_no_import.ty`.

## 3. `print`/`println` are paired; stderr has only `eprint`

`eprintln` does not exist. The suggestion steers you the wrong way:

```
error: unknown procedure 'eprintln'; did you mean 'println'?
```

For the message a CLI is trying to emit, `println` is the **wrong stream** — it
puts errors on stdout, where they corrupt whatever the tool is piped into. This
program's own gate asserts stdout stays empty on all five error paths, which is
the rule the suggestion quietly breaks.

`eprint` is documented, at `docs/reference/builtins.md:107` — ninety lines below
`print` and `println` at `docs/reference/builtins.md:16-17`, in a different table. A newcomer reading the
print family does not scroll past the intervening sections.

Suggested fixes, either alone would do: add `eprintln`, or make the suggestion
for `eprintln` be `eprint` rather than `println`, or move the stderr row next to
its stdout siblings. **The second was done** —
`unknown procedure 'eprintln' -- stderr has no println; use eprint(s + "\n")`.
`tests/diag/eprintln_wrong_stream.ty`.

## 4. `main` cannot return a status, and the error does not mention `exit`

```
error: 'main' returns nothing or Result(void, string), not int -- an Err reaching
  the entry point is printed, so the error type is the message
```

`diff(1)`'s contract is three statuses: 0 same, 1 different, 2 error. So is
`grep`'s, `cmp`'s and `test`'s. `Result(void, string)` cannot express it — it has
exactly two outcomes and one of them prints.

The capability is there: `exit(code)` is a builtin, documented at
`builtins.md:23`, and five programs under `tools/` already use it. But the
diagnostic names only the shape that cannot do the job, so the reader's next move
is to redesign around a two-outcome status. Adding "…or set the status with
`exit(code)`" to that message closes the gap at no design cost. **Done** —
the message now names `exit(code)` and cites diff(1)'s 0/1/2 as the case.
`tests/diag/main_returns_int.ty`.

## 5. What made the program *easier* than expected

Recorded because a friction log that only complains is not evidence.

**Value semantics made the hard part of Myers trivial.** The algorithm keeps a
frontier array `v` and must snapshot it at every edit distance to backtrack later.
In C that is a `memcpy` per round and a lifetime question; in a GC'd language it
is a `.clone()` you must remember. Here:

```tycho
push(trace, v)      # a SNAPSHOT, not an alias
```

is correct as written, and it is correct for the same reason the language is
built the way it is. I did not have to think about it once. That is the thesis
working.

**The `Result` + `match` idiom read well** for `parse_int_checked`, and
`enum Edit: Keep(int, int) / Del(int) / Ins(int)` with `match` is exactly the
shape an edit script wants — the compiler checked I handled all three arms.

## 6. Two shell traps, in the gate rather than the language

Not Tycho's fault, recorded because they nearly produced a false verdict about
Tycho, which is the failure mode this repo cares most about.

- `cmd && bad "..."` under `set -e` **kills the script silently** when `cmd`
  correctly exits non-zero. The lane produced no output at all and looked like it
  had passed.
- `cmd || true; rc=$?` captures the status of `|| true`, so `rc` is **always 0**.
  This turned every exit-code assertion into a no-op and then reported the
  program broken when it was not. The idiom that works is `rc=0; cmd || rc=$?`.

Both are noted in `run.sh` beside the lines that fix them.

---

## What the program is worth as a test of the language

It compiled on the fourth attempt; three of the four failures were the
diagnostics above, none was a type error in the algorithm, and the algorithm was
right the first time it ran — 206 generated pairs, every edit script rebuilding
both files exactly and matching GNU diff's edit distance. For a non-trivial
graph algorithm written from the docs by someone who had not used the language,
that is a good result, and the four stumbles are all in the first ten minutes.
