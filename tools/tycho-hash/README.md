# tycho-hash

sha256 for every regular file under a directory, hashed in parallel.

```sh
tycho-hash DIR [--workers=N]
```

**The output does not depend on how many workers ran.** Workers finish out of
order, so the order cannot come from them: each job carries its index, each
result carries it back, and the report is assembled by index. The bytes are
identical at any `--workers` value.

Build it from the repo root:

```sh
make tychoc
./tychoc tools/tycho-hash/main.ty -o tycho-hash
```

The design notes and the rough edges hit while writing it are in
[`FRICTION-OUTSIDE.md`](FRICTION-OUTSIDE.md).
