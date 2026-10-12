# `handle` probe, 2026-09-21 — 14 findings, eight fixed the same day

**Verdict: 14 findings, 8 fixed the same day, 5 open, 1 refuted class. Probed 2026-09-21 on the macOS arm64 laptop with `tychoc 0.8.5` built from `main` per [probe-procedure.md](probe-procedure.md). Outcome: the program is kept as [`tools/tycho-du/`](../../tools/tycho-du) with gate `make du-check`.**

First probe aimed at `handle` as the subject rather than one corner of
FFI generally ([2026-08-19](probe-ffi-2026-08-19.md) was the earlier one).

## The program

`tydu` — a recursive disk-usage reporter over POSIX `dirent`: largest files,
heaviest extensions, totals, and an explicit count of what it could not read.
280 lines of Tycho, an 84-line shim (`opendir`/`readdir`/`closedir`/`lstat` plus
live/open/close counters), `handle Dir: free: dw_close`, five `extern fn`s, and a
recursive `walk` returning `Result(int, WalkErr)`.

It ran, and it was checked against `find(1)`:
616 files, 235 directories, 5573303 bytes agree on all three totals. Built
from `--emit-c` under `-fsanitize=address,undefined -fno-sanitize-recover=all`
with the corelib shims from `--print-shims`, it is clean over a 620-file walk and
over 50 strict-mode error unwinds.

**Why it did not use `list_dir`:** the builtin "returns the same empty array for an empty directory and an
unreadable one — which is the one distinction a disk-usage tool must report."

## The score

| # | finding | verdict |
|--:|---|---|
| 1 | the spec's own one-line spelling of `handle` does not parse | **FIXED** `36fe264d` — and the corrected form is now a **compiled fence**, so it cannot drift again |
| 2 | spec §25 says a handle variable may be reassigned; the compiler refuses and `reference/ffi.md` agrees with the compiler | **FIXED** `4fa16643` |
| 3 | `is_null` on a handle exists in one sentence of one page; both normative tables say `ptr` only, and `tools/tycho-fh/fh.c` ships a shim justified by a comment saying it is impossible | **FIXED** `36fe264d` |
| 4 | an opener result never bound to a variable leaks, silently | **FIXED** `4fa16643` — now refused |
| 5 | the destructor is never called with NULL; the one shipped handle program's comments assume the opposite | **FIXED** `36fe264d` |
| 6 | `free:` naming an undeclared function falls through to `cc`; naming one at the wrong type compiles silently | **FIXED** `17ddcbde` |
| 7 | `docs/reference/ffi.md` links to itself twice as though it were another page | **FIXED** `36fe264d` |
| 12 | the worked example the FFI docs name for handles contains no handle | **FIXED** `36fe264d` |
| — | the same report's table of five other "dead links" | **NOT DEFECTS** — see below |
| 8 | `close(h)` on a handle is absent from `reference/builtins.md`, the page `docs/README.md` calls "the single answer layer" | **OPEN** |
| 9 | the reserved set includes the type keywords, so `bytes: int` is not a legal field; true, documented in appendix B, and named on no entry-path page | **OPEN** |
| 10 | `expected an expression` for a value `if` in a nested position never says the rule is *tail position only* | **OPEN** |
| 11 | two `-Wunused-value` warnings out of `core:strings` land in every user's build output | **OPEN** — the layout probe hit the same two |
| 13 | three things correct but unfindable: `-> Option(string)` on an `extern`, `--print-shims`, and that `tychoc --help` beats both FFI pages on the command line | **OPEN** |
| 14 | §25's scope-exit list reads as though a `continue` frees a handle owned by the enclosing scope | **OPEN** |

Verified here 2026-09-22: 8, 11, 13 and 14 still reproduce against `main`.

**The dead-link table is an artifact of the strip, not a defect.** The report
names `rfc/ffi-threading-design-review.md`, `internals/design-aggregate-ref.md`,
`internals/value-semantics-limits.md`, `internals/README.md`, `rfc/README.md` and
`tests/reject/handle_dup_name.ty` as unreachable. Every one exists in the tree;
[probe-procedure.md](probe-procedure.md)'s setup deletes `docs/internals`,
`docs/rfc` and `tests` on purpose, and `make check-links` is green. The strip
guarantees this class of false finding on every future run, and the procedure
states it where it lists the deletions.

