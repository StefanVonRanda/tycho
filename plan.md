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


## A string literal evaluated by two threads is a data race

Found by the new `make flow-check` on its first outing, out of that phase's
scope and left alone. **Not specific to tycho-flow** — it is codegen, so it is
reachable from any literal any `spawn`ed or `parallel for` body evaluates.

`src/tychoc.c:10513` emits every string literal as a lazily initialised function
static:

```c
({ static char *_l = 0; if (!_l) _l = tycho_str_intern("#"); _l; })
```

and `runtime/tycho_rt.c@tycho_str_intern` is a plain `malloc` + `memcpy` that
returns a fresh copy — despite the name, there is no table and no lock. So the
first worker to reach the literal allocates a copy and publishes the pointer in
`_l` with a plain store, while its siblings read `_l` and then read the bytes it
wrote. Nothing orders any of it.

**Evidence.** `tools/tycho-flow/run.sh`'s TSan leg, on `stamp` — the pool's
per-item work — on roughly 3 runs in 12 (measured 2026-08-12, x86-64, gcc
libtsan). Two reports per occurrence, one bug: `data race ... Location is global
'_l.82'` (the pointer) and `data race ... Location is heap block ... allocated by
thread T6 ... #1 tycho_str_intern` (the payload). `make conc` has never reported
it; its fixtures do not evaluate a literal inside a worker.

**Why it is not merely formal.** The publishing store has no release ordering
and the reading load no acquire, so on a weakly-ordered target (aarch64) a
sibling can observe a non-NULL `_l` before the `memcpy` behind it is visible —
a live string pointing at uninitialised bytes. On x86-64 the store order saves
it, which is why nothing has been seen to misbehave. The lost copy also leaks
one allocation per racing literal.

**The lane does not fail for it**: `tools/tycho-flow/run.sh` classifies TSan
reports and tolerates exactly the ones naming `_l` or `tycho_str_intern`, out
loud, and fails for everything else. **Fix this and delete that classifier** —
while it is there, a genuine race in the same shape is invisible.

Options, cheapest first: emit the literal as a file-scope `static char *const`
initialised at program start before any task exists; or make the static
`_Atomic` with acquire/release; or drop the cache and intern eagerly into a
read-only table at startup. Whichever lands, `make conc` wants a fixture that
evaluates a literal inside a `parallel for` body, because today nothing does.

**Verify:** `make test` (codegen), `make conc`, `make flow-check` with the
classifier removed.
