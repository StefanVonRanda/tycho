# Design: `match` on scalar values

Status: the deliverable of `plan.md` Phase 1 (the tycho-vm findings). Read
before Phase 2 implements. This document is the contract; the six decisions
below each carry their reason, and the codegen section carries the numbers
that justify the choice.

## The demand, measured

`match` is enum-only, so every dispatch table in the tree is an `if`/`elif`
ladder. Six files carry a ladder of 4+ arms over a scalar:

| file | arms | subject |
|---|---|---|
| `tools/lsp.ty` | 55 | byte values from string indexing |
| `tools/tycho-vm/main.ty` | 38 | opcodes (24-opcode dispatch, `:622`) |
| `tools/tychofmt.ty` | 19 | byte values |
| `corelib/json/json.ty` | 18 | byte values (`corelib/json/json.ty@parse_value`) |
| `corelib/markdown/markdown.ty` | 16 | byte values |
| `tools/tycho-q/main.ty` | 9 | byte values |

Two shapes the arms actually need, both already written as `if`:

- The VM's dispatch groups seven arithmetic ops behind one range test —
  `elif op >= OP_ADD and op <= OP_GE:` (`tools/tycho-vm/main.ty:634`) — and
  then re-dispatches inside. That is a range arm in disguise.
- json's number dispatch is a literal-or-range test — `c == 45 or (c >= 48
  and c <= 57)` — the range is **inclusive** (`<=`, not `<`).

Every ladder subject is `int`: string indexing returns the byte **value as an
int** (`src/tychoc.c:5476`), so the char-position ladders all compare ints.
**No ladder in the tree dispatches on `string` or `bytes`.**

## How `match` works today

- **Parse** — `parse_match` (`src/tychoc.c@parse_match`) requires an IDENT per
  arm: "a match arm `Variant(bindings):` or `Variant:`". A scalar literal dies
  there with that message, which is why `match` on an int is currently a parse
  error.
- **Resolve** — the `S_MATCH` pass (`src/tychoc.c:7531`) already enforces
  wildcard-last (`tests/reject/match_wildcard_not_last.ty`), duplicate arms
  (`tests/reject/match_dup_arm.ty`), and exhaustiveness
  (`tests/reject/match_non_exhaustive.ty`) — all enum machinery, all reusable.
- **Codegen** — a match emits an `if`/`else if` chain on the variant tag with
  a fail-closed fallback (observed in emitted C:
  `fprintf(stderr, "tycho: non-exhaustive match\n"); exit(1)`).
- **Spec** — §14.3 `docs/spec/10-statements.md:32-60`; grammar rule
  `docs/spec/appendix-a-grammar.md:130`.

## Decisions

### D1 — Which types. `int` now; `char` and `bool` fall out free; `string`/`bytes` deferred; `float` refused

- **`int`** — the only type with customers (the table above). Primary.
- **`char`** — the same machine type as `int` (one byte, `long` in C,
  `src/tychoc.c:744`). Arm literals are `'a'`-spelled; the byte-value ladders
  use `int` and stay `int`. No customer for the distinct `char` type today,
  but the cost is zero once the int machinery exists.
- **`bool`** — a two-value domain, so exhaustiveness is provable (D2).
- **`string` / `bytes`** — **deferred, with the measurement as the reason.**
  Zero ladders in the tree dispatch on either (counted above). A string arm
  needs string-comparison codegen (a string is a length-headered `char *`,
  not a scalar), and a feature whose only customer is the program written to
  want it is the demo problem this whole exercise exists to catch. Comes back
  if a second caller appears. A `match` on a `string`/`bytes` subject is a
  **compile error at resolve**, with the reason in the message.
- **`float`** — **refused.** Equality on floats is the classic trap (arms on
  a continuous domain invite NaN and mid-computation surprises), no customer
  exists, and `if` with comparisons is the honest form. The refusal is
  stated, not silent.

### D2 — `_` and exhaustiveness. `_` is REQUIRED for `int`/`char`; optional for `bool`

An `int` match without `_` cannot be exhaustive and is a **compile error** —
the same fail-closed stance the resolver already takes on enums
(`tests/reject/match_non_exhaustive.ty`). The emitted runtime abort stays as
the backstop (it becomes unreachable: the resolver proves coverage). For
`bool`, `true:` + `false:` covers the domain, so `_` is optional, exactly like
the enum rule.

Reason: enums are exhaustiveness-checkable by construction; ints are not. A
non-exhaustive scalar match would be a new silent-fall-through failure mode —
the thing the repo's reject suite exists to refuse (json's "cannot spin" is
the same instinct).

### D3 — Duplicate arms are an error, and the rule extends to overlap

A repeated literal is an error, as a repeated variant already is. Two arms
whose patterns **overlap** are also an error: `1..9` plus `5`, or `1 | 3`
plus `3`. Overlap detection is interval arithmetic over the arm patterns
(merge sorted intervals; any collision fails, naming both arms).

Reason: a duplicate makes an earlier arm dead and the match ordering-dependent
— the bug a `match` exists to make unexpressible. Ranges make this subtle, so
the resolver must check it rather than the writer.

### D4 — No fall-through

One arm runs; arms are blocks (statement form) or value expressions (value
form, §13.5) — unchanged from enums. Fall-through is the classic source of
the exact bugs `match` removes, and range arms make it unnecessary: the range
IS the group (the VM's arithmetic group becomes one `OP_ADD..OP_GE` arm
instead of seven labels with a shared body).

### D5 — Ranges and sets. Both: `1..9` (inclusive) and `1 | 3 | 5`

- `..` is **inclusive**, matching the code the customers write today
  (`c >= 48 and c <= 57`).
- `|` already lexes (bitwise-or), so a set is sugar for repeated literals —
  no token work, and the dup/overlap rule applies to the set as a unit.
- `..` is **not lexed today** (verified: `1..9` is currently a parse error).
  The float hazard is smaller than it looks: a trailing-dot float does not
  exist (`1.` fails as "expected a field name or tuple index after '.'"), so
  the rule is simply — in `lex_num`, a dot not followed by a digit ends the
  number; two adjacent dots lex as `..`. `1.5` still lexes as a float, `1.5`
  never collides with `..`, and `1..9` becomes int `1`, `..`, int `9`.

### D6 — Codegen. Emit a C `switch` for `int`/`char` arms when `narms >= 4`; a chain otherwise; unrolled ranges

- The emitted C is `switch (subject) { case L: ... }` for int/char matches
  with 4+ arms, with the subject cached in a local first (the "evaluated
  exactly once" rule of §14.3 carries over — the existing `_m0` caching
  pattern extends to a scalar temp).
- Ranges emit as **consecutive case labels** (`case 48: case 49: ... case 57:`).
  Unrolling, not GNU `case 48 ... 57:`: the ranges in the tree are all small
  (7–10 values), and unrolling is portable under any dialect. (The repo
  compiles with plain `cc`, no `-std` — `src/tychoc.c:13270` — so GNU case
  ranges would work, but there is no reason to depend on them.)
- The table-vs-binary-search decision is **cc's**, not ours: at `-O2`, GCC and
  Clang lower a dense switch to a jump table and a sparse one to a binary
  search. Emitting a chain and hoping cc's if-to-switch conversion fires is
  fragile; emitting a `switch` is deterministic.
- The 4-arm threshold: an indirect jump plus bounds check costs about as much
  as 2–3 comparisons, so below 4 a chain wins and above it the switch does
  not lose. The exact crossover is cc's to decide; 4 is our floor.
- Enum `match` codegen is **unchanged** (chain on tag). Rewriting it to a
  switch is out of scope; it has no customers complaining.

## What it would buy — the arithmetic

From the plan's own measurements (`plan.md:36-38`): VM throughput is 1.39 M
instructions/sec — about **720 ns per instruction** — and the dispatch is
~12 int comparisons per instruction (`plan.md:54`). An int compare plus
branch is ~2 cycles, ~0.7 ns at ~3 GHz, so the whole dispatch chain is
**~8 ns ≈ 1% of the per-instruction budget**. A jump table replaces it with a
bounds check plus an indirect jump, ~2–3 ns — a saving of **~0.5–1% of VM
time, inside measurement noise**.

The dominant cost is elsewhere: a struct-with-a-string copy is **815 ns**
against **110 ns** for a bare `JMP` (`plan.md:36`) — 7×. Copies and arena
traffic, not comparisons, are what the VM spends its 720 ns on.

**Conclusion, stated plainly:** scalar `match` is an **ergonomics** feature
with fail-closed semantics, not a speed feature. It turns 12-comparison
ladders into readable, dup-checked, exhaustiveness-checked arms. Phase 2 must
measure the VM's instructions/sec before and after and report both; the
prediction is **flat within noise**, and a change of more than ~2% would
itself be a surprise worth explaining.

**Measured at Phase 2 (2026-08-03).** The old if/elif dispatch (built from
git HEAD) vs the scalar match, same compiler, same workload: 300 fib runs,
best of 3 — 2.354 s → 2.311 s (**-1.8%**); 500 sort runs, best of 5 —
0.616 s → 0.601 s (**-2.4%**). Marginally faster than the chain, consistent
with the switch lowering; the estimate's ~1% dispatch slice was a little low
because a 24-way comparison chain mispredicts more than the naive 2-cycles-
per-compare figure assumed. Neither number is a reason to build for speed.

## What Phase 2 touches

- **Lexer** — the `..` token and the dot-not-followed-by-digit rule (D5).
- **`parse_match`** (`src/tychoc.c@parse_match`) — the arm grammar accepts,
  beside variant names and `_`: an int/char/bool literal, a set
  (`lit | lit | ...`), and a range (`lit..lit`). The diagnostic widens from
  "a match arm `Variant(bindings):` or `Variant:`" to name the new forms.
  String and float literals stay rejected at parse.
- **Resolve** (`src/tychoc.c:7531`) — a scalar subject path: subject is
  `int`/`char`/`bool`; arm literals typed against the subject exactly as
  assignment would type them (newtype rules unchanged — a newtype over int
  is its own type and takes its own spellings); literal arms on an enum
  subject and variant arms on a scalar subject are distinct errors; the
  dup/overlap check (D3); the `_` rule (D2); string/bytes/float subjects
  refused with the demand-gated reason (D1).
- **Codegen** (`src/tychoc.c:10251`) — the switch emission (D6). The
  `S_MATCH` statement and value forms both flow through it, so the value
  form (`x := match c: ...`) works without separate work.
- **Spec** — §14.3 (`docs/spec/10-statements.md:32-60`), the statements
  section that names the arm forms, and the grammar
  (`docs/spec/appendix-a-grammar.md:130`).
- **Fixtures** — ordinary `tests/` fixtures (the freeze lanes are retired;
  new syntax gets a plain fixture again): accepted forms, the value form,
  and rejects — dup, overlap, missing `_` on int, literal arm on enum,
  variant arm on int, `match` on string, `match` on float.

## Done-when (Phase 2's gate)

1. `tools/tycho-vm/main.ty:622`'s dispatch is written as a scalar `match`,
   with the grouped arithmetic arm spelled `OP_ADD..OP_GE`.
2. `corelib/json/json.ty@parse_value`'s byte dispatch is written as a scalar
   `match`.
3. Both still pass their gates (`make test`, `make corelib`, `make vm-check`),
   and the VM's instructions/sec is measured before and after and reported.

## Edge cases, settled

- **Subject evaluated exactly once** even when arms have side effects — the
  scalar temp is assigned once before the switch.
- **Empty `_`-only matches** — legal (`match c: _: ...`), a pass-through.
- **A set containing a range** (`1 | 3..5`) — legal; the pieces merge into
  one interval set for overlap checking.
- **Nested patterns on scalar arms** — none: a scalar arm has no payload.
  Option/Result nesting is unchanged.
- **No `default` keyword** — `_` is the spelling; one spelling, one rule.
