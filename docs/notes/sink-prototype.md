# Prototype: the explicit `sink` parameter convention (tychoc only)

This is a working **prototype** in the C reference compiler (`tychoc`) of the `sink`
(owned, consuming) parameter convention identified in `hylo-mvs-research.md`. It is *not*
mirrored in `tychoc0` and not in the test harness — it exists to learn what `sink` can
and cannot do in Tycho's arena model. The findings refine the research note.

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

## The honest finding: the arena bounds `sink`'s benefit to fresh values

The elision fires for a **fresh value** (literal, call result) because it is already built
in the call's transient scope, so handing it off is a same-arena move. It does **not**
fire for a **named variable**, even one that is dead after the call: `scale2(a)` copies
`a`. The reason is structural, not a missing optimization —

> `arg_into` adopts only when `can_move_from` holds, and that requires the source's arena
> to equal the call's scope. A named variable lives in an outer / longer-lived arena, so
> adopting it would hand a buffer from one arena to a callee that mutates it; the move is
> only sound (and only a no-op) when the arenas coincide.

So in Tycho's arena model `sink` delivers:

1. **owned, mutable parameters** — new capability; a borrow cannot be mutated, so today
   you must copy first. `sink` removes that copy for the build-and-hand-off pattern.
2. **zero-copy handoff of fresh values** into a mutating callee.

…but **not** the general "move an existing variable into a callee without a copy" that
`sink` gives in Hylo's move-semantics model, because there lifetime is tracked
independently of lexical scope, whereas here lifetime *is* the arena. This is the same
boundary `bench/trie` and `value-semantics-limits.md` hit from the storage side, now seen
from the copy-elimination side.

## What a production `sink` would still need

- **Arena-placement analysis** so a dead named variable can be adopted: place such a
  variable in (or re-home it once to) an arena compatible with the sink call, so the
  handoff is a same-arena move. This is the real work, and the real payoff.
- **A consume diagnostic** for predictability (Hylo errors on use-after-`sink`); the
  prototype silently copies instead, which is sound but not the compiler-checked guarantee.
- **`tychoc0` mirror** + harness tests before it is a language feature rather than an
  experiment.

## Verdict

`sink` is sound and arena-compatible, and it adds a genuine capability (owned-mutable
params + fresh-value handoff). But the prototype shows its copy-elimination payoff is
**narrower** in an arena language than the research note implied: bounded to fresh values
unless arena-placement analysis is added. Worth pursuing only alongside that analysis —
otherwise it is a clarity/ownership convention more than a performance one.
