# FRICTION

One line per moment the language got in the way while writing a real web server.
Non-blocking by construction: anything that blocks the server earns a phase in
`plan.md` instead. This file is a deliverable; fixing everything in it is not.

The program these notes came from is `server/` — `tycho-httpd`, ~440 lines,
serving `server/www` to a real browser. Everything below was hit while writing
it, not imagined about it.

> **How to read the plan references in this file (added 2026-07-30, RESOLVED
> 2026-07-31).** This block used to say that every "`plan.md` phase N" below —
> 51 of them — meant `docs/internals/plan-friction-DONE.md`, and that one line
> here made 51 claims true. **Measured, it did not.** The 53 such references in
> this file were written under **six different plans**: 28 under
> `docs/internals/plan-loops-cleanup-DONE.md`, 14 under
> `docs/internals/plan-friction-DONE.md`, 8 under
> `docs/internals/plan-option-result-DONE.md`, one each under
> `docs/internals/plan-postfreeze-rawstring-DONE.md` and
> `docs/internals/plan-prunner-DONE.md`, and one that still means the live
> `plan.md`. The re-scoring passes are interleaved line by line — 542 is
> friction, 543 and 544 are loops-cleanup, 545 is option-result — which is
> exactly the shape a single key cannot describe.
>
> Every one has therefore been rewritten in place to name the document it meant,
> so this block is now history rather than a lookup. "**`Option`/`Result` phase
> N**" and "**that plan's phase N**" still mean
> `docs/internals/plan-option-result-DONE.md` (5 phases, head `8aac642`).

## The headline

**The language has a good answer for fallible calls and the standard library
does not use it.** `Option`/`Result` are real types and `or_return` is a real
postfix operator that unwraps or short-circuits (`docs/spec/10-statements.md:75`).
Exactly **1 of the corelib's 386 functions returns an `Option`** —
`io.read_line` (`corelib/io/io.ty:69`). Every other fallible call in this server
reports failure with a sentinel, and the sentinel is different each time:

| call | failure is… | also means |
|---|---|---|
| `net.accept` | a negative int | — |
| `net.write`, `httpd.write_response` | `-1` | — |
| `net.read` | empty `bytes` | EOF **and** idle timeout |
| `io.read_bytes` | empty `bytes` | empty file **and** missing file **and** a directory |
| `path.safe_join` | `""` | — |
| `httpd.parse_request` | `method == ""` | EOF **and** timeout **and** malformed |
| `net.set_read_timeout_ms` | `false` | — |

Six spellings of "it went wrong", three of which are indistinguishable from a
legitimate success. The server code reads as `if x < 0` / `if len(x) == 0` /
`if s == ""` scattered through every IO path, and two of the phase's real bugs
were exactly this collision: the server could not tell a malformed request from
a hangup, and cannot tell an empty directory from a file. Nothing about the
language forced that — `or_return` was sitting right there.

> **Status, 2026-07-26.** The `net.*` rows above are historical: `core:net`'s
> fallible TCP calls now return `Result(T, net.NetErr)` and `net.read`
> distinguishes `Eof` / `Timeout` / `Failed` (`docs/internals/plan-option-result-DONE.md` phase 1). What that phase
> *measured* is that the win is confined to the ambiguous sentinels — converting
> an unambiguous one is line-for-line neutral — and that the conversion surfaced
> five new ergonomic gaps of its own, recorded below.
>
> **Also historical, from `docs/internals/plan-option-result-DONE.md` phase 2:** the `io.read_bytes` and
> `httpd.parse_request` rows. `read_bytes` returns `Result(bytes, io.IoErr)` —
> an empty file is `Ok`, `Err(NotFound)` and `Err(IsDir)` are distinct — and
> `parse_request` / `read_request` return `Result(Request, httpd.ReqErr)` with
> `Malformed` / `Closed` / `Timeout` / `Failed`, all four recorded as distinct in
> `corelib/test/httpd.out`. The missing `unwrap_or` is now `core:result`. The rows
> that remain true are `path.safe_join`, the `io` write side, `net.udp_*` and
> `net.set_read_timeout_ms` — each deliberately left on a sentinel that has
> exactly one meaning.
>
> **Closed out, from `docs/internals/plan-option-result-DONE.md` phase 3.** The headline's own worked example — the
> `read_head` reimplementation in the phase-7 list below — is **gone**. `server/`
> calls `httpd.read_request_capped(conn, MAX_HEAD)` and acts on the cause it
> returns: `Malformed` → `400`, `Closed` → no response and no log line,
> `Timeout` → `408` (or a quiet close for a keep-alive idle expiry), `TooLarge` →
> `431`. 19 lines of duplicated corelib loop and a local `Head` struct deleted;
> `server/main.ty` ended the plan at 371 code lines, **exactly where it started**.
> The honest arithmetic: the application did not get shorter, it got *truthful*,
> and the ~100 corelib lines that bought it are recorded in `plan.md`'s verdict.
> One item in this file is closed. The `stat` item is not, and is now known not to
> be an error-model problem at all.
>
> **Closed out, from `docs/internals/plan-option-result-DONE.md` phase 4 (added on a user directive after phase 3
> called it done).** The `stat` item too: `io.is_dir(p) -> Result(bool, IoErr)`
> exists over a 4-line `stat(2)` shim, and `resolve()` asks it instead of
> `len(io.list(p)) > 0`, so a directory redirects to its slash form whether or not
> it holds an index and a non-directory never does. `server/main.ty` stayed at 371
> code lines. The plan's Goal ("the two known wrong answers fixed") is met — and the
> half that needed a syscall cost 3% of the lines the half that needed a type did.
>
> **Recount, 2026-07-30.** "1 of the corelib's 386 functions" is the 2026-07-26 figure.
> At `afa67da` the corelib is **406** package functions, **1** returning an `Option` and
> **15** returning a `Result` — so the headline's *ratio* has moved even though its
> subject (only `io.read_line` returns an `Option`) has not. See the verdict's
> 2026-07-30 postscript for what that fifteenfold `Result` growth did and did not buy.
>
> **Correction from `docs/internals/plan-option-result-DONE.md` phase 5.** "Exactly where it started" was true for four
> phases and is no longer: phase 5 replaced the startup `--root` check's one wrong
> message with four accurate ones and `server/main.ty` ended at **380** code lines,
> +9. The plan's own final number is a *growth*, not a wash.

## The score against this file, re-scored against the tree, 2026-07-26

Two plans have now been run *because* of this file: the `Option`/`Result` plan
(archived, `docs/internals/plan-option-result-DONE.md`, 5 phases, head `8aac642`) and
the plan that worked this file as a list (10 phases, head `309c393`). This section is
the second one's closing act, and it is a re-score rather than an update: **every
"CLOSED" note in this file was checked against the thing it names before the counts
below were written** — 89 content checks (does that function / flag / document /
diagnostic exist at HEAD and say what the note claims?) and 8 live runs of the code.

**No stale CLOSED was found.** Two stale *OPENs* were, and they are named below.

> **Overtaken, 2026-07-30 (head `afa67da`).** The counts in this section are the
> 2026-07-26 figures and are kept as the record of that day. Roughly 60 commits have
> landed since — raw strings, element-wise array arithmetic, Odin-style loops with
> `range()` deleted, a `char` type, and the **retirement of the `tychoc0` freeze
> lanes** — and the freeze retirement alone closes two items below and invalidates
> the stated *reason* for several "deliberately kept" ones. The current open list is
> the dated section that follows this one; this table is not it.

### The count

| region | closed | refused with a number | deliberately kept | open |
|---|---|---|---|---|
| Phase 7 — writing the server (12) | 10 | 1 | 0 | 1 |
| Earlier phases (15) | 4 | 1 | 6 | 4 |
| Created by the `Option`/`Result` plan (16) | 13 | 1 | 2 | 0 |
| Created by this plan's nine phases (9) | 2 | 0 | 4 | 3 |
| C-level, no Tycho spelling (2) | — | — | 2 | — |
| Found by this re-score (2) | 0 | 0 | 0 | 2 |
| **total (56)** | **29** | **3** | **14** | **10** |

"Deliberately kept" is not a synonym for open. It is: a decision recorded with its
reason (`crlf()`, the `out` local, the payload-free enums), a consequence of the
`tychoc0` freeze that cannot be fixed while the freeze holds, or an incident written
down because the lesson outlives the fix. Fourteen items are in that state and **none
of them is work waiting for someone**. Ten are.

- **Of the 12 phase-7 items: 10 closed, 1 refused with a number, 1 open.** The
  `Option`/`Result` plan closed 2 (`read_head`, `stat`/`is_dir`); this plan closed 8
  (`\r`, multi-line strings, `argv[0]`, `--root DIR`, `exit(0)`, `reason_phrase`,
  `getpeername`, the `to_str`/`to_bytes` sandwich) and refused 1 with a measured
  number (the work queue, ~283 lines across 4 files). The **one open item is the
  first one in the list** — `send` is a builtin and `fn send(...)` is accepted
  silently. Reproduced at HEAD by this re-score, and the reproduction found the
  fix's own mechanism already in the tree: `fn die(s: string) -> int` is rejected at
  the *definition* (`error: 'die' is already defined`) while `fn send(a: int, b: int)
  -> int` compiles and dies at the *call* (`error: send(ch, v) takes a channel, got
  int`). So `die`/`exit` are in the table the duplicate check consults and the channel
  builtins are not; the fix is small and the decision it needs — which builtin names
  are shadowable — is the reason it is still open.
- **Two stale OPENs, the mirror image of the thing this re-score was written to
  catch.** `:232` ("there is no `unwrap_or`, `is_ok`, `is_some` or `is_err` anywhere")
  and `:324` ("a socket read timeout is indistinguishable from EOF") were both closed
  by the `Option`/`Result` plan and neither item line was ever struck through:
  `corelib/result/result.ty` has all seven combinators, and `net.read` returns
  `Err(Eof)` / `Err(Timeout)` as distinct values. Both closures are stated in the
  headline block at the top of this file and neither reached the item. **A file where
  the summary and the item list can disagree needs the audit run in both directions.**
- **The `Option`/`Result` plan created 16 items, not 6.** Measured, not recounted from
  its prose: `FRICTION.md` went from 38 bullets at `241c159` to 57 at `8aac642`, of
  which 3 are that plan's own score bullets — 5 items in its phase-1 section, 5 in
  phase 2, 3 in phase 3, 3 in phase 4. Its verdict said "created six new items of its
  own" and this file repeated the number. It was a 2.7× undercount, and it mattered:
  the 6 is what made "−2 original, +5 new" look like a near-wash instead of a plan
  that opened 16 questions to settle 4.
- **This plan created 9** (57 → 66 bullets), and settled 27 across ten phases —
  24 closed, 3 refused with a number. **Settled-to-created: 27 : 9, against the
  previous plan's 4 : 16.** That is the one ratio that moved, and it moved because the
  items were worked as a list of costed questions rather than as evidence for a thesis.
- **`server/main.ty`: 380 code lines before this plan, 341 after — 39 lines shorter,
  the first reduction in two plans.** The trajectory, verified commit by commit:
  380 (`8aac642`) → 378 (phase 1) → 378 (1b, 2) → 380 (phase 3) → 376 (4) → 372 (5) →
  **341** (6) → 341 (7, 8, 9). One phase did nearly all of it: `core:cli` learning
  `--root DIR` deleted a 59-line hand-rolled parser (`-31`).
- **What it cost.** `src/tychoc.c` **+359 raw lines, +176 non-comment** (11795 → 12154;
  9161 → 9337 code) — over half of it phase 3's nested patterns. Corelib packages
  **+57** Tycho code lines and **+17** C shim lines; corelib test programs **+111**;
  `docs/` +590/−84 with one new document (`docs/bootstrap.md`); `scripts/` +203/−8,
  which bought three gates that did not exist (`scripts/entrypoints.sh`, the
  source→doc half of `check_citations.py`, the un-rotted `bytes-rehome` lane). Four
  goldens moved and **all four are pure appends** (+53 / −0).
- **The shape of the two plans is opposite and the numbers say so plainly.** The
  `Option`/`Result` plan spent **+182 library lines** and the application got **9
  lines longer**. This plan spent **+176 compiler lines and +74 library lines** and the
  application got **39 lines shorter**. Ten of this plan's 24 closures were compiler
  changes, and a compiler change is paid once and refunded at every call site; a
  library conversion is paid once and charged at every call site. That is the
  generalisation this file can now support with two data points instead of one.

### What the previous plan's score got right

Its two analytical points survive the re-score intact, and both are corrected only for
which plan's phase 5 is meant.

The `stat` closure is evidence *against* the `Option`/`Result` verdict rather than for
it: it was closed by 4 lines of C and 10 of Tycho with no `Option`, no `Result` and no
`or_return` involved, and it is the only one of that plan's two closures that changed
what a client receives. The error-model work made failure *sayable*; the syscall made
an answer *right*. Those are different kinds of win and this file was conflating them.

**And its phase 5** (the `Option`/`Result` plan's — `8aac642`, not this plan's) closed
the directory-creation gap with `io.make_dir` / `io.remove` (35 library lines,
`Result(bool, IoErr)` both, non-recursive on purpose) and deleted the `os.system`
shell-out from `corelib/test/io`. It found **no new language friction at all**, and it
cost the application **+9 lines**, all in one startup check, to replace a single wrong
message ("`--root` is empty or not a directory", said for an empty directory, a plain
file, a missing path and an unreadable one alike) with four accurate ones. `Result`
made the distinctions *available*; **spending them costs one branch per cause**, and
nothing about the error model makes accuracy free at the call site.

### The real remaining debt, re-scored 2026-07-30 against `afa67da` — 10 open items

Every item below was **re-verified against the tree at `afa67da`** before it was
scored: the compiler was run by hand on the shape the item describes, or the cited
source was opened, or the gate/shim was invoked directly. **All ten reproduce.**
Nothing on the previous list turned out to be already closed — but four of them are
*bigger* or *cheaper* than the previous list said, and every citation on the list has
been re-derived, because the old ones had drifted.

