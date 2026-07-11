# Soundness fuzzer

Differential + sanitizer fuzzing of the two transpilers. It hunts for
value-semantics / arena bugs — use-after-free, miscompiles, copy/move
unsoundness — that the hand-written `tests/` might miss.

## How it works

`gen.py <seed>` emits a **random, well-typed, deterministic, terminating** Tycho
program. It is type-directed (only constructs that type-check) and biased toward
the arena-stressing shapes: value-copy binds (`b := a`), heap built in loops/
blocks, `push`, struct/array/enum/tuple/option/map nesting, recursive enums +
`match`, `inout` container fill, and returned heap values. The program
accumulates an `int` checksum over everything it builds and prints it once.

`run.py <N> [start]` runs `N` seeds. For each, it compiles the program with:

- **tychoc** (the C reference transpiler) → native `-O2`  — the trusted oracle output.
- **tychoc0** (the self-hosted transpiler) → native `-O2` — must match tychoc byte-for-byte.
- **tychoc0** → `-fsanitize=address,undefined` — must not fault, and must match its
  own native output (catches UAF / heap corruption / UB).

Any output divergence, sanitizer fault, crash, or compile-acceptance mismatch is
a **finding**; the program is saved to `fuzz/findings/seed_<n>.ty`. Programs both
transpilers reject are skipped, so the generator can be aggressive. Leak detection
is off — leaks aren't soundness bugs; this targets *correctness*.

## Run it

```
make fuzz            # 500 seeds (default) — full sweep, ~30 min under ASan
make fuzz-quick      # 60 seeds (~1-2 min) — inner dev-loop smoke test
make fuzz-quick QN=120  # a slightly deeper quick sweep
make fuzz N=5000     # more
python3 fuzz/run.py 1000 2000   # 1000 seeds starting at seed 2000
```

Use `make fuzz-quick` while iterating on a compiler change for fast feedback;
run the full `make fuzz` before committing.

Deterministic: a finding's `seed_<n>.ty` reproduces with `python3 fuzz/gen.py <n>`.

## Coverage / extending

`gen.py` currently exercises int / `float` / string / `char` / arrays (`[int]`,
`[string]`, `[float]`) / structs / recursive enums + `match` / `Option(int)`
(+ `[Option(int)]`) / `Result([int], string)` / `(int, string)` tuples /
`{string:int}` + `{string:float}` maps / `type Nt = int` newtypes / array &
string slices (`v[:]`, `v[0:]`, …) / `inout [int]` / returned arrays / SOA core
ops (`soa []Struct`: push, `len`, `a[i].f` read/write, whole-element gather) /
`or_return` (Ok-unwrap / Err-propagate via a helper) / FFI `extern fn` over
scalars+`string`+opaque `ptr` (a fixed vocabulary backed by `ffi_shim.c`, linked
by `run.py`: `is_null`/`null`, ptr handle round-trips, and the string-return
arena-copy + NULL-guard). To widen coverage, add a
type to `types_simple()` plus its `gen_expr` (construct) and `checksum_into`
(consume) cases, or add a statement kind in `gen_stmt`.

Two oracle-safety rules the generator must keep: (1) every value must reduce
into the `int` checksum — floats and int-newtypes via `to_int` (deterministic
truncation, identical in both transpilers since they emit the same C); (2) never
emit a construct that can fault at runtime in a *valid* program, or tychoc's own
run faults and run.py reports a false FAIL. The live example: **array** slices
`exit(1)` on out-of-bounds (string slices clamp), so array slices are restricted
to whole-array forms (`[:]`, `[0:]`) whose bounds hold at any runtime length.
SOA scatter/gather use index 0 with a length >= 1 build, and `or_return` only
appears inside the `Result`-returning helper (never `main`), keeping every
generated program valid under both transpilers.

## Robustness fuzzer (fail-closed) — `gen_malformed.py` + `run_reject.py`

A second lane that feeds **malformed** input to both transpilers and asserts each
**fails closed** — the opposite of the soundness lane above (which feeds only
valid programs). `gen_malformed.py <seed>` corrupts a real corpus program
(`tests/` + `examples/`) — truncation, byte/line edits, unbalanced brackets,
token soup, deep nesting — or emits pure random soup. `run_reject.py [N]` builds
**both** transpilers under ASan+UBSan and, per seed, a **false-positive-free**
oracle:

1. **No crash** — no segfault / abort / ASan / UBSan / hang. A clean error exit
   (a diagnostic) is the *correct* outcome; only an uncontrolled fault is a FAIL.
2. **No fail-open** — if a transpiler *accepts* the input it must emit C that
   compiles (`cc -fsyntax-only`); accepting bad input and emitting broken C is a
   missed-rejection / codegen-on-garbage bug.

Accept/reject **divergence** between the two front-ends is recorded (saved for
review) but is **not** a hard failure — they legitimately differ near the
grammar boundary. `make fuzz-reject` (N defaults to 500).

It found and fixed a series of tychoc0 fail-opens (duplicate decls/params/locals,
param-shadow, unknown function in statement position, no-main, unknown type
name) and a parser leniency (two statements on one line).

**Known limitation (documented, not guarded):** a sufficiently deep nested
expression (thousands of `[`/`(`) overflows the recursive-descent parser's C
stack — it errors cleanly without ASan, but ASan's ~3-4x stack overhead lowers
the threshold. This is the standard recursive-descent limit (gcc/clang/rustc all
overflow given enough nesting); rather than guard it, `gen_malformed.py` caps its
deep-nesting op at a realistic-but-stressful depth so the lane exercises nesting
without depth-DoS.
