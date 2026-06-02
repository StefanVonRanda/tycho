# Soundness fuzzer

Differential + sanitizer fuzzing of the two compilers. It hunts for
value-semantics / arena bugs — use-after-free, miscompiles, copy/move
unsoundness — that the hand-written `tests/` might miss.

## How it works

`gen.py <seed>` emits a **random, well-typed, deterministic, terminating** Hier
program. It is type-directed (only constructs that type-check) and biased toward
the arena-stressing shapes: value-copy binds (`b := a`), heap built in loops/
blocks, `push`, struct/array/enum/tuple/option/map nesting, recursive enums +
`match`, `inout` container fill, and returned heap values. The program
accumulates an `int` checksum over everything it builds and prints it once.

`run.py <N> [start]` runs `N` seeds. For each, it compiles the program with:

- **hierc** (the C reference compiler) → native `-O2`  — the trusted oracle output.
- **hierc0** (the self-hosted compiler) → native `-O2` — must match hierc byte-for-byte.
- **hierc0** → `-fsanitize=address,undefined` — must not fault, and must match its
  own native output (catches UAF / heap corruption / UB).

Any output divergence, sanitizer fault, crash, or compile-acceptance mismatch is
a **finding**; the program is saved to `fuzz/findings/seed_<n>.hi`. Programs both
compilers reject are skipped, so the generator can be aggressive. Leak detection
is off — leaks aren't soundness bugs; this targets *correctness*.

## Run it

```
make fuzz            # 500 seeds (default)
make fuzz N=5000     # more
python3 fuzz/run.py 1000 2000   # 1000 seeds starting at seed 2000
```

Deterministic: a finding's `seed_<n>.hi` reproduces with `python3 fuzz/gen.py <n>`.

## Coverage / extending

`gen.py` currently exercises int / string / arrays / structs / recursive enums +
`match` / `Option(int)` (+ `[Option(int)]`) / `(int, string)` tuples /
`{string:int}` maps / `inout [int]` / returned arrays. To widen coverage, add a
type to `types_simple()` plus its `gen_expr` (construct) and `checksum_into`
(consume) cases, or add a statement kind in `gen_stmt`.