Two items that were **not** on the numbered list did close, both by the same event —
the `tychoc0` freeze lanes were retired 2026-07-29 — and both are struck through in
place below (the six non-gated `tychoc0` runners, and "new language syntax can no
longer be given a `tests/` fixture").

The old list addressed its items by bare `:N` line numbers into this file. Those are
gone: a bare `:N` binds to the previously named path (`CLAUDE.md`, "Citations"), so
they were never self-references at all, and they went stale on every edit. Items are
named by their section instead.

Ordered so the top of the list is what to pick up first.

1. **Two shims do not compile under `-std=c11`** (*Found by phase 1's gate sweep*) —
   **the cheapest thing on this list, and it is now two files, not one.**
   `cc -std=c11 -c corelib/net/net_shim.c` gives **4 errors** (`corelib/net/net_shim.c:84`
   storage size of `hints`, `corelib/net/net_shim.c:88` implicit `getaddrinfo`,
   `corelib/net/net_shim.c:89` invalid use of undefined `struct addrinfo`,
   `corelib/net/net_shim.c:90` implicit `freeaddrinfo`), and **`corelib/tls/tls_shim.c`
   has the same defect with 9 errors** from `corelib/tls/tls_shim.c:38` — which the
   original item did not know, because it found the first one through
   `scripts/frontparity.sh` and that gate no longer runs. Re-measured here by invoking
   `cc` directly on all 11 shims: those two fail, seven pass, and `corelib/image/image_shim.c`'s
   single "error" is only a missing `png.h` (environmental, item 6). **The fix is already
   in the tree four times** — an `#ifndef`/`#define` pair with a one-line reason at
   `corelib/io/io_shim.c:10-11`, `corelib/os/os_shim.c:9-10`,
   `corelib/datetime/datetime_shim.c:10-11` and `corelib/time/time_shim.c:22-23` — so this
   is **3 lines copied into each of 2 files**, with a known-good pattern to copy. Left on
   scope three times now.
2. **`spawn f(x)` as a bare statement** (*Earlier phases*) — reproduced verbatim at HEAD:
   `spawn work(1)` gives `error: a statement must be a declaration, assignment, or call --
   a bare expression has no effect` (`src/tychoc.c:3575`), which still never states the
   real rule — a task handle must be *bound* so the compiler can hang the implicit join on
   it. **One line of diagnostic text at a known line.** Open only because nobody has spent
   it.
3. **`docs/bootstrap.md` is not reachable from `docs/README.md`** (*Found by phase 1's gate
   sweep*) — verified: `grep bootstrap docs/README.md` is empty. Sharper than the old
   entry: the index deliberately points at *directories* (`docs/reference/`, `docs/guides/`,
   `docs/spec/`, `docs/internals/`, `docs/rfc/`), so almost every unlisted file is covered
   by its directory — and `docs/bootstrap.md` is **the only top-level `docs/*.md` no index
   entry reaches**, the other five (`docs/architecture.md`, `docs/from-c-to-arenas.md`,
   `docs/thesis.md`, `docs/tutorial.md`, `docs/README.md` itself) all being named. **One
   link**, plus the real question behind it: `scripts/check_links.sh` checks that links
   *resolve*, not that documents are *reachable*, so an orphan is invisible to every gate.
   Three files under `docs/internals/` are additionally mentioned by no Markdown at all.
4. **The `send` collision** (*Phase 7*) — reproduced at HEAD, and this re-score found the
   fix's exact shape. `fn send(a: int, b: int) -> int` still compiles silently and dies at
   the *call* with `error: send(ch, v) takes a channel, got int`, while `fn die(s: string)
   -> int` is rejected at the *definition*. **The reason is now pinned:** the definition-time
   duplicate check is `if (sig_find(pr->name) || consts_find(pr->name)) die_dup_proc(...)`
   (`src/tychoc.c:7842`), and `sig_find` searches `g_sigs` — which holds `die` and `exit`
   as real entries (`src/tychoc.c:4521-4522`, in `register_builtins`,
   `src/tychoc.c:4508`) but **holds no entry for `send`, `recv` or `close` at all**; those
   three are recognised ad hoc during resolution (`src/tychoc.c:5609`,
   `src/tychoc.c:5618`, `src/tychoc.c:5624`). So it is not a table that omits three rows,
   it is three builtins that were never in the table. **The code is ~1 line** at
   `src/tychoc.c:7842`; the open part is the decision — which builtin names are
   shadowable — because landing it newly rejects any program defining `send`/`recv`/`close`.
5. **Stale in-tree comments asserting constraints that the freeze retirement killed**
   (*phase 10, widened here*) — the old entry named two files; there are **six sites**, and
   they now carry two *different* false claims:
   - "the language has no nested patterns" — `corelib/net/net.ty:20`,
     `examples/corelib/httpd/main.ty:55`, and (new here) `corelib/result/result.ty:29-31`,
     which goes further and tells the reader "nothing in `corelib/` may use one".
   - "this package is compiled by the FROZEN `tychoc0`, so it must not use X" —
     `corelib/httpd/httpd.ty:100-109` (why `crlf()` must stay),
     `corelib/httpd/httpd.ty:281-289` (why `out` must stay), `tools/lsp.ty:259`
     (why `"" + '\r' + '\n'` must stay). **The freeze lanes were retired 2026-07-29**, so
     every one of these states a live constraint that no longer exists — each run.sh header
     in the tree has already been corrected to the past tense, and these six were missed.
   `corelib/test/io/main.ty:44` and `corelib/test/result/main.ty:128` show the corrected
   form. **~15 lines of comment across 6 files**, and worth doing before someone reads one
   of them as a reason not to write the obvious thing.
6. **`ends_with` needs `core:strings`** (*Earlier phases*) — still true: `ends_with` lives
   at `corelib/strings/strings.ty:37` and `core:httpd` still hand-rolls its own
   `has_ext` (`corelib/httpd/httpd.ty:387`) rather than import the package for one
   predicate. **Not lines — a corelib layering decision** about whether a leaf package may
   depend on `core:strings`. Note the precedent that has since landed: `core:io` *dropped*
   a dependency (`core:path`) when a syscall made it unnecessary, so the tree's current
   direction is fewer inter-package edges, not more.
7. **`parallel for` caps concurrency at `min(N, ncpu)` and nothing warns** (*Earlier
   phases*) — **reproduced live at HEAD**, not merely re-read. Four iterations of an equal
   fixed workload: `TYCHO_THREADS=4` → **222 ms**, `TYCHO_THREADS=2` → **433 ms**,
   `TYCHO_THREADS=1` → **853 ms**. All N iterations *do* run (the reduction total is
   identical at every width); what is capped is how many run *at once*, so an iteration
   chunked behind one that never returns never starts. Three things the old entry did not
   know:
   - The behaviour is now **specified**, which it was not: "the iteration space is split
     into chunks; the reference implementation uses `ncpu()` chunks and MAY expose an
     override (`TYCHO_THREADS`)" (`docs/spec/13-concurrency.md:78-82`).
   - The width is now **readable from Tycho**: `ncpu()` is a registered builtin
     (`src/tychoc.c:4519`, lowering at `src/tychoc.c:9129`), so a program can at least
     ask. Measured on this box: `ncpu()` → 16.
   - There is an **undocumented hard ceiling of 64 chunks** — `if (_pk < 1) _pk = 1; if
     (_pk > 64) _pk = 64;` (`src/tychoc.c:10040`, inside `gen_parfor`,
     `src/tychoc.c:10026`) — which `docs/spec/13-concurrency.md` does not mention, so on a
     box with more than 64 CPUs the spec's "uses `ncpu()` chunks" is false. **That half is
     a ~1-line spec fix and should be split out and taken;** the warning half remains
     uncosted, because `N` is a runtime expression (`docs/spec/13-concurrency.md:86`) and
     there is nothing static to warn about. Runtime detail at
     `runtime/tycho_rt.c:843-852` (`tycho_ncpu`).
   - **Confirmed and bounded by measurement, 2026-07-31** (`docs/internals/plan-prunner-DONE.md` phase 1, a real
     `parallel for` program rather than the server that never used one). The 64 is exact
     and it was forced, at K=200 jobs of 50 ms each: `TYCHO_THREADS=32` → `maxconc=32`,
     `=64` → `maxconc=64`, `=100` → `maxconc=64` with `ncpu()` reporting 100. **The cap
     limits width; it does not starve** — 200 items came back 200, unique, at every
     setting, which is the half the entry left open ("an iteration chunked behind one that
     never returns never starts" is true only for a body that can fail to return, and a
     terminating body is never at risk). At the real workload — 560 jobs, `ncpu()` = 16 —
     the cap is not in the picture at all. So the live half of this item is now just the
     undocumented ceiling and `ncpu()`'s own false definition above it
     (`docs/spec/16-builtins.md:251`), split out as `plan.md`'s carried-forward phase 7.
8. **No direct spelling for N workers** (*Earlier phases*) — reproduced: `hs := [spawn
   work(1), spawn work(2)]` is refused with `tychoc: a task handle cannot be stored in a
   container or aggregate -- wait(t) first` (`src/tychoc.c:639`, `task_container_err`,
   fail-closed at the type-intern choke points so a task cannot escape and be waited twice
   or never). `server/main.ty:499-501` still pays the recursive fan-out — worker k spawns
   worker k+1 into a frame-local, then runs its own accept loop. **An array of handles is a
   type-system change, not an item-sized fix. Uncosted, and still the honest core of what
   is left**, together with item 7.
   **NARROWED, 2026-07-31, and the item reads stronger than it is** (`docs/internals/plan-prunner-DONE.md` phases 1–3).
   The first program in this tree to actually run a worker pool started **16 workers in one
   line and stored no handle**: `parallel for` is a direct spelling for N workers, and
   `parallel for x in ch:` — specified at `docs/spec/13-concurrency.md:91-92`, worked at
   `docs/guides/concurrency.md:86-104`, fixtured at `tests/conc/parfor_chan.ty:16` — is a
   direct spelling for a *bounded pool over a queue*, the exact shape this item says has
   none. So the premise "it is either N hand-written `spawn` lines or a recursive fan-out"
   is **false for N = ncpu**, which is the N most programs want, and an array of task
   handles is not what a worker pool needs. **What survives is one sentence and it is
   sharper than the original:** the program cannot choose N. `ParallelFor`
   (`docs/spec/02-grammar.md:248-249`) has no width slot in the grammar, so the only knob
   is `TYCHO_THREADS`, read once per process at `runtime/tycho_rt.c:848` — a fixture runner
   that wants `-j 4` on a laptop and `-j 32` in CI cannot say so from inside the language,
   and must be *launched* differently instead. `server/main.ty:499-501`'s recursion is
   still evidence, but for a **different** want: N long-lived workers each carrying its own
   `wid`, which `parallel for` genuinely cannot express because a chunk's identity is not
   observable. Two items, not one, and only the second needs the type system.
9. **`corelib/test/image` is skipped without libpng** (*Phase 7 of `plan.md`,
   non-blocking*) — confirmed environmental and confirmed *live*: `corelib/image/deps`
   names `libpng`, `pkg-config --exists libpng` fails on this machine, and
   `corelib/run.sh:39` prints `skip <name> (missing dependency: ...)` and continues. Its
   golden therefore asserts nothing here. **Not closable in-tree** — it is a property of
   the machine, and the skip is the deliberate design that keeps `make ci` green on
   platforms without the lib. Listed so nobody re-derives it a third time.
10. **This file's own coordinates drift silently, and no gate can see it** (*phase 10*) —
    still open, and **re-measured here rather than restated.** Fifteen `path:line`
    citations were opened at HEAD and checked against what this file says is there:
    **11 of the 15 no longer point at their subject.** All eleven are into
    `src/tychoc.c` — the `\r` escape set, the literal-intern emit site, the
    adjacent-literal join, `is_place`, the `exit` builtin registration, `copy_into`'s
    `T_BYTES` case, `instantiate_generic`, the zero-cost reinterpret, `detect_package`,
    the `bytes` representation and the channel-handle type syntax — because every compiler
    phase shifts everything below it, and `src/tychoc.c` is now 754 KB. The four that
    survived are all into files that barely moved (`corelib/httpd/httpd.ty:336`,
    `corelib/httpd/httpd.ty:352`, `server/main.ty:564`, `runtime/tycho_rt.c:557`) — which
    is the shape of the problem: **the citations that matter most are the ones most likely
    to be wrong.** `scripts/check_citations.py` cannot catch it by construction; it
    verifies anchored `path:line@token` refs against the token and only bounds-checks bare
    ones, and every bare ref into a 12k-line file stays in bounds forever. This file has
    ~85 `path:line` citations and **4 anchor markers**. The fix is a mechanical pass to
    anchored form, after which the gate polices them. **The same defect in a second
    dimension was found by this re-score and repaired rather than listed:** 51 "`plan.md`
    phase N" references pointed at a `plan.md` that is now an unrelated plan — fixed with
    one definitional note at the top of this file, because rewriting 51 references would
    have gone stale again at the next archive. That asymmetry is the lesson: **a coordinate
    that names a moving target should be replaced by one that names a stable one, not
    repointed.**

**What moved and what did not.** Items 1, 2, 3 and 5 are lines and links with the work
already identified — roughly a day between them, and item 1 has a known-good pattern to
copy four times over. Items 7 and 8 are the concurrency pair and are still the honest
core: one wants a type-system answer, the other wants a warning there is nothing static
to warn from. **That sentence did not survive being tested — see the 2026-07-31 section
below.** Item 8's type-system answer is not what a worker pool wants (it stores no
handles), and item 7's danger is bounded by measurement; what is left of the pair is one
missing width parameter. Item 6 wants a decision, not lines. Items 9 and 10 are properties of the
environment and of the file itself. **The list did not shrink, and that is the finding —
sixty commits of real language work went past this list without touching it**, because
every one of them was driven by `new_ideas.md` and by the loop and array plans instead.
A list nothing is pulling from does not get shorter on its own.

## Re-scored against a real concurrent program, 2026-07-31 (head `9e7a090`)

Everything above about concurrency was written from `server/`, and **`grep -n parallel
server/main.ty` is empty** — that program used `spawn`/`wait` and never wrote a `parallel
for` at all. Items 7 and 8, which the score above calls "the honest core of what is left",
were therefore inference from a program that did not exercise the construct they name.

`tools/prunner/main.ty` does. 479 lines; it runs the whole 560-fixture corpus over a
bounded channel with a `parallel for` pool and a spawned fan-in collector, and its report
is byte-identical to `sh tests/run.sh`'s at **7.62x** (471,695 ms → 61,867 ms, `maxconc`
measured at 16 = `ncpu()`). Evidence under `docs/internals/plan-prunner-DONE.md` phases 1 and 2. Items 7 and 8 are
re-scored in place above. What is new is below.

- **The finding that outranks every complaint here: it compiled first try, twice out of
  two.** Phase 1's ~240-line pool (two channels, a spawned producer, a spawned collector,
  a `parallel for` with `select`/`recv`, a subprocess call inside the body) and phase 2's
  479-line rewrite (seven lane judges, `os.run`, structs-with-strings through a channel,
  `sort.asc`) each built with **no errors and no warnings on the first `./tychoc`**. This
  file's picture of concurrent Tycho as a fight is not what either phase found, and the
  expectation of a fight was set *by this file*. "Concurrency is the best thing in the
  language" (What was good, below) now has a second, independent data point from a
  workload that is not a web server.
