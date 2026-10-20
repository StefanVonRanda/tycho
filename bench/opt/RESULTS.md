# Compiler-optimization results

## O1 — immortal allocation + literal lifetime elision (SHIPPED 0.8.9)

Date: 2026-10-20. Host: Linux x86-64, AMD Ryzen 7 7735HS.
Compiler: `tychoc 0.8.9` at the working tree.

Fixture: `bench/litarr_build.ty` — 6,000,000 `push(b.tags, "literal")` calls into
a projected `[string]` field. Output is `6000000` in every build.

Method: build the fixture with the pre-change runtime and with the immortal
runtime, then run each binary five times under `bench/peakrss.c` and take the
median. The fixture, compiler source and machine are otherwise identical.

```text
cc -O2 -o /tmp/peakrss bench/peakrss.c
./tychoc1 bench/litarr_build.ty -o /tmp/lit_before   # pre-change tree
./tychoc1 bench/litarr_build.ty -o /tmp/lit_after    # immortal runtime
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/lit_before; done
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/lit_after;  done
```

| build | output | peak RSS (median of 5) | wall time (median of 5) |
|---|---:|---:|---:|
| copy before | 6000000 | 207,460 KB | 61 ms |
| immortal pointer | 6000000 | **114,056 KB** | **28 ms** |

That is **45% less peak RSS and 54% less wall time** on a literal-heavy builder.

### What shipped

- `TYCHO_LIT` stores a **negative** length header. `tycho_str_len` masks it to
  the real length; no other representation changed.
- `tycho_str_copy` returns an immortal pointer unchanged: strings are immutable,
  so sharing is value-identical.
- `tycho_str_recycle` skips immortal pointers; `tycho_str_slot_set`,
  `tycho_arr_str_set`, `push`/`pop`, concat, equality, split, hash and print all
  read through `tycho_str_len`.
- No compiler branch was needed. The existing copy sites inherit the elision;
  both compilers benefit.

### Gates

`make ci N=0` green; `make test` 1156 passed / 0 failed; `make corelib` 46 ok;
`make parse-check` 1505 files green; `make bench` all bounds; `make docs-fences`,
`make check-links`, `make surface-check`, `make goldens-check` green.

## O2 — read-only pass-by-pointer (SHIPPED 0.8.9)

Date: 2026-10-20. Host: Linux x86-64, AMD Ryzen 7 7735HS.
Compiler: `tychoc 0.8.9`.

Fixture: `bench/opt/bigparam/main.ty` — 5,000,000 calls to
`fn first(xs: [256]int) -> int`, which reads one element. The emitted C is
compiled with `-O2 -fno-inline` so the call cannot be inlined away and the ABI
copy is the measured cost.

```text
./tychoc bench/opt/bigparam/main.ty --emit-c -o /tmp/bp_after
TYCHOC_NO_PTR_PARAMS=1 ./tychoc bench/opt/bigparam/main.ty --emit-c -o /tmp/bp_before
./tychoc1 bench/opt/bigparam/main.ty --emit-c -o /tmp/bp_shipped
cc -O2 -fno-inline -std=c11 -o /tmp/bp_after.bin /tmp/bp_after.c
cc -O2 -fno-inline -std=c11 -o /tmp/bp_before.bin /tmp/bp_before.c
cc -O2 -fno-inline -std=c11 -o /tmp/bp_shipped.bin /tmp/bp_shipped.c
```

| build | output | peak RSS (median of 10) | wall time (median of 10) |
|---|---:|---:|---:|
| value copy before | 5000000 | 1,400 KB | 140 ms |
| pointer borrow | 5000000 | 1,400 KB | **66 ms** |
| packaged self-hosted (`tychoc1`) | 5000000 | 1,412 KB | **10 ms** |

That is **53% less wall time** in the reference compiler; RSS is unchanged
because the array is one live value either way. The medians are over ten runs
(two batches of five) to damp host frequency drift. With default `-O2` the tiny
callee is inlined and the copy disappears, which is why the fixture disables
inlining: the ABI matters for calls C would otherwise not inline.

The pointer ABI is bridged through named-function value thunks, lambda thunks
and spawn trampolines; FFI and `inout`/`sink` keep their existing ABI.

**What the release actually contains:** the native archive ships `tychoc1`
(the self-hosted compiler), not the reference compiler. `tychoc1` already
lowers `[N]T` to a 24-byte dynamic-array descriptor, so it passes the array
without copying its 2 KB payload and measures 10 ms here. O2 is therefore a
reference/bootstrap-compiler parity change: it removes the 2 KB copy from
`./tychoc` and brings it to the representation the shipped compiler already
had. The packaged binary's behavior and performance are unchanged.

## O3–O7

No measurements yet. Each phase adds its fixture and records the same table
(copy/control vs optimized) before the phase is marked done.
