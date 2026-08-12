# Open work

> **This file holds only what is NOT done.** A phase is deleted when it is
> ticked, not archived here — its evidence lives in the commit that closed it,
> where it is attached to the diff it describes and `git log -S` can find it.
>
> Trimmed 2026-08-12. It had reached 1,396 lines, of which 1,299 were evidence
> under 19 finished phases against 2 open ones: 93% archive. That is the second
> time in two days the file needed rotating, so the convention changed rather
> than the file — see CLAUDE.md, "Plans". `git show d78db13:plan.md` recovers
> the long version; every phase's evidence is also in its own commit.

## Phase 1 — `design-scalar-match.md`'s tycho-vm refs

Found while converting that document's `src/tychoc.c` refs, and deliberately left
alone: out of that phase's scope, and the section is historical.

`docs/internals/design-scalar-match.md` cites `tools/tycho-vm/main.ty:622` twice
(the demand table, and Done-when item 1) and `:634` once. **Line 622 is blank
today and 634 is an unrelated comment.** The dispatch is now `match op:` at
`tools/tycho-vm/main.ty:775` with the grouped arm `OP_ADD..OP_GE:` at `:782` —
so Done-when items 1 and 2 are already satisfied and the document does not say so.

The care needed: the demand table and the `elif op >= OP_ADD and op <= OP_GE:`
quotation describe the **pre-implementation** state on purpose ("both already
written as `if`"). Re-pointing them at the post-implementation code would destroy
a measurement record. Decide per ref whether it wants an anchor, a date-stamped
"as measured before Phase 2" note, or the Done-when box ticked.

**Verify:** `make check-links`. `tools/tycho-vm/main.ty` is 974 lines, so its bare
refs *can* fail a bounds check — these did not, which is why they survived.

## tycho-db — remaining layers

A relational database in `tools/tycho-db/`, and the tree's first program with
its own internal packages. `sql/` (lexer, parser, AST) landed in `b899be01`.
Imports resolve relative to the importing file: a sibling is `../sql`.

Each layer is one phase, one commit, in this order — every step must leave a
runnable program, never a half-wired one.

