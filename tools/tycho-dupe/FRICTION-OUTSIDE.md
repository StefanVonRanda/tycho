# Friction writing `tycho-dupe`, from outside

Rule: the same tree produces the same report, and duplicate sets are ordered by
bytes reclaimable. The report must not depend on directory order or hash-map
order.

Written 2026-10-19 against `docs/`, `corelib/`, `examples/`, `tools/` and
`README.md` by an AI agent (DeepSeek V4.1 Flash) on Linux x86-64,
`tychoc 0.8.7`. **This is a first-contact record, not a blinded probe.** The
agent was given the `tycho-syntax` correction layer and could see the full
checkout, including `tools/` and `tests/`; the compiler source was not read. The
blinded runs in [`docs/internals/`](../../docs/internals/) are still the
stronger instrument. Treat this as a lower bound on a stranger's friction.

> **Status, 2026-10-19: all seven phases landed.** Findings 1, 2, 5 and 6 are
> fixed in the tree; findings 3 and 4 are documented and pinned in the
> reference/spec. (The seven phases also include the `parse_int` rename, which
> was a known trap rather than a numbered finding here.) The program now uses
> `io.stat`/`lstat`/`same_file`, `path.real_checked`, `path.quote` and
> `eprintln`. The findings below keep the wording they had at first contact; the
> status notes say what changed.

The program is `main.ty` beside this file: 374 lines, no C shim, built and
gated by `make dupe-check`. It walks one or more roots, stats every file,
discards unique sizes, hashes only same-size candidates in 1 MiB chunks with
`core:sha256`, groups by digest, and orders groups by
`size * (files - 1)`. It supports `--min-size`, `--top`, `--json`, `--`, and
multiple roots.

A real scan of `corelib/` found one duplicate already in the tree:
`corelib/net/deps` and `corelib/signal/deps` both contain the same 17-byte link
flags. That is the tool doing its job, not a language finding.

---

# What went right

**The memory model disappeared.** I wrote strings, arrays of structs, nested
arrays, maps, `Result`, function-valued comparators and chunked file I/O. I
never wrote `free`, never named an arena, never added a lifetime annotation, and
never cloned a value to satisfy an owner. The one place mutation crossed a call,
`inout` was enough. This is exactly the batch-CLI, build-and-return shape the
thesis claims, and the claim held without effort.

**`corelib` was enough.** `io.list_checked`, `io.is_dir`, `io.size`,
`io.read_at`, `path.join`, `path.real`, `sha256.init/update/final_hex`,
`sort.sort_by`, `strings.parse_int_checked` and `match` on `Result` covered
everything. The streaming SHA-256 API is the right shape for a real tool: the
hash of a 2 MiB file costs 1 MiB of buffer, not 2 MiB of `string`.

**The two compilers agree.** `tychoc` and `tychoc1` emit byte-identical JSON for
the gate's tree. That is 374 lines using generics, maps, nested arrays, `inout`,
`Result` and function values, not a hello-world. The specification is tight
enough to implement twice.

**Diagnostics named the fix.** There were only two compile errors:

```text
error: 'bytes' is a reserved keyword and cannot be used as a field name
error: argument 3 of 'add_file' is inout; pass it as '&variable'
```

