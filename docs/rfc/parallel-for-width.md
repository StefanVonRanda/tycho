# A width slot for `parallel for`

Status: **BUILT 2026-08-13.** What follows is the proposal as written; the
differences the implementation forced are listed under "What shipped" at the end.

Written 2026-08-13 to cost open-list item 8 in
[`../internals/FRICTION.md`](../internals/FRICTION.md), which had stood as
"uncosted, and still the honest core of what is left". It is costed now, and it
is smaller than the item says.

## The want, split in two

The item's title is "no direct spelling for N workers". Its own 2026-07-31
narrowing already split that in two, and only one half is a type-system problem:

- **(a) The program cannot choose N.** `parallel for` fans out to `ncpu()`
  tasks and the only knob is the `TYCHO_THREADS` environment variable, read once
  per process in `runtime/tycho_rt.c@tycho_ncpu`. A fixture runner that wants
  `-j 4` on a laptop and `-j 32` in CI cannot say so from inside the language; it
  must be *launched* differently.
- **(b) N long-lived workers, each carrying its own identity.** `server/main.ty`
  still pays a recursive fan-out for this: worker k spawns worker k+1 into a
  frame-local, then runs its own accept loop, because a chunk's identity is not
  observable and a task handle cannot be stored in a container
  (`src/tychoc.c@task_container_err`).

This RFC proposes (a) only. (b) is discussed at the end because the obvious
spelling for it now collides with a rule that landed on 2026-08-13.

## Why (a) is not a type-system change

The channel-drain form already lowers to a counting `parallel for` whose width
is an ordinary expression node. From the lowering, `src/tychoc.c` in the
`IS_CHAN(src)` branch:

```
/* K = ncpu() workers, each draining until closed:
 *   parallel for __pw in range(0, ncpu()):
 *       for true: select { recv(coll, x): BODY ; closed: break } */
Expr *nc = new_expr(E_CALL, s->line); nc->sval = "ncpu"; nc->pkg = "";
s->name = "__pw"; s->r_start = z; s->r_stop = nc;
```

`ncpu()` is synthesised as an `E_CALL` node and dropped into `r_stop`, the same
slot a user's `0..<N` fills. **Giving the program a width means substituting the
user's expression for that node** — no new type, no change to the affine handle
rule, no container of handles. The counting form needs the matching change one
layer down: `gen_parfor` chunks the iteration space into `tycho_ncpu()` tasks
(`src/tychoc.c@gen_parfor`), and that call site becomes the width expression.

## Proposed spelling

```text
parallel(4) for x in work:          # drain `work` with exactly 4 workers
parallel(w) for i in 0..<n:         # chunk 0..<n into `w` tasks
parallel for x in work:             # unchanged: ncpu() workers
```

Grammar, replacing the two productions at `docs/spec/02-grammar.md@ParallelFor`:

```
ParallelFor ::= "parallel" Width? "for" IDENT "in" "0" "..<" Expr ":" NEWLINE Block
              | "parallel" Width? "for" IDENT "in" IDENT ":" NEWLINE Block
Width       ::= "(" Expr ")"
```

Parsed at the existing `TK_PARALLEL` arm, which today consumes `parallel` and
demands `for` next; the width is an optional parenthesised expression between
them, carried on the statement beside `s->parallel`.

## The decisions this needs, and the answer each should get

1. **Evaluated when?** Once, at the spawn site, before any task starts — the
   same rule the counting form's `N` already has. An expression re-evaluated per
   chunk would make the width unobservable and racy.