## What went right, in the probe's words

- **The affine diagnostics.** "The best I have seen for a resource-ownership
  rule" — every violation refused, at the right line, with the model explained
  rather than the parse, and a `note:` pointing back at the `handle` declaration.
  The `sink` refusal answering the next question unprompted ("pass it plainly:
  that is already a borrow") saved a doc lookup.
- **The rule shaped the program, correctly.** Because `walk` can neither return
  nor store a `Dir`, the walk had to be recursive with the handle owned by the
  frame that opened it — "the right design for this program and I would not
  necessarily have chosen it."
- **RAII held on all six exits of `walk`, including the `or_return`**, proved
  from the emitted C and then measured: 400 unwinds out of a live-handle frame
  give `opens 400 closes 400 live 0`, and 6000 `opendir`s under `ulimit -n 64`
  never fail.
- **Spec §24.1.1 predicted the right `extern` shape for a call returning both a
  size and a classification**, C signature included — "the best-written page in
  the documentation."
- **Two `reference/ffi.md` warnings named the probe's bug before it wrote it**:
  the `closedir(d); return ent->d_name;` shape, which is literally the API it was
  binding, and the malloc-return leak. Two bug classes pre-empted by prose.
- The non-FFI half compiled essentially first-try, and `parse_int_checked` was
  chosen over `parse_int` *because the catalogue names the fail-open at the point
  of choice* — [FRICTION #4 and #56](FRICTION.md)'s convention doing its job on a
  stranger.

## What it could not do

- Learn from any page what happens to an unbound handle. It added counters to its
  own shim and measured. That is finding 4.
- Learn which side the null guard is on without reading generated C from a
  deliberately broken program (finding 5).
- Distinguish "closed" from "never opened": `is_null` reads the variable, which
  `close` nulls, so both answer the same. Using a handle after `close` is
  documented as permitted and is not compile-rejected, so the one remaining
  misuse of an exactly-once resource is undetectable in both directions. The
  probe worked around it by never calling `close` at all. **Open, and a design
  question rather than a defect.**
- Use LeakSanitizer — unsupported on darwin/arm64, which is the same coverage
  hole [STATUS](../../STATUS.md) records for `make ci` on this platform. It
  substituted counters and an fd-exhaustion stress, which for this claim is
  better evidence.

## The run itself

`pi-run.log` holds one line, and it is a failed launch: `Error: Model
"mimo-v2.5" is ambiguous across providers […] No matching provider is
authenticated.` So the `pi` path did not run this probe and **which model wrote
it is not recorded anywhere in the directory.**
[probe-procedure.md](probe-procedure.md) covers model choice and needs a step that writes the model down.

The watchdog itself was defective on this host and was fixed first —
[FRICTION 122](FRICTION.md).

## Filing

**The program is in the tree** as [`tools/tycho-du/`](../../tools/tycho-du), with
its report beside it as `FRICTION-OUTSIDE.md` — what
[probe-procedure.md](probe-procedure.md) asks for when a probe program is worth
keeping. The case was finding 12's: it is the tree's **second** `handle` user and
its **only** demonstration of `handle` plus `--shim` together, which is the pairing
`docs/reference/ffi.md` recommends and which nothing in the tree showed. Both FFI
pages now point at it.

`make du-check` is its gate, lane `[4c/35]`. The leg that earns it is the one no
golden can show: 200 strict walks over a `chmod 000` directory, each unwinding
through `or_return` out of a frame that still owns its `Dir`, all ending `live=0`
with `opens == closes`. Both of the gate's load-bearing legs were run against
deliberate breakage before being trusted — a corrupted golden reddens [1], and a
destructor that does not decrement the live counter reddens [3a] and [3b] with
`live 600` and `live 200`.

The shim's comment claiming the destructor receives NULL was corrected on the way
in: it is the same false claim `tools/tycho-fh/fh.c` carried, which is finding 5,
and importing it unchanged would have re-seeded the defect this round closed.