The first is documented since 2026-09-24 (the handle probe's finding 9) and is
still the first thing I typed; the second is a one-token fix and the message
says which token.

**Determinism was cheap.** `sort.asc(names)` fixed directory order,
`sort.sort_by` fixed file and group order, and the hash tie-break makes the
report stable. The language did not fight the deterministic version.

---

# Findings

## 1. `core:io` can classify a directory but not a regular file — no `stat` kind or inode

A duplicate finder has to know what an entry *is*. `io.is_dir` answers one
question, `io.size` and `io.mtime` answer others, and both follow symlinks.
There is no `stat`/`lstat` result, no file kind, and no inode or device. So:

- a **symlink to a file** looks like a second file; I detect it by comparing
  `path.real(full)` with the lexical path, which is a workaround, not a kind;
- a **hard link** looks like a duplicate, because it has a different path and
  the same bytes;
- a **FIFO, socket or device** is not distinguishable from a regular file, so
  `--min-size=0` can open one and block.

Measured, on this tree:

```text
$ ln hard/a.txt hard/b.txt
$ ./tycho-dupe hard
1 duplicate set, 11 B reclaimable across 2 files

11 B  2 files  (11 B each)  abb7f0ae43ba
  hard/a.txt
  hard/b.txt
```

Deleting one of those reclaims nothing. The tree already carries the answer in
C: `tools/tycho-du/du.c` uses `lstat` for exactly this reason. A tool that
should be pure Tycho needs a shim to ask a question the filesystem answers in
one call.

The cost is also three syscalls where one would do: each entry is
`io.is_dir` + `io.size` + `path.real`. A `stat` result collapses them.

**Suggested fix.**

```text
enum FileKind:
    File, Dir, Symlink, Fifo, Socket, Device, Other

struct Stat:
    kind: FileKind
    size: int
    mtime: int
    inode: int
    device: int

fn stat(p: string) -> Result(Stat, IoErr)       # follows symlinks
fn lstat(p: string) -> Result(Stat, IoErr)      # does not
fn same_file(a: string, b: string) -> Result(bool, IoErr)   # dev + ino
```

`same_file` is the narrow API a duplicate finder actually wants: skip a path
whose `(device, inode)` was already seen. A general `stat` pays for itself in
`tycho-du`, `tycho-hash`, static servers and any future backup tool. This is an
additive `corelib` change, which `surface.lock` already permits.

> **Fixed 2026-10-19:** `io.stat`, `io.lstat` and `io.same_file` are in
> `corelib/io`; the duplicate finder uses `lstat` for the walk and `same_file`
> for repeated roots, and its gate covers symlink, cycle and hard-link cases.

## 2. `path.real` collapses three failures into `""`

`path.real(p)` is documented as returning `""` when the path "does not exist,
cannot be read, or does not fit." My walk uses it as the dedupe key:

```text
resolved := path.real(p)
key := resolved
if key == "":
    key = p
```

The fallback is safe, but it cannot tell a permission error from a missing path
from an over-long path, and a symlink that cannot be resolved is then treated as
an ordinary path. The project has already made this move once: most of `io` now
returns `Result` instead of a sentinel, and `read_text` exists beside `read`
precisely because `""` cannot distinguish an empty file from a failure.

**Suggested fix.** Add `real_checked(p) -> Result(string, PathErr)` with at
least `NotFound`, `Denied`, `TooLong` and `BadPath`, and keep `real` as the
fail-closed convenience. The same shape as `io.read_text` beside `io.read`.

> **Fixed 2026-10-19:** `path.real_checked` exists with `PathErr`; the duplicate
> finder uses it to distinguish a missing root from a permission-denied one, and
> its gate tests both.

## 3. Forwarding an `inout` parameter needs `&scan` again

`walk_dir` takes `scan: inout Scan` and forwards it to `add_file`, which also
takes `inout Scan`. The natural spelling is:

```text
add_file(full, expected, min_size, scan)
```

and the compiler says:

```text
error: argument 3 of 'add_file' is inout; pass it as '&variable'
```

The fix is `&scan`. It works, and `tests/calc.ty` already does the same thing
with `parse_expr(toks, &pos)` from an `inout pos`, so the rule is not new. The
friction is that the reference pages show `&` from an ordinary variable, never
the forwarding case, so the first-contact writer has to discover it by
compiling. One example in `reference/functions.md` would close it. Allowing the
direct pass is the nicer language change, but it is a surface change; the doc
fix is free.

> **Documented 2026-10-19:** `reference/functions.md` and `spec/11-functions.md`
> now show the forwarding call (`bump(&n)`) as a compiling docs fence.

## 4. "A read-only look that costs nothing" is not true for fixed arrays or structs holding them

`Scan` holds a `[File]` and a `[string: bool]`. I first passed it whole to the
print functions, then changed the signatures to pass three ints, because I could
not tell whether that copied the scan or borrowed it. The docs say both things:

- `reference/basics.md`: "By default a parameter is a copy — for large values
  (arrays, maps, structs holding them) the callee instead gets a read-only look
  that costs nothing."
- `reference/functions.md`: "a heap-bearing value ... is a read-only borrow."

A side probe isolates the boundary. For a fixed array, the emitted C is:

```text
tycho_int h_show(Arena *_parent, TychoArrC0 h_xs);
struct TychoArrC0_ { tycho_int  v[64]; };
```

That is 512 bytes passed by value on every call. A struct with a
`data: [64]int` field emits `S_Big h_b` by value for the same reason. So the
sentence is true for **heap-bearing** values — the descriptor is copied and the
backing storage is shared read-only — and false for fixed arrays and structs
that inline them. `Scan` is the first kind and is cheap; the next program's
`Big` is the second and is not. The docs do not say which kind a given type is,
and the call site gives no signal.

This is the thesis's §4c wall in a smaller shape: correct code, a copy the
programmer cannot see. It is not a request for references; a read-only pointer
for large value types is enough.

**Suggested fix.** State the rule as a guarantee rather than an optimization:
default parameters are read-only borrows for heap-bearing values, scalars copy,
and a fixed array or a struct that inlines one copies unless the parameter is
`inout` or `sink`. Add a table with one example each for scalar, `string`,
`[int]`, `[N]int`, map, heap-bearing struct, inline-array struct, generic,
`inout` and `sink`. If a read-only parameter is a large value type, pass it by
pointer in the emitted C, or add a `borrow`/`const` parameter mode. A
`--warn-copy` at the call site would make the remaining copies visible.

> **Documented 2026-10-19:** the reference, guide and spec now carry the shape
> table; `[N]T` / `vector[N]T` and inline-array structs are named as full-value
> copies, not hidden under "large values."

## 5. `eprint` has no `eprintln`

Every error path in a CLI writes a line to stderr:

```text
eprint("tycho-dupe: " + msg + "\n")
```

`eprintln` is refused with "stderr has no println", so the newline is
boilerplate at every site and `usage_err` has to concatenate a multi-line string
for the same reason. This is small, but it is a papercut every tool pays for.
Add `eprintln(s)`, or a `stderr` writer, or put the idiom on the first CLI page.

> **Fixed 2026-10-19:** `eprintln(s)` is a builtin in both compilers; the
> duplicate finder's error paths and the language fixture use it.

## 6. Text output cannot represent arbitrary POSIX filenames

A path is arbitrary bytes. A file named `line\nbreak.txt` makes the default
report ambiguous; `--json` escapes it correctly and round-trips through Python's
`json`. That is a tool-design note, not a language defect: `println` cannot
quote what it does not know is a path. But the next tool will make the same
mistake unless the docs say "if you print paths, quote them or emit JSON." A
`path.quote`/`shell_quote` helper would be a small, useful corelib addition; a
docs line is cheaper.

> **Fixed 2026-10-19:** `path.quote(p)` is POSIX shell-safe single quoting; the
> duplicate finder's text report uses it, and `--json` remains raw.

---

# What I could not do

- Skip a hard link without a C shim, because `core:io` has no inode.
- Tell a FIFO or socket from a regular file without a C shim.
- Report *why* `path.real` failed.
- Produce a default text report that round-trips a path containing a newline.
- Tell, at a call site, whether a value type is passed by descriptor or copied
  inline.
- Pass a `[64]int` to a read-only function without a 512-byte copy.

# What is already known, and not re-filed

- **`bytes` is reserved.** The handle probe's finding 9; fixed in the reference
  pages on 2026-09-24. It is still the first error I hit, because the reflex is
  `bytes: int`. The fix is the correction layer, not a new document.
- **`parse_int` fails open.** Historical: it did when this record was written.
  The rename landed on 2026-10-19 — `parse_int` is now the strict `Result` form
  and `parse_int_prefix` is the explicit fail-open scanner. The checked parser
  is the obvious one now.
- **The `io` sentinel history.** Most of `io` is `Result` now; that work is why
  this program could be written without guessing. `path.real` is the remaining
  sentinel in the path layer.

# Suggestions, in priority order

1. **`core:io.stat` / `lstat` / `same_file`.** Highest value: it removes the
   only C shim this program would need, collapses three syscalls per entry into
   one, and gives every filesystem tool a correct answer for symlinks, hard
   links and special files.
2. **`path.real_checked`.** Follow the `io.read_text` precedent; keep `real` as
   the convenience.
3. **Document `inout` forwarding.** One example in `reference/functions.md`;
   optionally allow the direct pass later.
4. **Make the parameter copy/borrow rule explicit, and stop copying large
   read-only value types.** A table in the reference; pass a large `[N]T` or
   inline-array struct by pointer when the parameter is read-only; a
   `--warn-copy` where the compiler can see a copy.
5. **`eprintln`, or a documented stderr idiom.** Small, universal.
6. **Make the checked integer parser the obvious one.** The fail-open name is
   still a trap for a first-contact CLI.
7. **A path-quoting helper or a docs line.** Text reports of arbitrary paths
   need it; JSON is the safe default.

The program is worth keeping for the gate alone: it covers filesystem walking,
content hashing, `Result` propagation, sorting, nested aggregates, JSON output
and both compilers on a surface no other tool in `tools/` covers. It also gives
`make dupe-check` a negative control for the "only same-size candidates are
hashed" claim: the 6-byte `unique.txt` is read and discarded, while the 2 MiB
and 1 MiB groups prove the chunked hash path at two different boundaries.
