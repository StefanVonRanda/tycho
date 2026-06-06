# SQLite — a real-library FFI dogfood

hier calling **real SQLite** (in-memory) through its FFI. Demonstrates the whole
stack on a serious C library:

- **`ptr` handles** — the `sqlite3*` db and `sqlite3_stmt*` cursor are opaque
  `ptr`s hier holds and passes back, never dereferences.
- **string args** — SQL text passes as a zero-cost `char*`.
- **arena-copied string returns** — `sx_col_text` returns SQLite's internal text
  pointer, which is only valid until the next `step()`/`finalize()`. hier
  arena-copies it on return, so the value outlives the cursor. `run.sh` builds
  the program under ASan/UBSan to prove there's no use-after-free.
- **`--shim`** — SQLite's `sqlite3_open`/`prepare_v2` use `T**` out-params hier's
  FFI can't express, so `sqlite_shim.c` adapts them to return-the-handle
  signatures. This is the Stage-3 companion-file pattern.
- **`--pkg sqlite3`** — pulls the link flags from `pkg-config`.

## Build & run

```
hierc demo.hi -o demo --shim sqlite_shim.c --pkg sqlite3   # or: --link sqlite3
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

Builds `demo.hi` with the C reference compiler **and** the self-hosted hierc0,
runs the hierc0 output under ASan/UBSan, and checks both against `expected.out`.
Skips cleanly if `libsqlite3` isn't installed (so it's not wired into `make ci`,
which must stay dependency-free).
