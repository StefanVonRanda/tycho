# RFCs

Proposals, resolved and open. Maintainer material — a proposal describes what
the language might do, which is not the same as what it does. For current
behaviour, read [`../reference/`](../reference/).

- [`parallel-for-width.md`](parallel-for-width.md) — **accepted and built
  2026-08-13.** The `parallel(W) for` width slot; what shipped is recorded at
  the end of the file.
- [`value-lifetime-regions.md`](value-lifetime-regions.md) — **study, closed
  with a recommendation not to build.** Whether a region lifetime can follow a
  value instead of a scope; the answer is to build nothing yet (§6 states the
  single condition that would change that).
- [`or-return-mapping.md`](or-return-mapping.md) — **decided 2026-09-25.**
  Error-mapping `or_return` was measured across 42 sites and set aside in
  favour of `or_else`, which shipped in 0.8.6. The body is the proposal as
  measured, kept for the reasoning.
- [`limited-references-spike.md`](limited-references-spike.md) — **decided:
  stored references rejected.** The spike behind subscripts, the one
  limited-reference construct that fits the arena model; graphs stay as
  index pools.
- [`ffi-threading-design-review.md`](ffi-threading-design-review.md) —
  **review, mostly shipped.** FFI and threading, reviewed together; the header
  lists which recommendations landed and which stay open.
