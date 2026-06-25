# The explicit `sink` parameter convention (experimental, both compilers)

A working implementation of the `sink` (owned, consuming) parameter convention identified
in `../internals/hylo-mvs-research.md`, in **both** compilers (`tychoc` and the self-hosted `tychoc0`),
with a shared harness test (`tests/sink.ty`, both compilers byte-identical). Still labelled
experimental — direct calls are fully supported and sound; one peripheral combination
(UFCS) is not yet wired in tychoc0 (see Limitations). It exists to learn what `sink` can and
cannot do in Tycho's arena model; the findings refine the research note.

## What was built

Syntax mirrors `mut` (convention after the colon): `x: sink T`.

```
fn scale2(xs: sink [int]) -> int:   # xs is OWNED and MUTABLE (a borrow is read-only)
    s := 0
    for i in range(0, len(xs)):
        xs[i] = xs[i] * 2           # legal only because xs is `sink`
        s = s + xs[i]
    return s

fn main():
    print(str(scale2([5, 5, 5])))   # 30 — fresh value adopted, mutated in place, NO copy
    b := [10, 20]
    r := scale2(b)                  # b is used below, so the arg is COPIED
    print(str(r) + " " + str(b[0])) # 60 10 — b is unchanged (value semantics hold)
```

Implementation (all in `src/tychoc.c`): a `sink` flag on `Param`/`Sig`; a contextual
keyword in the parameter parser (not reserved); sink params registered as **mutable**
(so `xs[i] = …`, `xs.f = …` are allowed, unlike a read-only borrow); and at the call
site the sink argument is routed through `arg_into` — the existing move-on-last-use hook
that **adopts** a movable value with no copy and **copies** an otherwise-live one.

## Results — it works, and it is sound

- `scale2([5,5,5]) == 30`: the fresh literal is built in the call's scope and handed off
  with **zero copies** (verified in the generated C — no `tycho_arr_int_copy`), then
  mutated in place. This is the elision win.
- `scale2(b)` with `b` used afterward: the argument is **copied**, so `b` is unchanged.
  Value semantics preserved; **ASan/UBSan-clean**.
- No regression: `make test` 227/0, fixpoint B==C green (the machinery is inert for every
  program that doesn't use `sink`).

## The arena-placement analysis: dead named variables adopt too

The first cut copied every *named* variable, even one dead after the call, because it
reused `can_move_from`, whose same-arena requirement a named variable can't meet (it lives
in an outer arena, not the call's transient scratch). The fix turned out to be a
*relaxation*, not a re-placement: for a `sink` call the same-arena requirement is
unnecessary.

A `sink` callee CONSUMES the argument — it mutates the buffer during the call and does not
keep it (escape is a separate copy, below). So adopting a named local is sound when:

1. **the buffer outlives the call** — true for ANY tracked local, because a local lives in
   a block/function arena that strictly encloses the per-statement scratch (`_t`) the
   arguments are built in; and
2. **the mutation is unobserved** — true when the source is read exactly once and outside
   any loop, so this read is its last on every path.

That is `can_move_from`'s gate minus the arena match. `can_move_into_sink` implements
exactly that, and a `sink` array argument now adopts a dead local from any enclosing arena:

    a := [1, 2, 3]
    scale2(a)        // emitted: h_scale2(&_t, h_a)  — NO copy; a is dead, adopted + mutated

The copies that remain are the genuinely necessary ones, verified by an adversarial
battery (all value-semantics-correct, ASan/UBSan-clean):

| case | behaviour | why |
|---|---|---|
| `dbl(a)`, `a` dead after | **adopt** (no copy) | last use, not in a loop |
| `dbl(b)` then read `b` | copy | `b` observed after → must stay independent |
| `dbl(c)` inside a `for` | copy each iteration | one textual read, many dynamic reads |
| `dbl(d)` then a closure captures `d` | copy | the capture counts as a later read |
| fresh `dbl([5,5,5])` | adopt | already owned in the call scope |

So `sink` now elides the copy for **fresh values and dead named variables** — the
build-and-hand-off and transform-and-discard patterns — and copies only when value
semantics actually require independence.

## Consume diagnostic — done (both compilers)

`sink` no longer silently copies a reused value: passing an **owned** variable (a local or a
`sink` parameter) to a `sink` parameter and then using it again — or passing it inside a loop
— is now a **compile error** in both compilers, so the move is a checked guarantee rather than
a best-effort optimization. You keep a value by handing the sink an explicit copy:

```
b := [5]
keep := b            // explicit copy; b is read again below, so this copies
x := dbl(keep)       // keep is dead -> adopted; b untouched
print(b[0])          // 5
```

The rule excludes **borrowed/mut parameters** (they can never move, so they always copy — an
error would offer no escape) and field/index arguments (you can't move a part out of a value).
A `sink` parameter *is* owned, so reusing one after handing it on errors like a local would.
Reject golden: `tests/reject/sink_use_after.ty` (both compilers reject); the escape hatch is
exercised in `tests/sink.ty`.

## Remaining

- **Escape is still a copy**: returning / storing-into-a-returned-value re-homes the sink
  param to `_parent`. That is the arena's hard limit (a value outliving its scope must be
  copied to a longer-lived arena) — the same boundary `bench/trie` hits from the storage
  side. `sink` removes the *call-site* copy, not the *escape* copy. This is fundamental to
  the arena model, not a missing feature.
`tychoc0` is mirrored: `sink` is in both compilers with a shared golden (`tests/sink.ty`,
including UFCS), so it is no longer a single-compiler experiment.

**UFCS of `sink` now works on both compilers** (`recv.method()` with a `sink` receiver).
Closing the tychoc0 side took three parts, the third being a latent bug the first two
exposed:

1. the UFCS matcher (`recv_matches`) strips the `~` marker so a `sink` first parameter
   matches the receiver type (`mut` is deliberately *not* relaxed — a `mut` receiver would
   need `&`, which the desugar doesn't insert);
2. the UFCS call routes its arguments through `gen_call_args_sink` (adopt-or-copy);
3. a **move-on-last-use bug** the first two surfaced: `recv.method()` parses as a
   dotted-name `ECall("recv.method", [])` — receiver *in the name*, empty args — so
   `compute_movables`'s `rd_expr` never counted the receiver's read, wrongly marking it
   movable and **adopting (corrupting) a still-live receiver**. `rd_expr`'s `ECall` case
   now counts the dotted prefix. This was latent before `sink` because borrows don't move;
   `sink`'s adopt is the first construct that consumes a UFCS receiver. (Verified
   value-semantics-correct: `g.dbl()` then reading `g` copies; a dead receiver adopts.)

## Verdict

`sink` is sound, arena-compatible, in **both compilers** (direct and UFCS calls), and
harness-tested (`make test` 229/0, fixpoint B==C). It elides the call-site copy for fresh
values and dead named variables, and the move is now a **compiler-checked guarantee** —
use-after-`sink` is an error, not a silent copy. The arena-placement step (adopt a dead
local from any arena) was the real payoff and landed as a small relaxation of
move-on-last-use, not a new pass. The only thing the arena model fundamentally can't give
`sink` is escape elision (a returned value must be copied to a longer-lived arena). Within
that limit, `sink` is a real, predictable copy-eliminating convention — graduated from
experiment to feature.
