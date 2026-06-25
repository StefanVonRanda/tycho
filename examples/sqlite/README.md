# SQLite — a real-library FFI dogfood

tycho calling **real SQLite** (in-memory) through its FFI, with **no hand-written
shim** — every binding is a direct `extern` to a `libsqlite3` symbol. Demonstrates
the whole stack on a serious C library:

- **out-parameter constructors (`mut ptr`)** — `sqlite3_open(":memory:", &db)` and
  `sqlite3_prepare_v2(..., &st, ...)` write their `sqlite3**` / `sqlite3_stmt**`
  result through a pointer out-param. tycho declares it `mut ptr` and the compiler
  passes the address for you (FFI R4) — the reason this binding no longer needs a
  shim.
- **`ptr` handles** — the `sqlite3*` db and `sqlite3_stmt*` cursor are opaque
  `ptr`s tycho holds and passes back, never dereferences.
- **`null` for unused C args** — `sqlite3_exec`'s trailing callback/arg/errmsg
  pointers pass as `null`.
- **string args** — SQL text passes as a zero-cost `char*`.
- **arena-copied string returns** — `sqlite3_column_text` returns SQLite's internal
  text pointer, valid only until the next `step()`/`finalize()`. tycho arena-copies
  it on return, so the value outlives the cursor. `run.sh` builds the program under
  ASan/UBSan to prove there's no use-after-free.
- **integer returns** — `sqlite3_step`'s result code is compared directly to
  `SQLITE_ROW` (100); row values use `sqlite3_column_int64` for a clean 64-bit read.
- **`--pkg sqlite3`** — pulls the link flags from `pkg-config`.

A note on integer widths: SQLite's result codes are all non-negative, so reading its
32-bit `int` returns as tycho's 64-bit `int` is correct here without ceremony. A C
function that can return a *negative* `int` needs `to_i32(...)` around the call to
sign-extend it (still no shim) — see `docs/ffi.md`.

## Build & run

```
tychoc demo.ty -o demo --pkg sqlite3      # no --shim; or: --link sqlite3
./demo
```

Expected output:

```
1:alice
2:bob
3:carol
count=3 sum=6
```

## Test (both compilers + ASan)

```
sh run.sh
```

Builds `demo.ty` with the C reference compiler **and** the self-hosted tychoc0,
runs the tychoc0 output under ASan/UBSan, and checks both against `expected.out`.
Skips cleanly if `libsqlite3` isn't installed (so it's not wired into `make ci`,
which must stay dependency-free).
