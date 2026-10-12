# tycho-fold

Rule: width counts **codepoints**, not bytes. Use `--bytes` only when you want byte counting.

Wrap text to a width:

```sh
tycho-fold FILE [--width=N] [--bytes]
```

`--width` defaults to 40.

In Tycho `len(s)` is a byte count, so a wrapper built on it puts `héllo wörld` in the wrong column — and can split a line mid-sequence, emitting a lone continuation byte and output that is no longer valid UTF-8.

`--bytes` selects the byte-counting behaviour deliberately, so the two can be compared: they agree on ASCII and differ on anything else.

Build it from the repo root:

```sh
make tychoc
./tychoc tools/tycho-fold/main.ty -o tycho-fold
```

The design notes and the rough edges hit while writing it are in
[`FRICTION-OUTSIDE.md`](FRICTION-OUTSIDE.md).
