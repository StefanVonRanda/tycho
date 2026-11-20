# RFCs

Design records kept for their reasoning. A proposal describes what the language
might do, which is not the same as what it does: for current behaviour read
[`../spec/`](../spec/README.md) and [`../reference/`](../reference/).

- [`value-lifetime-regions.md`](value-lifetime-regions.md) — **study, closed
  with a recommendation not to build.** Whether a region lifetime can follow a
  value instead of a scope; §6 states the single condition that would change
  the answer.
- [`limited-references-spike.md`](limited-references-spike.md) — **decided:
  stored references rejected.** The spike behind subscripts, the one
  limited-reference construct that fits the arena model; graphs stay as
  index pools.
- [`ffi-threading-design-review.md`](ffi-threading-design-review.md) — why
  race-freedom stops at the FFI, and the one open item (per-task failure
  isolation).
