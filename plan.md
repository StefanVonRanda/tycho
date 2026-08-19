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

## From the last three probes (2026-08-19)

Records: `docs/internals/probe-select-2026-08-19.md`,
`probe-newtype-subscript-bounded-2026-08-19.md`, `probe-generics-2026-08-19.md`.
Every surface that had zero non-author coverage now has a record. Each item
below was rebuilt on `main` before being written here.

### 1. `select` with `recv` + `default` and no `closed:` takes NEITHER arm

After the channel is closed and drained. Measured: 100 iterations of that shape
gave `recvs=1 defaults=0` -- 99 iterations selected nothing. An unbounded loop
of that shape spins forever with no crash and no diagnostic. `default` means
"nothing ready, proceed", so a closed channel is exactly when it must fire.
- **Also undocumented:** `closed:` outranks `default:` when both are present.
- **Done when:** `default` fires on a closed channel, or the compiler refuses a
  `select` with `recv`+`default` and no `closed:` arm. The precedence rule is
  stated in `docs/reference/concurrency.md` either way.
- **Gates:** `make test`, `make conc`, `make flow-check`.

### 2. The arithmetic message states a rule that is not the rule

`Price * Qty` (two newtypes over int) gives *"arithmetic requires two ints or
two floats (got Price, Qty)"*. Both ARE ints by that wording, and `Price * Price`
compiles. The real rule is same-type-only, and the suggested fix (convert one
side) is not the fix.
- **Done when:** the message names the actual rule for a newtype pair.
- **Gates:** `make test`, `make ledger-check` (the newtype lane).

### 3. `bounded[N]T` is undocumented as a value

`docs/reference/index.md` claims to catalogue every feature and contains ZERO
occurrences of `bounded`. No document anywhere shows how to CONSTRUCT one --
`grep -rn 'bounded\[' docs/` finds type positions only. The probe guessed
`x : bounded[N]T = []` and says it was lucky.
- **Done when:** `reference/index.md` lists it and one page shows construction,
  push, the capacity refusal, and that `pop`/slicing are rejected.
- **Gates:** the two doc gates; `make docs-fences` for any new fence.

### 4. Smaller, all confirmed

`pop` and slicing rejected on `bounded` means a fixed-capacity queue cannot be
dequeued. The `inout` docs say "must name a mutable variable" but a field place
(`&b.asks`) is accepted -- doc narrower than the language. Forwarding an `inout`
parameter needs `&` at the inner call and no example shows it. Cross-package
`subscript` calls are undocumented though they work, nested included. No
top-level `X := 6` -- `const NAME = expr` is the form, in spec §12.2 but not on
the reference page, and its error (`expected 'fn'`) is the only one in three
runs that did not name its own fix.

### 5. `const` refuses an array literal with a message naming the wrong rule

`const XS = [1, 2, 3]` gives *"const value must be a literal"* -- and `[1, 2, 3]`
is a literal. Same class as finding 2: the message states a rule that is not the
rule being enforced. Found by the mimo-v2.5 run, the one thing that round found
that the Claude round did not.
- **Done when:** the message says what a const may actually hold.
- **Gates:** `make test`.

### Three runs in a row hit this

`tychoc` compiles every `.ty` beside the entry file as one package, so two
programs cannot share a directory. `docs/reference/packages.md` says so in a last
paragraph where it reads as style advice.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
