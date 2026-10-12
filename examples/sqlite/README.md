# SQLite — a real-library FFI dogfood

Rule: every binding is a direct `extern` to a `libsqlite3` symbol — no hand-written shim.

Tycho calling **real SQLite** (in-memory) through its FFI. It shows
the whole stack working against a serious C library:

- **out-parameter constructors (`inout ptr`)** — `sqlite3_open(":memory:", &db)` and
  `sqlite3_prepare_v2(..., &st, ...)` write their `sqlite3**` / `sqlite3_stmt**`
  result through a pointer out-param. Tycho declares it `inout ptr` and the transpiler
  passes the address (FFI R4) — the reason this binding needs no
  shim.
- **`ptr` handles** — the `sqlite3*` db and `sqlite3_stmt*` cursor are opaque
  `ptr`s Tycho holds and passes back, never dereferences.
- **`null` for unused C args** — `sqlite3_exec`'s trailing callback/arg/errmsg
  pointers pass as `null`.
- **string args** — SQL text passes as a zero-cost `char*`.
- **arena-copied string returns** — `sqlite3_column_text` returns SQLite's internal
  text pointer, valid only until the next `step()`/`finalize()`. Tycho arena-copies
  it on return, so the value outlives the cursor. `run.sh` builds the program under
  ASan/UBSan to prove there's no use-after-free.
- **integer returns** — `sqlite3_step`'s result code is compared directly to
  `SQLITE_ROW` (100); row values use `sqlite3_column_int64` for a clean 64-bit read.
- **`--pkg sqlite3`** — pulls the link flags from `pkg-config`.

SQLite's result-code functions return C `int`, so the bindings use Tycho `i32` and
emit matching prototypes. The 64-bit row-value API uses `-> int`. Sized integer
arguments and returns are distinct types; see `docs/reference/ffi.md`.

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

## Test (ASan)

```
sh run.sh
```

Builds `demo.ty` with `tychoc`, runs the output under ASan/UBSan, and checks it
against `expected.out`.
Skips cleanly if `libsqlite3` isn't installed, so it's not wired into `make ci`,
which has to stay dependency-free.