- **A restriction improved the design, which is the one outcome this file has no category
  for.** `docs/spec/13-concurrency.md:100` permits a `parallel for` body exactly one
  outer-scope write — a `+`/`*` reduction on `int`/`float` — and
  `docs/spec/13-concurrency.md:110-111` makes any other write a compile error
  (`tests/conc/reject/parfor_push.ty`: "parallel for cannot mutate captured variable 'xs'
  in place"). A fixture runner's results are per-item, not a scalar sum, so they **must**
  leave through a second channel carrying an explicit index. That was not a choice — and
  it is exactly what makes a lost job detectable: an index that never arrives prints
  `FAIL <name> (NO RESULT — the runner lost this job)` and an index filled twice is
  rewritten to `DUPLICATE RESULT`. The plan's stated worst case, a parallel runner
  reporting green over lost work, **cannot happen quietly**, and the restriction is why.
- **The affine-channel rule cost nothing, at two scales.** `docs/spec/13-concurrency.md:127`
  ("cannot be returned, stored in a container, or captured") forces the pool to be a scope
  rather than a reusable object, and `run_pool(js, tmp) -> [Res]` still reads as an
  ordinary function: channels interior, array of results out. It read the same way at 243
  toy jobs and at 560 real ones. The friction would start if two call sites wanted to
  share one pool, and nothing in this tree does.
- **A struct carrying heap strings crosses a channel exactly like one carrying ints, and
  this is the single thing that made the work tractable.** `Res{idx, ok, name, reason,
  t0, t1}` — two strings per message, 560 messages — needs no encoding, no parsing back
  and produces no aliasing surprise. Probed before it was relied on, half expecting
  `int`-only. A channel restricted to scalars would have forced a side table keyed by
  index and every attribution bug that implies.
- **NEW — §22 of the spec does not describe the construct every per-item worker pool
  depends on.** `send` on a captured channel from inside a `parallel for` body is what
  routes results out, and within §22 (`docs/spec/13-concurrency.md:76-121`) the only
  mention of a channel is that it may be the foreach *source*
  (`docs/spec/13-concurrency.md:91-92`). Worse, the section states that "each chunk's
  captured values are deep-copied into it" (`docs/spec/13-concurrency.md:81-82`), which
  read literally would give every chunk its own private queue — the opposite of what the
  compiler does and of what 560 jobs crossing one channel proves. The rule is written
  down, but only in the **non-normative** guide: "the chunk tasks share the captured
  channels (a channel is a scalar handle, passed by value — not deep-copied per chunk)"
  (`docs/guides/concurrency.md:154-155`). **A conformance gap, not a language defect** —
  the implementation is right and the normative document is silent where a second
  implementation would have to guess. ~3 sentences in §22 plus a carve-out beside the
  write rule. Filed as `plan.md`'s carried-forward phase 10.
- **NEW — `iter.map` cannot change the element type, and the lambda syntax hid it.**
  `corelib/iter/iter.ty:8` is `fn map(xs: [$T], f: fn($T) -> $T) -> [$T]`: **one** type
  variable, so `[Res] -> [int]` is not expressible through it. Measured — a
  correctly-spelled lambda gets `error: argument 2 of 'iter__map' is fn(Res) -> int, which
  does not fit the parameter pattern`. What made this cost time is that the *first*
  attempt reached for `map(r => r.idx, rs)`, which is not the lambda spelling
  (`docs/spec/09-expressions.md:187` is `fn(params) -> R: expr`) and gives
  `error: expected ')'` — so the parse error was read as "lambdas are the problem" and the
  real signature was never reached. The extraction became a scatter loop, which is better
  code here anyway, so this cost nothing in the end. **Known cost:** a second type
  parameter on `map`. Multi-parameter generics work (`corelib/result/result.ty:117` takes
  three), but that is a *value* parameter — whether the inference reaches a
  **function-typed** `fn($T) -> $U` parameter is **not verified**, and is the thing to
  check before costing this. `filter` is unaffected (`fn($T) -> int` already).
- **NEW, small — `cli.has` answers a narrower question than its name, and there is no
  diagnostic possible.** A bare `--stats` lands in `Cli.flags`, not `Cli.keys`, so
  `cli.has(c, "stats")` returns **false** and `cli.flag(c, "stats")` returns true;
  measured, both spellings compile, both return `bool`, and the failure is a missing line
  of output with nothing printed. **It is not a defect** — the doc comment at
  `corelib/cli/cli.ty:159` says outright "Was option `key` (a `--key=value`) supplied at
  all?", and `has` (`corelib/cli/cli.ty:160`) / `flag` (`corelib/cli/cli.ty:167`) scan
  different vectors on purpose. The trap is the **name**: `has` reads as the general
  question and answers only the valued half. A decision (rename to `has_value`, or a
  `supplied(c, name)` that scans both), not lines — and the only thing here that failed
  *silently*, which is why it is written down despite being small.
- **NOT an item, recorded so it is not filed as one.** A multi-line `if A or B or\n C:`
  does not continue and gives `error: expected an expression` — but wrapping the condition
  in parentheses works, verified, and implicit joining inside `(`/`[` has existed since
  `tests/multiline_literals.ty` (see the phase-7 multi-line-string entry below). The
  language has the feature; the author did not reach for it.
- **NEW, one line — the guide's bounded-fan-out section points its reader at the
  desugaring.** `docs/guides/concurrency.md:86-104` introduces `parallel for x in ch:`,
  explains that it "desugars to a `parallel for` over `0..<ncpu()`" and then closes with
  "Worked example: `tests/conc/workers.ty`" (`docs/guides/concurrency.md:104`) — and
  `tests/conc/workers.ty:2-3` says of itself that it is "the pattern `parallel for x in
  ch:` sugars over; written here with today's primitives". The fixture that demonstrates
  the sugar is `tests/conc/parfor_chan.ty:16` and the guide does not name it. **This is
  what actually happened rather than a hypothetical:** this plan's recon read
  `tests/conc/workers.ty`, copied the manual `0..<N` + `select` idiom into
  `tools/prunner/main.ty:355-360`, and never learned the sugar existed until phase 3 went
  looking. The sugar accepts a `send` body — verified with a scratch program, 200 jobs
  sent, 200 received — so it would have removed the `0..<n` sizing, the `select`
  scaffolding and the `sent != n` invariant. **Nothing in the language was in the way;
  one wrong pointer in a guide was.** Fix: name both fixtures at
  `docs/guides/concurrency.md:104`.
- **The work-queue refusal below is confirmed a second time, and at the same time shown to
  be workload-shaped.** That entry (phase 7, refused with a number) established that an
  MPMC work queue is writable today with no compiler change; this program is an
  independent instance at 560 jobs and 16 workers. But its *conclusion* — that the queue
  buys nothing, because the cap is a worker blocking in `recv(2)` — is a property of the
  **server's** workload, not of the queue. Here the jobs are finite, CPU-and-subprocess
  bound, and every one terminates, so the same construct is the entire 7.62x. **Same
  shape, opposite verdict, and the difference is whether a work item is guaranteed to
  finish.** Neither result generalises without that qualifier.

**What this program did NOT touch, so that nobody over-reads the evidence.** It is *one*
shape: a bounded pool over a channel with a fan-in, over jobs that all terminate.

- **`select` under contention is untested.** Both `select`s in `tools/prunner/main.ty`
  have exactly **one** `recv` arm and no `default:` and no `closed:`. Nothing here
  exercises arm ordering, and the spec's "select is not fair, so an earlier-listed ready
  channel is always preferred" (`docs/spec/13-concurrency.md:165-167`) was never in play.
- **No long-lived workers.** Every worker is a `parallel for` chunk that ends with the
  loop; none outlives the pool's scope. The server's shape — a worker owning a connection
  for its whole life — is exactly what this says nothing about, and it is the shape item 8
  still has a live complaint about.
- **No failure mid-pool.** Every fixture failure here is a *judged verdict*, a value
  carried in a message. No job aborted, no `die()` ran inside a chunk, nothing sent on a
  closed channel. What happens to the other fifteen chunks when one aborts is **unknown
  from this evidence**, and it is the first thing to test before this pattern is trusted
  where a work item can crash.
- **Backpressure was never observed biting.** Capacity 16 with a collector draining
  continuously; the results channel filling and parking a worker did not happen, or at
  least was not measured.
- **Nothing above 64 workers as a real workload.** The 64-chunk probe used synthetic 50 ms
  sleeps; the real corpus ran at `ncpu()` = 16, nowhere near `src/tychoc.c:10040`.
- **No nested parallelism** — no `parallel for` inside a spawned task, and no pool inside
  a pool.

## Phase 7 — writing the server

- **Phase 7** — `send` is a builtin, and defining `fn send(conn, r, head_only, keep) -> int` is accepted **silently**; the collision surfaces only at the call site as `error: send(ch, v) takes a channel and a value`, which points at my call and describes a channel operation I never wrote. Nothing is reported at the definition, which is where the mistake is.
- ~~**Phase 7** — `httpd.read_request` collapses EOF, idle timeout and a malformed request line into `method == ""`, so a server that must answer `400` to garbage but hang up silently on a disconnect cannot use it at all; `server/main.ty` reimplements the read loop (`read_head`) purely to keep the raw buffer and recover that one bit.~~ **CLOSED, `docs/internals/plan-option-result-DONE.md` phase 3.** `read_request` returns `Result(Request, ReqErr)` with five named causes, and `read_request_capped(fd, cap)` returns the raw buffer as the second element of a tuple, so the cap decision (`431`) and the log line for an unparseable request no longer need a private read loop. `read_head`, `struct Head` and `term()` are deleted.
- ~~**Phase 7** — `httpd.reason_phrase` is a closed `if`-chain and `httpd.response()` takes no reason, so status `431` goes on the wire as `HTTP/1.1 431 Status`. The workaround is to bypass the constructor and build `httpd.Response(431, "Request Header Fields Too Large", []string, []string, body)` positionally — it works across a package boundary, which is good, but it couples the caller to the struct's field order to set a string. **Bit a second time in `docs/internals/plan-friction-DONE.md` phase 3:** answering `408` needed the identical bypass, so the workaround got factored into a local `phrased_response(status, reason)` — a private reimplementation of the constructor the corelib should have had.~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 5, and it was TWO fixes, not one** — which is the finding worth carrying: the item names a missing *table row* and a missing *parameter*, and either alone leaves the other half open. Both landed: `408`/`431` joined `reason_phrase` (`corelib/httpd/httpd.ty:336-339`) so the existing constructor renders them, **and** `response_reason(status, reason, body)` (`:352-353`) is the constructor that takes a phrase, with `response()` rewritten as one line on top of it (`:357-358`) — so exactly one site in the tree builds a `Response` positionally and it is inside the package that owns the struct. **+6 corelib code lines** against the file's own ~5-line estimate. `phrased_response()` is deleted, and so are `oversize_response()`/`timeout_response()`, which existed only to hold the two literal phrases: both call sites are now `error_response(431)` / `error_response(408)`, the same constructor the other four statuses already used, and `server/main.ty` went **376 → 372 code lines** — three functions to zero. The wire proof is at the corelib level, one probe compiled against a `git archive HEAD` tree and against the fix: `HTTP/1.1 431 Status` → `HTTP/1.1 431 Request Header Fields Too Large`, `HTTP/1.1 408 Status` → `HTTP/1.1 408 Request Timeout`. What the item could not know: the *live* wire never changes, because the bypass had been producing the correct bytes all along (the `431` body is 680 bytes before and after) — so the thing that was actually broken was `httpd.response()`, i.e. **the spelling every other consumer of `core:httpd` would have reached for**, and the server was the one caller paying to route around it.
- ~~**Phase 7** — no `\r` escape in string literals (`\n \t \\ \"` only), so the most common byte pair in HTTP is a function call, `httpd.crlf()`. And `const TERM = httpd.crlf() + httpd.crlf()` is rejected — `error: const value must be a literal` — so the header terminator has to be a function that reallocates two strings on every loop iteration.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 2.** `\r` was **one character** in the lexer's escape set (`src/tychoc.c:382`) and `const` string folding was **five lines** in `const_fold` (`:4317-4321`), so `const TERM = "\r\n" + "\r\n"` is now a single four-byte literal. What the entry did not know is the reason the escape set was so small: a string literal's text is pasted **verbatim** into the generated C literal and interned by `strlen` (`src/tychoc.c:9455`, `runtime/tycho_rt.c:1005`). That is why `\r` is free (C spells it the same) and why `\0` and `\xNN` are **deliberately refused** — `\0` would truncate the interned length and C's `\x` is greedy over hex digits (`"\x41" "1"` would mean `\x411`), so both need a byte-exact literal on the emit path. **The cost, measured by reading the three places that would change:** the lexer's pass-through would become a decode-to-bytes (`src/tychoc.c:319-400`), the single emit site that pastes the text verbatim would need a `\xNN`-emitting re-escaper beside the 10-line one already there (`:9455`, escaper at `:12586-12597`), and `tycho_str_intern` would need a length-carrying twin because its contract is literally "a C string, `strlen`-bounded" (`runtime/tycho_rt.c:1000-1012`) — 3 functions changed plus 1 new runtime entry point, on the order of 35 lines. Not adjacent to a one-character fix, so: refused, with the number. **`httpd.crlf()` is KEPT DELIBERATELY and this is the finding worth carrying:** `core:httpd` is compiled by the FROZEN `tychoc0` through `examples/webserver/run.sh`, which asserts `tychoc == tychoc0 == golden`, and `compiler/tychoc0.ty:195` rejects `\r`. So the literal cannot be written in any file the frozen compiler reads — the corelib, `tools/*.ty`, `tests/*.ty`, `examples/*.ty` — and the friction is closed in `server/`, `corelib/test/{httpd,csv,strings}` and `examples/corelib/httpd` instead. The allocation half of the complaint was fixed *without* the literal: `read_request_capped` hoists `term := crlf() + crlf()` out of its read loop (`corelib/httpd/httpd.ty:239`), which is what "reallocates two strings on every loop iteration" was actually about.
- ~~**Phase 7** — no multi-line string literal and no line continuation, so the 10-line HTML error page is 10 consecutive `s += "..."` statements. (`+=` does exist; I wrote `s = s + ...` for half the file before checking, because nothing in the corelib I had read used it.)~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 2 — and half of it was already there.** Tycho has had implicit line-joining inside `(`…`)` / `[`…`]` since `tests/multiline_literals.ty` was written; what was missing was only the *literal* half. Adjacent string literals now join (`"a" "b"` is `"ab"`, C/Python rule, **two lines** in `parse_primary`, `src/tychoc.c:2184-2185`), so the two together ARE the multi-line string form and no new delimiter was invented. `server/main.ty`'s `error_body` is now one parenthesized expression instead of twelve `s +=` statements and `usage` likewise (378 code lines before, 378 after — the win is 23 statements becoming 2 expressions and 7 fewer `tycho_str_concat` sites in the emitted C, not lines). An f-string never joins, because it is already sugar for a `+` chain. **Cost recorded for the reader:** a `tests/` fixture for either form is impossible while `compiler/fixpoint.sh` and `scripts/frontparity.sh` feed `tests/*.ty` to the frozen `tychoc0`; the coverage is the golden-validated `corelib/test/` and `server/` programs instead, written down in `docs/spec/appendix-e-conformance.md`.
- ~~**Phase 7** — there is no `stat` and no `is_dir`. `io.exists` answers by listing the parent directory, and the only directory test available is `len(io.list(p)) > 0`, which reports an **empty directory as a file** — `server/main.ty`'s `resolve()` ships a documented wrong answer (a 0-byte `200`) because the question cannot be asked.~~ **CLOSED, `docs/internals/plan-option-result-DONE.md` phase 4** — and it was never an error-model item, which is the finding worth keeping. Phase 2 turned the 0-byte `200` into a `404` (`io.read_bytes` → `Err(io.IsDir)`); phase 3 measured the residue and left it; phase 4 wrote the syscall: `io.is_dir(p) -> Result(bool, IoErr)` over a 4-line `iox_stat_kind` in `io_shim.c`, and `resolve()` now asks the kernel. `GET /emptydir` answers `301 -> /emptydir/` instead of `404` (measured live against a `296bbc2` binary), a non-directory never redirects, and `server/main.ty` did not gain a line — 371 before, 371 after. The `Result` is house style; the fix is `stat`. **The comparison this file should remember: 14 library lines added a question and moved a status code, where ~100 added return types and moved none.**
- ~~**Phase 7** — `args()` includes `argv[0]` but `cli.parse` requires it removed, so every program opens with the same four-line copy loop; `examples/weblog/main.ty:129` has the identical loop with a comment explaining the same thing.~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 6, and it turned out to be three code lines**: `cli.argv()` is `a := args()` then `return a[1:len(a)]` (`corelib/cli/cli.ty`), so both copy loops are deleted and a program's front door is `c := cli.parse(cli.argv())`. Array slicing and calling the `args()` builtin from *inside a package* both already worked — measured with a scratch package before anything was written — so the entire cost of this item was **deciding where the convention lives**. The other option the phase considered was making `parse` skip element 0 itself, and it is worse for a reason this file could not have known: **two consumers in this tree hand `parse` a synthetic argv with no program name in it** (`corelib/test/cli/main.ty:14`, `examples/corelib/cli/main.ty:14`), so a `parse` that dropped element 0 would have silently eaten `--out=build/app` — a wrong answer, not an error, and `examples/corelib/cli.out` would have had to be re-recorded to bless it. So `parse` stays a pure function over a vector and the argv[0] convention lives in the function whose *name* is about argv. The new golden asserts the mismatch the wrapper absorbs, in the same breath: **`len(args()) = 1`, `len(cli.argv()) = 0`.** The lesson worth carrying: an item that reads as "the library is missing a function" was really "the library is missing a *name*" — nothing in the language was in the way, and four lines were copied into every program for want of somewhere obvious to put them.
- ~~**Phase 7** — `core:cli` cannot express `--root DIR`: values must be `=`-attached by design, so any CLI wanting the conventional Unix spelling hand-rolls its parser. That is 45 of this server's lines.~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 6 — and "by design" was right, which is why the fix is an ADDITION and not a reversal.** The header at `corelib/cli/cli.ty:9-11` states the real decision, and it is not aesthetic: *"Values are ALWAYS attached with `=`, so the parser needs no schema of which options take a value."* Schema-**freedom** is the property, and it is exactly what `--root DIR` cannot have — `DIR` is `--root`'s value or a positional, and nothing in the token can say which. So the two are not in conflict and there was nothing to undo: `parse(av)` is unchanged and still schema-free, and `parse_spec(av, valued, boolean)` is the same loop with `strict = true`, asking for the schema only from callers that want the second spelling. One shared loop, so the two entry points cannot drift on the spellings they share. **`parse`'s unchangedness is proven, not asserted**: `corelib/test/cli.out` is **+26 / −0**, a pure append, and `examples/corelib/cli.out` — which only calls `parse` — is untouched. **The count in this entry was an UNDER-count**, which is the number worth correcting: the hand-rolled parser plus its copy loop was **59** code lines (`opt_name`/`opt_inline`/`wants_value` 12, the stepping loop 43, the copy loop 4), not 45, and what replaces it is **32** — of which *2* are the schema itself (`valued := ["root", "host", "port", "workers", "idle-ms"]`, `boolean := ["quiet", "q", "help", "h"]`) and the rest is the part that was always this program's: which names exist, their defaults, and what is out of range. `server/main.ty` went **372 → 341** code lines (−31) and `examples/weblog/main.ty` **170 → 163** (−7) against **+45** in `corelib/cli/cli.ty` — the inverse of the shape the score section above complains about, where the library grew and the server grew too. Nothing left in `server/main.ty` splits a token or counts an index. All **47** spellings the server accepted were driven against a HEAD-built binary and 44 are byte-identical; the 3 that moved are all an **error becoming a success** on a spelling that never worked — `-qh` (the hand-rolled parser had no short-cluster rule at all, `core:cli` has had `-abc` since it was written), bare `--` (rejected only because `opt_name("--")` returned `"--"` and fell through to the unknown arm — an accident, not a decision), and `--bogus --help` (HEAD's loop was argv-*ordered*, so whichever came first won; a parsed `Cli` carries no order, so "`--help` always answers" was chosen and written down at the check). The decisions the item did not specify are documented in `corelib/cli/cli.ty`'s `parse_spec` comment as a rule table, in `docs/guides/corelib.md:353`, and as executable assertions in the golden — including the one that preserves this file's own server: **`--quiet=1` sets the flag and drops the value**, because that is what the hand-rolled parser did.
- ~~**Phase 7** — `die()` is the language's only exit and it always exits **1**, so `--help` cannot be answered with status 0 through it. The fix was to thread a `help: bool` field through the config struct so `main` could return normally — a data-flow change to work around a missing `exit(0)`.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 4.** `exit(code)` is a new `Sig` builtin — **1 line** in `register_builtins` (`src/tychoc.c:4302`) plus **3** in codegen (`:8897-8899`), which emit C's `exit(3)` directly rather than a `tycho_exit` wrapper: there is nothing to wrap, `exit()` flushes stdio itself, and not touching `runtime/tycho_rt.c` means not changing the runtime text embedded in every emitted `.c`. The spelling was chosen over a two-argument `die(msg, code)` because the builtin `Sig` table is fixed-arity (`.nparams`), so an overload would need special-casing in the resolver, and because the two calls want *different* streams — `die` writes stderr, an answered `--help` writes stdout. `server/main.ty`'s `--help` arm is now `print(usage())` + `exit(0)`: the `help: bool` field is gone from `struct Config`, the `if cfg.help: … return` block is gone from `main`, and `Config(...)` went from 7 positional fields to 6. **`./tycho-httpd --help; echo $?` → `0`** and a bad flag still exits `1` with its message, both **byte-identical to a binary built from `git show HEAD:server/main.ty`**. What the item could not know: the same freeze that blocked `\r` and nested patterns applies again, and this time it is a *new builtin* rather than new syntax — `exit` cannot appear in anything the frozen `compiler/tychoc0.ty` compiles (the corelib, `tools/*.ty`, `tests/*.ty`, `examples/*.ty`), because `scripts/frontparity.sh` reports exactly that as a divergence. `server/` is fed to `tychoc0` by no runner, which is why the consumer that complained is reachable anyway. Recorded in `docs/spec/appendix-e-conformance.md`; `frontparity` still **288 / 0**.
- ~~**Phase 7** — `net.accept` hands back a bare fd and `core:net` exposes `getsockname` (`netx_port_of`) but no `getpeername`, so an access log cannot record the client address — the single most useful field in a real access log is unreachable from Tycho.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 5.** `netx_peer_addr` is 17 C lines (`corelib/net/net_shim.c:204-219`, `getpeername` into a `sockaddr_storage` then `inet_ntop`, IPv4 **and** IPv6) under `net.peer_addr(fd) -> Result(string, NetErr)` (`corelib/net/net.ty:143-147`), where `""` is never an `Ok`: a log that cannot name the client says so rather than printing a blank column. `server/main.ty`'s log line leads with it now — `w1 127.0.0.1 GET / 200 2659 0.210ms` — asked **once per connection**, not per request, since the peer of an accepted fd cannot change. **The half the item did not see is that the answer is thread-shaped.** N workers are N pthreads sharing one listening fd, so the shim's return buffer is `__thread`, not `static`: a shared buffer would be a data race on precisely the field this item exists to add. The borrow is safe because an extern `-> string` return is copied at the call site (`src/tychoc.c:8871-8874`), so the Tycho value outlives the next request's overwrite — verified across a 50-request flood on 4 workers, every line carrying the address, none empty or truncated. **The general lesson: "the corelib is missing a syscall" and "the corelib is missing a THREAD-SAFE syscall" are different items, and only the second one is true here** — the same shape as the `SIGPIPE`/Nagle pair below, where what was unreachable from Tycho was a property of the *process*, not of the call.
- **Phase 6 of `plan.md`, non-blocking** — `examples/corelib/cli/main.ty` documents only `parse`, not `parse_spec`/`argv`, so the example a reader reaches for first does not show the spelling most CLIs want. Left deliberately: its golden is that phase's proof that `parse` did not move, and re-recording it would spend the proof to demonstrate what `corelib/test/cli/main.ty` already asserts.
- ~~**Phase 7 of `plan.md`, non-blocking — the frozen-`tychoc0` reach is bigger than `frontparity` can see, and phase 6 got it wrong in good faith.** `scripts/frontparity.sh`'s glob (`:126-127` when this was written; `:164-165` today, with the blind spot closed) feeds `examples/*.ty` but never `examples/<dir>/main.ty`, while four per-example runners *do* feed theirs to `tychoc0` — `examples/webserver/run.sh:24`, `examples/weblog/run.sh:24`, `examples/fetch/run.sh:35`, `examples/sqlite/run.sh:31`. So `core:cli` **is** in the frozen compiler's reach (via `examples/weblog`), which phase 6 recorded as out of it; nothing broke only because phase 6 added no new syntax. None of the four runners is in `make ci`, so **no gate can catch a corelib package adopting new syntax** — the failure surfaces the next time somebody runs a non-gated runner by hand. The enumerated split (13 reachable, 24 free) is in `docs/spec/appendix-e-conformance.md`; making it *checkable* wants a runner, and that is not this plan's business.~~ **CLOSED, `docs/internals/plan-postfreeze-rawstring-DONE.md` phase 8 — and phase 7 was RIGHT, which is recorded because two phases of this plan disagree in the record.** `core:cli` is inside the freeze: a `tychoc0` built at this commit, fed `examples/weblog/main.ty`, emits **81 `cli__` symbols**. Phase 6's own evidence (`plan.md:1745-1754`) listed `examples/weblog/` as a `core:cli` consumer and then called it "none of them a tychoc0 input" — the list was right, the conclusion was wrong, and its `frontparity` 288 / 0 could not have contradicted it because that was exactly the blind spot. Phase 6's claim is now annotated in place. **The fix is 6 lines of glob**: `scripts/frontparity.sh` also feeds the four per-example entry points, taking it from `agreed: 288` to `agreed: 292  diverged: 0`. **Reddened deliberately, in the shape the entry says nothing could catch**: giving `corelib/cli/cli.ty` a `\r` escape makes the extended lane report `FAIL examples/weblog/main.ty ... lex: unsupported string escape (use \n \t \\ \")` while the pre-phase-8 script reports `agreed: 288 diverged: 0 / all green` **on the identical tree** — the blind spot, measured from both sides. `server/` and `examples/corelib/{result,httpd}` are excluded by name with the measured reason (`parse: line 2348: unexpected token` for `server/`): they are the witnesses deliberately written outside the freeze. What is still true: `frontparity` is not in `make ci` (it is a removed gate's harness, kept on disk), so the enforcement is a runner you must invoke — but it is now **one** runner that sees the whole reach instead of four that each see a slice.
- **Phase 7 of `plan.md`, non-blocking** — `corelib/test/image` is **skipped** in an environment without libpng (`corelib/run.sh` prints `skip image (missing dependency: libpng)`), so its golden asserts nothing there. Phase 7's one-line change in it (indexing a `bytes` directly instead of aliasing a `to_str` view) was therefore verified by diffing the **emitted C** — the only delta is `h_sig` disappearing and `tycho_str_get(h_sig, i)` becoming `tycho_str_get(h_png, i)`, i.e. the identical call on the identical pointer — rather than by running the test.
- ~~**Phase 7** — scrubbing control bytes out of a hostile request target (`log_safe`) has to go `string` → `[]int` → `to_bytes` → `to_str`, because a `string` cannot be rebuilt in place and `bytes` cannot be indexed. The Phase 2 `to_str`/`to_bytes` sandwich, biting exactly as predicted, in the one function where a server must be paranoid.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 7 — and it cost the application 0 code lines, which is the honest number.** `log_safe` is now `string` → `bytes` → `string`: `b := to_bytes(s)`, then `b[i]` classified and either `scrubbed + '.'` or `scrubbed + b[i:i+1]` appended, then `to_str` at the end. Both ends are **zero-cost reinterprets** (`src/tychoc.c:9187-9188`), so the `[]int` and the one **real** allocation in the old path — `to_bytes([int])` at `:9185-9186` — are both gone; `server/main.ty` is **341 code lines before and after**, and `log_safe` is 17 code lines before and after. The win is that the function now works in one domain instead of three, not that it is shorter. **The half the item did not see is that the straight version uncovered a use-after-free.** `out := to_str(b)` over a scope-owned `bytes`, then `return out`, returned a **dangling pointer**: `to_str` is a zero-cost reinterpret but the compiler reported it as a *call*, so `is_place` (`:8597`) said "a fresh value built in the target arena" and skipped the re-home. Measured on a program containing **no phase-7 syntax at all** — a buffer holding `"ABCDEFGH"` printed as `[8]` under a `tychoc` built from `git show HEAD:src/tychoc.c`, and `[ABCDEFGH]` after. Pre-existing since the reinterpret existed; found only because this item's fix is the shape that triggers it. Fixed in-phase (it blocked the item, it was not absorbed opportunistically), pinned by `reinterp_ret` in `corelib/test/io`, and the scrubber itself re-verified against a HEAD-built binary: control bytes 1..31 and 0x7f in a target, a 300-byte target, a control byte in the *method*, and a `\r\n`-injection attempt all produce **byte-identical access-log lines** with **zero** control bytes surviving and no injected line.
- ~~**Phase 7** — `parallel for` and `spawn` are the only concurrency shapes, and neither can express "hand this connection to whoever is free". One worker owns one connection for its whole life, so N workers is a hard cap of N concurrent connections; there is no way to write an event loop or a work queue over accepted fds without a channel of ints and a hand-rolled dispatcher.~~ **REFUSED WITH THE NUMBER, `docs/internals/plan-loops-cleanup-DONE.md` phase 9 — and the premise is measurably FALSE in its most important half, which is the finding worth carrying.** The work queue **is** writable today, with no compiler change: a channel handle has type syntax and a spawned worker may take one as a parameter (`src/tychoc.c:662-666`; the guards at `:1179-1181`/`:7806` forbid only *storing* or *returning* one), `send`/`recv`/`close` are builtins over it (`:5610`, `:5619`, `:5630`), and the MPMC contract is normative — "with multiple receivers, each value is delivered to exactly one receiver" (`docs/spec/13-concurrency.md` §23.1). It was written in `server/` (**+39 / −1 lines**: one acceptor loop sending accepted fds into `channel(int, 64)`, N workers `recv`-ing) and measured against a **same-machine** reproduction of the recorded baseline (the recorded box gave 12,456 / 41,046 / 79,712 req/s for 1/4/8 clients; this one gives **14,829 / 50,150 / 93,441**). **Throughput was a wash** — 91,667 vs 93,441 on the recorded keep-alive shape (−1.9%, inside noise), and **+17.2%** on connection churn (47,205 vs 40,292), where one dedicated acceptor beats eight threads contending on `accept(2)`. **The cap did not move, to the millisecond**: 4 workers / 4 silent peers → 4,824 ms vs the baseline's 4,753 ms; 8 / 8 → 4,830 vs 4,829. **The cap was never a property of dispatch — it is a property of a worker blocking in `recv(2)`, and choosing which idle worker gets the next fd cannot make a busy worker idle.** Worse, the queue moves the backlog from the kernel to userspace and so makes an unserved connection *look* served: with all 8 workers pinned, **40 complete requests were accepted in 3 ms and none was answered within a second**, where the kernel's backlog at least enforces `listen()`'s limit and lets `connect()` block. **Not adopted; `server/main.ty` is byte-identical to before.** What would actually lift the cap is readiness notification, and the cost was read out of the source: `corelib/net/net_shim.c` is 361 lines with 12 exports (`:110`, `:162`, `:170`, `:185`, `:204`, `:225`, `:271`, `:300`, `:316`, `:324`, `:339`, `:350`) and **not one** is `poll`/`select`/`epoll` or `O_NONBLOCK` — but the syscall is the *cheap* part, since `[int]` crosses the FFI both ways as a `(const long*, long)` pair (`docs/spec/14-ffi.md` §24.1), so `netx_poll` is ~45 C lines plus ~20 in `corelib/net/net.ty`. The expensive part is that **`httpd.read_request_capped` is a blocking accumulator whose whole state is locals** (`corelib/httpd/httpd.ty:243-247`, looping `net.read` at `:271`), so an event loop needs it split into a `struct Pending` + `feed`/`finish` — ~60 lines, **additive** because three consumers call the blocking form and the frozen `tychoc0` compiles this package (`examples/webserver/run.sh:24`) — and that `serve_conn` (`server/main.ty:363-473`, **70 code lines**) plus a write path that loops `send()` internally (`corelib/net/net_shim.c:225`) becomes ~150 lines with per-fd write buffering. **~283 lines across 4 files, ~80 of them inside the freeze, and a redesign of `core:httpd`'s read surface — refused on Anti-scope, with that number.** Two facts to weigh against the refusal: the cap is already **tunable** (`--workers` accepts 1..256, `server/main.ty:564`, under a 1024 live-task ceiling, `runtime/tycho_rt.c:557-562`), and the concurrency model is not the thing in the way — the missing readiness call in `core:net` is.

## Phase 1 of the Option/Result plan — converting `core:net`

Found while converting `core:net`'s fallible TCP surface to `Result(T, net.NetErr)` and
rewriting `server/`, `corelib/httpd`, both corelib tests and both examples against it.

- **`Option`/`Result` phase 1** — there is no `unwrap_or`, `is_ok`, `is_some` or `is_err` anywhere: searched `docs/spec/16-builtins.md`, `docs/spec/12-aggregates.md` and all of `corelib/`, zero hits. Every caller whose own return type is not a `Result` hand-writes the same three-line `match` to collapse one — `server/main.ty`'s `nwrote`, and a separate copy each in `corelib/test/net/main.ty` and `corelib/test/httpd/main.ty`. It is the single largest cost of adopting `Result`, and it is a missing three-line library function.
- ~~**`Option`/`Result` phase 1** — there are **no nested patterns**: `Err(net.Timeout)` is rejected with `error: expected ')'`, and so is `Err(C(n))` for a local enum. Worse, `Err(A)` where `A` is a nullary variant *parses* — as a **binding named `A`**, not as a pattern — and the mistake surfaces only if a second arm exists, as `error: duplicate Err arm`. So telling two failure causes apart always costs a second `match`, which is why `net.NetErr` was given payload-free variants: `if e == net.Timeout` is one line where `match e:` is three.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 3.** All three spellings work: `Err(net.Timeout)`, `Err(Timeout)` and `Err(C(n))` are legal arms, and the misparse is gone by construction rather than by diagnostic. **It was two bugs, and the entry only saw one of them.** The parser half is what the entry describes — `MatchArm` held *binding names*, not patterns, and the arm loop ate one bare `TK_IDENT` per slot (`src/tychoc.c:2786` at `667f0d9`), so `net.Timeout` died on the `.`, `C(n)` on the `(`, and `A` **fit**, as a binding. The half it did not see is in **codegen**: an `Option`/`Result` match was not an arm chain at all but a hard binary `if` with exactly one Ok arm and one Err arm found by name (`:10250-10254` as it stood then; the replacement `gen_match_side` is at `:9932`), so multiple `Err` arms had nowhere to go. That is the structural reason this was not a parser tweak: `gen_match_side` replaces the binary `if` with an ordered decision list per side. **The rule that kills the silent bind:** inside a pattern the payload's enum type is already known, so a name that is a variant of that enum is **always a pattern, never a binding** — which also means `Err(Timeout)` needs no qualifier, and a bare name that *cannot* be a legal pattern is now a hard error instead of a bind. **116 code lines in `src/tychoc.c`** (+192 with comments), one new field on `Variant` (`raw`, the name as written), four on `MatchArm`. **Refused, with the number:** nesting inside a *plain enum* arm (`Wrap(A)`) and nesting deeper than one level, both ~70 lines across the enum arm loop (`:7183-7222`, whose `covered[]` becomes 2-dimensional) and the enum dispatch (`:10644-10677`, which needs the chain nested inside each tag test) — refused for a shape nothing in the tree writes, **but the trap is closed there too**: `Wrap(A)` is now `error: 'A' is a variant of Cause, not a binding name`. **The payload-free enums stay payload-free** — that was a redesign this plan's Anti-scope forbids, and `err_or` + `==` is still the right tool for a caller who is not opening a `match`. What the item did not know is that **`corelib/` still cannot use the form it asked for**: `core:httpd`, `net`, `io`, `result` and `tools/*.ty`/`tests/*.ty`/`examples/*.ty` are compiled by the frozen `compiler/tychoc0.ty`, so the call sites converted are in `server/` (five arms, one per cause, replacing five `e == httpd.X` tests and an `answer` bool), `corelib/test/{io,httpd}` and `examples/corelib/result` — whose goldens still match **byte for byte**, which is the real proof the new codegen is behaviour-identical to the `==` chains it replaced.
- ~~**`Option`/`Result` phase 1** — `die()` is typed `void` and the compiler does not model it as diverging, so it cannot be the tail of a value-`match` arm: `srv := match net.listen(...): Ok(fd): fd / Err(e): die("cannot bind")` is rejected with `a value if/match branch must produce a value, not void`. The statement form needs a dummy `srv := 0` first, making the `Result` version **one line longer** than the `if srv < 0: die(...)` it replaced — the only call site in `server/` where the conversion cost a line.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 4.** The item's diagnosis is exactly right and the fix is **not a type**: there is no bottom type in Tycho and adding one would touch every unification site. Divergence is modelled where the *tails* are handled instead. The value if/match desugar has two halves that must agree about which branch carries a value — `ctrl_rewrite_tails` (`src/tychoc.c:2949`), which turns each branch's trailing `S_EXPR` into `name = tail` / `return tail` / a place-set, and `ctrl_collect_tails` (`:2978`), which hands the tails to unification in the `S_DECL` arm of `resolve_stmt` (`:6961-6984`). **Both now skip a diverging tail**, so the branch keeps the plain statement it already was: it contributes no type and gets no destination. That is 14 code lines total (`+44` with comments), the largest single piece being the `expr_diverges` predicate and its justification. **The predicate is syntactic, and that is sound rather than convenient** — `die` and `exit` are registered builtins and a program defining either name is rejected outright (measured: `fn die(s: string) -> int` → `error: 'die' is already defined`), so the name cannot mean anything else; `e->sval` is the written name both before resolution and after, because builtins are never mangled — which is why codegen has always matched `die` the same way. `!e->qual` excludes `pkg.die`, `!e->lhs` excludes a call through a function value. **Fail-closed half:** if *every* branch diverges there is no value at all, and `t` would otherwise stay at its `T_VOID` "unset" sentinel and be pushed as the variable's type — so that is now a hard error naming the fix (`x := if true: die("a") else: exit(2)` → `every branch of this value if/match diverges, so there is no value to bind to 'x' -- write the if/match as a plain statement`). **`server/main.ty` lost the dummy** and is now the item's own spelling; the file went **380 → 376 code lines** (611 → 606 total), and the live matrix on `127.0.0.1:18099` with 4 workers is transcript-identical to a `HEAD`-built binary. Because the skip went into the shared desugar rather than into the `:=` case, **all four tail positions** got it at once: `x := if/match`, `x = if/match`, `place = if/match` and `return if/match` all accept a `die`/`exit` arm (each verified with its own scratch program and exit status). The **fall-off-the-end lint is deliberately unchanged** — a `-> int` function whose `else` branch dies still warns "not all paths return a value", because `block_ends_in_return` (`:9220`) governs a *codegen* decision (the defensive `return (T){0}`) as well as the lint, and `runtime/tycho_rt.c:1190-1192` documents that fallback as intentional: `tycho_die` is not declared `noreturn`, so dropping the return would hand C a fall-off-the-end path. Measured to be pre-existing, not new: the identical warning fires on the plain statement form `if n > 0: return n * 2 / else: die("neg")`.
- ~~**`Option`/`Result` phase 1** — `tychoc` compiles every `.ty` in the entry file's directory, not just the entry file, so two unrelated scratch programs side by side collide with `'main' is already defined` pointing at the file you asked it to build. Nothing says the sibling file is involved; it cost four compile cycles to work out that the fix was `mkdir`.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 8 — as the DIAGNOSTIC, because the directory scan is a feature and changing it would break a committed fixture.** Two halves the entry did not have. (1) **The scan is conditional**: it happens only when the entry file declares a `package` header (`src/tychoc.c:12618-12620` — `detect_package` decides between `compile_package` and single-file `parse_program`), which is why two headerless scratch files beside each other build fine and only *importing* programs collide. That is the worst possible trigger: a scratch program written to exercise the corelib must say `package main`, so the trap is armed exactly when you are probing a corelib item. (2) **A package may legally span files** — `tests/pkg/multifile/{main,util}.ty` is the fixture — so "only compile the entry file" is not available; it would delete a documented feature to improve an error message. (3) **The blamed file depends on sort order**, which the entry read as "it points at the file you asked for": `scan_pkg_files` qsorts (`src/tychoc.c:11807`) and the diagnostic fires at the *second* definition, so a sibling named `aprobe.ty` blames `main.ty` and a sibling named `probe2.ty` blames itself. Measured both ways before the fix. Now `die_dup_proc` finds the same-named proc in a **different** file of the package and names it: `main.ty:5: error: 'main' is already defined -- also at .../aprobe.ty:5, a DIFFERENT file in the same package: tychoc compiles every .ty beside the entry file, so two unrelated programs cannot share one directory`. **13 code lines in `src/tychoc.c`**, and a same-file duplicate deliberately keeps the plain message (it is self-evident, and lengthening it would be noise). The emitted C of all 15 entry points is **byte-identical** to a HEAD-built compiler's, which is the proof this touched only an error path.
- ~~**`Option`/`Result` phase 1** — the FFI has no way for C to return a classification alongside a `bytes` payload, and `-> Result(T, E)` is not a documented `extern` return shape (`docs/spec/14-ffi.md:20-47` lists only scalars, sized ints, `string`/`Option(string)`, `bytes`, `[int]`/`[float]`, `ptr`, handles, and numeric `inout`). Making `net.read` say *why* it read nothing needed `status: inout int` threaded ahead of the two `bytes` out-params — which works, and is undocumented as the way to do this.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 8 — documented, as §24.1.1 of `docs/spec/14-ffi.md`.** The shape is now normative with the C ABI spelled out, because the ordering is the one non-obvious part and neither shim's comment states the *rule*, only its own instance: written parameters lower in written order (an `inout` becoming `T*`), then a `bytes`/array **return** appends two trailing out-params — so the classification pointer sits ahead of the payload's even though it is written last (`gen_extern_proto`, `src/tychoc.c:10934-10946`; emitted proto `extern void netx_read(tycho_int , tycho_int , tycho_int *, unsigned char **, tycho_int *);`). Also written down: why `-> Result(T, E)` is absent **by decision** rather than by omission (no flat C ABI for a Tycho aggregate; the wrapper on the Tycho side is what makes the `Result`), that the classification must be a numeric scalar because that is the whole `inout` crossable set, that the shim must set it to a failure code before anything can fail, and — the half neither shim says — **when NOT to use it**: `iox_stat_kind(path) -> int` carries the same four codes with no `inout`, because there the kind *is* the answer.

## Phase 2 of the Option/Result plan — the combinators, `io.read_bytes`, `httpd.read_request`

Found while adding `core:result` and converting the two genuinely ambiguous calls.

- ~~**`Option`/`Result` phase 2** — a **qualified name written anywhere in a generic call's argument list does not resolve**. `result.unwrap_or(net.port_of(fd), -1)` fails with `error: package 'net' has no symbol 'net__port_of'`, `result.err_or(r, net.Failed)` with `error: unknown variable 'net'`, and `result.unwrap_or(r, httpd.bad_request())` with `error: package 'httpd' has no symbol 'httpd__bad_request'` — while the identical spellings are accepted in `==` and as arguments to concretely-typed parameters, and an *unqualified* local call inline is fine. So generic instantiation loses the package qualifier, and every corelib call site pays one extra line to bind the value to a local first. It is what stops `n := result.unwrap_or(io.read_bytes(p), empty)` — the whole point of a combinator — from being the one-liner it should be.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 1.** And the diagnosis in this entry was **wrong in an instructive way**: generic instantiation was not losing the qualifier, and generics were not really the subject. **Resolution is not single-pass.** `instantiate_generic` resolves every argument once to infer `$T` (`src/tychoc.c:7496`), then the ordinary concrete-signature loop resolves *the same AST nodes again* against the bound parameter types (`src/tychoc.c:5911`) — and the two rewrites that turn a written `pkg.x` into a mangled `pkg__x` mutated the node in place without being idempotent. The `E_CALL` one kept `e->qual` after rewriting `e->sval`, so the second pass computed `net__` + `net__port_of`; **the doubled prefix in the error message was the entire tell, and it was printing the mangled name back at me as if I had typed it.** The `pkg.Variant` one reinterpreted an `E_FIELD` as an `E_CALL` but left `e->lhs` pointing at the package ident, so the second pass took the call-on-a-fn-value branch and asked for a variable named `net`. The proof that "generic" was a red herring: `Box(net.Failed)` on a plain generic **struct** literal failed identically, because that path also infers-then-re-resolves. The fix is a one-bit `pkg_done` latch on `Expr` plus one `e->lhs = NULL` — **4 code lines in `src/tychoc.c`**, which removed **18 lines** of bound-first workaround across 7 files and made `server/main.ty` shorter (380 → 378) for the first time in two plans. `tests/pkg/generic_qual_arg` is the regression, reddened deliberately against the pre-fix compiler. The lesson worth keeping: **an error message that quotes a name the programmer never wrote is reporting a second visit to the same node, not a lookup failure** — and this file spent a whole phase telling every future caller to bind a local because the message was read as a fact about generics rather than as evidence of re-entry.
- ~~**`Option`/`Result` phase 2** — **two error types in one function make `or_return` unavailable again**, and nothing says so until you try. `examples/corelib/httpd/main.ty`'s `round_trip` returns `Result(int, net.NetErr)` and seven `net.*` calls short-circuit through it beautifully; the one `httpd.read_request` call in the middle returns `Result(Request, httpd.ReqErr)` and has to be collapsed by hand, because there is no `map_err` and no conversion between error enums. A function that touches two packages' fallible calls gets `or_return` for whichever one it picked as its own error type and a manual collapse for the other.~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 9, and it is a plain library function — `4 code lines`.** `result.map_err(r, replacement)` is `Ok(v): return Ok(v)` / `Err(e): return Err(replacement)` over **three** type params, `$T` passing through untouched while only the error type moves; the recorded call site is now `served := result.map_err(httpd.read_request(conn_in), net.Failed) or_return` (`examples/corelib/httpd/main.ty:22`). **Phase 1 is what made it writable** — the qualified-name-in-a-generic-argument bug would have stopped `net.Failed` being written inline, which is the entire ergonomic point. Same line count as the `unwrap_or` collapse it replaced, **different behaviour**: the old spelling continued with a dummy `Request` nobody sent, the new one ends the function. The example's golden is **byte-identical** (the exchange succeeds, so the changed path is not taken), which is the proof only the failure path moved; the regression is `two_types` in `corelib/test/result`, whose golden is **+4 / −0**, a pure append. **The freeze is satisfied and measured, not assumed:** `core:result` is inside the frozen `tychoc0`'s reach (`core:httpd` imports it, and `examples/webserver/run.sh:24` feeds the package to a freshly built `tychoc0`) and the three-type-param generic compiles there — `webserver: ok (tychoc == tychoc0 == golden)`. **The alternative was built before being rejected:** the closure form (`f: fn($E) -> $F`, `Err(e): return Err(f(e))`) also compiles and runs, and was not adopted because the caller who needs this has one target variant in mind, so a callable charges every call site a named two-line enum-converting helper — which *is* the hand-written collapse this entry exists to delete — and nothing else in `core:result` takes a callable. What `map_err` does cost is written at the declaration: **the original cause is gone**, so it is for callers whose own enum already has a variant meaning what happened.
- ~~**`Option`/`Result` phase 2** — converting a call that a big block consumes costs an **indentation level**, not just lines: `server/main.ty`'s `serve_conn` went 60 → 71 code lines almost entirely because `match httpd.parse_request(raw)` has to wrap the whole request-handling body to bind `Ok(req)`. There is no `if let`, no early-return binding form, and `or_return` is unavailable (the enclosing function returns a served count) — so the only tool re-indents 30 lines.~~ **REFUSED WITH THE NUMBER, `docs/internals/plan-loops-cleanup-DONE.md` phase 9 — and `if let` is the wrong ask, which is the first finding.** `if let Ok(req) := httpd.parse_request(raw):` still puts the whole body inside its own block at the same depth: it saves an *arm*, not an indentation level. Only an **early-return binding form** — `x := e or_else: <diverging block>`, the `guard let` shape — flattens anything. Both were costed by reading `src/tychoc.c`. **`if let` as parser-only sugar is ~45 lines** and the machinery is all there: `parse_match` (`src/tychoc.c:2734`) already parses arm patterns into a `MatchArm` carrying `variant`, `binds[8]` and phase 3's `sub`/`subbinds`/`sub_vi` (`:1456-1458`), and the resolver (`:6799`) and codegen (`:9355`, `:10113`) already run an ordered Ok/Err decision chain — so it is a contextual `let` after `TK_IF`, the pattern parser factored out, and a synthetic two-arm `S_MATCH`. It was not built **because it would not close this entry.** **The binding form is ~105 code lines in `src/tychoc.c`**: a token beside `TK_ORRETURN` (`:123`), a parse hook on the decl path where `x := if/match` already lives (`:3260`), a resolver unwrap in the `S_DECL` arm, a divergence check reusing `expr_diverges` (`:2847`) and `block_ends_in_return` (`:9298`), codegen, and diagnostics beside the existing `or_return requires the enclosing function to return a Result, but it returns %s` (`:4795-4796`) — plus a spec section beside §14.6 (`docs/spec/10-statements.md:143`) and a fixture that, being **new syntax**, can live only in `corelib/test/` or `server/` and never in `tests/` or `examples/` (frozen `tychoc0`; `scripts/frontparity.sh` reports it as a divergence). **And the payoff was measured, not assumed:** `serve_conn` (`server/main.ty:363-473`) is **70 code lines** with **six** arms — five `Err` causes that each answer differently (`:390-414`: silent close, silent close, 408, 431, 400) and `Ok(req)`, which holds **45 of the 70**. A binding form moves those 45 out one level and pushes the five causes into the `or_else` block, where they are still the same five-way match: **net line change ≈ 0**, one indentation level, in **one** function in the whole tree, unusable in `corelib/`. 105 compiler lines for that — **refused, with the number.** The footnote this entry's own 60 → 71 needs: `docs/internals/plan-loops-cleanup-DONE.md` phase 3 has since turned what was one `Err(e)` plus five `==` tests into five real arms **on purpose**, because acting on the cause is the point of the conversion, and a happy-path binding form pushes them back into one block. The 11 lines were not bought by a missing keyword; they were bought by a function that has six outcomes.
- ~~**`Option`/`Result` phase 2** — the FFI trick for classifying a `bytes` result (`status: inout int` threaded ahead of the two out-params) had to be reproduced verbatim in `corelib/io/io_shim.c` from `net_shim.c`, because it is still undocumented in `docs/spec/14-ffi.md`. Two shims now depend on an ABI detail written down only in each other's comments.~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 8**, by the same §24.1.1 as the phase-1 half above — which cites both shims as its worked examples (`corelib/net/net_shim.c:236-259`, `corelib/io/io_shim.c:61-96`) so the next one has a spec to copy instead of a sibling.
- ~~**`Option`/`Result` phase 2** — `examples/webserver/main.ty` was left **uncompilable by phase 1** (`error: ordering compares two ints ...` on `if srv < 0`, against the converted `net.listen`): it imports `core:net` but was not in that commit's file list, and no gate builds it (`make ci` skips it — see the phase 0 note below), so nothing went red for a whole phase. Fixed here because it also consumes `io.read_bytes` and `httpd.read_request`.~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 8 — with a gate, and the gate is 40 lines of shell that cost `make ci` milliseconds.** `scripts/entrypoints.sh` (`make entrypoints`, CI step 3b) compiles **every** entry point under `examples/` plus `server/main.ty` — 11 of them — with `--emit-c`, which stops before `cc`, so the lane needs no libcurl, no sqlite3, no libpng and no link step. **Reddened deliberately on the exact breakage this entry records**: restoring `if lr < 0` in `examples/webserver/main.ty` gives `FAIL examples/webserver/main.ty / examples/webserver/main.ty:199: error: ordering compares two ints, two floats, two strings, or two values of the same numeric newtype` and `entrypoints: FAILED (1 of 11 entry points do not compile)`. **Reddened a second time on its own vacuity**, which is phase 1b's lesson applied: renaming `examples/webserver/main.ty` away gives `MUST-COVER FILE GONE ... this lane asserts LESS than it claims`, rather than a green run over a shorter list. The glob is per-directory, so a new `examples/<dir>/` is covered the day it is added; `examples/corelib/*` is excluded because `examples/corelib/run.sh` already compiles, runs and goldens all 38. What the lane does **not** assert is written in its header: not that the emitted C compiles, not that the program runs, and nothing about `tychoc0`.

## Phase 3 of the Option/Result plan — acting on the cause, and deleting `read_head`

Found while moving the cap and the raw buffer into `core:httpd` so `server/` could
stop reimplementing the read loop.

- ~~**`Option`/`Result` phase 3** — **a tuple literal will not infer a `Result` element.** `return (Err(A), "partial")` from a function declared `-> (Result(int, E), string)` is rejected with `error: tuple element 1 needs a concrete value`, pointing at the `return`; the same `Err(A)` is accepted as a bare `return` from a `-> Result(int, E)` function, and accepted inside a tuple once it has been through a typed local (`out: Result(int, E) = Err(A)`) or a helper function. So the one shape the language provides for "return a value AND a classification" costs an extra local and an extra assignment per exit, purely because inference does not reach into the tuple. It is why `httpd.read_request_capped` builds its outcome in `out` instead of returning it directly.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 3 — and it was a CONFORMANCE BUG, not a missing feature, which is the finding worth carrying.** `docs/spec/04-inference.md` §6.1 has always listed "a tuple or array literal's element type" as a context that supplies an expected type; the compiler simply had no `E_TUPLE` arm in checking mode, so a tuple literal always fell through to the synthesis path (`src/tychoc.c:4840-4851` at `667f0d9`) where each element is resolved with no expected type and a `T_OK_PARTIAL` is rejected. `Ok`/`Err` resolving to a *partial* is deliberate (`:4974-4979`) and `resolve_exp` already grounded one against an `IS_RES(want)` (`:6367-6371`) — which is exactly why a bare `return` and a typed local worked. **The fix is 11 code lines**: an `E_TUPLE` arm in `resolve_exp` that pushes each element's expected type in. It returns the **synthesized** element types rather than `want`, so a mismatch reports through the caller's own equality check instead of a second visit to the same node — phase 1's lesson applied on purpose. **`httpd.read_request_capped` still builds its outcome in `out`, and this is the part the item could not have predicted:** the direct form compiles under `src/tychoc.c` but not under the frozen `compiler/tychoc0.ty`, which `examples/webserver/run.sh:24-27` feeds `core:httpd` while asserting `tychoc == tychoc0 == golden`. Measured by making the change and running the runner: `line 540: returning (Result(,httpd__ReqErr),str) but this function returns (Result(httpd__Request,httpd__ReqErr),str)`. So the item is fixed in the compiler and the workaround is **kept deliberately**, with the reason written at the declaration — the same conclusion phase 2 reached about `httpd.crlf()`, reached again by a different route. The direct form is demonstrated in `corelib/test/result` (`outcome`), which no runner feeds to `tychoc0`.
- ~~**`Option`/`Result` phase 3** — tuples are the right shape for this and **nothing pointed at them**. The obvious reading of "a function returns one value" sends you to an `inout` out-param or a wrapper struct; `docs/spec/03-types.md:193` and `docs/spec/02-grammar.md:137` do document 2–8 element tuples with destructuring (`got, raw := f()`), but no corelib function in the tree returns one, so there is no example to copy. Both alternatives were written and compiled before the tuple was found: `inout string` works (§11.3) and costs the caller a dummy `raw := ""`; a `struct` with a `Result` field also works (verified — a `Result` *can* be a struct field, even though it cannot be a tuple literal element). Three shapes, one documented, none demonstrated.~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 8 — and the premise was FALSE when it was written, which is the finding worth carrying.** "No corelib function in the tree returns one" was wrong by five: `strings.split_once -> (string, string)` (`corelib/strings/strings.ty:193`) and `path.split_path -> (string, string)` (`corelib/path/path.ty:95`) have been there since the language was renamed (`39d75be`), `datetime.parse_offset -> (int, bool)` — the value-**and**-verdict shape this entry needed, exactly — since `4c7f8a5`, plus `bignum.divmod -> (Big, Big)` and `datetime.civil_from_days -> (int, int, int)`. So three alternatives were not written for want of a demonstration; they were written because **nothing at the place you look points anywhere**: §5.3.3 was four sentences, no example, no citation, no statement of what the shape is *for*. The fix is therefore documentation and it is now gated: §5.3.3 says outright that a tuple is the shape for "a value AND a classification", says why the two alternatives cost more (a dummy local; a nominal type per call site), notes that since §6.2(7) a `Result` element may be written inline (`return (Err(Timeout), buf)`), carries a **runnable** worked example — `scripts/spec_check.sh` compiles and runs it: `ok docs/spec/03-types.md:231`, taking the spec from 7 runnable examples to 8 — and then lists the five corelib functions by `path:line` so the next reader has code to copy. It also points at `14-ffi.md` §24.1.1 for the C-boundary case, since a tuple does not cross. **Lesson: "the library does not demonstrate X" and "the documentation does not point at X" are different items, and only the second one was true.**
- **`Option`/`Result` phase 3** — a **payload-free error enum cannot say "how much"**, so the `431` decision had to become its own variant. `Err(TooLarge)` tells the caller the cap was hit but not what the cap was or how far past it the peer got, and adding that payload would break the `==` comparison the whole design rests on. (Nested patterns landed in `docs/internals/plan-friction-DONE.md` phase 3, so `Err(TooLarge(n))` would now *match* — but every `==` call site in the tree would still break, and `corelib/` cannot use a nested pattern anyway while the frozen `tychoc0` compiles it. So this item is unchanged by phase 3.) The five-variant enum is the right call here, but the pattern does not scale: every quantitative failure needs either a variant or a second return value.

## Phase 4 of the Option/Result plan — the missing syscall

Found while writing `io.is_dir` and its test.

- ~~**`Option`/`Result` phase 4** — **nothing in Tycho can create a directory.** Verified absent, not assumed: `docs/spec/16-builtins.md` §29.10 lists five filesystem/time builtins (`read_file`, `write_file`, `list_dir`, `clock`, `now`) and none of them makes a directory, and `mkdir`/`make_dir`/`create_dir` return zero hits across `corelib/`, `src/tychoc.c` and `runtime/`. There is no remove either. So `corelib/test/io` — the test for a `stat(2)` wrapper — has to build its empty directory with `os.system("rm -rf … && mkdir -p …")`: a corelib test depending on `/bin/sh` to set up a filesystem state the corelib itself cannot reach. The asymmetry is the finding: the library can now *classify* a directory but not *make* one.~~ **CLOSED, `docs/internals/plan-option-result-DONE.md` phase 5.** `io.make_dir(p)` (`mkdir(2)`, no `-p`) and `io.remove(p)` (`remove(3)`, one entry, **never recursive**) both return `Result(bool, IoErr)` where `Ok(true)` is "changed it" and `Ok(false)` is "already how you asked" — `make_dir` splits `EEXIST` into `Ok(false)` (already a directory: goal met) and `Err(Exists)` (a file is in the way: goal unreachable), which is exactly the ambiguity test this plan was built on. `corelib/test/io` no longer imports `core:os` and the `rm -rf && mkdir -p` line is gone. A non-empty directory is `Err(Failed)`, which is the property that keeps `io.remove` from being `rm -rf` behind a corelib name.
- ~~**`Option`/`Result` phase 4** — `io.exists` and `io.is_dir` now answer overlapping questions by different means, and the cheaper one is the newer one: `exists` lists the whole parent directory (O(entries), and it cannot see a `.`/`..`-only leaf) where `is_dir` is one `stat`. `resolve()` ends up calling both on the same path. A `stat`-backed `exists` is the obvious follow-on and was refused on scope, but the general shape is worth recording — a missing syscall does not just block the question it names, it leaves *neighbouring* answers implemented the long way round.~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 5, and the follow-on was bigger than the swap.** `exists` is now `iox_stat_kind(p)` and two comparisons (`corelib/io/io.ty:252-254`), the same shim call `is_dir` uses; it still fails closed, so `false` means "`stat` could not say yes" — the old behaviour too, since an unlistable parent yielded no entries. `corelib/test/io.out` is **byte-identical** before and after, which is the proof the swap changed the means and not the meaning. **Two things the entry did not predict.** (1) **`core:io` lost a dependency**: `path.base`/`path.dir` were needed *only* by the old `exists`, so `import "core:path"` is gone and the module written up as "the first corelib module to COMPOSE other core modules" now composes one, not two — a stale claim in three places, all corrected. (2) **`resolve()`'s double call did not just halve, it collapsed**: making the second call a `stat` is what made the pair visibly redundant rather than merely ugly, and the two calls are now ONE `match io.is_dir(fsp)` reading all three answers off the Result (`server/main.ty:293-302`) — `Ok(true)` → `301` (or `404` when `dir_form` already appended `index.html`, i.e. a directory *named* `index.html`, which used to be a `200` → `read_bytes` → `Err(IsDir)` → `404`: same status, one syscall fewer, no wrong intermediate), `Ok(false)` → `200`, `Err(_)` → `404`. Per request for a real file: **2 syscalls (an opendir/readdir walk plus a stat) → 1 stat**. `corelib/io/io.ty` **98 → 93 code lines**, `server/main.ty` shorter as well. The entry's own closing sentence turned out to be the useful half and to run both ways: a missing syscall leaves neighbouring answers implemented the long way round, **and adding it does not fix them — someone has to go back and delete the long way round**, which is a second, separately-scoped piece of work that is easy to leave undone because nothing is red.
- **`Option`/`Result` phase 4** — reordering two guards to make room for a new one silently changed a security answer: hoisting `hidden_segment(path.clean(rel))` above the `index.html` append made `GET /` return **403**, because for the root target `rel` is `""` and `path.clean("")` returns `"."` (`corelib/path/path.ty:104-105`), which `hidden_segment` reads as a dotfile. Nothing in the compiler or the corelib could have caught it — `clean("")` returning `"."` is documented POSIX behaviour and both spellings type-check identically. It was caught by the live matrix (`50-request flood 0/50 200`), which is the argument for keeping that matrix.

### Two defects that were not expressible in Tycho at all

Both were found by running the server, both are C-level socket properties with
no Tycho spelling, and both therefore earned a `corelib/net/net_shim.c` fix
rather than a line in this file. Recording them here because *the reason they
were unreachable* is the ergonomic finding.

- **`SIGPIPE` killed the whole server.** Nothing in `corelib/`, `runtime/` or `src/` mentioned SIGPIPE. One client that sent a partial request and closed without reading terminated the process — every worker, every in-flight connection — measured as `poll()` = `-13`. A Tycho program cannot fix this: signal disposition is process-wide with no Tycho surface, and `netx_write` loops `send()` internally, so even a single logical `net.write` can issue several syscalls. Fixed with `MSG_NOSIGNAL` / `SO_NOSIGPIPE`; the server now survives 100 consecutive hostile disconnects. **RE-SCORED 2026-07-31 — the general claim inside this entry has stopped being true, and the entry's conclusion is unchanged by that.** Tycho does have a signal surface now: `core:signal` (`docs/spec/18-library.md` §32.27) installs a `SIGTERM`/`SIGINT` handler whose only action is `shutdown(fd, SHUT_RDWR)` on a listening socket. It is two functions wide and **cannot set a disposition** — there is no `signal.on(sig, handler)` and no way to spell `SIG_IGN` — so `SIGPIPE` remains unreachable from Tycho and `MSG_NOSIGNAL` in the shim is still the only fix for it. What changed is the *precision* of the complaint: "signal disposition is process-wide with no Tycho surface" is now "no Tycho surface for signal **disposition**", which is the narrower and more useful statement of the gap.
- **Nagle cost 620× on every small response.** `httpd.write_response` deliberately sends head and body as two writes so the body is never copied — a Phase 2 optimization. With Nagle on, that second small segment waits for the peer's delayed ACK. Measured, same server, same bytes: **43.73 ms/req (23 req/s) with two writes vs 0.07 ms/req (14,465 req/s) with one.** `TCP_NODELAY` fixes it and no Tycho program can set it. The lesson is sharper than the bug: a corelib change that saved one `memcpy` cost three orders of magnitude, and nothing in the language or library could have surfaced that to the person who wrote it.

## What was good

An honest account needs this half, and the good is not a consolation prize —
some of it is genuinely better than the mainstream alternatives.

- **Concurrency is the best thing in the language.** `spawn` / `wait`, no async colouring, no executor to configure, no locks anywhere in the server, and real parallelism: 1 / 4 / 8 client processes gave 12,456 / 41,046 / **79,712 req/s**, linear in worker count. The whole pool is five lines. Compare what "8 worker threads sharing an accept loop" costs to write in C. **Confirmed 2026-07-31 on a second, non-server workload, which matters because this bullet was written from a program that never used `parallel for`:** `tools/prunner/main.ty` runs 560 fixtures over a bounded channel at **7.62x** (`maxconc` measured at 16 = `ncpu()`), and both of its drafts — ~240 lines and 479 lines — compiled with no errors and no warnings on the first attempt. See the 2026-07-31 re-score section above.
- **Value semantics made the response builder obviously correct.** `r = httpd.with_header(r, ...)` returns a new value, so a three-call chain has no aliasing question, needs no defensive copy, and cannot be wrong. Zero aliasing bugs across the whole phase. This is the part I would keep unchanged.
- **`path.safe_join` is exemplary corelib design.** Fail-closed (returns `""`), handles both the absolute-path and the climb-out cases, and documents itself with worked examples including the answers. Thirteen traversal vectors — `..`, `%2e%2e`, `%2E%2E%2f`, `....//`, `//etc/passwd`, `/%00`, `.git` — all refused on the first attempt, with the whole defence being one call plus decoding in the right order.
- **`httpd.content_type` defaults to `application/octet-stream`, never `text/plain`, and says why in a comment.** That is the correct call and the reasoning was written down where the next person will read it.
- **Diagnostics are precise when they fire** — file, line, column, caret, and the source line quoted. The `send` collision is a gap in *where* the check happens, not in the quality of the message.
- **`bytes` bodies delivered byte-exact binary on the first try**: a 480×270 PNG, a PNG-in-ICO favicon, and a 95 KB TrueType font, all `cmp`-identical to disk, including a `HEAD` that reports the real `Content-Length` and sends nothing.
- **It compiled on the second attempt** — one rename — and then ran the entire abuse campaign (malformed request lines, binary junk, 60 KB headers, 2000 headers, slow-loris, RST-mid-body, 64-way floods, 6400-request runs) with **zero runtime crashes** once the two C-level socket issues were fixed. No use-after-free, no bounds abort, no leak, no data race across 8 threads sharing a listening fd.
- **The corelib is unusually well-commented.** Learning `path`, `httpd`, `net` and `datetime` well enough to build against them was fast, and the comments were accurate — with one exception, `httpd`'s old claim that an interior NUL truncates a string body, which Phase 2 measured as false.
- **Indentation-block syntax reads well at length.** `server/main.ty` is 440 lines and stayed readable without a formatter fight.

## The honest verdict

Tycho can write a real web server, and the result is fast, safe, and readable.
What it cannot yet do is make *failure* pleasant. The concurrency model, the
value semantics and the diagnostics are all better than the median; the error
model is worse, and it is worse by choice rather than by capability, because the
language already ships `Option`, `Result` and `or_return` and the standard
library uses them once in 386 functions. Adopting them across the IO surface
would remove more friction from this file than every other item combined.

**Postscript, 2026-07-26.** That last sentence was acted on and it was wrong. See
"The score against this file" above: five phases of adoption closed **two** of this
file's twelve items, created six new ones (one since closed), spent **182 library
code lines**, and left `server/main.ty` **nine lines longer** than it started —
371 → 380 — with the growth coming from the phase that made a startup message
*accurate*, not from the four that made failures *sayable*. The narrower claim
survives intact and is the one to carry forward: **where a sentinel is genuinely ambiguous, `Result` is a clear win;
where it has exactly one meaning, converting it is line-neutral at the call site
and pure cost in the package.** The first sentence of this verdict — that Tycho
cannot yet make *failure* pleasant — still holds; what changed is that the reason
is ergonomics (`unwrap_or`, nested patterns, `map_err`, inference) rather than the
absence of `Option` and `Result` from the type system.

**Postscript, 2026-07-30 (head `afa67da`).** Two numbers in the verdict above have
moved and the sentence they support has been overtaken by its own follow-through.
Re-counted at HEAD by `grep '^fn '` over the package sources (not `corelib/test/`):
the corelib is **406** functions, not 386; **1** still returns an `Option`
(`io.read_line`); **15** now return a `Result`. So "uses them once in 386 functions"
is no longer the state of the tree — it is the state the file was written in, and the
`Result` surface has grown fifteenfold since. More to the point: all four ergonomic
gaps this postscript named as the *real* reason failure is unpleasant —
`unwrap_or`, nested patterns, `map_err`, tuple inference — **have since landed**, each
with its measurement in the CLOSED notes above. **The verdict's diagnosis was acted on
and, unlike its predecessor, it held**: the four fixes cost 4, 116, 4 and 11 compiler
or library lines respectively, and none of them made the application longer. What this
file can now say with three data points instead of two is narrower and more useful than
either verdict: **converting a library to `Result` is charged at every call site; fixing
the ergonomics of `Result` is refunded at every call site.** The open list is no longer
about the error model at all.

## Earlier phases

- **Phase 1** — `spawn f(x)` as a bare statement is rejected with `a statement must be a declaration, assignment, or call -- a bare expression has no effect`, which never says the real rule: a task handle must be bound so the compiler can hang the implicit join on it.
- **Phase 1** — `parallel for i in 0..<N` runs only `min(N, tycho_ncpu())` iterations concurrently (`runtime/tycho_rt.c:843-852`); iterations chunked behind one that never returns never start, and nothing warns. `TYCHO_THREADS=2` silently cut a 4-worker server to 2.
- **Phase 1** — starting N workers has no direct spelling: task handles are affine and unstorable, so it is either N hand-written `spawn` lines or a recursive fan-out where each frame holds one handle.
- ~~**Phase 2** — `bytes` supports **only** `len()`, `to_str()`, and crossing the FFI: `a + b` is rejected (`arithmetic requires two ints or two floats (got bytes, bytes)`), `b[i]` is rejected (`can only index an array, a string, or a map`), `b[i:j]` is rejected (`can only slice an array, soa, or string`) — so every non-trivial `bytes` manipulation has to detour through `to_str`, do the work in `string`, and `to_bytes` back.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 7 — and the item's own diagnosis of *why* it was cheap was exactly right.** All three were **type-checking one-liners plus a widened predicate in codegen**: `bytes` is the same length-headered `char *` as `string` (`src/tychoc.c:548`), so `b[i]` is `tycho_str_get`, `b[i:j]` is `tycho_str_substr`, `a + b` is `tycho_str_concat` and `b + 'c'` is `tycho_str_concat_char` — **no new runtime function, no new C, 43 net lines in `src/tychoc.c` of which 19 are code**. `b[i]` returns **`int`**, the byte value, not a 1-length `bytes`: that is what a byte-classifying loop wants, it allocates nothing, and it keeps `b[i]` and `s[i]` (`:5155`) from meaning different things for one representation. The in-place accumulator was widened to `bytes` in the same pass (3 predicates), so `out = out + b[i:i+1]` in a loop is O(n), not O(n²). What the item could not know: the freeze bites a **fourth** time, and this time the blocked set was *enumerated* rather than assumed — closing the import graph from every `tychoc0` input reaches **13** corelib packages (`cli` `datetime` `http` `httpd` `io` `json` `markdown` `net` `path` `result` `sha256` `sort` `strings`) which may not use these operators, and leaves **24** free, including `base64` `compress` `crypto` `hash` `hex` `image` `md5` `raster` `tls` — the packages that would most want them. Recorded in `docs/spec/appendix-e-conformance.md`; `frontparity` still **288 / 0**.
- ~~**Phase 2** — the "arithmetic requires two ints or two floats" message for `bytes + bytes` suggests `to_float(x)`/`to_int(x)`, neither of which applies to a buffer; the useful advice would be "`bytes` has no operators — use `to_str` to concatenate".~~ **CLOSED, `docs/internals/plan-friction-DONE.md` phase 7.** `bytes + bytes` now compiles, so the message the item complained about is unreachable for that spelling; what remains is `bytes - bytes` and friends, and those get a 4-line arm that names the operator set instead of a numeric conversion: `bytes has no arithmetic (got bytes, bytes) -- bytes supports `a + b` and `b + 'c'` (concat), `b[i]` (the byte value, an int) and `b[i:j]` (a sub-buffer); for anything else use to_str(b)`. `bytes + int` and `bytes + "s"` get their own message (`cannot concatenate bytes with int -- to_bytes(x) to widen, or to_str(b) to work in strings`), because the fix there is a conversion and naming it is the whole job of the diagnostic.
- **Phase 2** — `string` is already fully byte-safe (interior `0x00` survives concat, index, slice, and `len`, measured), so `httpd`'s old header comment claiming an interior `0x00` truncates the body was **wrong**; the string/bytes split buys type-level intent and FFI shape, not binary safety the string model lacked.
- ~~**Phase 2** — `to_bytes("")` is the only spelling for an empty `bytes`; there is no `bytes` literal and no zero value, so every struct default and early return carries the call.~~ **REFUSED with the number, `docs/internals/plan-loops-cleanup-DONE.md` phase 5 — settled, not fixed, and the measurement is what settled it.** The premise that this costs anything at run time is **false**: `T_BYTES` lowers to `char *`, "the same length-headered buffer as string" (`src/tychoc.c:1373`), and `to_bytes` on a string is a **zero-cost reinterpret**, not a conversion (`:9187-9188`) — so `to_bytes("")` emits the same interned `""` a literal would, and the whole complaint is 11 characters of spelling at **10 sites in the entire tree** (4 in `corelib/test/`, 2 in `examples/corelib/result`, 1 each in `corelib/httpd/httpd.ty:142`, `corelib/image/image.ty:37`, `corelib/test/compress`, `server/main.ty`). Two candidate spellings measured against the current compiler: `b: bytes = ""` → `error: declared type bytes but value is string`; `b: bytes = []` → `error: cannot type a bare [] here -- no expected type`. The cheapest landing is a checking-mode arm in `resolve_exp` grounding a string *literal* against an expected `T_BYTES` — **~6 code lines**, the same shape as phase 3's 11-line `E_TUPLE` arm, needing no codegen and no runtime because the representation is already identical. Refused on three counts: it puts an **implicit string→bytes conversion into the type system** (a language change, ~25 lines of spec text in `04-inference.md` §6.1 / `03-types.md` / Appendix E, and a redesign this plan's Anti-scope forbids); **it cannot be used at the site this entry names** — "every struct default" is `corelib/httpd/httpd.ty:142`, and `core:httpd` is compiled by the frozen `compiler/tychoc0.ty` through `examples/webserver/run.sh` (fourth phase running that this constraint has bound); and a **real** `bytes` literal, byte-exact and `\xNN`-capable, is already costed at ~35 lines across 3 functions plus a new runtime entry point in the `\r` item above, where it belongs to the `bytes`-operators phase. **Net: 0 lines, 0 bytes of emitted code, one number written down.**
- **Phase 3** — no `ends_with` without importing `core:strings`, and a corelib package taking a dependency for one predicate is worse than the six-line `has_ext` helper it needs, so the helper gets rewritten per package.
- **Phase 4** — `core:net` had no way to bound a blocking read; `time.sleep_ms` cannot help because it cannot interrupt a `recv` already in progress. The idle timeout required a new shim call (`SO_RCVTIMEO`), which means "do not let a peer pin this worker" was not expressible in Tycho corelib until this commit.
- **Phase 4** — a socket read timeout is indistinguishable from EOF at the Tycho level (both yield empty `bytes`); fine for a server, but a client that needs to retry a timeout while giving up on an EOF cannot tell them apart.
- ~~**Phase 0** — six non-gated runners still build tychoc0 and compare against it (`examples/fetch`, `examples/sqlite`, `examples/webserver`, `examples/weblog`, `bench/run.sh`, `tools/prof/profile.sh`); none is in `make ci`, so none can redden, but each will drift as tychoc0 does.~~ **CLOSED, the Odin-loops plan's phase 1 (`1b93727`, 2026-07-29) — by retiring the legs, not by gating them.** A breaking loop-syntax change means the frozen `compiler/tychoc0.ty` can no longer parse the corpus, so every `tychoc0` leg in every runner was removed rather than fixed. Verified at HEAD: no `run.sh` in the tree builds `tychoc0`, and each header now records the retirement in the past tense — `examples/webserver/run.sh:36` ends `webserver: ok (tychoc == golden; the tychoc0 leg was retired 2026-07-29)`. **The item's worry is answered by deletion, and the cost is recorded in `CLAUDE.md`, "Two gates that used to be here": nothing replaces them, so a change that silently narrows what `src/tychoc.c` accepts no longer has a second implementation to disagree with it.** The item was right that the runners would drift; what it could not know is that the drift would be settled by removing the comparison rather than by protecting it.
- **Phase 0** — the harness scripts of the removed gates are still on disk unreferenced (`compiler/run.sh`, `compiler/fixpoint.sh`, `compiler/pkg-split.sh`, `scripts/frontparity.sh`, `tests/rtparity/`, `fuzz/run_pkg.py`, `fuzz/run_typeparity.py`, `run_parforparity.py`, `run_eqparity.py`, `run_unaryparity.py`); kept deliberately so the method behind the recorded self-hosting result stays readable.
- **Phase 0** — the 15 `tests/diag/*.h0err` tychoc0-diagnostic goldens are now orphaned; kept because three archived internals docs cite them.
- **Phase 0** — prepending a 50-line banner to `compiler/tychoc0.ty` invalidated every `:N` self-citation in its own comments (the citation gate only checks docs→source, not source→source), so the file now carries a "+50" correction note instead.
- ~~**Phase 0** — `docs/bootstrap.md` is cited by `compiler/tychoc0.ty`'s original header and by `Makefile`'s old `bootstrap` comment, but the file does not exist anywhere in the tree; the citation gate never caught it because it only validates Markdown-to-Markdown links.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 8 — the document is written, and the GATE now checks the direction that missed it.** Two corrections to the entry first: the `Makefile` no longer mentions `bootstrap` at all (`grep -c bootstrap Makefile` → `0`; phase 0 removed the target), and the live citations are **three**, not two — `compiler/tychoc0.ty:617`, `compiler/run.sh:3` and `compiler/fixpoint.sh`. Because `compiler/tychoc0.ty` is **frozen**, "remove the citations" was never available: the only way to make no live file cite a missing document was to write it. `docs/bootstrap.md` now names the stages those two script headers cite by number (Stage 1 = `compiler/run.sh`'s differential over the 51 `compiler/tests/` fixtures; Stages 2–3 = the A/B/C self-emission chain; Stage 4 = `fixpoint.sh`'s byte-identical `B == C`; Stage D = the package programs; Stage E = `pkg-split.sh`), states that **none of them is a gate any more**, and carries the two consequences that keep costing time — the freeze reaching 13 corelib packages, and tychoc0's own `:N` self-citations being off by −50. **The gate's general form: `scripts/check_citations.py` now checks SOURCE → DOC**, scanning every tracked non-Markdown file under a wider prefix set (including `Makefile`, `bench/`, `fuzz/`, `server/`) for `docs/<...>.md` mentions and requiring the document to exist, with line bounds when a `:N` is present. **Proven against the pre-fix state**: with `docs/bootstrap.md` moved aside it reports `NO SUCH DOCUMENT` for all three live citations by `path:line`. **And on its first run it found four more of the same bug** — `docs/memory-model.md`, `docs/ffi.md` and `docs/map-mutation.md` (twice) were cited by `bench/prongB/iter_transform.ty:10`, `corelib/crypto/crypto_shim.c:19`, `src/tychoc.c:4571` and `tests/map_mutation.ty:1` after all three documents moved into `docs/guides/`; all four repaired. Gate green at `22 anchored, 1549 bare, 76 source->doc`.

