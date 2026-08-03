# What the next program says the language needs

> This plan is a fresh clone, 2026-08-03: the completed tycho-scheme compiler
> plan lives at `docs/internals/plan-tycho-scheme-compiler-DONE.md`. The rule
> from that plan holds here: *does anything that is not the program written to
> want it need this?* A finding becomes a phase only when a second,
> independent caller exists.

## The program

**A persistent B-tree key-value store** -- `tools/tycho-kv/`, a real data
structure over real random-access file I/O. The store is a B-tree whose nodes
are fixed-size pages in a file: `core:io`'s positional reads (`read_at`) and
the `write_bytes` path the backlog added, manual bytes serialization (the
VM's .tyc pattern), and a page cache. The tree is flattened into page
numbers -- the flat-index idiom again -- and the differential against a naive
map-backed store over the same command script must be byte-identical.

The command line: `tycho-kv <file> <cmd> [args...]` with init/put/get/del/
scan, plus a batch mode that replays a command script -- the differential
drives both the B-tree store and the map-backed store through the same
script and compares their output byte-for-byte, the tycho-scheme pattern.

Stress points this should find: page-sized value copies (value semantics
over big buffers), bytes manipulation, Option/Result plumbing, and -- if the
program lives long enough -- concurrent range scans via `spawn`, which the
single-threaded compiler never exercised.

## Findings

**The differential is byte-identical.** Three command scripts (1,816 commands
in total, the biggest producing 5,074 output lines) replay against the B+
tree and against a naive in-memory map backend and print identical output;
the reloaded store reproduces the batch; the gate asserts all three on every
run. What the build found:

- **The B+ separator invariant is load-bearing.** A separator is a copy of
  its right child's first key, and the DELETE breaks it in two ways, both
  caught by the differential and fixed: (1) deleting a leaf's FIRST key
  leaves the parent's separator stale -- the delete must propagate the
  subtree's new first key upward; (2) a merge leaves the separator AFTER the
  merged child stale (it still holds the merged-away child's first key) --
  both boundaries around a rebalanced child must mirror the children again.
  The structural verifier (a `verify` batch command) now checks the full
  invariant, including sep[i] == leftmost key of child[i+1].
- **Pages in the file must be fixed-width.** Internal nodes are smaller than
  leaves; writing them back-to-back made the load's offset arithmetic drift
  and truncation errors appeared on reload. Internal pages are now padded to
  the leaf page size.
- **`cli.argv()` excludes the program name** -- argv[0] is the first real
  argument. The interpreter's `run`-first check masked this; the store's
  multi-command CLI hit it immediately.
- **No language gap.** The store needed value-semantic structs with fixed
  arrays (`[9]string` fields), bytes build/index/slice/concat/to_str, Option/
  Result, inout threading, maps-of-structs, and lexical string comparison --
  all present. The demand-gated finding it DID file: **core:io has no
  positional write** (`read_at` exists; the write side stops at whole-file
  `write_bytes`), so the store rewrites the whole file per batch. One caller
  (the store); a `write_at` becomes a phase when a second program wants it.
- The usual syntax tax, paid again: `a, b := fn()` destructuring has no
  parens, `while` is `for cond:`, match arms cannot hold literal payloads,
  fixed-array sizes must be literals not consts, and a full node needs M+1
  capacity to hold the temporary ninth key before its split.

## Phases

### Phase 1 -- `io.write_at`: the positional write the read side always had  [DONE 2026-08-03]

**What shipped:** `corelib/io/io.ty`'s `write_at(p, off, b) -> Result(bool,
IoErr)` plus `iox_write_at` in the shim (open `O_RDWR|O_CREAT`, `pwrite` at
the offset, fail closed on a partial write -- the write_bytes discipline;
created if absent, so a store's first flush can lay down the file; NOT
truncating). The store's flush now writes the superpage plus only the DIRTY
pages at their own offsets (the Store tracks them; every insert/delete/split
marks what it touched) -- a batch no longer rewrites the whole file. The
differential stayed byte-identical (kv-check green), which is the round-trip
proof; `corelib/test/io` gained a write_at case (in-place byte survives,
extend-past-EOF works). Gates: make corelib, shim-check, kv-check all green.

### Phase 2 -- the store's concurrent range scans  [DONE 2026-08-03]

**What shipped:** a `pscan` command (CLI + batch). The leaves are collected
in order (one serial pass), split into four contiguous slices, each scanned
in its own `spawn`ed task, and the rows merge in chunk order. The tasks
SHARE the loaded store: value semantics copies the map struct, the table is
heap data, and the share is read-only -- the probe was whether concurrent
map reads are safe. They are: every script's `pscan` variant is
byte-identical to its serial `scan` (the gate asserts it), on trees after
200 inserts + 67 deletes and on an empty store.

**Findings:** the first concurrent program found no language gap. One design
constraint, recorded not phased: Task handles are affine (cannot be stored in
a container), so the chunk count is a named constant with four explicit
spawns -- dynamic fan-out lives in `parallel for`, not in task arrays. The
runtime's spawn paths (conc tests) are unchanged.



**Why now:** the store filed it with one caller; the API asymmetry is the same
shape that phased `write_bytes` in the backlog (the read side has
`read_bytes`/`read_at`; the write side stopped at whole-file writes). `read_at`
has never had a `write_at` sibling, so every program that wants to update one
page in place must read the whole file, patch it, and rewrite it all.

**The work:**
1. `corelib/io/io.ty` gains `write_at(p, off, b) -> Result(bool, IoErr)` -- the
   sibling of `read_at`, not truncating (bytes after the write are untouched,
   so in-place page updates work; a missing file is `Err(NotFound)`).
   `corelib/io/io_shim.c` gains `iox_write_at` (open `r+b`, seek, write, fail
   closed on partial writes -- the write_bytes discipline).
2. The store's flush switches from rewriting the whole file to writing the
   superpage + the DIRTY pages at their offsets: the Store tracks dirty pages,
   every insert/delete/split marks the pages it touched, and the file's extent
   is unchanged. The differential must stay byte-identical (the reload after a
   batch reproduces it), which is the round-trip proof for write_at.
3. A corelib test for write_at: write at an offset, confirm the untouched
   bytes survive.

**Gates:** `make corelib` (the io module), `make shim-check` (the shim moved),
`make kv-check` (the store now flushes through write_at; the differential is
the round-trip proof).

### Phase 2 -- the store's concurrent range scans  [not started]

**Why now:** every program so far is single-threaded. The store's deferred
concurrency angle is the first real `spawn` stress in the plan: the scan of a
large range splits into chunks, one task per chunk reads its leaf slice from
the SHARED loaded store, and the main task merges the results in key order.

**The work:**
1. A `pscan` batch command: collect the leaves in order (one serial pass),
   hand each chunk of leaves to a spawned task, merge the returned rows.
2. The differential extends: `pscan` output must equal `scan` (which equals
   the map backend's), so a race, a lost chunk, or an out-of-order merge
   reddens the existing gate.
3. The probes this finds get recorded: whether a spawned task can read a map
   shared from the parent's store without copies, whether the runtime's task
   spawn cap (1024) and the parallel-for chunking behave, and any concurrency
   finding the first multi-threaded program surfaces.

**Gates:** `make kv-check` (the pscan differential), `make conc` (the runtime
paths the tasks use), `make test` only if a runtime change appears.

