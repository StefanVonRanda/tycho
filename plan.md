# What the next program says the language needs

> This plan is a fresh clone, 2026-08-03: the completed tycho-scheme compiler
> plan lives at `docs/internals/plan-tycho-scheme-compiler-DONE.md`. The rule
> from that plan holds here: *does anything that is not the program written to
> want it need this?* A finding becomes a phase only when a second,
> independent caller exists.

## The program

(TBD -- candidates under investigation.)

- **A persistent B-tree key-value store** (recommended): a real data
  structure with real random-access file I/O -- core:io's positional reads
  and the write_bytes path the backlog added, page serialization, buffering,
  and value semantics over page-sized byte buffers. Natural stress: large
  value copies, bytes manipulation, Option/Result plumbing, and a
  concurrency angle (parallel range scans via spawn) the single-threaded
  compiler never exercised. Differential: a naive map-backed store over the
  same keyset, byte-identical answers.
- **Streaming inflate for tycho-ar** (extraction): tycho-ar has a one-shot
  decompressor; a streaming form (a `decompress` that feeds an incremental
  consumer) would complete the tool and give the filed backlog items --
  recursive `make_dirs`, writable mtime -- their first real customer.
- The choice belongs to whoever starts the plan; the program section is
  filled when picked.

## Findings

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
