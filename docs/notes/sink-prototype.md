# The explicit `sink` parameter convention (experimental, both compilers)

A working implementation of the `sink` (owned, consuming) parameter convention identified
in `hylo-mvs-research.md`, in **both** compilers (`tychoc` and the self-hosted `tychoc0`),
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

## What a production `sink` would still need

- **A consume diagnostic** for predictability — Hylo *errors* on use-after-`sink`, forcing
  an explicit copy; this prototype silently copies instead (sound, but not the
  compiler-checked guarantee). A diagnostic would make the elision predictable not
  best-effort.
- **Escape is still a copy**: returning / storing-into-a-returned-value re-homes the sink
  param to `_parent`. That is the arena's hard limit (a value outliving its scope must be
  copied to a longer-lived arena) — the same boundary `bench/trie` hits from the storage
  side. `sink` removes the *call-site* copy, not the *escape* copy.
- **UFCS parity**: a `sink` method called as `recv.method()` is desugared to a sink-aware
  direct call by tychoc (sound — verified), but **tychoc0 rejects it** ("no such field"),
  because its UFCS matcher doesn't strip the `~` marker when matching the receiver to the
  first parameter. The two compilers therefore diverge on UFCS-of-`sink` (accept vs reject)
  — but **neither corrupts**: tychoc copies/adopts correctly, tychoc0 fails closed. Direct
  calls (the harness test and the common case) are sound and byte-identical on both. Wiring
  tychoc0's UFCS path requires both the matcher fix *and* routing the receiver through the
  adopt-or-copy (else the borrow-then-mutate would corrupt), so it is left for follow-up.

`tychoc0` is now mirrored: `sink` is in both compilers with a shared golden
(`tests/sink.ty`), so it is no longer a single-compiler experiment.

## Verdict

`sink` is sound, arena-compatible, and now elides the copy for both fresh values and dead
named variables (verified against an adversarial soundness battery; `make test` 227/0,
fixpoint B==C). The arena-placement step was the real payoff, and it landed as a small,
auditable *relaxation* of move-on-last-use (drop the arena match for a consuming call)
rather than a new analysis pass. What is left is ergonomics (a consume diagnostic) and
parity (`tychoc0`), not soundness — `sink` is a real, if still experimental,
copy-eliminating convention.
