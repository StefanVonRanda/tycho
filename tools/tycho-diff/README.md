# tycho-diff

A unified diff between two text files.

```sh
tycho-diff OLD NEW [--context=N]
```

`--context=N` sets the number of unchanged lines shown around each change
(default 3, `tools/tycho-diff/main.ty:14`).

Exit status follows `diff(1)`: **0** when the files match, **1** when they
differ, **2** on an error.

Build it from the repo root:

```sh
make tychoc
./tychoc tools/tycho-diff/main.ty -o tycho-diff
```

The design notes and the rough edges hit while writing it are in
[`FRICTION-OUTSIDE.md`](FRICTION-OUTSIDE.md).
