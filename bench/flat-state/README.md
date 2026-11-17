# flat-state — one root vs natural nesting, same state

The same music library in two layouts, to measure what a flat, pointer-free
element representation buys in Tycho when the semantics are identical:

- **flat/** — one `State` root. Names are `(offset, length)` into one text blob;
  albums are a pool of 24-byte `AlbumRec`; tracks are one global array of
  16-byte `TrackRec`, addressed by `(offset, count)`.
- **nested/** — the natural value layout. `State` holds `[Artist]` and a pool of
  `Album`, and every album owns a `[Track]` with a `string` per field.

Both build the same corpus, remove the same 1-in-20 albums, hash the live state
into a checksum, save, reload, hash again, and assert a removed handle is dead.
The `result` line must match exactly; that is the correctness oracle.

## Corpus

512 artists × 24 albums × 16 tracks = 196,608 tracks built, 12,288 albums.
Every 20th album is removed, leaving **11,673 live albums / 186,768 live
tracks**. Names repeat by construction (`corpus/corpus.ty`), which is what makes
interning meaningful.

## Wire format

Little-endian, versioned. The nested file stores a length-prefixed string per
field. The flat file stores one text blob, a `NameRef` table, then fixed-size
records; the track array is stored in album order, and each album's `track_off`
is normalized to its position in that sequence (the loader rejects a mismatch).
Both write the whole payload with `io.write_bytes` after building it in
`core:buffer`; both read it with one `io.read_bytes`.

## Run

```sh
sh bench/flat-state/run.sh              # 9 interleaved runs by default
RUNS=3 sh bench/flat-state/run.sh
TYCHOC=./tychoc sh bench/flat-state/run.sh
make bench-flat-state                   # TYCHOC=./tychoc by default
```

The harness compiles both programs, measures each build/load with
`bench/peakrss`, interleaves the layouts and alternates order, prints medians
plus min..max spreads, and checks the result lines against each other. It also
runs `TYCHO_ARENA_STATS=full` for teardown and high-water, and `strace` for the
file read/write syscall counts.

`RESULTS.md` is one `RUNS=9` session. Absolute times on this host move with the
`powersave` CPU governor; the ratios and the RSS/wire numbers are the stable
part. `tychoc1` and `tychoc` are byte-identical builds (0.9.2) and produce the
same wire bytes and checksum.

## What this is not

The flat root still holds array descriptors — the hidden pointer is in `[T]`,
not in the records. `to_bytes([T])` moves a whole packed array in one copy, but
save still stages the wire in `core:buffer`, which is an `[int]`, and compacts
the track array before converting it. The probe record in
`docs/internals/probe-flat-state-2026-11-13.md` covers the remaining copies.
