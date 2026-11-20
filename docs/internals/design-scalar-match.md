# Design: `match` on scalar values

> Status: shipped. Scalar `match` on `int`/`char`/`bool`, with inclusive ranges, sets, const names and `_`. An ergonomics feature, not a speed feature. Normative text: §14.3 in `docs/spec/10-statements.md`.

## The demand

Before scalar `match`, every dispatch table in the tree was an `if`/`elif`
ladder over an `int`: the LSP's byte classifier (55 arms), the VM's opcode
dispatch (38), `tychofmt` (19), `corelib/json` (18), `corelib/markdown` (16),
`tycho-q` (9). Two shapes recur:

- a **range** — the VM grouped seven arithmetic ops behind
  `op >= OP_ADD and op <= OP_GE`, now the arm `OP_ADD..OP_GE:`
  (`tools/tycho-vm/main.ty`);
- a **literal-or-range** — json's number test `c == 45 or (c >= 48 and c <= 57)`,
  inclusive on both ends.

String indexing yields the byte value as an `int`, so the character ladders
are int ladders too. No ladder dispatched on `string` or `bytes`.

## Decisions

### D1 — Which types. `int`, `char`, `bool`; `string`/`bytes` deferred; `float` refused

- **`int`** — the type with customers.
- **`char`** — the same machine type as `int`; arms are `'a'`-spelled. Free
  once the int machinery exists.
- **`bool`** — a two-value domain, so exhaustiveness is provable (D2).
- **`string` / `bytes`** — **deferred for lack of demand**: no ladder in the
  tree dispatches on either, and a string arm needs string-comparison codegen.
  Comes back if a real caller appears. A string arm is a compile error naming
  the `if`/`elif` alternative.
- **`float`** — **refused.** Float equality invites NaN and rounding surprises;
  `if` with comparisons is the form to use. The compiler's refusal message
  cites this section.

### D2 — `_` is required for `int`/`char`, optional for `bool`

An `int` match without `_` cannot be exhaustive and is a compile error ("a
match on int must carry a `_` arm"), the same fail-closed stance as enums. For
`bool`, `true:` + `false:` covers the domain. A non-exhaustive scalar match
would be a silent fall-through, the failure mode `match` exists to remove.

### D3 — Duplicate and overlapping arms are errors

A repeated literal is an error, and so is overlap: `1..9` plus `5`, or `1 | 3`
plus `3` ("duplicate or overlapping match arm: [1..5] overlaps [3..3]").
Overlap is interval arithmetic over the arm patterns. A duplicate makes an arm
dead and the match order-dependent; ranges make that subtle enough that the
compiler, not the writer, must check it.

### D4 — No fall-through

One arm runs; arms are blocks (statement form) or values (value form). A range
arm is the group, so fall-through is never needed.

### D5 — Ranges and sets: `1..9` (inclusive) and `1 | 3 | 5`

- `..` is **inclusive**, matching how the ladders were written (`c <= 57`).
- `|` is sugar for repeated literals, and the overlap rule treats a set as one
  unit. A set may contain a range (`1 | 3..5`).
- Lexing: a dot not followed by a digit ends a number, so `1..9` is int `1`,
  `..`, int `9`, and `1.5` still lexes as a float. A trailing-dot float (`1.`)
  does not exist, so nothing collides.

### D6 — Lowering: a chain on a cached subject

The subject is evaluated once into a temp (`_m0`), then each arm is a test in
an `if`/`else if` chain: `_m0 == 1`, `_m0 == 2 || _m0 == 4`,
`_m0 >= 5 && _m0 <= 9`, with `_` as the final `else`. Turning a dense chain
into a jump table is left to cc.

## Why it is not a speed feature

The VM's dispatch was ~12 int comparisons per instruction against a ~720 ns
per-instruction budget — about 1% of VM time; struct-with-string copies, not
comparisons, dominate. Measured on the VM after conversion: 300 fib runs
2.354 s → 2.311 s (−1.8%), 500 sort runs 0.616 s → 0.601 s (−2.4%). Within
the range where the lowering choice does not matter.

## Edge cases

- **Subject evaluated exactly once**, even when arms have side effects.
- **A `_`-only match** is legal (`match c: _: ...`).
- **Scalar arms have no payload**; Option/Result nesting is unchanged.
- **No `default` keyword** — `_` is the one spelling.
