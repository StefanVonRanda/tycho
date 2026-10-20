# tycho-dupe

Rule: the same tree produces the same report, and a duplicate set is ordered by
bytes reclaimable. The report does not depend on directory order or hash-map
order.

Find duplicate files by content. Only files whose size is shared with another
file are read, and only the first 1 MiB at a time is in memory.

```sh
tycho-dupe [--min-size N] [--top N] [--json] DIR
```

`--min-size` ignores files smaller than `N` bytes (default 1). `--top` limits
the displayed sets (default all). `--json` emits roots, scan totals, and groups.
Repeat `DIR` for multiple roots. Text mode shell-quotes each path with
`path.quote`; `--json` carries the raw path. Symlinks and hard links are skipped,
as are FIFOs, sockets and devices.

Build it from the repo root:

```sh
make tychoc
./tychoc tools/tycho-dupe/main.ty -o tycho-dupe
```

The probe record, including the corelib gaps the program hit, is in
[`FRICTION-OUTSIDE.md`](FRICTION-OUTSIDE.md).