## Found by `docs/internals/plan-friction-DONE.md` phase 1's gate sweep, out of its scope

- ~~**`docs/internals/plan-loops-cleanup-DONE.md` phase 1** — `scripts/tools_check.sh`'s `bytes-rehome` lane has been **silently vacuous since `eefc609`** and is red at HEAD. Its inline fixture writes `d := io.read_bytes(p)` then `len(d)`, which stopped compiling when that commit gave `read_bytes` a `Result` return (`error: len(...) takes an array, a string, bytes, a map, or a soa`), and `scripts/tools_check.sh:273` discards the compile's exit status — so the lane guarding a real use-after-free (`copy_into` missing `T_BYTES`) reports its own breakage as `grep: .../brh/main.c: No such file or directory`. A gate that throws away an exit code cannot tell "the invariant broke" from "my fixture no longer compiles", and it chose the scarier of the two messages. Left unfixed on scope; `docs/internals/plan-loops-cleanup-DONE.md` phase 1b.~~ **CLOSED, `docs/internals/plan-loops-cleanup-DONE.md` phase 1b.** 10 lines of shell, no compiler change: the fixture is now `d := result.unwrap_or(io.read_bytes(p), to_bytes(""))` — the one-liner that phase 1 made spellable, so the un-rotting is the first *use* of that fix outside its own regression test — and the compile is an `if !` with its stderr captured, with a third branch that says `bytes-rehome FIXTURE STALE: it no longer compiles, so this lane asserts NOTHING`. Both halves were reddened deliberately and restored: dropping `case T_BYTES` from `copy_into` (`src/tychoc.c:8551`) gives `bytes field NOT re-homed -- copy_into missing T_BYTES (dangling UAF!)`, and re-injecting the stale spelling gives the STALE line with the real compiler error indented under it. **The general lesson, and it is not about `bytes`: a gate is two claims — "the invariant holds" and "I am still able to ask" — and discarding an exit status silently merges them.** This one spent three commits reporting a missing file, which reads like a broken script rather than a broken guard, so it was believed and skipped. Any lane whose fixture is a program must fail on the fixture failing to build, distinctly from the assertion failing; a green gate that cannot articulate what it checked is worth less than no gate, because it is trusted.
- ~~**`docs/internals/plan-loops-cleanup-DONE.md` phase 2** — **new language syntax can no longer be given a `tests/` fixture.** `compiler/fixpoint.sh` and `scripts/frontparity.sh` both feed every `tests/*.ty`, `tests/pkg/*/main.ty`, `examples/*.ty` and `tools/*.ty` to the frozen `tychoc0`, so a fixture exercising anything `tychoc0`'s frontend does not know reddens two runners at a compiler that must not be edited. Phase 2's `\r` escape and adjacent-literal join are therefore covered by `corelib/test/` and `server/` (golden-validated, but not by `tests/run.sh`) and the gap is written into `docs/spec/appendix-e-conformance.md`. Not a defect in either runner — a consequence of the freeze, and the first time it has cost a fixture rather than a gate. Whoever un-freezes or retires `tychoc0` should re-home those fixtures into `tests/`.~~ **CLOSED, the Odin-loops plan's phases 1 and 2 (`1b93727`, `f7da4b1`) — and the entry's own last sentence is what happened.** The freeze lanes were retired, and the interim `tests/postfreeze/` lane built to hold new-syntax fixtures was **folded back into `tests/`** in the next phase; `tests/postfreeze/` no longer exists. New syntax now gets an ordinary `tests/` fixture again: `tests/nested_pattern.ty` and `tests/result_tuple.ty` are the two this file's own items were denied, and `corelib/test/result/main.ty:15-28` records where they came home from. **The lesson the entry named is the one that held: the constraint was never a defect in a runner, so it could only be closed by changing what the runners are for.**
- **`docs/internals/plan-loops-cleanup-DONE.md` phase 5** — `corelib/net/net_shim.c` does not compile standalone under `-std=c11`: `getaddrinfo` and `struct addrinfo` are hidden by strict ISO mode without `_POSIX_C_SOURCE`/`_DEFAULT_SOURCE` (`resolve4`, `scripts/frontparity.sh`, 4 errors). Pre-existing, not phase 5's — a `git archive HEAD` copy fails identically — and invisible in practice because `tychoc` invokes plain `cc` (`src/tychoc.c:12403`), whose default is `gnu17`. It means a shim's portability claim cannot be checked with the same flags the repo checks `src/tychoc.c` with. Left unfixed on scope. **Postscript, 2026-07-30 — still open, and it is TWO shims, not one.** Re-measured by running `cc -std=c11 -c` over all 11 shims directly, since `scripts/frontparity.sh` (the route that found it) is no longer a gate: `corelib/net/net_shim.c` fails with the same 4 errors, and **`corelib/tls/tls_shim.c` fails with 9 of the same kind** from `corelib/tls/tls_shim.c:38` — the identical `getaddrinfo`/`struct addrinfo` cause, never noticed because `core:tls` was not in the failing run. Seven shims pass; `corelib/image/image_shim.c` fails only on a missing `png.h`, which is item 9's environmental skip and not this. **And the fix is already in the tree four times**, as an `#ifndef`/`#define` pair with its reason on the line: `corelib/io/io_shim.c:10-11`, `corelib/os/os_shim.c:9-10`, `corelib/datetime/datetime_shim.c:10-11`, `corelib/time/time_shim.c:22-23`. So "~1 line" was right per file and wrong about the file count, and the thing that kept it open — that no gate compiles a shim standalone — is now the *only* thing keeping it open. Open list item 1.
- **`docs/internals/plan-loops-cleanup-DONE.md` phase 2** — the same freeze is why `httpd.crlf()` and `tools/lsp.ty:256`'s `"" + '\r' + '\n'` survive the item that made them unnecessary: `core:httpd` is imported by `examples/webserver/main.ty`, which `examples/webserver/run.sh:20-27` builds with `tychoc0` and requires to match byte for byte. **A frozen compiler in a comparison gate freezes the source it reads, not just itself** — the corelib is now, in effect, written in the intersection of two languages, and nothing in the tree said so before this line. **Postscript, 2026-07-30: the reason expired and the workarounds did not.** The freeze lanes were retired 2026-07-29, so the corelib is written in one language again — but `httpd.crlf()` is still defined (`corelib/httpd/httpd.ty:110`) with **4 call sites in its own package**, `tools/lsp.ty:260` still returns `"" + '\r' + '\n'`, and both still carry present-tense comments explaining that the frozen compiler forbids the literal. They survive now only because nobody swept them. **That is the generalisation this entry was missing: a workaround outlives its reason by default, because nothing goes red when the reason dies.** Folded into the open list as item 5 (the comments) — the code itself is a smaller, separate sweep.
- **`docs/internals/plan-friction-DONE.md` phase 8** — `docs/bootstrap.md` (written in that phase) is **not linked from `docs/README.md`**; `scripts/check_links.sh` checks that links resolve, not that documents are reachable, so an orphan document is invisible to every gate. Left unfixed on scope; a docs-index pass should list it.
- **`docs/internals/plan-friction-DONE.md` phase 1** — a 17-line growth in `src/tychoc.c` staled **11 anchored `path:line@token` citations** into it, across `docs/spec/15-program.md` and two internals docs. This is the citation gate working exactly as designed (it named every one, with the line the token actually moved to), and it is the argument for a compiler phase running `make check-links` even when it changed no Markdown: the docs cite the compiler by line, so *every* patch to `src/tychoc.c` is a documentation change.
- **`docs/internals/plan-loops-cleanup-DONE.md` phase 10** — **two in-tree comments still assert that the language has no nested patterns**, three phases after `docs/internals/plan-loops-cleanup-DONE.md` phase 3 added them: `corelib/net/net.ty:20-21` ("Tycho has no nested patterns -- `Err(net.Timeout)` does not parse -- so `==` is the only way") and `examples/corelib/httpd/main.ty:54-55` ("`Err(httpd.Malformed)` cannot be a match arm (Tycho has no nested patterns)"). Phase 3's evidence lists the files it swept — `httpd.ty`, `result.ty`, `io.ty`, `docs/guides/corelib.md` — and `core:net` was not among them. `corelib/test/io/main.ty:43` gets it right ("`compiler/tychoc0.ty`, whose grammar still has no nested pattern"), which is the distinction both stale comments miss: the *language* has them, the *frozen compiler that reads the corelib* does not. `examples/corelib/httpd` is one of the two files phase 8 excluded from `frontparity` **because** it is outside the freeze, so there the comment is not just mis-attributed — the nested arm would compile. Left unfixed on scope. **Postscript, 2026-07-30 — still open, now three files, and the distinction this entry drew has itself expired.** Both named comments survive verbatim (`corelib/net/net.ty:20`, `examples/corelib/httpd/main.ty:55`) and a **third** was found here: `corelib/result/result.ty:29-31`, which does not merely mis-attribute but instructs — "this package is compiled by the frozen `compiler/tychoc0.ty` … so nothing in `corelib/` may use one". Since the freeze lanes were retired 2026-07-29 that sentence is false in *both* halves, so the careful language/frozen-compiler distinction `corelib/test/io/main.ty:43` was praised for is no longer a distinction at all — there is one compiler, and it has nested patterns. This entry's original grep missed nothing; the two comments simply wrap the phrase across a line break, which is why a later search for `no nested patterns` finds neither. Widened and re-costed as open list item 5, which folds in three more sites of the same shape (`corelib/httpd/httpd.ty:100-109`, `corelib/httpd/httpd.ty:281-289`, `tools/lsp.ty:259`).
- **`docs/internals/plan-loops-cleanup-DONE.md` phase 10** — **this file's own `path:line` citations drift silently, and no gate can see it.** 30 of the 71 spot-checked in the CLOSED notes no longer point at what they name; the worst are into `src/tychoc.c`, where every compiler phase shifts everything below it — the literal-interning emit site cited by the `\r` item as `src/tychoc.c:8671` is now `src/tychoc.c:9455`, `copy_into`'s `T_BYTES` case cited by phase 1b as `src/tychoc.c:8217` is now `src/tychoc.c:8551`, and `instantiate_generic` cited by the qualified-name item as `src/tychoc.c:6895` is now `src/tychoc.c:7482`. Every closure is still *true*; the coordinates are not — and the three `as` values above are now the pre-repair record, `docs/internals/plan-loops-cleanup-DONE.md` phase 6 having repointed this file's live citations to today's lines. `scripts/check_citations.py` cannot catch it by construction: it verifies the 22 **anchored** `path:line@token` citations against the token and only bounds-checks the 1646 **bare** ones, and every citation in this file is bare. The fix is a mechanical pass to anchored form, after which the gate polices them — and the reason it matters is that this file is the place a future reader goes to find out why something is the way it is. Left unfixed on scope. **Postscript, 2026-07-30 — re-measured, and the repair phase 6 performed has already been undone by sixty commits.** Fifteen citations opened and checked at `afa67da`: **11 are wrong again**, every one of them into `src/tychoc.c`, and the four that survived are all into files that barely moved. So the repair-in-place strategy has now been tried once and measured to last about four days of active work on the compiler. The gate's blind spot is unchanged and structural — bounds-checking a bare ref into a 12k-line file can never fail. **A second dimension of the same defect was found here and repaired a different way:** 51 "`plan.md` phase N" references in this file now name an unrelated plan, because both plans they meant were archived; that was fixed with one definitional note at the top rather than 51 rewrites, on the reasoning that a rewrite would go stale at the next archive and a definition will not. **The two together make the case: repointing is not the fix, re-anchoring is** — and where a stable name exists, use it instead of a coordinate. Open list item 10.
- **`docs/internals/plan-loops-cleanup-DONE.md` phase 17 — the bare `src/tychoc.c:N` citation population, retired here by decision rather than swept.** Re-derived at `b5c8406`: **1457** refs name `src/tychoc.c` — 660 inside the frozen `docs/internals/plan-*-DONE.md` archives, 797 live. Of the whole population **139 are anchored `path:N@token` and every one of them is correct** (checked by re-running the anchor test over all of them: 0 mismatches), so the anchored half is not the problem and a sweep would not move it. The other **1318 are bare**, which the gate checks for bounds only — and bounds is exactly the property a drifted citation keeps, because `src/tychoc.c` is 12774 lines and almost any stale number is still *inside* it. **Two classes were considered for repointing and both were deliberately refused.** (1) The **127 refs in dated design records** — 90 in non-archived `docs/internals/*.md`, 37 in `docs/rfc/*.md`, led by `generics-stage2-body-cloning.md` (52), `generics-gap-fixes-plan.md` (44) and `ffi-threading-design-review.md` (26). A study that dates itself in its own filename is a photograph of the tree on that day; repointing its citations yields a document whose prose is dated and whose coordinates are current, and nothing tells the reader the two halves disagree. A stale ref in a dated record is legible; a fresh one is a lie the reader cannot detect. (2) The refs inside `plan.md`'s **completed-phase evidence blocks** — renumbering these makes a phase claim it verified something it never looked at, which is the same rule phase 4 settled for the archives. **What would actually fix the bare population is not a sweep but a conversion**: each ref re-read against the line it names and rewritten anchored, after which the gate polices it forever. That is 1318 hand-verified citations. Batch 10's phase-44 work is a 42-ref instance of exactly that job and it took a batch. Recorded, not actioned — and note this is the same defect as the entry above it, counted across the whole tree instead of this file.

