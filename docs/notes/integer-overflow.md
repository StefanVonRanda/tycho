# Integer overflow contract

> Internal working note. Status: SHIPPED 2026-06-23.

## The contract

Tycho's `int` is a 64-bit signed integer (C `long`). **Signed integer overflow
wraps two's-complement and is fully defined** — it is never C undefined
behaviour. `LONG_MAX + 1` is `LONG_MIN`; `LONG_MAX * 2` is `-2`; `x + 1 < x` is a
real (sometimes-true) comparison, not a tautology the optimizer may fold away.

This matches Go's integer semantics. It is *not* trapping (overflow does not
abort, unlike an out-of-bounds index) — that would add a branch to every signed
arithmetic op and regress the perf benchmarks against C / Go / Rust-release,
which all wrap. Trapping could be offered later as an opt-in hardened build.

## How it is enforced

Plain C signed overflow is UB, which an optimizer at `-O2`/`-O3` will exploit
(e.g. fold `x + 1 < x` to `false`). We close that hole with **`-fwrapv`** on
every cc invocation that turns Tycho-emitted C (or the compiler itself) into a
binary:

- generated programs: the codegen cc line in `src/tychoc.c` (`-O3 -fwrapv`);
- the C-built compiler: `Makefile` `CFLAGS` (`-O2 -fwrapv`);
- the self-hosted compiler + differential fixtures: `compiler/fixpoint.sh`;
- every test / fuzz / parity / conc / ffi / corelib harness that compiles
  generated C (so the validation oracles share production's overflow semantics —
  the fuzzer emits random arithmetic that *will* overflow).

`-fwrapv` also makes `-fsanitize=undefined` stop flagging signed overflow (it is
no longer UB), while leaving every other UBSan check (bounds, null, alignment,
bad shifts, …) active.

## Why it is load-bearing (verified)

The same emitted C for `tests/int_overflow.ty`:

```
cc -O2 -fwrapv  io.c -o w  &&  ./w   # -> "wrapped"
cc -O2          io.c -o n  &&  ./n   # -> "no-wrap"   (optimizer folded x+1 < x)
```

Without `-fwrapv` the optimizer assumed signed overflow cannot happen and
miscompiled the comparison. Regression: `tests/int_overflow.ty` (+ `.out`).
