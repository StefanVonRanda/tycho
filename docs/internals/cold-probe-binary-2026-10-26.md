# Cold binary-protocol probe, 2026-10-26

> **Provenance.** This run used two fresh subagents inside the author's own
> session: one docs-only writer and one separate spec reviewer. They did not read
> the compiler, runtime or corelib source. That is a useful second mental model,
> not an independent third party; the findings stand, the independence does not.

**Verdict: the language held; the docs and one missing I/O primitive did not. The
probe is not the 1.0 external-program gate.**

The writer built a binary key-value server and client in a sandbox containing
only `README.md`, `docs/**` and the compiler wrapper. The brief required
`u32be` framing, a 64 KiB cap, partial reads and writes, pipelined frames,
eight connections, two concurrent clients, a generic `Ring(T)`, a
`bounded[8]` connection table, a live `select`, malformed-frame survival, and a
deterministic raw-socket harness. The program was discarded; this record is the
artifact.

## What passed

- The writer's own harness: green. It covered the functional corpus, duplicate
  keys, empty values, spaces, stats accounting, two clients, byte-at-a-time
  frames, pipelined frames, unknown/empty/malformed/truncated/oversized frames,
  the ninth-connection refusal, and a final health request.
- The reviewer's independent raw-wire suite: 43 pass, 1 fail, 3 warnings. Client
  suite: 15 pass. A separate generic `Ring` full/wrap/clear check passed.
- The generic `Ring(T)`, `bounded[8]`, `select` over channels, fixed worker pool
  and `Result` error paths were all expressible from the docs alone.

## Friction that changed the tree

- **No stdout flush.** `print`/`println` are stdio-buffered on a pipe, so the
  server's `READY` line was invisible while the process lived. The writer tried
  libc `fflush` and `write` through FFI; both failed against the real C
  prototypes, and no documented flush existed. `io.flush_stdout()` now exists
  (`corelib/io/io.ty`, `corelib/io/io_shim.c`) and is covered by
  `corelib/test/io`.
- **`bounded[N]T = []` starts at count 0.** The docs said "0 to N" but not that
  an empty initialiser leaves every slot unwritable until `push`; the writer
  seed-pushed 65,540 slots. The bounded section in
  [arrays-slices](../reference/arrays-slices.md) now states this and points at
  `arrays.fill` for a dynamic backing array.
- **A generic struct cannot take `$N`.** `[$N]T` is a function-parameter const
  generic, not a struct capacity; the writer paid one capacity for every
  `Ring(T)`. The same doc section now says so.
- **`core:net`'s spec entry was stale.** It promised negative-int sentinels;
  the reference and 0.9.0 return `Result(T, NetErr)`. The spec entry in
  [18-library](../spec/18-library.md) now matches the implementation and points
  at the catalogue.
- **`input()` cannot distinguish EOF from a blank line.** Both return `""`; the
  builtins reference and spec now say so and point at the streaming
  `io.read_line` for EOF.
- **`string`/`bytes` NUL wording contradicted itself.** The reference said
  `bytes` was not NUL-terminated "unlike `string`"; both are length-counted and
  both preserve interior NULs. The `bytes` section now states the real
  difference: intent and API contract, not storage.

## Server-shape findings

The reviewer reproduced three real defects in the **probe**, not in Tycho:

1. A connection table keyed only by raw fd leaked slots and aliased new
   connections after fd reuse.
2. Pipelined responses were reordered or lost because normal requests went
   through the dispatcher while errors were written directly by the reader.
3. A blocking write in the dispatcher let one non-reading client stall every
   other connection and new accepts.

Those are now stated as rules in the
[server-shape section](../reference/concurrency.md#server-shape) so the next
writer starts with them.

## Remaining limits

- `bounded` still has no fill/resize or `pop`; a pre-sized ring pays seed pushes
  or uses a dynamic `[T]`.
- A generic struct still cannot take a const-generic capacity.
- A task cannot be stored, so a per-connection task inside an accept loop joins
  at the end of each iteration; the worker-pool shape is the documented one.
- `select` is channel-only; socket readiness is not a select arm. A server needs
  tasks for blocking I/O or `net.wait_readable`.
- The probe source is not checked in; the record and the doc/corelib changes are
  the durable result.

## Why this is not the 1.0 gate

The writer and reviewer were fresh agents, but they ran inside the author's
session. They do not replace a program written by another person or an external
security review. The 1.0 conditions in
[ROADMAP](../../ROADMAP.md#what-10-requires) remain open.