## The signal plan, 2026-07-31 (head `5428fa1`) — closed for one case, narrowed for the rest

`server/main.ty:646` prints `tycho-httpd: stopped after N requests` and was
**unreachable**. Nothing in the tree installed a `SIGTERM` or `SIGINT` handler, because
Tycho had no signal surface at all, and `server/run.sh` asserted wait status **143** — it
asserted the *absence* of clean shutdown and called that a passing gate. `core:signal`
closes that. The honest score is that it closes the **shutdown case**, not signal
handling, and the difference is deliberate rather than unfinished.

- **CLOSED — a Tycho program can shut down cleanly on `SIGTERM`/`SIGINT`.**
  `signal.on_shutdown(fd)` (`corelib/signal/signal.ty:60@on_shutdown`) installs one
  handler for both signals whose only action is `shutdown(fd, SHUT_RDWR)` on the
  listening socket. **One call arms an entire worker pool**
  (`server/main.ty:635@on_shutdown`) because the handler is per-process and acts on the
  shared listener, so which thread the kernel delivers to never matters. **No new control
  flow was needed anywhere**: every thread blocked in `accept` gets `Err`, the wind-down
  arm that already existed (`server/main.ty:494-495`) retires each loop, and the count
  line prints. Measured at `--workers 4`, four connections held so all four loops were
  provably busy: exit status **0**, the line, `/proc/<pid>` gone, shutdown in **1 ms**.
  The gate now asserts the clean exit it used to assert the absence of, plus `SIGINT`,
  plus `SIGKILL` as the control — 52 assertions to 57.