2. **`W < 1`?** Fail closed, not clamp. A literal `parallel(0)` is refused at
   compile time; a computed `W < 1` aborts at run time with its own message. The
   precedent is this language's habit of refusing rather than guessing, and the
   counter-precedent is instructive: `range()`'s zero-step abort was *lost* when
   the three-clause loop replaced it (`docs/spec/10-statements.md`, "The
   zero-step guarantee is gone"), and that is recorded there as a deliberate
   trade, not a model to copy.
3. **`W` versus `TYCHO_THREADS`?** The explicit width wins. The environment
   variable is a process-wide default for programs that did not choose; a
   program that says `parallel(4)` has chosen. Say this in §22 — a reader who
   expects `TYCHO_THREADS=1` to serialise everything for debugging needs to know
   it no longer does.
4. **Does `W` bound anything else?** No. It is the fan-out width of this
   statement, not a pool the runtime keeps.

## Cost, and the gates that can redden

- `src/tychoc.c`: the `TK_PARALLEL` arm, the `IS_CHAN` lowering's `ncpu()` node,
  and `gen_parfor`'s chunk count. Small and local — three sites.
- `docs/spec/02-grammar.md` (the two productions plus `Width`),
  `docs/spec/13-concurrency.md` §22, `docs/guides/concurrency.md`.
- Fixtures: a width of 1 must serialise, a width of 2 must still drain the whole
  channel, `parallel(0)` must be refused, and the *answer* must not depend on the
  width — the property `tools/tycho-flow`'s lane already asserts for its pipeline.
- Gates: `make test` (new fixtures), `make conc`, and `sh scripts/entrypoints.sh`
  if the grammar change touches anything a consumer program spells. A width that
  silently did nothing would pass every one of those except a fixture that
  *counts* workers, so that fixture is the load-bearing one.

## (b) CLOSED 2026-08-13 — it was never a type-system problem

Written below as "still open, and now harder to spell", on the assumption that
per-worker identity needed a second binder or task handles in a container.
Neither: **the counting form already binds the identity**, and the width slot
this RFC added supplies the count.

```tycho
struct Config:
    root: string

fn accept_loop(cfg: Config, srv: int, wid: int) -> int:
    return wid

nworkers := 4
cfg := Config(".")
srv := 0

parallel(4) for wid in 0..<nworkers:   # nworkers workers, each with its own wid
    accept_loop(cfg, srv, wid)
```

Probed directly — 4 workers, ids summing to 6 — and then applied to the program
the item cited: `server/main.ty`'s recursive fan-out is gone and
`make server-check` starts the real server and passes. The reasoning below is
kept because its analysis of the SECOND BINDER is still correct; what it got
wrong is treating the first binder as unavailable.

## The original: why (b) looked still open, and harder to spell

The natural spelling for per-worker identity is a second binder:

<!-- fence-skip: a REJECTED spelling, kept to show what was not proposed; `tests/reject/for_two_binders.ty` pins the refusal -->
```tycho
parallel(4) for x, wid in work:     # NOT proposed
```

That collides head-on with a rule gated the same day this was written: a `for`
head binds exactly one name, enforced with its own diagnostic and pinned by
`tests/reject/for_two_binders.ty`. Both neighbours spend a second binder on the
*index* (measured 2026-08-13: Go's first binder and Odin's second), so a second
binder meaning "worker id" would take a slot every reader expects to mean
something else.

The alternatives, none costed here: a builtin readable inside a parallel body
(`worker_id()`), which is honest about being ambient rather than bound; or the
original ask, task handles in a container, which is the type-system change the
item names and which nothing in this RFC makes cheaper.


## What shipped, and where it differs from the proposal above

Landed in `src/tychoc.c` at the three sites this RFC named, plus the two spec
sections. Two things the proposal did not say, both found by building it:

- **The range is `1..64`, not `>= 1`.** 64 is the static `_pts[64]` task array in
  `gen_parfor`, and it was already the silent ceiling for `ncpu()`. Silently
  clamping an explicit `parallel(100)` would run a program nobody asked for, so a
  literal outside the range is refused in the parser (where the caret lands on the
  author's digit) and a computed one aborts at run time via `tycho_die`.
- **The width lands in `r_stop`, not in a new codegen path.** For the
  channel-drain form the iteration space IS the worker count, so the author's
  expression simply replaces the synthesised `ncpu()` node and every existing
  clamp reads it unchanged.

**What is asserted, and what is not.** `tests/conc/parfor_width.ty` proves the
ANSWER does not move with the width (328350 at 1, at 3 and at `ncpu()`), the two
`reject/` fixtures pin the literal refusals and `abort/parfor_width_computed.ty`
pins the runtime one. None of those can show the width is honoured *at all*:
with no way to observe a worker's identity from inside the loop — item 8b, still
open — a `parallel(3)` that quietly ran at `ncpu()` prints the same bytes. So
`tests/conc/run.sh` reads the emitted `_pk` initialiser and requires 1, 3 and
`tycho_ncpu()` to each appear. Proven to redden: making `gen_parfor` ignore
`s->par_width` fails that leg and the abort fixture, 43 passed / 2 failed
against 45 / 0.
