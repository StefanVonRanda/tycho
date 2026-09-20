# Subnormal float literals become 0.0 on aarch64 Windows

Status: **OPEN**. Found 2026-09-20 by `make platform-check`, the first run that
ever executed Tycho on Windows. Not diagnosed to a root cause and not fixed.

## The defect

A float literal below `DBL_MIN` reaches the program as `0.0` when compiled by
`tychoc` running on **aarch64 Windows**. The smallest *normal* double is
unaffected, so the boundary is exactly normal-vs-subnormal.

Minimal reproducer:

```tycho
fn main():
    a := 1e-308
    b := 5e-324
    c := 2.2250738585072014e-308
    println(str(a))
    println(str(b))
    println(str(c))
```

| literal | macos-arm64 / linux / windows-x86_64 | windows-arm64 |
|---|---|---|
| `1e-308` (subnormal) | `1e-308` | **`0.0`** |
| `5e-324` (`DBL_TRUE_MIN`) | `4.94065645841247e-324` | **`0.0`** |
| `2.2250738585072014e-308` (`DBL_MIN`) | correct | correct |

## Scope

**aarch64 Windows only.** `windows-x86_64` passes the whole fixture set
289/289 including `float_roundtrip`, and macos-arm64, linux-arm64 and
linux-x86_64 are 1065/1065. So it is not "Windows" and it is not "aarch64" --
it is the pair.

The lane that catches it is `tests/float_roundtrip.ty`; the platform matrix
reports `windows-arm64 288/289` with that fixture the sole failure.

## What has been ruled out

Each of these was measured on the guest, not assumed:

- **Flush-to-zero in hardware.** `FPCR = 0x0`; bit 24 (`FZ`) is clear. A C
  program compiled with the same `clang` prints `DBL_TRUE_MIN` and `1e-308`
  correctly. This was the first hypothesis and it was wrong.
- **The CRT's `strtod`.** `strtod("1e-308")` returns `9.9999999999999991e-309`
  and `strtod("5e-324")` returns `4.9406564584124654e-324` on that guest --
  both correct.
- **The CRT's `snprintf`.** `%.17g` prints both subnormals correctly there.
- **The runtime's formatter.** `tycho_float_to_str` /
  `ty_fmt_shortest` (`runtime/tycho_rt.c:1762`) is reached with a value that is
  ALREADY zero; it is not losing anything.
- **A long-double path in the emitter.** `c_dtoa`
  (`src/tychoc.c@c_dtoa`) formats with `%.17g` on a plain `double`. There is no
  `%Lg` anywhere in `src/tychoc.c`.

So both halves of the CRT behave correctly in isolation, and the runtime is
handed a value that is already wrong. The corruption happens inside `tychoc`
itself, between lexing the literal and writing the C.

## The one odd datum

An earlier emit of `tests/float_roundtrip.ty` on that guest produced

```c
h_show(&_t, TYCHO_LIT("1e-308      "), 6.0440297390716244e-4932);
```

`6.04e-4932` is far below double range and squarely in x87 80-bit
long-double territory. That number is the best lead in this file: it suggests
a varargs or width mismatch somewhere on the path, even though the obvious
`%Lg` is not present. It did not reproduce in the minimal case above, which
emitted a plain `0.0`, so the two may be different manifestations or the
earlier emit may have been from a run with a clobbered workspace (two matrix
runs were racing at the time -- see `506b5d79`).

## Where to start

1. Re-emit `sub.ty` on the guest and read the literal `tychoc` writes for
   `h_a`/`h_b`. If it is `0` the loss is at lex/parse; if it is
   `6.04e-4932`-shaped the loss is at emit.
2. `src/tychoc.c:652` is where a float literal is read with `c_strtod`
   (`src/tychoc.c@c_strtod`), which wraps `strtod` for comma-decimal locales.
   The wrapper copies into a buffer under some locales -- check the buffer and
   the `end` handling for a subnormal's longer digit string.
3. Note that `tychoc.exe` on that guest is built by
   `scripts/platform/win11arm/payload.ps1` with `-O1 -fwrapv -std=c11`, which
   is NOT the flag set the Makefile uses. Worth rebuilding it with the
   Makefile's flags before blaming the source.

## Reproducing

```sh
make platform-check                 # windows-arm64 reports 288/289
sh scripts/platform_matrix.sh --full --only windows-arm64
```

The VM is a UTM guest named `Windows` on the Mac mini; see
[`../../scripts/platform/win11arm/vm.sh`](../../scripts/platform/win11arm/vm.sh)
and the matrix in
[`../../scripts/platform_matrix.sh`](../../scripts/platform_matrix.sh).
