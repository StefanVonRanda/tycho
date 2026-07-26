# The bootstrap: how Tycho self-hosted, and what is left of it

This document exists because three files in the tree cite it by name —
`compiler/tychoc0.ty:617`, `compiler/run.sh:3` ("Stage 1 of docs/bootstrap.md") and
`compiler/fixpoint.sh:2` ("Stage 4 self-host fixpoint (docs/bootstrap.md)") — and
until 2026-07-26 it did not exist. The stage numbers in those headers had no
referent, which is the whole reason for this page: it names the stages, says which
script runs each one, and states plainly that **none of them runs in a gate any
more.**

> **Read [`compiler/tychoc0.ty`'s banner](../compiler/tychoc0.ty) first if you are
> here to learn the language.** `tychoc0` is FROZEN (2026-07-26) and is already
> diverging from Tycho. The normative language is [`docs/spec/`](spec/) and the
> reference implementation is `src/tychoc.c`. This page is history plus a map of
> the harnesses that produced it.

## The two compilers

| | what it is | where |
|---|---|---|
| `tychoc` | the reference compiler, written in C. Emits C, then invokes `cc`. | `src/tychoc.c` |
| `tychoc0` | a compiler for a **subset** of Tycho, written in Tycho — 17,244 lines including the subset needed to compile itself. Emits C to stdout; reads a path argument or stdin. | `compiler/tychoc0.ty` |

`tychoc0` is the bootstrap artifact. It was never the daily driver and never
compiled the whole language: the claim it was built to settle is that **Tycho is
expressive enough to write a Tycho compiler in**, and the way that claim is
falsifiable is a fixed point plus a differential.

## The stages, and the script that runs each

**Stage 1 — differential against the C compiler.** `compiler/run.sh` builds
`tychoc0` with `tychoc`, then for each of the 51 fixtures in `compiler/tests/`
compiles it *both* ways and requires the two binaries to print identically
(`compiler/run.sh:12-22`). This is the stage that says "`tychoc0` is a correct
compiler for its subset", and it is differential rather than golden-based on
purpose: the oracle is the other implementation, not a recorded file.

**Stages 2 and 3 — the self-emission chain.** With `H = compiler/tychoc0.ty`:

```
A = tychoc(H)      the C compiler builds the Tycho compiler       -> exe A
B = cc(A(H))       A compiles H; the emitted C is cc'd            -> exe B
C = cc(B(H))       B compiles H; the emitted C is cc'd            -> exe C
```

`A` was produced by a C-built compiler; `B` and `C` were produced by a
Tycho-built one.

**Stage 4 — the fixpoint.** `compiler/fixpoint.sh` asserts the emitted C of `B`
and of `C` is **byte-identical** (`compiler/fixpoint.sh:21-22`), i.e. `B == C`: the
Tycho compiler reproduces itself exactly, so nothing about `tychoc`'s own
compilation is leaking into the result. The same script then re-runs the Stage 1
differential over `tests/*.ty` and `examples/*.ty` (`:24-30`).

**Stage D — package programs.** Still in `fixpoint.sh` (`:31-53`): every
`tests/pkg/*/` package program is built by `B` in two ways — from `tychoc
--bundle`'s post-order source stream, and through `tychoc0`'s standalone on-disk
driver (`read_file`/`list_dir`/`args`, no `--bundle` middleman) — and both must
match the C compiler's output.

**Stage E — the packaging dogfood.** `compiler/pkg-split.sh` mechanically splits
`tychoc0.ty` into a two-package program (`package main` importing `package rt`)
and `fixpoint.sh` proves the split compiler is itself a fixed point and emits
byte-identical C to the single-file one. Repackaging the compiler changes no
output.

**The frontend tripwire.** `scripts/frontparity.sh` scores one direction only —
a program `tychoc` accepts and `tychoc0` refuses — with no `cc` and no run, so it
reddens in seconds where `fixpoint` takes minutes. Read its header for what it
covers and, as importantly, what it does not.

## The result

Tycho self-hosts. The fixpoint held, and `tychoc0` matched the reference
compiler's output across the test corpus, the corelib, the concurrency suite and
the FFI suite. `compiler/tychoc0.ty`'s banner is the primary record; the measured
detail is in [`docs/thesis.md`](thesis.md) and
[`docs/architecture.md`](architecture.md).

## What is live, and what is not — 2026-07-26

**Nothing on this page is a gate any more.** `tychoc0` was frozen on 2026-07-26
and cut out of CI: thirteen of the nineteen steps that compared the two compilers
were removed, and `scripts/ci.sh` builds no `tychoc0` binary. Conformance is now
stated against the specification and locked by recorded fixtures — see
[Appendix E.1](spec/appendix-e-conformance.md#e1-the-conformance-oracle).

The harness scripts survive **unreferenced, on purpose**, so the method behind the
recorded result stays readable: `compiler/run.sh`, `compiler/fixpoint.sh`,
`compiler/pkg-split.sh`, `scripts/frontparity.sh`, `tests/rtparity/`, and the
parity fuzzers under `fuzz/`. They still work if you run them by hand. They will
drift as `tychoc0` drifts, and that drift is expected rather than a defect.

Two consequences a reader needs, because both have cost real time:

1. **The freeze reaches the corelib, not just `tychoc0`.** Four per-example
   runners still feed their entry point to a freshly built `tychoc0`
   (`examples/webserver/run.sh:24`, `examples/weblog/run.sh:24`,
   `examples/fetch/run.sh:35`, `examples/sqlite/run.sh:31`), so every corelib
   package in those programs' import closure is written in the intersection of two
   languages. The enumerated split — **13 packages blocked, 24 free** — is in
   [Appendix E.2](spec/appendix-e-conformance.md#e2-the-coverage-matrix), and
   since 2026-07-26 `scripts/frontparity.sh` feeds those four entry points too, so
   the split is checkable by running it rather than by closing the import graph on
   paper.
2. **`compiler/tychoc0.ty`'s own `:N` self-citations are off by −50.** The freeze
   banner was prepended to an otherwise unchanged file; add 50 to any line number
   quoted in its comments. Citations *from* `docs/` into it were corrected and are
   right.
