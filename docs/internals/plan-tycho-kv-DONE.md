# What the next program says the language needs

**Status: DONE 2026-08-03. Program: persistent B-tree key-value store (`tools/tycho-kv/`). Outcome: `io.write_at` shipped, concurrent `pscan` shipped, differential byte-identical over 1,816 commands.**

> This plan is a fresh clone, 2026-08-03: the completed tycho-scheme compiler
> plan lives at `docs/internals/plan-tycho-scheme-compiler-DONE.md`. The rule
> from that plan holds here: *does anything that is not the program written to
> want it need this?* A finding becomes a phase only when a second,
> independent caller exists.

## The program

**A persistent B-tree key-value store** -- `tools/tycho-kv/`, a real data
structure over real random-access file I/O. The store is a B-tree whose nodes
are fixed-size pages in a file: `core:io`'s positional reads (`read_at`) and
positional writes, manual bytes serialization (the
VM's .tyc pattern), and a page cache. The tree is flattened into page
numbers -- the flat-index idiom again -- and the differential against a naive
map-backed store over the same command script is byte-identical.

The command line: `tycho-kv <file> <cmd> [args...]` with init/put/get/del/
scan, plus a batch mode that replays a command script -- the differential
drives both the B-tree store and the map-backed store through the same
script and compares their output byte-for-byte.

## Findings

**The differential is byte-identical.** Three command scripts (1,816 commands
in total, the biggest producing 5,074 output lines) replay against the B+
tree and against a naive in-memory map backend and print identical output;
the reloaded store reproduces the batch; the gate asserts all three on every
run. What the build found:

- **The B+ separator invariant is load-bearing.** A separator is a copy of
  its right child's first key, and DELETE breaks it in two ways, both
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
  all present. The demand-gated finding it filed: **core:io has no
  positional write** (`read_at` exists; the write side stops at whole-file
  `write_bytes`), so the store rewrites the whole file per batch. Fixed by `write_at` below.
- The usual syntax tax: `a, b := fn()` destructuring has no
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
differential stayed byte-identical (kv-check green); `corelib/test/io` gained a write_at case (in-place byte survives,
extend-past-EOF works). Gates: make corelib, shim-check, kv-check all green.

### Phase 2 -- the store's concurrent range scans  [DONE 2026-08-03]

**What shipped:** a `pscan` command (CLI + batch). The leaves are collected
in order (one serial pass), split into four contiguous slices, each scanned
in its own `spawn`ed task, and the rows merge in chunk order. The tasks
SHARE the loaded store: value semantics copies the map struct, the table is
heap data, and the share is read-only. Every script's `pscan` variant is
byte-identical to its serial `scan` (the gate asserts it), on trees after
200 inserts + 67 deletes and on an empty store.

**Findings:** the first concurrent program found no language gap. One design
constraint, recorded not phased: Task handles are affine (cannot be stored in
a container), so the chunk count is a named constant with four explicit
spawns -- dynamic fan-out lives in `parallel for`, not in task arrays. The
runtime's spawn paths (conc tests) are unchanged.
