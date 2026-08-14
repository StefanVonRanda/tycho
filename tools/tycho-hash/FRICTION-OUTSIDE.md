# Friction writing `tycho-hash`, from outside

The **second** `ROADMAP.md` §1 program: sha256 over a directory tree, hashed by a
worker pool, with a report that does not depend on the pool width. Written
2026-08-14 from `docs/` — the concurrency guide, `docs/reference/builtins.md` and
the corelib sources — by someone who did not write the compiler.

`tools/tycho-diff` was deliberately a *sequential* algorithm. This one exists for
the other half of the surface: `spawn`, channels, backpressure, and the
determinism question every parallel tool has to answer. **Two of the three
programs §1 asks for now exist; it wants three, by two people.**

None of the four findings below repeat `tycho-diff`'s, which is the argument for
writing a second program at all rather than a longer first one.

---

## 1. `core:io` lists a directory, but not under any name you would search for

The function is **`io.list(p) -> [string]`**. Searching the corelib for
`read_dir`, `list_dir`, `readdir` or `dir` finds `io.is_dir` and `io.make_dir`
and misses the one that does the listing, because its name contains no "dir".
I concluded there was no directory listing in corelib at all and went looking for
how `tycho-ar` shells out — it does not; it calls `io.list`.

Nothing is missing. The cost is entirely in the name.

## 2. `io.list` returns `[]` for a directory it cannot read

Same value as an empty directory. That is the fail-open shape FRICTION #4
records for `parse_int` and #56 for `decimal.from_str`: a caller cannot tell
"nothing there" from "could not look". For a hashing tool the difference decides
whether a subtree is silently absent from the manifest.

Everything around it returns `Result`: `io.is_dir`, `io.read_bytes`,
`io.read_text`. `io.list` is the outlier, exactly as `from_str` was the outlier
in `core:decimal`.

## 3. `sha256.hex` takes a `string`; a file is `bytes`

So a file hash is not the one-shot it looks like — it is `init()` /
`update(&st, b)` / `final_hex(&st)`. That is the right API and `tycho-ar` uses it
the same way; the friction is that the one-shot exists next to it under the
obvious name and does not take the obvious type.

## 4. A spawned function must return a value, and a worker pool cannot be a loop

Two constraints that meet in the same place:

```
error: a spawned function must return a value (wait(t) yields it)
```

A fire-and-forget worker has to invent a return. And a `task` cannot live in an
array (affine: one owner, scope-bound), so **the worker set is a fixed list of
named `spawn`s** — the width cannot be computed:

```tycho
w1 := spawn worker(0, nw, jobs, out)
...
w8 := spawn worker(7, nw, jobs, out)
```

`--workers=N` therefore cannot change how many are *spawned*, only how many
*consume* (a worker whose id is past the live count returns immediately). That is
a real design constraint, not a bug — the alternative is `parallel for`, which
does the fan-out properly but **reduces only `int`/`float`, up to four**, so it
cannot collect a hash string. Between them there is no shape that both fans out
dynamically and returns non-scalar results.

The invented return earned its keep here: each worker returns how many files it
hashed, and the sum must be exactly the file count — which is what catches a job
dropped or done twice.

## 5. What went right

**Determinism was cheap to get and provable.** Each job carries its index, each
result carries it back, the report is assembled by index. The lane asserts the
output is byte-identical at 1, 2, 3, 5 and 8 workers, and the hashes agree with
`sha256sum(1)` — an independent implementation, not the program agreeing with
itself.

**Channels as parameters read well.** An affine value cannot be captured by a
closure or a `parallel for`, and being pushed toward passing them explicitly
made the ownership obvious rather than hidden.

---

## The mistake worth recording: my own check proved nothing

The first version parsed `--workers` and then spawned all eight regardless. The
option did nothing, so the determinism run compared **eight workers against eight
workers**, five times, and passed. A green result, measuring nothing.

It was caught by asking what the check would look like if the feature were
broken — not by the check itself. The lane now reads the per-worker split back:
at 8 workers every worker must take at least one file, and at 1 worker the first
must take **all** of them and the rest none. That second assertion is the
negative control for `--workers`, and it is the one that would have failed on the
first version.

The same trap then repeated one level up: my first attempt at a *negative
control* for the hash comparison keyed on a variable assigned one line later, so
it corrupted nothing and the lane stayed green — which briefly looked like the
comparison leg was decoration. Both the leg and the control have to be checked.
