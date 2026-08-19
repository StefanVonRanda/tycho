# newtype / `subscript` / `bounded` probe, 2026-08-19

A fresh agent built a 600-line price-time-priority limit order book across two
packages: newtypes `Price`/`Qty`/`OrderId`/`Trader`, resting orders in
`bounded[LEVEL_CAP]Order`, three `subscript`s, and a session driver. **The
program is not the artifact — this is.** It was thrown away.

It hit **three compiler errors in 600 lines**, which is the headline: the
friction here is documentation, not the language.

## 1. An arithmetic message states a rule that is not the rule

```text
error: arithmetic requires two ints or two floats (got Price, Qty)
```
`Price` and `Qty` are both newtypes over `int`, so by the message's own terms
they ARE two ints. Confirmed here: `Price * Price` compiles; `Price * Qty` does
not. The real rule is that both sides must be the SAME type — the message
describes a different one and sends the reader to convert, which is not the fix.

## 2. `bounded[N]T` is missing from the reference index, and nothing shows how to build one

`docs/reference/index.md` claims to catalogue every feature and contains **zero**
occurrences of `bounded`. All prose lives in spec §5.3.10. Worse, `grep -rn
"bounded\[" docs/` finds only type-position mentions — **no document anywhere
shows how to construct one.** The agent guessed `x : bounded[N]T = []` and says
it was lucky.

## 3. `pop` and slicing are rejected on `bounded`, so it cannot be dequeued

A fixed-capacity queue cannot be drained from the front. The agent invented
tombstone-and-rebuild. Both refusals carry a workaround in the message; the docs
state only the prohibition, so a reader learns the shape by hitting it.

## 4. Smaller, all confirmed

- The `inout` docs say the argument "must name a mutable variable"; the compiler
  also accepts a field place (`&b.asks`). The doc is narrower than the language,
  so the agent wrote an unnecessary fallback.
- Forwarding an `inout` parameter still needs `&` at the inner call, and no doc
  example shows it.
- Cross-package `subscript` calls are undocumented — `b.ask(i)` works, including
  nested (`b.bid(0).resting(0).qty = ...`, checked at runtime).
- The "subscript rooted in a fresh local" rule is unreachable: the single-`yield`
  rule fires first.

## What went right

Three errors in 600 lines, and the program was correct on its first run. The
work shipped earlier the same day showed up in the agent's own account: **batched
errors with quoted source**, and **diagnostics carrying a second location with
the unmangled type and its declaration**.

Newtype distinctness held across the package boundary. `bounded` refused the 9th
order into an 8-slot level by name, with the cap in the message. The `subscript`
behaved as a real place and chained through an `inout` array element. Zero
warnings on the final build.
