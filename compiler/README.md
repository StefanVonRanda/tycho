# `compiler/` — the self-hosting artifact (FROZEN 2026-07-26)

Everything in this directory is **preserved history, not live infrastructure**.

## What is here

| Path | What it is |
|---|---|
| `tychoc0.ty` | A compiler for Tycho, written in Tycho (~16k lines). The artifact that proved self-hosting. **Frozen** — read its header comment first. |
| `tests/` | The bootstrap fixture corpus `tychoc0` was validated against. |
| `run.sh` | The old `make bootstrap` harness: build `tychoc0`, run each fixture through it and the reference compiler, compare. |
| `fixpoint.sh` | The old `make fixpoint` harness: `A = tychoc·tychoc0.ty`, `B = A·tychoc0.ty`, `C = B·tychoc0.ty`, assert `B == C` byte-identical. |
| `pkg-split.sh` | A helper for the multi-file split of `tychoc0.ty`. |

`run.sh` and `fixpoint.sh` have **no `make` target any more** and are not run by
`scripts/ci.sh`. They are kept so the method behind the recorded result is
readable, not because anything invokes them.

## What it proved

Compiled by the C reference compiler and then run on its own source, `tychoc0`
reproduces its own emitted C **byte-for-byte**, and the programs it built matched
the reference compiler's output across the whole test corpus, the corelib, the
concurrency suite and the FFI suite. Tycho self-hosts. That result stands as
recorded; it is not re-run.

## What changed on 2026-07-26

`tychoc0` was **frozen and removed from every gate**. Thirteen of the nineteen CI
steps used to assert that `tychoc` and `tychoc0` *agree* — `fixpoint`,
`frontparity`, `rtparity`, `typeparity`, `parforparity`, `eqparity`,
`unaryparity`, the differential `fuzz`/`fuzz-pkg` halves, and the `tychoc0` side
of `test`, `corelib`, `conc`, `ffi`, `recursion` and `spec-check`. All of those
comparisons are gone. Nothing mirrors a language change into `tychoc0` any more.

## The consequence: it is diverging

`tychoc0` compiles **the language as it stood on the freeze date**. From that date
forward it and `tychoc` accept and reject different programs, and nothing checks
the difference. The first divergence is already in the tree: `tychoc` rejects a
reserved word used as a procedure name (`fn handle(...)`) with a diagnostic naming
the keyword; `tychoc0` still accepts it.

So `tychoc0` is **not authoritative about Tycho's semantics**. Reading it to learn
how the language behaves today means reading history. [`docs/spec/`](../docs/spec/)
is normative and `src/tychoc.c` (`tychoc`) is the reference implementation.

None of this means it is broken. It is not deprecated for being wrong — it
correctly compiles the language it was frozen against, which is exactly what it
was built to demonstrate. It is finished, not failed. Do not "fix" it, do not
update it, and do not re-gate it.