- **STILL OPEN, narrowed — there is no general handler, and "narrow" is the design and
  not the backlog.** `signal.on(sig, handler)` was refused, with the reason written into
  the package header (`corelib/signal/signal.ty:19-31`): calling a Tycho function from
  handler context is a *language* feature, because every Tycho value lives in a
  bump-allocated arena that is not re-entrant and channel operations park behind a mutex
  (`runtime/tycho_rt.c:657@mu`) — a handler that interrupts the allocator or the lock
  holder and then allocates or touches a channel deadlocks or corrupts the process it was
  invoked to shut down. **What a general version would need, so the next person costs it
  rather than rediscovers it:** (1) an async-signal-safe hand-off out of handler context —
  a self-pipe or `signalfd` read by a dedicated thread — so the callback runs on an
  ordinary stack; (2) the handler itself still held to a `sig_atomic_t` store, unchanged
  from what ships; (3) a `pthread_sigmask` policy, since `pthread_create` is called with a
  NULL attribute and there is no mask anywhere in the tree, so "an arbitrary worker runs
  your callback" is not a contract anyone can code against; (4) the re-entrancy clause in
  the spec. None of it is needed to shut a server down, and the tree has exactly one
  caller.
- **NEW, measured — clean shutdown is not *prompt* shutdown, and `--idle-ms` is what
  bounds it.** A worker can only notice a shutdown between requests: one parked in
  `serve_conn` on an idle keep-alive connection waits out `SO_RCVTIMEO` first. At
  `--workers 4 --idle-ms 5000`, 0 or 1 held connections shut down in **1 ms** and four
  held connections take **5141 ms** — exit 0 with the line in every case, so bounded and
  correct rather than hung. The 1-connection case is fast only by luck of routing: this
  kernel delivered `SIGTERM` to the main thread, which is accept loop 1, which was the
  busy one, so `EINTR` released it directly. The fix already has its API and no caller —
  `signal.shutdown_requested()` (`corelib/signal/signal.ty:64@shutdown_requested`) exists
  for exactly this. Filed as `plan.md` phase 15.
- **NEW, small — the mechanism that works is a Linux behaviour, not a POSIX guarantee.**
  `shutdown()` waking a thread blocked in `accept(2)` on a *listening* socket is not
  specified, and backlogged connections are dropped rather than drained. It was chosen by
  measurement over three alternatives, not by preference: `close(fd)` released 1 loop of 4
  and then handed the descriptor number back out to a later `open()` while three threads
  were still blocked on it, and a `poll`-before-`accept` gate hung 2 of 4 under live
  traffic (a thundering herd past the readiness check). Recorded in
  `docs/spec/18-library.md` §32.27 as a property a second implementation must
  re-establish rather than assume.
- **What went right, and it is the same shape as the concurrency re-score above: the
  language and the build were not in the way.** A new corelib package needs **zero**
  build wiring — `corelib/run.sh` globs `corelib/test/*/main.ty` and picks the shim up by
  path, and `tychoc` discovers `corelib/<pkg>/<pkg>_shim.c` the same way — so there is no
  registry to join, and `make -s corelib` scored the new package on its first run. The
  whole surface is 65 lines of Tycho over 126 of C, and the only thing that cost real
  effort was the *measurement* that picked the mechanism.
