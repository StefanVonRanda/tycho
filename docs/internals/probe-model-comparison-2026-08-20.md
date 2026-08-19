# Two models, same four surfaces — what a probe's instrument is worth

The four probes of 2026-08-19 were run twice: once by Claude subagents spawned
inside the author's own session, and once by `mimo-v2.5` (a different vendor's
model) driven through the `pi` CLI. Same sanitised checkouts, same briefs, same
compiler built from `main`.

**This document exists because the first round's provenance was wrong.** The
records `probe-ffi-2026-08-19.md`, `probe-generics-2026-08-19.md` and
`probe-newtype-subscript-bounded-2026-08-19.md` read as independent probes. They
were Claude subagents — the same model family as the assistant that then
evaluated their findings, which is much closer to the author's own mental model
than §1 asks for. Their findings reproduce (each was rebuilt on `main` before
being written down) but the independence claim did not hold.

## What each instrument produced

| | Claude subagents | mimo-v2.5 via pi |
|---|---|---|
| program size | 474–853 lines, up to two packages | ~300–350 lines, one file each |
| runs that produced a log | 3 of 3 | 3 of 4 |
| genuine defects found | 1 (`select` takes no arm on a closed channel) | 0 |
| message-accuracy bugs | 2 | 1 |
| findings that were documented behaviour | ~4 | most |

## The failure worth recording

The `select` run under mimo-v2.5 ended after 35 minutes with
`finish_reason: repetition_truncation` and **no LOG.md at all**. It produced a
program that compiles, and no findings. A probe that cannot finish its report is
a probe that returned nothing.

## What the weaker model was better at

Nothing measured here, and that is the finding. The expectation was that a less
capable model with no Tycho context would behave more like a genuine newcomer.
What it produced was mostly expectation mismatches against Python and C — "there
is no `while`", "enum variant names are global", "no module-level variables" —
which are documented language choices rather than gaps. It logged one good
diagnostic (`inout string` at the FFI, whose message explains precisely why it is
refused) as friction.

It did find one real thing the Claude runs missed: `const XS = [1, 2, 3]` is
refused with *"const value must be a literal"*, and `[1, 2, 3]` is a literal. The
message names the wrong rule, the same class of defect as the arithmetic message.

## What this says about running probes

A probe's value came from **where it was aimed**, not from which model held the
pen. The `select` defect was found by aiming at `select`; the `bounded`
documentation hole by requiring `bounded`. Both instruments rediscovered the
same package-per-directory rule, because both met it the same way.

Neither instrument is a stranger. Both are a program that read the docs. That is
worth having — it found real defects twice — and it is not what §1 means by a
second mental model.
