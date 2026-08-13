# FRICTION

One line per moment the language got in the way while writing a real web server.
Non-blocking by construction: anything that blocks the server earns a phase in
`plan.md` instead. This file is a deliverable; fixing everything in it is not.

The program these notes came from is `server/` — `tycho-httpd`, ~440 lines,
serving `server/www` to a real browser. Everything below was hit while writing
it, not imagined about it.

> **Plan references, removed 2026-08.** This file used to point every closure at
> the plan that closed it (`docs/internals/plan-*-DONE.md`). Those archives were
> pruned; the pointers now say "the X plan" with no phase numbers. The full
> archives remain at `git show docs-archive:docs/internals/plan-*-DONE.md`.

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
> distinguishes `Eof` / `Timeout` / `Failed` (the option-result plan). What that phase
> *measured* is that the win is confined to the ambiguous sentinels — converting
> an unambiguous one is line-for-line neutral — and that the conversion surfaced
> five new ergonomic gaps of its own, recorded below.
>
> **Also historical, from the option-result plan:** the `io.read_bytes` and
> `httpd.parse_request` rows. `read_bytes` returns `Result(bytes, io.IoErr)` —
> an empty file is `Ok`, `Err(NotFound)` and `Err(IsDir)` are distinct — and
> `parse_request` / `read_request` return `Result(Request, httpd.ReqErr)` with
> `Malformed` / `Closed` / `Timeout` / `Failed`, all four recorded as distinct in
> `corelib/test/httpd.out`. The missing `unwrap_or` is now `core:result`. The rows
> that remain true are `path.safe_join`, the `io` write side, `net.udp_*` and
> `net.set_read_timeout_ms` — each deliberately left on a sentinel that has
> exactly one meaning.
>
> **Closed out, from the option-result plan.** The headline's own worked example — the
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
> **Closed out, from the option-result plan (added on a user directive after phase 3
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
> **Correction from the option-result plan.** "Exactly where it started" was true for four
> phases and is no longer: phase 5 replaced the startup `--root` check's one wrong
> message with four accurate ones and `server/main.ty` ended at **380** code lines,
> +9. The plan's own final number is a *growth*, not a wash.

## The score against this file, re-scored against the tree, 2026-07-26

Two plans have now been run *because* of this file: the `Option`/`Result` plan
(archived: the option-result plan) and
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
>
> **Still not it, 2026-07-31 (head `9e8f8f2`).** That open list has since been re-scored
> again, in place, and stands at **11 items**. It is the section titled "The real remaining
> debt — the open list, re-scored in place", immediately below; its own header carries the
> current count and the current head.

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

### The real remaining debt — the open list, re-scored in place

**Re-scored 2026-07-31 against `9e8f8f2` — 11 open items.** 43 commits since the
previous pass: `core:signal`, `io.mtime` / `io.read_at` / `io.size`,
`Last-Modified` / `304`, `Range` / `206` / `416`, bounded worker retry, and a
citation gate that grew four directions, ambiguous-anchor rejection and a
`path@SYMBOL` form. **One numbered item closed** (item 7, the `parallel for`
fan-out — its spec defect was corrected and its undocumented ceiling documented),
**one grew** (item 1 is now three shims, because the plan that shipped
`core:signal` added a fourth instance of the same defect), and **two were added**
(items 11 and 12). Four unnumbered items elsewhere in this file closed as well;
they are struck in place in their own sections.

**The numbering is frozen on purpose.** Sections below this one address items by
number ("item 7", "Open list item 5"), so a closed item is struck where it stands
and new ones are appended. The count above is the count of *unstruck* entries.

The paragraphs below are the previous pass's, kept as its record:

> **Re-scored 2026-07-30 against `afa67da` — 10 open items.**
> Every item below was **re-verified against the tree at `afa67da`** before it was
> scored: the compiler was run by hand on the shape the item describes, or the cited
> source was opened, or the gate/shim was invoked directly. **All ten reproduce.**
> Nothing on the previous list turned out to be already closed — but four of them are
> *bigger* or *cheaper* than the previous list said, and every citation on the list has
> been re-derived, because the old ones had drifted.

> Two items that were **not** on the numbered list did close, both by the same event —
> the `tychoc0` freeze lanes were retired 2026-07-29 — and both are struck through in
> place below (the six non-gated `tychoc0` runners, and "new language syntax can no
> longer be given a `tests/` fixture").
>
> The old list addressed its items by bare `:N` line numbers into this file. Those are
> gone: a bare `:N` binds to the previously named path (`CLAUDE.md`, "Citations"), so
> they were never self-references at all, and they went stale on every edit. Items are
> named by their section instead.

The 2026-07-30 list was ordered so the top was what to pick up first, and that
order is preserved. **The two appended items are not last in priority just
because they are last in the list** — item 11 is the cheapest thing here. The
pick-up order is written out in full under "What moved this pass" below.

1. ~~**Three shims do not compile under `-std=c11`** (*Found by phase 1's gate sweep*)~~ —
   **CLOSED 2026-07-31 at `ada7893` by the shim-gate phase, gate first.**
   `scripts/shim_check.sh` /
   `make shim-check` / `scripts/ci.sh` step `[3d/13]` compiles every
   `corelib/*/*_shim.c` standalone under `-std=c11`, and the three failures were fixed
   by copying `corelib/io/io_shim.c`'s `_DEFAULT_SOURCE` block. The gate was **proven
   to redden** by reverting the `signal` fix. Two things the item got wrong, both found
   by measuring: `corelib/image/image_shim.c` was never a defect (its one error is a
   missing `png.h`, item 9), and a first version of the gate that kept `-pthread` was
   silently green on the unfixed shim — `-pthread` defines `_REENTRANT`, which glibc
   turns back into `_POSIX_C_SOURCE=199506L`. Phase 1's evidence block has the numbers.
   The record of what the item said when open follows.
   **still the cheapest thing on this list, and it is now three files. It was one, then
   two, then three, and the growth is the item.**
   Re-measured at HEAD by invoking `cc -std=c11 -c` directly on all **12** shims
   (there were 11 at the previous pass): **eight pass**, three fail, and
   `corelib/image/image_shim.c`'s single "error" is only a missing `png.h`
   (environmental, item 9 — the previous pass wrote "item 6" here, which was wrong).
   - `corelib/net/net_shim.c` — **4 errors** (`corelib/net/net_shim.c:84` storage size
     of `hints`, `corelib/net/net_shim.c:88` implicit `getaddrinfo`,
     `corelib/net/net_shim.c:89` invalid use of undefined `struct addrinfo`,
     `corelib/net/net_shim.c:90` implicit `freeaddrinfo`).
   - `corelib/tls/tls_shim.c` — **9 errors** of the same kind from
     `corelib/tls/tls_shim.c:38`.
   - **NEW at this pass — `corelib/signal/signal_shim.c`, 3 errors**: storage size of
     `sa` (`corelib/signal/signal_shim.c:201`), implicit `sigemptyset`
     (`corelib/signal/signal_shim.c:204`), implicit `sigaction`
     (`corelib/signal/signal_shim.c:206`). `struct sigaction` and its two helpers are
     POSIX, not ISO C, so this is the identical cause with a different header.
   **This is the finding, and it outranks the fix.** The signal plan wrote a brand-new
   shim, in a tree where this item had been open and named for two re-scores, and
   reproduced the defect — because **no gate compiles a shim standalone**, so nothing
   could tell it. The item is no longer "two files want three lines"; it is "every new
   shim will want them, and the tree has no way to notice". A gate that runs
   `cc -std=c11 -fsyntax-only` over `corelib/*/*_shim.c` would cost about as much as
   `scripts/entrypoints.sh` and would close the class rather than the instances.
   **The fix per file is already in the tree four times** — an `#ifndef`/`#define` pair
   with a one-line reason at `corelib/io/io_shim.c:10-11` (`_DEFAULT_SOURCE`),
   `corelib/os/os_shim.c:9-10`, `corelib/datetime/datetime_shim.c:10-11` and
   `corelib/time/time_shim.c:22-23` (`_POSIX_C_SOURCE 200809L`) — so it is **3 lines
   copied into each of 3 files**, plus the gate. Left on scope four times now.
2. ~~**`spawn f(x)` as a bare statement**~~ — **CLOSED 2026-07-31: the parser now names
   the rule.** `parse_stmt` tests `E_SPAWN` ahead of the generic bare-expression
   `die_at` and says `a `spawn` must be bound to a task handle -- write
   `t := spawn f(args)`, because the implicit join at scope exit needs a handle to wait
   on (a Task cannot be discarded)`. Measured before and after on the same scratch
   program; **no `tests/diag/` fixture pinned the old text** (grepped `tests/` for it and
   found the string only in `docs/internals/`), so the corpus count is unchanged at 560.
   The record of what the item said when open follows.
   (*Earlier phases*) — reproduced verbatim again at
   `9e8f8f2` with a scratch program: `spawn work(1)` gives `error: a statement must be a
   declaration, assignment, or call -- a bare expression has no effect`
   (`src/tychoc.c:3774`, unmoved since the previous pass), which still never states the
   real rule — a task handle must be *bound* so the compiler can hang the implicit join on
   it. **One line of diagnostic text at a known line.** Open only because nobody has spent
   it, through two re-scores.
3. ~~**`docs/bootstrap.md` is not reachable from `docs/README.md`**~~ — **CLOSED
   2026-07-31: `docs/README.md` now lists it** under "How the docs are organized", beside
   `architecture.md`. The real question behind it — nothing checks that a document is
   *reachable*, only that its links resolve — is **not** closed and is filed as item 13
   below. A seventh site of item 5's class was found in the same file while adding the
   link and is recorded there. The record of what the item said when open follows.
   (*Found by phase 1's gate
   sweep*) — re-verified at the time: `grep -c bootstrap docs/README.md` → `0`. Sharper than the old
   entry: the index deliberately points at *directories* (`docs/reference/`, `docs/guides/`,
   `docs/spec/`, `docs/internals/`, `docs/rfc/`), so almost every unlisted file is covered
   by its directory — and `docs/bootstrap.md` is **the only top-level `docs/*.md` no index
   entry reaches**, the other five (`docs/architecture.md`, `docs/from-c-to-arenas.md`,
   `docs/thesis.md`, `docs/tutorial.md`, `docs/README.md` itself) all being named. **One
   link**, plus the real question behind it: `scripts/check_links.sh` checks that links
   *resolve*, not that documents are *reachable*, so an orphan is invisible to every gate.
   Three files under `docs/internals/` are additionally mentioned by no Markdown at all.
4. **The `send` collision** (*Phase 7*) — reproduced again at `9e8f8f2` with two scratch
   programs, and every citation on this entry re-checked and still correct.
   `fn send(a: int, b: int) -> int` compiles silently and dies at the *call* with
   `error: send(ch, v) takes a channel, got int`, while `fn die(s: string) -> int` is
   rejected at the *definition* with `error: 'die' is already defined`. **The reason is
   pinned:** the definition-time duplicate check is
   `if (sig_find(pr->name) || consts_find(pr->name)) die_dup_proc(...)`
   (`src/tychoc.c:8244`), and `sig_find` searches `g_sigs` — which holds `die` and `exit`
   as real entries (`src/tychoc.c:4743-4744`, inside `src/tychoc.c@register_builtins`)
   but **holds no entry for `send`, `recv` or `close` at all**; those three are recognised
   ad hoc during resolution (`src/tychoc.c:5871`, `src/tychoc.c:5880`,
   `src/tychoc.c:5899`). So it is not a table that omits three rows, it is three builtins
   that were never in the table. **The code is ~1 line** at `src/tychoc.c:8244`; the open
   part is the decision — which builtin names are shadowable — because landing it newly
   rejects any program defining `send`/`recv`/`close`. Note the generic path a line above
   (`src/tychoc.c:8238`) consults the same two tables plus `generic_find`, so whatever is
   decided has to be written twice.

   **DECISION TAKEN 2026-08-12 — none of them is shadowable.** The repo owner
   resolved the open half in favour of the compiler, and `docs/spec/01-lexical.md`
   §3.7 was rewritten to match: a builtin name may not be shadowed, the builtin is
   selected at every unqualified call, and a same-named procedure is unreachable by
   that name. The spec had said the opposite ("**shadows** that builtin … a
   conforming implementation must not change which procedure is *selected*"), which
   is what kept this item open — it was never a compiler bug, it was the spec
   describing a language nobody had built. Re-probed at `77bd826` over every name in
   `src/tychoc.c@shadows_builtin`: 26 are rejected at the declaration
   (`'X' is already defined`), the other 28 declare with a warning and are then
   overtaken by the builtin at the call site. Which of those two the caller sees
   turns out to depend on the **arguments**, not the name — `to_u8("abcd")` draws the
   builtin's arg-check while `to_u8(5)` silently returns the builtin's answer with
   the local body never entered — so the earlier per-name grouping of this behaviour
   was an artefact of the probe's arguments. **Still open, and now purely cosmetic:**
   making the ~28 accepted names a hard error like the other 26. Not done here
   because it would newly reject `corelib/json/json.ty@keys`, which is public API
   (`json.keys(j)`, still legal — a package qualifier reaches the procedure). The
   warning at `src/tychoc.c@shadows_builtin` now names the real outcome and the fix.
5. ~~**Stale in-tree comments asserting constraints that the freeze retirement killed**~~
   — **CLOSED 2026-07-31: all six rewritten, and a seventh found.** Each now states what
   the old reason was, that it expired on 2026-07-29, and what (if anything) survives as
   the real reason — the form `corelib/test/io/main.ty` already modelled.
   `corelib/net/net.ty@NetErr`'s header and `examples/corelib/httpd/main.ty` now say
   nested patterns DO parse (since 2026-07-26) and that `==` is the one-line spelling,
   not the only one; `corelib/result/result.ty` retires "nothing in `corelib/` may use
   one"; `corelib/httpd/httpd.ty@crlf` keeps `crlf()` for compatibility rather than for
   tychoc0's lexer; `corelib/httpd/httpd.ty@read_request_capped` keeps its typed local as
   a style choice; `tools/lsp.ty@crlf` drops the retired `scripts/frontparity.sh` reason.
   **One claim was disproved while rewriting it**: `corelib/test/result/main.ty` says
   rewriting `read_request_capped`'s tail to `return (Err(why), buf)` "still fails". It
   does not — patched in and compiled `corelib/test/httpd/main.ty` clean, then reverted.
   That is filed as item 14. The **seventh** site is `docs/README.md`'s Contributing
   paragraph, which still promised "every language feature must work in *both* compilers,
   or the fixpoint goes red" while the `CONTRIBUTING.md` it points at already said those
   gates are gone. The record of what the item said when open follows.
   (*phase 10, widened at the previous pass*) — **all six sites re-read at `9e8f8f2` and
   all six survive verbatim.** Two of the six citations had drifted and are re-derived
   here; the entry carries two *different* false claims:
   - "the language has no nested patterns" — `corelib/net/net.ty:20-21`
     ("Tycho has no nested patterns -- `Err(net.Timeout)` does not parse"),
     `examples/corelib/httpd/main.ty:55`, and `corelib/result/result.ty:29-31`, which goes
     further and tells the reader "nothing in `corelib/` may use one".
   - "this package is compiled by the FROZEN `tychoc0`, so it must not use X" —
     `corelib/httpd/httpd.ty:101-109` (why `corelib/httpd/httpd.ty@crlf` must stay;
     was cited `:100-109`), `corelib/httpd/httpd.ty:312-320` (why the `out` local must
     stay; **was cited `:281-289`, a 31-line drift**), `tools/lsp.ty:277-279` (why
     `"" + '\r' + '\n'` must stay). **The freeze lanes were retired 2026-07-29**, so
     every one of these states a live constraint that no longer exists — each run.sh header
     in the tree has already been corrected to the past tense, and these six were missed.
   `corelib/test/io/main.ty:51-52` and `corelib/test/result/main.ty:126-130` show the
   corrected form, the second one naming the *real* surviving constraint
   (`docs/spec/appendix-e-conformance.md` §E.2.1) instead of the expired one — which is
   the model to copy, because at least one of the six sites does still have a reason to
   stay — just not this one. **~15 lines of comment across 6 files**, and worth doing
   before someone reads one
   of them as a reason not to write the obvious thing.
6. **`ends_with` needs `core:strings`** (*Earlier phases*) — still true at `9e8f8f2`:
   `corelib/strings/strings.ty@ends_with` exists and `core:httpd` still hand-rolls its own
   `corelib/httpd/httpd.ty@has_ext` rather than import the package for one predicate.
   `core:httpd`'s imports are `core:net` and `core:result` and nothing else
   (`corelib/httpd/httpd.ty:42-43`), which is the shape the decision is about.
   **Not lines — a corelib layering decision** about whether a leaf package may depend on
   `core:strings`. Note the precedent that has since landed: `core:io` *dropped* a
   dependency (`core:path`) when a syscall made it unnecessary, so the tree's current
   direction is fewer inter-package edges, not more. (The old entry cited `has_ext` at
   `corelib/httpd/httpd.ty:387`; it is 40 lines further down now, which is why this
   citation is a `path@SYMBOL` — the definition has a name and its line number was only
   ever a record of how much prose sat above it.)
7. ~~**`parallel for` caps concurrency at `min(N, ncpu)` and nothing warns** (*Earlier
   phases*)~~ — **CLOSED 2026-07-31 at `9e8f8f2`, by the spec catching up with the
   compiler.** Both live halves are answered in `docs/spec/`, verified by reading it:
   - **The 64-chunk ceiling is documented.** `docs/spec/13-concurrency.md:81-83` now
     reads "the reference implementation uses `min(ncpu(), N)` chunks, MAY expose an
     override (`TYCHO_THREADS`), and MAY impose a fixed upper bound on the chunk count —
     the reference bounds it at **64**, so above 64 the fan-out is narrower than `ncpu()`
     reports". The old text's "uses `ncpu()` chunks" — false on both counts, the `min`
     and the cap — is gone. The compiler side is cited anchored from the spec's own
     provenance block, `src/tychoc.c:11199@_pk > 64`, so the gate now polices it.
   - **`ncpu()`'s false definition is corrected**, which was the other half:
     `docs/spec/16-builtins.md:251` states outright that it is "the *requested* worker
     count, **not** the width a `parallel for` will actually use" and that "a program that
     sizes a buffer or a work split from `ncpu()` MUST NOT assume that many chunks run".
   - **The warning half stays uncosted and that is now a settled non-item**, not a gap:
     `N` is a runtime expression, so there is nothing static to warn from, and
     the prunner plan measured that the cap limits width
     without starving — 200 items came back 200 at every setting.
   **What is left is item 8**, which already carries it: the one thing the spec fix does
   not give anyone is a way for a program to *choose* the width. The record of this item,
   as it stood, follows — the numbers in it are measurements of past trees and stay.
   - **Reproduced live**, not merely re-read. Four iterations of an equal
   fixed workload: `TYCHO_THREADS=4` → **222 ms**, `TYCHO_THREADS=2` → **433 ms**,
   `TYCHO_THREADS=1` → **853 ms**. All N iterations *do* run (the reduction total is
   identical at every width); what is capped is how many run *at once*, so an iteration
   chunked behind one that never returns never starts. Three things the old entry did not
   know:
   - The behaviour is now **specified**, which it was not: "the iteration space is split
     into chunks; the reference implementation uses `ncpu()` chunks and MAY expose an
     override (`TYCHO_THREADS`)". *(That is the pre-fix wording of §22, quoted as it stood
     on 2026-07-30; `docs/spec/13-concurrency.md:81-83` is the corrected text and no
     longer says this.)*
   - The width is now **readable from Tycho**: `ncpu()` is a registered builtin
     (`src/tychoc.c:5171@ncpu`, lowering at `src/tychoc.c:10224@tycho_ncpu`), so a program can at least
     ask. Measured on this box: `ncpu()` → 16.
   - There was an **undocumented hard ceiling of 64 chunks** — `if (_pk < 1) _pk = 1; if
     (_pk > 64) _pk = 64;` (`src/tychoc.c:10501`, inside `src/tychoc.c@gen_parfor`) —
     which `docs/spec/13-concurrency.md` did not mention, so on a box with more than 64
     CPUs the spec's "uses `ncpu()` chunks" was false. **That half is a ~1-line spec fix
     and should be split out and taken** — *it was, and closing it is what closed this
     item.* The warning half remains uncosted, because `N` is a runtime expression and
     there is nothing static to warn about. Runtime detail at
     `runtime/tycho_rt.c@tycho_ncpu`.
   - **Confirmed and bounded by measurement, 2026-07-31** (the prunner plan, a real
     `parallel for` program rather than the server that never used one). The 64 is exact
     and it was forced, at K=200 jobs of 50 ms each: `TYCHO_THREADS=32` → `maxconc=32`,
     `=64` → `maxconc=64`, `=100` → `maxconc=64` with `ncpu()` reporting 100. **The cap
     limits width; it does not starve** — 200 items came back 200, unique, at every
     setting, which is the half the entry left open ("an iteration chunked behind one that
     never returns never starts" is true only for a body that can fail to return, and a
     terminating body is never at risk). At the real workload — 560 jobs, `ncpu()` = 16 —
     the cap is not in the picture at all. So the live half of this item is now just the
     undocumented ceiling and `ncpu()`'s own false definition above it, split out as a
     carried-forward phase. *(It was taken, and both halves are the CLOSED note at the
     head of this item.)*
8. **No direct spelling for N workers** (*Earlier phases*) — reproduced again at
   `9e8f8f2` with a scratch program: `hs := [spawn work(1), spawn work(2)]` is refused with
   `tychoc: a task handle cannot be stored in a container or aggregate -- wait(t) first`
   (`src/tychoc.c@task_container_err`, fail-closed at the type-intern choke points so a
   task cannot escape and be waited twice or never). `server/main.ty@worker` still pays the
   recursive fan-out — worker k spawns worker k+1 into a frame-local, then runs its own
   accept loop. **An array of handles is a type-system change, not an item-sized fix.
   Uncosted, and still the honest core of what is left.** *(This entry cited
   `src/tychoc.c:701` and `server/main.ty:499-501` at the previous pass; the first drifted
   by one line and the second by 440, because `server/main.ty` roughly doubled — 1088 lines
   now. Both are `path@SYMBOL` here, which is why they will not drift again.)*
   **NARROWED, 2026-07-31, and the item reads stronger than it is** (the prunner plan).
   The first program in this tree to actually run a worker pool started **16 workers in one
   line and stored no handle**: `parallel for` is a direct spelling for N workers, and
   `parallel for x in ch:` — specified at `docs/spec/13-concurrency.md:99-100`, worked at
   `docs/guides/concurrency.md:86-112`, fixtured at `tests/conc/parfor_chan.ty:16` — is a
   direct spelling for a *bounded pool over a queue*, the exact shape this item says has
   none. So the premise "it is either N hand-written `spawn` lines or a recursive fan-out"
   is **false for N = ncpu**, which is the N most programs want, and an array of task
   handles is not what a worker pool needs. **What survives is one sentence and it is
   sharper than the original:** the program cannot choose N. `ParallelFor`
   (`docs/spec/02-grammar.md:248-249`) has no width slot in the grammar, so the only knob
   is `TYCHO_THREADS`, read once per process in `runtime/tycho_rt.c@tycho_ncpu` — a fixture
   runner that wants `-j 4` on a laptop and `-j 32` in CI cannot say so from inside the
   language, and must be *launched* differently instead. `server/main.ty@worker`'s recursion
   is still evidence, but for a **different** want: N long-lived workers each carrying its
   own `wid`, which `parallel for` genuinely cannot express because a chunk's identity is
   not observable. Two items, not one, and only the second needs the type system.
   **Re-checked at `9e8f8f2` and the narrowed form holds in both halves.** The grammar
   still has no width slot — `docs/spec/02-grammar.md:248-249` is two productions, a
   counting one over a literal `0..<Expr` and a foreach one over a bare name, and neither
   carries a count. And `server/main.ty` still spells its pool as a recursion carrying
   `wid`, which the file now uses for more than logging: the shutdown registry is indexed
   by `wid - 1` (`server/main.ty:903-914`), so worker identity is load-bearing there and
   `parallel for` remains unable to supply it.
 9. **`corelib/test/image` is skipped without libpng** (*the friction plan
    phase 7, non-blocking*) — confirmed environmental and confirmed *live*:
    `corelib/image/deps` names `libpng`, `pkg-config --exists libpng` still fails on this
    machine at `9e8f8f2`,
    and `corelib/run.sh:39` prints `skip <name> (missing dependency: ...)` and continues. Its
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

    **NARROWED 2026-07-31 at `9e8f8f2`, and the narrowing is a decision the tree has now
    made twice, not a measurement that moved.** The claim is still true and was re-measured
    here; the *prescription* — "a mechanical pass to anchored form" — is the part that has
    been settled against, and this file now carries the settlement in two sections of its
    own ("Retired citation drift" and "Retired citation phases, second set"). The
    reconciliation, so the item and those sections stop disagreeing:
    - **The population splits three ways, and only one third is work.** Refs that are
      *correct* stay bare — declined three times with a measurement each time, most
      recently because a repair pass was found to have rotted again in four days. Refs that
      are *unresolvable as posed* — a basename with no directory, a `:N` into the live
      `plan.md` — are retired, because no gate and no sweep can settle them. Refs that are
      **already false** are the actionable third, and they are filed as phases on the live
      plan rather than here.
    - **This file's own five already-false refs are protected**, and the reason is written
      into that phase: several of this file's entries are struck-through closed records
      quoting line counts as *evidence for a decision*, and `CLAUDE.md`'s record-line rule
      forbids repairing those numbers. Two of the five sit in live prose rather than in a
      record and were repaired here, by naming the construct instead of the line
      (`server/main.ty@stopped`, `server/main.ty@accept_loop`); the other three sit
      inside measurements and were left exactly as written.
    - **Re-measured on the open list itself**, which is the sample that matters because it
      is the part anyone acts on. The previous pass re-derived every citation on this list
      on 2026-07-30. One day and 43 commits later, **5 of roughly 25 have drifted** — and
      the inversion is the finding: **not one of them is into `src/tychoc.c`.** The
      previous pass concluded that compiler refs are the ones that rot, from a batch of
      compiler work; this batch was server and corelib work, and it rotted
      `server/main.ty` (by 440 lines — the file roughly doubled) and
      `corelib/httpd/httpd.ty` (by 31 and 40) while every `src/tychoc.c` ref on the list
      held. **Which file drifts is a property of what is being worked on, not of how big
      the file is**, so "anchor the compiler refs" is not the rule it looked like.
    - **What changed underneath, and it is why converting is now cheap:**
      `scripts/check_citations.py` gained a `` `path@SYMBOL` `` form that takes no line
      number and is checked by finding the token in that file, so it survives every
      insertion and reddens only on a rename or a deletion. The open list's definition
      citations were converted to it in this pass. That is the mechanism the item asked
      for; what it does **not** license is a sweep over correct refs, which is the thing
      three separate passes have declined.
11. ~~**NEW — `docs/spec/14-ffi.md` §24.1.1 documents one of the two ways a shim returns a
    payload and a classification, and the tree now uses both**~~ — **CLOSED 2026-07-31:
    §24.1.1 now states both arrangements and the rule that forces each.** The new text
    says the split is decided by what can cross as an `inout`, not by taste: a `bytes`
    payload cannot be one (`src/tychoc.c@ffi_scalar_type`) so it takes the return; a
    scalar payload can, so the collision moves to the code space and the status keeps the
    return. Provenance rewritten from "used twice" to the five real uses, and re-derived
    as `path@SYMBOL` refs — the old block's `src/tychoc.c:11414-11443` for
    `gen_extern_proto` was **43 lines stale** and, being a bare range, unreachable by the
    citation gate. `corelib/io/io.ty`'s three cross-referencing comment blocks now point
    at §24.1.1 instead of deriving it. The record of what the item said when open follows.
    (*the conditional-requests
    and byte-ranges work*). §24.1.1 was normative and good: it says the classification is a
    numeric `inout` and the payload keeps the return, spells the C ABI out
    (`docs/spec/14-ffi.md:77-94`), and its provenance block says "the shape is used
    twice — `netx_read` and `iox_read_file`". **It is used five times now, and two of them
    are the mirror image**: `iox_stat_mtime` and `iox_stat_size`
    (`corelib/io/io.ty:100`, `corelib/io/io.ty:106`) put the *status* in the return and the
    *payload* in the `inout`. Neither §24.1.1 nor its "When NOT to use it" paragraph
    (`docs/spec/14-ffi.md:111-115`, which covers the no-payload case,
    `corelib/io/io.ty@iox_stat_kind`) mentions that arrangement exists.
    **The rule is not a preference, and that is why it belongs in the spec.** Which slot
    each half takes is *forced*, by two different constraints:
    - A `bytes` or array payload **cannot** be an `inout` — the compiler rejects it in as
      many words (`src/tychoc.c@ffi_scalar_type`; the message names `bytes` explicitly at
      `src/tychoc.c:4114`), because the crossable `inout` set is int/char/float/bool/ptr.
      So a `bytes` payload takes the return and the status takes the `inout`. That is
      §24.1.1's case.
    - A **scalar** payload can occupy either slot, so the choice is forced the other way
      instead: an epoch second cannot share a code space with the 0..3 status codes, so
      the status takes the return and the payload takes the `inout`. That is the mirror,
      and it is the case §24.1.1 is silent on.
    **This is the same failure §24.1.1 was written to fix, one turn later.** That section
    exists because the `inout`-ahead-of-the-payload ordering was reproduced verbatim from
    one shim's comments into another's with no spec to copy. `corelib/io/io.ty:86-105` is
    now three comment blocks working the mirror rule out by hand and cross-referencing each
    other — "the mirror of `iox_read_file`, where the `bytes` payload must be the return so
    the status takes the `inout` instead" — which is a sibling's comment doing a spec's job
    again. **~4 sentences plus one provenance line**, and the "used twice" count wants
    correcting to five in the same edit.
12. ~~**NEW — `cli.has` answers a narrower question than its name, and no diagnostic is
    possible**~~ — **CLOSED 2026-07-31 as WORKING-AS-INTENDED, decided rather than
    renamed.** The decision and what a rename would have cost: `has` → `has_value` breaks
    three call sites (`corelib/test/cli/main.ty` ×3) and one line of
    `docs/guides/corelib.md` — cheap — but it does not remove the guess, it moves it: a
    caller asking "did the user supply `--stats`?" would still have to choose between
    `has_value` and `flag`, with the same silent-false failure and the same absence of a
    diagnostic (both spellings compile, both return `bool`). What removes the trap is
    saying so at the definition, so `corelib/cli/cli.ty@has`'s doc comment now names the
    failure mode, names `flag` as the predicate for a bare `--key`, and gives
    `cli.has(c, k) || cli.flag(c, k)` as the call-site spelling for "supplied in any
    form" — deliberately not a corelib function, because a caller who cannot say which
    they mean usually has a schema question (`parse_spec`), not a lookup question.
    The record of what the item said when open follows.
    (*promoted from the 2026-07-31 concurrency re-score below, where it was
    recorded but never numbered*). A bare `--stats` lands in `Cli.flags`, not `Cli.keys`,
    so `cli.has(c, "stats")` returns **false** while `cli.flag(c, "stats")` returns true;
    both spellings compile, both return `bool`, and the failure is a missing line of output
    with nothing printed. **It is not a defect** — `corelib/cli/cli.ty@has` and
    `corelib/cli/cli.ty@flag` scan different vectors on purpose and the doc comment above
    `has` says outright "Was option `key` (a `--key=value`) supplied at all?". Re-checked at
    `9e8f8f2`: unchanged, and no `supplied` exists. **A decision, not lines** — rename to
    `has_value`, or add a `supplied(c, name)` that scans both — and it is numbered here
    because it is the only thing in this file that failed *silently*, which is the class
    this file exists to catch.
13. **NEW — nothing checks that a document is REACHABLE, only that its links resolve**
    (*split off item 3 when item 3 was closed, 2026-07-31*). `scripts/check_links.sh`
    reports "139 markdown files, no dead relative links" — a link that points nowhere is
    a hard failure, a document nobody points at is invisible. `docs/bootstrap.md` was
    orphaned for days and was found by a human reading the index, not by a gate. The
    2026-07-31 pass also recorded three files under `docs/internals/` mentioned by no
    Markdown at all. **The open question is what "reachable" should mean**, and it is not
    obvious: the index deliberately points at *directories*, so a naive
    reachability check over links alone would flag almost every file in `docs/reference/`
    and `docs/guides/`. A gate that treats "your directory is linked" as reachable would
    have stayed green on `docs/bootstrap.md`'s whole outage, since `docs/` is trivially
    reachable — so the cheap version of this gate is the version that does not work.
    Sized as a decision, not a line.
14. **NEW — `corelib/test/result/main.ty` states a compile failure that no longer
    happens** (*found 2026-07-31 while closing item 5*). Its note says the real surviving
    constraint on `httpd.read_request_capped` is `docs/spec/appendix-e-conformance.md`
    §E.2.1, and that "rewriting it to `return (Err(why), buf)` still fails, because the
    Ok payload type cannot be grounded from a partially-inferred Request". **Measured:
    it does not fail.** Replacing that function's `out` local with
    `return (Err(why), buf)` / `return (Err(TooLarge), buf)` /
    `return (parse_request(buf), buf)` and compiling `corelib/test/httpd/main.ty` builds
    clean; the patch was reverted. So the comment is one generation behind the same way
    the six sites of item 5 were, except its expired claim is about the *live* compiler
    rather than the frozen one — which makes it the harder class to notice, because
    nothing about it looks dated. Worth re-deriving what §E.2.1's surviving constraint
    actually is before rewriting the note, rather than deleting the sentence.
15. **NEW — `README.md` documents two `make` targets that do not exist**
    (*found 2026-07-31 while closing item 3*). `README.md:223` lists
    `make bootstrap` / `make fixpoint` — "Build / self-host-check
    `compiler/tychoc0.ty`" — and `grep -n 'fixpoint\|bootstrap' Makefile` is **empty**;
    both went with the 2026-07-29 freeze retirement. A reader following the README gets
    "No rule to make target". One table row, and the same class as item 5 in a file no
    in-tree comment gate covers.

> **What moved and what did not (2026-07-30, kept as that pass's record).** Items 1, 2, 3
> and 5 are lines and links with the work already identified — roughly a day between them,
> and item 1 has a known-good pattern to copy four times over. Items 7 and 8 are the
> concurrency pair and are still the honest core: one wants a type-system answer, the other
> wants a warning there is nothing static to warn from. **That sentence did not survive
> being tested — see the 2026-07-31 section below.** Item 8's type-system answer is not what
> a worker pool wants (it stores no handles), and item 7's danger is bounded by measurement;
> what is left of the pair is one missing width parameter. Item 6 wants a decision, not
> lines. Items 9 and 10 are properties of the environment and of the file itself. **The list
> did not shrink, and that is the finding — sixty commits of real language work went past
> this list without touching it**, because every one of them was driven by `new_ideas.md`
> and by the loop and array plans instead. A list nothing is pulling from does not get
> shorter on its own.

**What moved this pass, 2026-07-31 at `9e8f8f2` — and the previous paragraph's finding
was overturned.** It said a list nothing pulls from does not get shorter. Something did
pull from it: item 7 was closed, and it was closed *because* the concurrency work went
looking for what the list said was open. Four more of this file's unnumbered items closed
the same way. So the mechanism works; what the previous pass had actually measured was a
batch that happened to be aimed elsewhere.

**Pick-up order, cheapest first.**

1. **Item 11** — one paragraph plus a provenance line in `docs/spec/14-ffi.md`, and it is
   the only open item where the tree already has three working instances and no written
   rule. Minutes.
2. **Item 2** — one line of diagnostic text at a line that has not moved in two re-scores.
3. **Item 3** — one link in `docs/README.md`.
4. **Item 1** — 3 lines into each of 3 files with a pattern to copy, *plus* the gate that
   stops the next shim repeating it. The gate is the part worth the time.
5. **Item 5** — ~15 lines of comment across 6 files, all six re-read and located here.
6. **Item 4** — ~1 line of code, gated on a decision (which builtin names are shadowable).
7. **Item 6**, **item 12** — decisions, not lines: a corelib layering rule, and a naming
   question in `core:cli` where the current name is documented and the trap is that the
   documentation is what you have to read to avoid it.
8. **Item 8** — the last genuinely hard one. A width slot on `ParallelFor`, or task
   handles in a container; only the second needs the type system, and neither is
   item-sized.
9. **Items 9, 10** — a property of this machine and a property of this file. Neither is
   closable by writing code, and both are listed so nobody re-derives them again.

**Spent, 2026-07-31 — the pick-up order above is now history.** Items 1, 2, 3, 5, 11 and
12 are struck: six of the eleven then-open entries, in one plan of two phases, and the
nine lines above covered every one of them. **What is left open is
items 4, 6, 8, 9, 10 and the three this pass created (13, 14, 15)** — and none of the
survivors is a line of code. Items 4 and 6 want decisions, 8 wants a design, 9 and 10 are
properties of the machine and of this file, and 13 wants a definition of "reachable" that
the cheap version of the gate does not have. **That is the real finding of the batch: the
cheap end of this list was cheap, and clearing it leaves nothing that a bigger effort
budget alone would close.**

**And the ordering was by lines, which is again not effort.** The two doc-only items (3,
11) ran to roughly their estimate. Item 5's "~15 lines across 6 files" cost more than
item 11's paragraph, because rewriting an expired reason means first establishing what
the surviving reason is — which took a compile probe and disproved a sibling comment
(item 14). Item 12 was a decision and cost the least of all: the answer was already in
the tree. And **item 2, "one line of diagnostic text", was by far the most expensive** —
not for the line, but because inserting seven lines at `src/tychoc.c`'s line 3574 staled
**80 anchored citations** across 16 files. The line was minutes; its blast radius was the
work. Nothing in a list ordered by lines can see that, because the cost is a property of
*where* the line goes, not how long it is.

## Re-scored against a real concurrent program, 2026-07-31 (head `9e7a090`)

Everything above about concurrency was written from `server/`, and **`grep -n parallel
server/main.ty` is empty** — that program used `spawn`/`wait` and never wrote a `parallel
for` at all. Items 7 and 8, which the score above calls "the honest core of what is left",
were therefore inference from a program that did not exercise the construct they name.

`tools/prunner/main.ty` does. 479 lines; it runs the whole 560-fixture corpus over a
bounded channel with a `parallel for` pool and a spawned fan-in collector, and its report
is byte-identical to `sh tests/run.sh`'s at **7.62x** (471,695 ms → 61,867 ms, `maxconc`
measured at 16 = `ncpu()`). Evidence under the prunner plan. Items 7 and 8 are
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
- ~~**NEW — §22 of the spec does not describe the construct every per-item worker pool
  depends on.**~~ **CLOSED 2026-07-31, verified at `9e8f8f2` by reading §22.**
  `docs/spec/13-concurrency.md:86-90` now carries the carve-out as a normative paragraph
  of its own: "Each chunk's captured values are deep-copied into it, **with one exception:
  a `Channel(T)` capture is a scalar handle and is passed by value.** Every chunk therefore
  shares the one queue rather than receiving a private copy of it. This is not a detail of
  the reference implementation — it is what makes a worker pool expressible at all, and an
  implementation MUST NOT deep-copy a captured channel." That is the `MUST NOT` the entry
  said a second implementation would otherwise have to guess at, and it is in the normative
  document rather than only in the guide. The entry as it stood:
  `send` on a captured channel from inside a `parallel for` body is what
  routes results out, and within §22 (`docs/spec/13-concurrency.md:76-121`) the only
  mention of a channel is that it may be the foreach *source*
  (`docs/spec/13-concurrency.md:91-92`). Worse, the section states that "each chunk's
  captured values are deep-copied into it" (`docs/spec/13-concurrency.md:81-82`), which
  read literally would give every chunk its own private queue — the opposite of what the
  compiler does and of what 560 jobs crossing one channel proves. The rule is written
  down, but only in the **non-normative** guide: "the chunk tasks share the captured
  channels (a channel is a scalar handle, passed by value — not deep-copied per chunk)"
  (`docs/guides/concurrency.md` "the chunk tasks share the captured channels").
  **A conformance gap, not a language defect** — the implementation is right and the
  normative document is silent where a second implementation would have to guess.
  ~3 sentences in §22 plus a carve-out beside the write rule. Filed as a carried-forward
  phase. **The estimate was right: what landed is one paragraph.**
- ~~**NEW — `iter.map` cannot change the element type, and the lambda syntax hid it.**~~
  **CLOSED 2026-07-31, verified at `9e8f8f2`:** `corelib/iter/iter.ty:14` is now
  `fn map(xs: [$T], f: fn($T) -> $U) -> [$U]`, two type variables, so `[Res] -> [int]` is
  expressible. **And it answers the question the entry flagged as unverified** — the entry
  said "whether the inference reaches a **function-typed** `fn($T) -> $U` parameter is
  **not verified**, and is the thing to check before costing this". It does; that was the
  whole cost. `filter` is unchanged (`corelib/iter/iter.ty:20`, `fn($T) -> int`), as the
  entry predicted. The entry as it stood:
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
  diagnostic possible.** **PROMOTED 2026-07-31 to open list item 12** — still open, still
  unchanged at `9e8f8f2` (no `supplied` exists), and numbered there because an unnumbered
  bullet in a dated section is not something anyone picks up. A bare `--stats` lands in `Cli.flags`, not `Cli.keys`, so
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
- ~~**NEW, one line — the guide's bounded-fan-out section points its reader at the
  desugaring.**~~ **CLOSED 2026-07-31, verified at `9e8f8f2`:** `docs/guides/concurrency.md`
  now closes the section by naming both fixtures with the right framing —
  "Worked example of the sugar: `tests/conc/parfor_chan.ty` … `tests/conc/workers.ty` is
  the *manual* form the sugar replaces … read it to see what the sugar saves, not as the
  way to write this" (`docs/guides/concurrency.md:108-112`). The section also absorbed
  item 7's spec fix a few lines above, stating that the chunk count is `min(ncpu(), N)` and
  capped at 64 (`docs/guides/concurrency.md:104-106`).
  **One wrong pointer cost a plan its recon; naming both fixtures is what fixed it.**
  The entry as it stood:
  `docs/guides/concurrency.md:86-104` introduces `parallel for x in ch:`,
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
  sleeps; the real corpus ran at `ncpu()` = 16, nowhere near `src/tychoc.c:10501`.
- **No nested parallelism** — no `parallel for` inside a spawned task, and no pool inside
  a pool.

## Phase 7 — writing the server

- **Phase 7** — `send` is a builtin, and defining `fn send(conn, r, head_only, keep) -> int` is accepted **silently**; the collision surfaces only at the call site as `error: send(ch, v) takes a channel and a value`, which points at my call and describes a channel operation I never wrote. Nothing is reported at the definition, which is where the mistake is.
- ~~**Phase 7** — `httpd.read_request` collapses EOF, idle timeout and a malformed request line into `method == ""`, so a server that must answer `400` to garbage but hang up silently on a disconnect cannot use it at all; `server/main.ty` reimplements the read loop (`read_head`) purely to keep the raw buffer and recover that one bit.~~ **CLOSED, the option-result plan.** `read_request` returns `Result(Request, ReqErr)` with five named causes, and `read_request_capped(fd, cap)` returns the raw buffer as the second element of a tuple, so the cap decision (`431`) and the log line for an unparseable request no longer need a private read loop. `read_head`, `struct Head` and `term()` are deleted.
- ~~**Phase 7** — `httpd.reason_phrase` is a closed `if`-chain and `httpd.response()` takes no reason, so status `431` goes on the wire as `HTTP/1.1 431 Status`. The workaround is to bypass the constructor and build `httpd.Response(431, "Request Header Fields Too Large", []string, []string, body)` positionally — it works across a package boundary, which is good, but it couples the caller to the struct's field order to set a string. **Bit a second time in the friction plan:** answering `408` needed the identical bypass, so the workaround got factored into a local `phrased_response(status, reason)` — a private reimplementation of the constructor the corelib should have had.~~ **CLOSED, the friction plan, and it was TWO fixes, not one** — which is the finding worth carrying: the item names a missing *table row* and a missing *parameter*, and either alone leaves the other half open. Both landed: `408`/`431` joined `reason_phrase` (`corelib/httpd/httpd.ty:336-339`) so the existing constructor renders them, **and** `response_reason(status, reason, body)` (`:352-353`) is the constructor that takes a phrase, with `response()` rewritten as one line on top of it (`:357-358`) — so exactly one site in the tree builds a `Response` positionally and it is inside the package that owns the struct. **+6 corelib code lines** against the file's own ~5-line estimate. `phrased_response()` is deleted, and so are `oversize_response()`/`timeout_response()`, which existed only to hold the two literal phrases: both call sites are now `error_response(431)` / `error_response(408)`, the same constructor the other four statuses already used, and `server/main.ty` went **376 → 372 code lines** — three functions to zero. The wire proof is at the corelib level, one probe compiled against a `git archive HEAD` tree and against the fix: `HTTP/1.1 431 Status` → `HTTP/1.1 431 Request Header Fields Too Large`, `HTTP/1.1 408 Status` → `HTTP/1.1 408 Request Timeout`. What the item could not know: the *live* wire never changes, because the bypass had been producing the correct bytes all along (the `431` body is 680 bytes before and after) — so the thing that was actually broken was `httpd.response()`, i.e. **the spelling every other consumer of `core:httpd` would have reached for**, and the server was the one caller paying to route around it.
- ~~**Phase 7** — no `\r` escape in string literals (`\n \t \\ \"` only), so the most common byte pair in HTTP is a function call, `httpd.crlf()`. And `const TERM = httpd.crlf() + httpd.crlf()` is rejected — `error: const value must be a literal` — so the header terminator has to be a function that reallocates two strings on every loop iteration.~~ **CLOSED, the loops-cleanup plan.** `\r` was **one character** in the lexer's escape set (`src/tychoc.c:443`) and `const` string folding was **five lines** in `const_fold` (`:4539-4543`), so `const TERM = "\r\n" + "\r\n"` is now a single four-byte literal. What the entry did not know is the reason the escape set was so small: a string literal's text is pasted **verbatim** into the generated C literal and interned by `strlen` (`src/tychoc.c:9916`, `runtime/tycho_rt.c:1015`). That is why `\r` is free (C spells it the same) and why `\0` and `\xNN` are **deliberately refused** — `\0` would truncate the interned length and C's `\x` is greedy over hex digits (`"\x41" "1"` would mean `\x411`), so both need a byte-exact literal on the emit path. **The cost, measured by reading the three places that would change:** the lexer's pass-through would become a decode-to-bytes (`src/tychoc.c:379-461`), the single emit site that pastes the text verbatim would need a `\xNN`-emitting re-escaper beside the 10-line one already there (`:9906`, escaper at `:13087-13098`), and `tycho_str_intern` would need a length-carrying twin because its contract is literally "a C string, `strlen`-bounded" (`runtime/tycho_rt.c:1010-1022`) — 3 functions changed plus 1 new runtime entry point, on the order of 35 lines. Not adjacent to a one-character fix, so: refused, with the number. **`httpd.crlf()` is KEPT DELIBERATELY and this is the finding worth carrying:** `core:httpd` is compiled by the FROZEN `tychoc0` through `examples/webserver/run.sh`, which asserts `tychoc == tychoc0 == golden`, and `compiler/tychoc0.ty:195` rejects `\r`. So the literal cannot be written in any file the frozen compiler reads — the corelib, `tools/*.ty`, `tests/*.ty`, `examples/*.ty` — and the friction is closed in `server/`, `corelib/test/{httpd,csv,strings}` and `examples/corelib/httpd` instead. The allocation half of the complaint was fixed *without* the literal: `read_request_capped` hoists `term := crlf() + crlf()` out of its read loop (`corelib/httpd/httpd.ty:239`), which is what "reallocates two strings on every loop iteration" was actually about.
- ~~**Phase 7** — no multi-line string literal and no line continuation, so the 10-line HTML error page is 10 consecutive `s += "..."` statements. (`+=` does exist; I wrote `s = s + ...` for half the file before checking, because nothing in the corelib I had read used it.)~~ **CLOSED, the loops-cleanup plan — and half of it was already there.** Tycho has had implicit line-joining inside `(`…`)` / `[`…`]` since `tests/multiline_literals.ty` was written; what was missing was only the *literal* half. Adjacent string literals now join (`"a" "b"` is `"ab"`, C/Python rule, **two lines** in `parse_primary`, `src/tychoc.c:2286-2287`), so the two together ARE the multi-line string form and no new delimiter was invented. `server/main.ty`'s `error_body` is now one parenthesized expression instead of twelve `s +=` statements and `usage` likewise (378 code lines before, 378 after — the win is 23 statements becoming 2 expressions and 7 fewer `tycho_str_concat` sites in the emitted C, not lines). An f-string never joins, because it is already sugar for a `+` chain. **Cost recorded for the reader:** a `tests/` fixture for either form is impossible while `compiler/fixpoint.sh` and `scripts/frontparity.sh` feed `tests/*.ty` to the frozen `tychoc0`; the coverage is the golden-validated `corelib/test/` and `server/` programs instead, written down in `docs/spec/appendix-e-conformance.md`.
- ~~**Phase 7** — there is no `stat` and no `is_dir`. `io.exists` answers by listing the parent directory, and the only directory test available is `len(io.list(p)) > 0`, which reports an **empty directory as a file** — `server/main.ty`'s `resolve()` ships a documented wrong answer (a 0-byte `200`) because the question cannot be asked.~~ **CLOSED, the option-result plan** — and it was never an error-model item, which is the finding worth keeping. Phase 2 turned the 0-byte `200` into a `404` (`io.read_bytes` → `Err(io.IsDir)`); phase 3 measured the residue and left it; phase 4 wrote the syscall: `io.is_dir(p) -> Result(bool, IoErr)` over a 4-line `iox_stat_kind` in `io_shim.c`, and `resolve()` now asks the kernel. `GET /emptydir` answers `301 -> /emptydir/` instead of `404` (measured live against a `296bbc2` binary), a non-directory never redirects, and `server/main.ty` did not gain a line — 371 before, 371 after. The `Result` is house style; the fix is `stat`. **The comparison this file should remember: 14 library lines added a question and moved a status code, where ~100 added return types and moved none.**
- ~~**Phase 7** — `args()` includes `argv[0]` but `cli.parse` requires it removed, so every program opens with the same four-line copy loop; `examples/weblog/main.ty:129` has the identical loop with a comment explaining the same thing.~~ **CLOSED, the friction plan, and it turned out to be three code lines**: `cli.argv()` is `a := args()` then `return a[1:len(a)]` (`corelib/cli/cli.ty`), so both copy loops are deleted and a program's front door is `c := cli.parse(cli.argv())`. Array slicing and calling the `args()` builtin from *inside a package* both already worked — measured with a scratch package before anything was written — so the entire cost of this item was **deciding where the convention lives**. The other option the phase considered was making `parse` skip element 0 itself, and it is worse for a reason this file could not have known: **two consumers in this tree hand `parse` a synthetic argv with no program name in it** (`corelib/test/cli/main.ty:14`, `examples/corelib/cli/main.ty:14`), so a `parse` that dropped element 0 would have silently eaten `--out=build/app` — a wrong answer, not an error, and `examples/corelib/cli.out` would have had to be re-recorded to bless it. So `parse` stays a pure function over a vector and the argv[0] convention lives in the function whose *name* is about argv. The new golden asserts the mismatch the wrapper absorbs, in the same breath: **`len(args()) = 1`, `len(cli.argv()) = 0`.** The lesson worth carrying: an item that reads as "the library is missing a function" was really "the library is missing a *name*" — nothing in the language was in the way, and four lines were copied into every program for want of somewhere obvious to put them.
- ~~**Phase 7** — `core:cli` cannot express `--root DIR`: values must be `=`-attached by design, so any CLI wanting the conventional Unix spelling hand-rolls its parser. That is 45 of this server's lines.~~ **CLOSED, the friction plan — and "by design" was right, which is why the fix is an ADDITION and not a reversal.** The header at `corelib/cli/cli.ty:9-11` states the real decision, and it is not aesthetic: *"Values are ALWAYS attached with `=`, so the parser needs no schema of which options take a value."* Schema-**freedom** is the property, and it is exactly what `--root DIR` cannot have — `DIR` is `--root`'s value or a positional, and nothing in the token can say which. So the two are not in conflict and there was nothing to undo: `parse(av)` is unchanged and still schema-free, and `parse_spec(av, valued, boolean)` is the same loop with `strict = true`, asking for the schema only from callers that want the second spelling. One shared loop, so the two entry points cannot drift on the spellings they share. **`parse`'s unchangedness is proven, not asserted**: `corelib/test/cli.out` is **+26 / −0**, a pure append, and `examples/corelib/cli.out` — which only calls `parse` — is untouched. **The count in this entry was an UNDER-count**, which is the number worth correcting: the hand-rolled parser plus its copy loop was **59** code lines (`opt_name`/`opt_inline`/`wants_value` 12, the stepping loop 43, the copy loop 4), not 45, and what replaces it is **32** — of which *2* are the schema itself (`valued := ["root", "host", "port", "workers", "idle-ms"]`, `boolean := ["quiet", "q", "help", "h"]`) and the rest is the part that was always this program's: which names exist, their defaults, and what is out of range. `server/main.ty` went **372 → 341** code lines (−31) and `examples/weblog/main.ty` **170 → 163** (−7) against **+45** in `corelib/cli/cli.ty` — the inverse of the shape the score section above complains about, where the library grew and the server grew too. Nothing left in `server/main.ty` splits a token or counts an index. All **47** spellings the server accepted were driven against a HEAD-built binary and 44 are byte-identical; the 3 that moved are all an **error becoming a success** on a spelling that never worked — `-qh` (the hand-rolled parser had no short-cluster rule at all, `core:cli` has had `-abc` since it was written), bare `--` (rejected only because `opt_name("--")` returned `"--"` and fell through to the unknown arm — an accident, not a decision), and `--bogus --help` (HEAD's loop was argv-*ordered*, so whichever came first won; a parsed `Cli` carries no order, so "`--help` always answers" was chosen and written down at the check). The decisions the item did not specify are documented in `corelib/cli/cli.ty`'s `parse_spec` comment as a rule table, in `docs/guides/corelib.md:353`, and as executable assertions in the golden — including the one that preserves this file's own server: **`--quiet=1` sets the flag and drops the value**, because that is what the hand-rolled parser did.
- ~~**Phase 7** — `die()` is the language's only exit and it always exits **1**, so `--help` cannot be answered with status 0 through it. The fix was to thread a `help: bool` field through the config struct so `main` could return normally — a data-flow change to work around a missing `exit(0)`.~~ **CLOSED, the loops-cleanup plan.** `exit(code)` is a new `Sig` builtin — **1 line** in `register_builtins` (`src/tychoc.c:4524`) plus **3** in codegen (`:9323-9325`), which emit C's `exit(3)` directly rather than a `tycho_exit` wrapper: there is nothing to wrap, `exit()` flushes stdio itself, and not touching `runtime/tycho_rt.c` means not changing the runtime text embedded in every emitted `.c`. The spelling was chosen over a two-argument `die(msg, code)` because the builtin `Sig` table is fixed-arity (`.nparams`), so an overload would need special-casing in the resolver, and because the two calls want *different* streams — `die` writes stderr, an answered `--help` writes stdout. `server/main.ty`'s `--help` arm is now `print(usage())` + `exit(0)`: the `help: bool` field is gone from `struct Config`, the `if cfg.help: … return` block is gone from `main`, and `Config(...)` went from 7 positional fields to 6. **`./tycho-httpd --help; echo $?` → `0`** and a bad flag still exits `1` with its message, both **byte-identical to a binary built from `git show HEAD:server/main.ty`**. What the item could not know: the same freeze that blocked `\r` and nested patterns applies again, and this time it is a *new builtin* rather than new syntax — `exit` cannot appear in anything the frozen `compiler/tychoc0.ty` compiles (the corelib, `tools/*.ty`, `tests/*.ty`, `examples/*.ty`), because `scripts/frontparity.sh` reports exactly that as a divergence. `server/` is fed to `tychoc0` by no runner, which is why the consumer that complained is reachable anyway. Recorded in `docs/spec/appendix-e-conformance.md`; `frontparity` still **288 / 0**.
- ~~**Phase 7** — `net.accept` hands back a bare fd and `core:net` exposes `getsockname` (`netx_port_of`) but no `getpeername`, so an access log cannot record the client address — the single most useful field in a real access log is unreachable from Tycho.~~ **CLOSED, the loops-cleanup plan.** `netx_peer_addr` is 17 C lines (`corelib/net/net_shim.c:204-219`, `getpeername` into a `sockaddr_storage` then `inet_ntop`, IPv4 **and** IPv6) under `net.peer_addr(fd) -> Result(string, NetErr)` (`corelib/net/net.ty:143-147`), where `""` is never an `Ok`: a log that cannot name the client says so rather than printing a blank column. `server/main.ty`'s log line leads with it now — `w1 127.0.0.1 GET / 200 2659 0.210ms` — asked **once per connection**, not per request, since the peer of an accepted fd cannot change. **The half the item did not see is that the answer is thread-shaped.** N workers are N pthreads sharing one listening fd, so the shim's return buffer is `__thread`, not `static`: a shared buffer would be a data race on precisely the field this item exists to add. The borrow is safe because an extern `-> string` return is copied at the call site (`src/tychoc.c:9307-9310`), so the Tycho value outlives the next request's overwrite — verified across a 50-request flood on 4 workers, every line carrying the address, none empty or truncated. **The general lesson: "the corelib is missing a syscall" and "the corelib is missing a THREAD-SAFE syscall" are different items, and only the second one is true here** — the same shape as the `SIGPIPE`/Nagle pair below, where what was unreachable from Tycho was a property of the *process*, not of the call.
- **the friction plan, non-blocking** — `examples/corelib/cli/main.ty` documents only `parse`, not `parse_spec`/`argv`, so the example a reader reaches for first does not show the spelling most CLIs want. Left deliberately: its golden is that phase's proof that `parse` did not move, and re-recording it would spend the proof to demonstrate what `corelib/test/cli/main.ty` already asserts.
- ~~**the friction plan, non-blocking — the frozen-`tychoc0` reach is bigger than `frontparity` can see, and phase 6 got it wrong in good faith.** `scripts/frontparity.sh`'s glob (`:186-187` when this was written; `:224-225` today, with the blind spot closed) feeds `examples/*.ty` but never `examples/<dir>/main.ty`, while four per-example runners *do* feed theirs to `tychoc0` — `examples/webserver/run.sh:24`, `examples/weblog/run.sh:24`, `examples/fetch/run.sh:35`, `examples/sqlite/run.sh:31`. So `core:cli` **is** in the frozen compiler's reach (via `examples/weblog`), which phase 6 recorded as out of it; nothing broke only because phase 6 added no new syntax. None of the four runners is in `make ci`, so **no gate can catch a corelib package adopting new syntax** — the failure surfaces the next time somebody runs a non-gated runner by hand. The enumerated split (13 reachable, 24 free) is in `docs/spec/appendix-e-conformance.md`; making it *checkable* wants a runner, and that is not this plan's business.~~ **CLOSED, the postfreeze-rawstring plan — and phase 7 was RIGHT, which is recorded because two phases of this plan disagree in the record.** `core:cli` is inside the freeze: a `tychoc0` built at this commit, fed `examples/weblog/main.ty`, emits **81 `cli__` symbols**. Phase 6's own evidence (`plan.md:1745-1754`) listed `examples/weblog/` as a `core:cli` consumer and then called it "none of them a tychoc0 input" — the list was right, the conclusion was wrong, and its `frontparity` 288 / 0 could not have contradicted it because that was exactly the blind spot. Phase 6's claim is now annotated in place. **The fix is 6 lines of glob**: `scripts/frontparity.sh` also feeds the four per-example entry points, taking it from `agreed: 288` to `agreed: 292  diverged: 0`. **Reddened deliberately, in the shape the entry says nothing could catch**: giving `corelib/cli/cli.ty` a `\r` escape makes the extended lane report `FAIL examples/weblog/main.ty ... lex: unsupported string escape (use \n \t \\ \")` while the pre-phase-8 script reports `agreed: 288 diverged: 0 / all green` **on the identical tree** — the blind spot, measured from both sides. `server/` and `examples/corelib/{result,httpd}` are excluded by name with the measured reason (`parse: line 2348: unexpected token` for `server/`): they are the witnesses deliberately written outside the freeze. What is still true: `frontparity` is not in `make ci` (it is a removed gate's harness, kept on disk), so the enforcement is a runner you must invoke — but it is now **one** runner that sees the whole reach instead of four that each see a slice.
- **the friction plan, non-blocking** — `corelib/test/image` is **skipped** in an environment without libpng (`corelib/run.sh` prints `skip image (missing dependency: libpng)`), so its golden asserts nothing there. Phase 7's one-line change in it (indexing a `bytes` directly instead of aliasing a `to_str` view) was therefore verified by diffing the **emitted C** — the only delta is `h_sig` disappearing and `tycho_str_get(h_sig, i)` becoming `tycho_str_get(h_png, i)`, i.e. the identical call on the identical pointer — rather than by running the test.
- ~~**Phase 7** — scrubbing control bytes out of a hostile request target (`log_safe`) has to go `string` → `[]int` → `to_bytes` → `to_str`, because a `string` cannot be rebuilt in place and `bytes` cannot be indexed. The Phase 2 `to_str`/`to_bytes` sandwich, biting exactly as predicted, in the one function where a server must be paranoid.~~ **CLOSED, the loops-cleanup plan — and it cost the application 0 code lines, which is the honest number.** `log_safe` is now `string` → `bytes` → `string`: `b := to_bytes(s)`, then `b[i]` classified and either `scrubbed + '.'` or `scrubbed + b[i:i+1]` appended, then `to_str` at the end. Both ends are **zero-cost reinterprets** (`src/tychoc.c:9623-9624`), so the `[]int` and the one **real** allocation in the old path — `to_bytes([int])` at `:9611-9612` — are both gone; `server/main.ty` is **341 code lines before and after**, and `log_safe` is 17 code lines before and after. The win is that the function now works in one domain instead of three, not that it is shorter. **The half the item did not see is that the straight version uncovered a use-after-free.** `out := to_str(b)` over a scope-owned `bytes`, then `return out`, returned a **dangling pointer**: `to_str` is a zero-cost reinterpret but the compiler reported it as a *call*, so `is_place` (`:9023`) said "a fresh value built in the target arena" and skipped the re-home. Measured on a program containing **no phase-7 syntax at all** — a buffer holding `"ABCDEFGH"` printed as `[8]` under a `tychoc` built from `git show HEAD:src/tychoc.c`, and `[ABCDEFGH]` after. Pre-existing since the reinterpret existed; found only because this item's fix is the shape that triggers it. Fixed in-phase (it blocked the item, it was not absorbed opportunistically), pinned by `reinterp_ret` in `corelib/test/io`, and the scrubber itself re-verified against a HEAD-built binary: control bytes 1..31 and 0x7f in a target, a 300-byte target, a control byte in the *method*, and a `\r\n`-injection attempt all produce **byte-identical access-log lines** with **zero** control bytes surviving and no injected line.
- ~~**Phase 7** — `parallel for` and `spawn` are the only concurrency shapes, and neither can express "hand this connection to whoever is free". One worker owns one connection for its whole life, so N workers is a hard cap of N concurrent connections; there is no way to write an event loop or a work queue over accepted fds without a channel of ints and a hand-rolled dispatcher.~~ **REFUSED WITH THE NUMBER, the loops-cleanup plan — and the premise is measurably FALSE in its most important half, which is the finding worth carrying.** The work queue **is** writable today, with no compiler change: a channel handle has type syntax and a spawned worker may take one as a parameter (`src/tychoc.c:724-728`; the guards at `:1241-1243`/`:8198` forbid only *storing* or *returning* one), `send`/`recv`/`close` are builtins over it (`:5872`, `:5881`, `:5926`), and the MPMC contract is normative — "with multiple receivers, each value is delivered to exactly one receiver" (`docs/spec/13-concurrency.md` §23.1). It was written in `server/` (**+39 / −1 lines**: one acceptor loop sending accepted fds into `channel(int, 64)`, N workers `recv`-ing) and measured against a **same-machine** reproduction of the recorded baseline (the recorded box gave 12,456 / 41,046 / 79,712 req/s for 1/4/8 clients; this one gives **14,829 / 50,150 / 93,441**). **Throughput was a wash** — 91,667 vs 93,441 on the recorded keep-alive shape (−1.9%, inside noise), and **+17.2%** on connection churn (47,205 vs 40,292), where one dedicated acceptor beats eight threads contending on `accept(2)`. **The cap did not move, to the millisecond**: 4 workers / 4 silent peers → 4,824 ms vs the baseline's 4,753 ms; 8 / 8 → 4,830 vs 4,829. **The cap was never a property of dispatch — it is a property of a worker blocking in `recv(2)`, and choosing which idle worker gets the next fd cannot make a busy worker idle.** Worse, the queue moves the backlog from the kernel to userspace and so makes an unserved connection *look* served: with all 8 workers pinned, **40 complete requests were accepted in 3 ms and none was answered within a second**, where the kernel's backlog at least enforces `listen()`'s limit and lets `connect()` block. **Not adopted; `server/main.ty` is byte-identical to before.** What would actually lift the cap is readiness notification, and the cost was read out of the source: `corelib/net/net_shim.c` is 361 lines with 12 exports (`:170`, `:222`, `:230`, `:245`, `:264`, `:285`, `:331`, `:360`, `:376`, `:384`, `:399`, `:410`) and **not one** is `poll`/`select`/`epoll` or `O_NONBLOCK` — but the syscall is the *cheap* part, since `[int]` crosses the FFI both ways as a `(const long*, long)` pair (`docs/spec/14-ffi.md` §24.1), so `netx_poll` is ~45 C lines plus ~20 in `corelib/net/net.ty`. The expensive part is that **`httpd.read_request_capped` is a blocking accumulator whose whole state is locals** (`corelib/httpd/httpd.ty:243-247`, looping `net.read` at `:271`), so an event loop needs it split into a `struct Pending` + `feed`/`finish` — ~60 lines, **additive** because three consumers call the blocking form and the frozen `tychoc0` compiles this package (`examples/webserver/run.sh:24`) — and that `serve_conn` (`server/main.ty:363-473`, **70 code lines**) plus a write path that loops `send()` internally (`corelib/net/net_shim.c:225`) becomes ~150 lines with per-fd write buffering. **~283 lines across 4 files, ~80 of them inside the freeze, and a redesign of `core:httpd`'s read surface — refused on Anti-scope, with that number.** Two facts to weigh against the refusal: the cap is already **tunable** (`--workers` accepts 1..256, `server/main.ty:564`, under a 1024 live-task ceiling, `runtime/tycho_rt.c:557-562`), and the concurrency model is not the thing in the way — the missing readiness call in `core:net` is.

## Phase 1 of the Option/Result plan — converting `core:net`

Found while converting `core:net`'s fallible TCP surface to `Result(T, net.NetErr)` and
rewriting `server/`, `corelib/httpd`, both corelib tests and both examples against it.

- **`Option`/`Result` phase 1** — there is no `unwrap_or`, `is_ok`, `is_some` or `is_err` anywhere: searched `docs/spec/16-builtins.md`, `docs/spec/12-aggregates.md` and all of `corelib/`, zero hits. Every caller whose own return type is not a `Result` hand-writes the same three-line `match` to collapse one — `server/main.ty`'s `nwrote`, and a separate copy each in `corelib/test/net/main.ty` and `corelib/test/httpd/main.ty`. It is the single largest cost of adopting `Result`, and it is a missing three-line library function.
- ~~**`Option`/`Result` phase 1** — there are **no nested patterns**: `Err(net.Timeout)` is rejected with `error: expected ')'`, and so is `Err(C(n))` for a local enum. Worse, `Err(A)` where `A` is a nullary variant *parses* — as a **binding named `A`**, not as a pattern — and the mistake surfaces only if a second arm exists, as `error: duplicate Err arm`. So telling two failure causes apart always costs a second `match`, which is why `net.NetErr` was given payload-free variants: `if e == net.Timeout` is one line where `match e:` is three.~~ **CLOSED, the loops-cleanup plan.** All three spellings work: `Err(net.Timeout)`, `Err(Timeout)` and `Err(C(n))` are legal arms, and the misparse is gone by construction rather than by diagnostic. **It was two bugs, and the entry only saw one of them.** The parser half is what the entry describes — `MatchArm` held *binding names*, not patterns, and the arm loop ate one bare `TK_IDENT` per slot (`src/tychoc.c:2929` at `667f0d9`), so `net.Timeout` died on the `.`, `C(n)` on the `(`, and `A` **fit**, as a binding. The half it did not see is in **codegen**: an `Option`/`Result` match was not an arm chain at all but a hard binary `if` with exactly one Ok arm and one Err arm found by name (`:10702-10706` as it stood then; the replacement `gen_match_side` is at `:10383`), so multiple `Err` arms had nowhere to go. That is the structural reason this was not a parser tweak: `gen_match_side` replaces the binary `if` with an ordered decision list per side. **The rule that kills the silent bind:** inside a pattern the payload's enum type is already known, so a name that is a variant of that enum is **always a pattern, never a binding** — which also means `Err(Timeout)` needs no qualifier, and a bare name that *cannot* be a legal pattern is now a hard error instead of a bind. **116 code lines in `src/tychoc.c`** (+192 with comments), one new field on `Variant` (`raw`, the name as written), four on `MatchArm`. **Refused, with the number:** nesting inside a *plain enum* arm (`Wrap(A)`) and nesting deeper than one level, both ~70 lines across the enum arm loop (`:7550-7589`, whose `covered[]` becomes 2-dimensional) and the enum dispatch (`:11126-11159`, which needs the chain nested inside each tag test) — refused for a shape nothing in the tree writes, **but the trap is closed there too**: `Wrap(A)` is now `error: 'A' is a variant of Cause, not a binding name`. **The payload-free enums stay payload-free** — that was a redesign this plan's Anti-scope forbids, and `err_or` + `==` is still the right tool for a caller who is not opening a `match`. What the item did not know is that **`corelib/` still cannot use the form it asked for**: `core:httpd`, `net`, `io`, `result` and `tools/*.ty`/`tests/*.ty`/`examples/*.ty` are compiled by the frozen `compiler/tychoc0.ty`, so the call sites converted are in `server/` (five arms, one per cause, replacing five `e == httpd.X` tests and an `answer` bool), `corelib/test/{io,httpd}` and `examples/corelib/result` — whose goldens still match **byte for byte**, which is the real proof the new codegen is behaviour-identical to the `==` chains it replaced.
- ~~**`Option`/`Result` phase 1** — `die()` is typed `void` and the compiler does not model it as diverging, so it cannot be the tail of a value-`match` arm: `srv := match net.listen(...): Ok(fd): fd / Err(e): die("cannot bind")` is rejected with `a value if/match branch must produce a value, not void`. The statement form needs a dummy `srv := 0` first, making the `Result` version **one line longer** than the `if srv < 0: die(...)` it replaced — the only call site in `server/` where the conversion cost a line.~~ **CLOSED, the loops-cleanup plan.** The item's diagnosis is exactly right and the fix is **not a type**: there is no bottom type in Tycho and adding one would touch every unification site. Divergence is modelled where the *tails* are handled instead. The value if/match desugar has two halves that must agree about which branch carries a value — `ctrl_rewrite_tails` (`src/tychoc.c:3092`), which turns each branch's trailing `S_EXPR` into `name = tail` / `return tail` / a place-set, and `ctrl_collect_tails` (`:3152`), which hands the tails to unification in the `S_DECL` arm of `resolve_stmt` (`:7327-7350`). **Both now skip a diverging tail**, so the branch keeps the plain statement it already was: it contributes no type and gets no destination. That is 14 code lines total (`+44` with comments), the largest single piece being the `expr_diverges` predicate and its justification. **The predicate is syntactic, and that is sound rather than convenient** — `die` and `exit` are registered builtins and a program defining either name is rejected outright (measured: `fn die(s: string) -> int` → `error: 'die' is already defined`), so the name cannot mean anything else; `e->sval` is the written name both before resolution and after, because builtins are never mangled — which is why codegen has always matched `die` the same way. `!e->qual` excludes `pkg.die`, `!e->lhs` excludes a call through a function value. **Fail-closed half:** if *every* branch diverges there is no value at all, and `t` would otherwise stay at its `T_VOID` "unset" sentinel and be pushed as the variable's type — so that is now a hard error naming the fix (`x := if true: die("a") else: exit(2)` → `every branch of this value if/match diverges, so there is no value to bind to 'x' -- write the if/match as a plain statement`). **`server/main.ty` lost the dummy** and is now the item's own spelling; the file went **380 → 376 code lines** (611 → 606 total), and the live matrix on `127.0.0.1:18099` with 4 workers is transcript-identical to a `HEAD`-built binary. Because the skip went into the shared desugar rather than into the `:=` case, **all four tail positions** got it at once: `x := if/match`, `x = if/match`, `place = if/match` and `return if/match` all accept a `die`/`exit` arm (each verified with its own scratch program and exit status). The **fall-off-the-end lint is deliberately unchanged** — a `-> int` function whose `else` branch dies still warns "not all paths return a value", because `block_ends_in_return` (`:9646`) governs a *codegen* decision (the defensive `return (T){0}`) as well as the lint, and `runtime/tycho_rt.c:1200-1202` documents that fallback as intentional: `tycho_die` is not declared `noreturn`, so dropping the return would hand C a fall-off-the-end path. Measured to be pre-existing, not new: the identical warning fires on the plain statement form `if n > 0: return n * 2 / else: die("neg")`.
- ~~**`Option`/`Result` phase 1** — `tychoc` compiles every `.ty` in the entry file's directory, not just the entry file, so two unrelated scratch programs side by side collide with `'main' is already defined` pointing at the file you asked it to build. Nothing says the sibling file is involved; it cost four compile cycles to work out that the fix was `mkdir`.~~ **CLOSED, the loops-cleanup plan — as the DIAGNOSTIC, because the directory scan is a feature and changing it would break a committed fixture.** Two halves the entry did not have. (1) **The scan is conditional**: it happens only when the entry file declares a `package` header (`src/tychoc.c:13128-13130` — `detect_package` decides between `compile_package` and single-file `parse_program`), which is why two headerless scratch files beside each other build fine and only *importing* programs collide. That is the worst possible trigger: a scratch program written to exercise the corelib must say `package main`, so the trap is armed exactly when you are probing a corelib item. (2) **A package may legally span files** — `tests/pkg/multifile/{main,util}.ty` is the fixture — so "only compile the entry file" is not available; it would delete a documented feature to improve an error message. (3) **The blamed file depends on sort order**, which the entry read as "it points at the file you asked for": `scan_pkg_files` qsorts (`src/tychoc.c:12310`) and the diagnostic fires at the *second* definition, so a sibling named `aprobe.ty` blames `main.ty` and a sibling named `probe2.ty` blames itself. Measured both ways before the fix. Now `die_dup_proc` finds the same-named proc in a **different** file of the package and names it: `main.ty:5: error: 'main' is already defined -- also at .../aprobe.ty:5, a DIFFERENT file in the same package: tychoc compiles every .ty beside the entry file, so two unrelated programs cannot share one directory`. **13 code lines in `src/tychoc.c`**, and a same-file duplicate deliberately keeps the plain message (it is self-evident, and lengthening it would be noise). The emitted C of all 15 entry points is **byte-identical** to a HEAD-built compiler's, which is the proof this touched only an error path.
- ~~**`Option`/`Result` phase 1** — the FFI has no way for C to return a classification alongside a `bytes` payload, and `-> Result(T, E)` is not a documented `extern` return shape (`docs/spec/14-ffi.md:20-47` lists only scalars, sized ints, `string`/`Option(string)`, `bytes`, `[int]`/`[float]`, `ptr`, handles, and numeric `inout`). Making `net.read` say *why* it read nothing needed `status: inout int` threaded ahead of the two `bytes` out-params — which works, and is undocumented as the way to do this.~~ **CLOSED, the loops-cleanup plan — documented, as §24.1.1 of `docs/spec/14-ffi.md`.** The shape is now normative with the C ABI spelled out, because the ordering is the one non-obvious part and neither shim's comment states the *rule*, only its own instance: written parameters lower in written order (an `inout` becoming `T*`), then a `bytes`/array **return** appends two trailing out-params — so the classification pointer sits ahead of the payload's even though it is written last (`gen_extern_proto`, `src/tychoc.c:11428-11440`; emitted proto `extern void netx_read(tycho_int , tycho_int , tycho_int *, unsigned char **, tycho_int *);`). Also written down: why `-> Result(T, E)` is absent **by decision** rather than by omission (no flat C ABI for a Tycho aggregate; the wrapper on the Tycho side is what makes the `Result`), that the classification must be a numeric scalar because that is the whole `inout` crossable set, that the shim must set it to a failure code before anything can fail, and — the half neither shim says — **when NOT to use it**: `iox_stat_kind(path) -> int` carries the same four codes with no `inout`, because there the kind *is* the answer.

## Phase 2 of the Option/Result plan — the combinators, `io.read_bytes`, `httpd.read_request`

Found while adding `core:result` and converting the two genuinely ambiguous calls.

- ~~**`Option`/`Result` phase 2** — a **qualified name written anywhere in a generic call's argument list does not resolve**. `result.unwrap_or(net.port_of(fd), -1)` fails with `error: package 'net' has no symbol 'net__port_of'`, `result.err_or(r, net.Failed)` with `error: unknown variable 'net'`, and `result.unwrap_or(r, httpd.bad_request())` with `error: package 'httpd' has no symbol 'httpd__bad_request'` — while the identical spellings are accepted in `==` and as arguments to concretely-typed parameters, and an *unqualified* local call inline is fine. So generic instantiation loses the package qualifier, and every corelib call site pays one extra line to bind the value to a local first. It is what stops `n := result.unwrap_or(io.read_bytes(p), empty)` — the whole point of a combinator — from being the one-liner it should be.~~ **CLOSED, the loops-cleanup plan.** And the diagnosis in this entry was **wrong in an instructive way**: generic instantiation was not losing the qualifier, and generics were not really the subject. **Resolution is not single-pass.** `instantiate_generic` resolves every argument once to infer `$T` (`src/tychoc.c:7878`), then the ordinary concrete-signature loop resolves *the same AST nodes again* against the bound parameter types (`src/tychoc.c:6213`) — and the two rewrites that turn a written `pkg.x` into a mangled `pkg__x` mutated the node in place without being idempotent. The `E_CALL` one kept `e->qual` after rewriting `e->sval`, so the second pass computed `net__` + `net__port_of`; **the doubled prefix in the error message was the entire tell, and it was printing the mangled name back at me as if I had typed it.** The `pkg.Variant` one reinterpreted an `E_FIELD` as an `E_CALL` but left `e->lhs` pointing at the package ident, so the second pass took the call-on-a-fn-value branch and asked for a variable named `net`. The proof that "generic" was a red herring: `Box(net.Failed)` on a plain generic **struct** literal failed identically, because that path also infers-then-re-resolves. The fix is a one-bit `pkg_done` latch on `Expr` plus one `e->lhs = NULL` — **4 code lines in `src/tychoc.c`**, which removed **18 lines** of bound-first workaround across 7 files and made `server/main.ty` shorter (380 → 378) for the first time in two plans. `tests/pkg/generic_qual_arg` is the regression, reddened deliberately against the pre-fix compiler. The lesson worth keeping: **an error message that quotes a name the programmer never wrote is reporting a second visit to the same node, not a lookup failure** — and this file spent a whole phase telling every future caller to bind a local because the message was read as a fact about generics rather than as evidence of re-entry.
- ~~**`Option`/`Result` phase 2** — **two error types in one function make `or_return` unavailable again**, and nothing says so until you try. `examples/corelib/httpd/main.ty`'s `round_trip` returns `Result(int, net.NetErr)` and seven `net.*` calls short-circuit through it beautifully; the one `httpd.read_request` call in the middle returns `Result(Request, httpd.ReqErr)` and has to be collapsed by hand, because there is no `map_err` and no conversion between error enums. A function that touches two packages' fallible calls gets `or_return` for whichever one it picked as its own error type and a manual collapse for the other.~~ **CLOSED, the friction plan, and it is a plain library function — `4 code lines`.** `result.map_err(r, replacement)` is `Ok(v): return Ok(v)` / `Err(e): return Err(replacement)` over **three** type params, `$T` passing through untouched while only the error type moves; the recorded call site is now `served := result.map_err(httpd.read_request(conn_in), net.Failed) or_return` (`examples/corelib/httpd/main.ty:22`). **Phase 1 is what made it writable** — the qualified-name-in-a-generic-argument bug would have stopped `net.Failed` being written inline, which is the entire ergonomic point. Same line count as the `unwrap_or` collapse it replaced, **different behaviour**: the old spelling continued with a dummy `Request` nobody sent, the new one ends the function. The example's golden is **byte-identical** (the exchange succeeds, so the changed path is not taken), which is the proof only the failure path moved; the regression is `two_types` in `corelib/test/result`, whose golden is **+4 / −0**, a pure append. **The freeze is satisfied and measured, not assumed:** `core:result` is inside the frozen `tychoc0`'s reach (`core:httpd` imports it, and `examples/webserver/run.sh:24` feeds the package to a freshly built `tychoc0`) and the three-type-param generic compiles there — `webserver: ok (tychoc == tychoc0 == golden)`. **The alternative was built before being rejected:** the closure form (`f: fn($E) -> $F`, `Err(e): return Err(f(e))`) also compiles and runs, and was not adopted because the caller who needs this has one target variant in mind, so a callable charges every call site a named two-line enum-converting helper — which *is* the hand-written collapse this entry exists to delete — and nothing else in `core:result` takes a callable. What `map_err` does cost is written at the declaration: **the original cause is gone**, so it is for callers whose own enum already has a variant meaning what happened.
- ~~**`Option`/`Result` phase 2** — converting a call that a big block consumes costs an **indentation level**, not just lines: `server/main.ty`'s `serve_conn` went 60 → 71 code lines almost entirely because `match httpd.parse_request(raw)` has to wrap the whole request-handling body to bind `Ok(req)`. There is no `if let`, no early-return binding form, and `or_return` is unavailable (the enclosing function returns a served count) — so the only tool re-indents 30 lines.~~ **REFUSED WITH THE NUMBER, the loops-cleanup plan — and `if let` is the wrong ask, which is the first finding.** `if let Ok(req) := httpd.parse_request(raw):` still puts the whole body inside its own block at the same depth: it saves an *arm*, not an indentation level. Only an **early-return binding form** — `x := e or_else: <diverging block>`, the `guard let` shape — flattens anything. Both were costed by reading `src/tychoc.c`. **`if let` as parser-only sugar is ~45 lines** and the machinery is all there: `parse_match` (`src/tychoc.c:2877`) already parses arm patterns into a `MatchArm` carrying `variant`, `binds[8]` and phase 3's `sub`/`subbinds`/`sub_vi` (`:1518-1520`), and the resolver (`:7136`) and codegen (`:9785`, `:10564`) already run an ordered Ok/Err decision chain — so it is a contextual `let` after `TK_IF`, the pattern parser factored out, and a synthetic two-arm `S_MATCH`. It was not built **because it would not close this entry.** **The binding form is ~105 code lines in `src/tychoc.c`**: a token beside `TK_ORRETURN` (`:183`), a parse hook on the decl path where `x := if/match` already lives (`:3434`), a resolver unwrap in the `S_DECL` arm, a divergence check reusing `expr_diverges` (`:2990`) and `block_ends_in_return` (`:9724`), codegen, and diagnostics beside the existing `or_return requires the enclosing function to return a Result, but it returns %s` (`:5017-5018`) — plus a spec section beside §14.6 (`docs/spec/10-statements.md:143`) and a fixture that, being **new syntax**, can live only in `corelib/test/` or `server/` and never in `tests/` or `examples/` (frozen `tychoc0`; `scripts/frontparity.sh` reports it as a divergence). **And the payoff was measured, not assumed:** `serve_conn` (`server/main.ty:363-473`) is **70 code lines** with **six** arms — five `Err` causes that each answer differently (`:390-414`: silent close, silent close, 408, 431, 400) and `Ok(req)`, which holds **45 of the 70**. A binding form moves those 45 out one level and pushes the five causes into the `or_else` block, where they are still the same five-way match: **net line change ≈ 0**, one indentation level, in **one** function in the whole tree, unusable in `corelib/`. 105 compiler lines for that — **refused, with the number.** The footnote this entry's own 60 → 71 needs: the loops-cleanup plan has since turned what was one `Err(e)` plus five `==` tests into five real arms **on purpose**, because acting on the cause is the point of the conversion, and a happy-path binding form pushes them back into one block. The 11 lines were not bought by a missing keyword; they were bought by a function that has six outcomes.
- ~~**`Option`/`Result` phase 2** — the FFI trick for classifying a `bytes` result (`status: inout int` threaded ahead of the two out-params) had to be reproduced verbatim in `corelib/io/io_shim.c` from `net_shim.c`, because it is still undocumented in `docs/spec/14-ffi.md`. Two shims now depend on an ABI detail written down only in each other's comments.~~ **CLOSED, the friction plan**, by the same §24.1.1 as the phase-1 half above — which cites both shims as its worked examples (`corelib/net/net_shim.c:236-259`, `corelib/io/io_shim.c:74-109`) so the next one has a spec to copy instead of a sibling.
- ~~**`Option`/`Result` phase 2** — `examples/webserver/main.ty` was left **uncompilable by phase 1** (`error: ordering compares two ints ...` on `if srv < 0`, against the converted `net.listen`): it imports `core:net` but was not in that commit's file list, and no gate builds it (`make ci` skips it — see the phase 0 note below), so nothing went red for a whole phase. Fixed here because it also consumes `io.read_bytes` and `httpd.read_request`.~~ **CLOSED, the friction plan — with a gate, and the gate is 40 lines of shell that cost `make ci` milliseconds.** `scripts/entrypoints.sh` (`make entrypoints`, CI step 3b) compiles **every** entry point under `examples/` plus `server/main.ty` — 11 of them — with `--emit-c`, which stops before `cc`, so the lane needs no libcurl, no sqlite3, no libpng and no link step. **Reddened deliberately on the exact breakage this entry records**: restoring `if lr < 0` in `examples/webserver/main.ty` gives `FAIL examples/webserver/main.ty / examples/webserver/main.ty:199: error: ordering compares two ints, two floats, two strings, or two values of the same numeric newtype` and `entrypoints: FAILED (1 of 11 entry points do not compile)`. **Reddened a second time on its own vacuity**, which is phase 1b's lesson applied: renaming `examples/webserver/main.ty` away gives `MUST-COVER FILE GONE ... this lane asserts LESS than it claims`, rather than a green run over a shorter list. The glob is per-directory, so a new `examples/<dir>/` is covered the day it is added; `examples/corelib/*` is excluded because `examples/corelib/run.sh` already compiles, runs and goldens all 38. What the lane does **not** assert is written in its header: not that the emitted C compiles, not that the program runs, and nothing about `tychoc0`.

## Phase 3 of the Option/Result plan — acting on the cause, and deleting `read_head`

Found while moving the cap and the raw buffer into `core:httpd` so `server/` could
stop reimplementing the read loop.

- ~~**`Option`/`Result` phase 3** — **a tuple literal will not infer a `Result` element.** `return (Err(A), "partial")` from a function declared `-> (Result(int, E), string)` is rejected with `error: tuple element 1 needs a concrete value`, pointing at the `return`; the same `Err(A)` is accepted as a bare `return` from a `-> Result(int, E)` function, and accepted inside a tuple once it has been through a typed local (`out: Result(int, E) = Err(A)`) or a helper function. So the one shape the language provides for "return a value AND a classification" costs an extra local and an extra assignment per exit, purely because inference does not reach into the tuple. It is why `httpd.read_request_capped` builds its outcome in `out` instead of returning it directly.~~ **CLOSED, the loops-cleanup plan — and it was a CONFORMANCE BUG, not a missing feature, which is the finding worth carrying.** `docs/spec/04-inference.md` §6.1 has always listed "a tuple or array literal's element type" as a context that supplies an expected type; the compiler simply had no `E_TUPLE` arm in checking mode, so a tuple literal always fell through to the synthesis path (`src/tychoc.c:5062-5073` at `667f0d9`) where each element is resolved with no expected type and a `T_OK_PARTIAL` is rejected. `Ok`/`Err` resolving to a *partial* is deliberate (`:5196-5201`) and `resolve_exp` already grounded one against an `IS_RES(want)` (`:6669-6684`) — which is exactly why a bare `return` and a typed local worked. **The fix is 11 code lines**: an `E_TUPLE` arm in `resolve_exp` that pushes each element's expected type in. It returns the **synthesized** element types rather than `want`, so a mismatch reports through the caller's own equality check instead of a second visit to the same node — phase 1's lesson applied on purpose. **`httpd.read_request_capped` still builds its outcome in `out`, and this is the part the item could not have predicted:** the direct form compiles under `src/tychoc.c` but not under the frozen `compiler/tychoc0.ty`, which `examples/webserver/run.sh:24-27` feeds `core:httpd` while asserting `tychoc == tychoc0 == golden`. Measured by making the change and running the runner: `line 540: returning (Result(,httpd__ReqErr),str) but this function returns (Result(httpd__Request,httpd__ReqErr),str)`. So the item is fixed in the compiler and the workaround is **kept deliberately**, with the reason written at the declaration — the same conclusion phase 2 reached about `httpd.crlf()`, reached again by a different route. The direct form is demonstrated in `corelib/test/result` (`outcome`), which no runner feeds to `tychoc0`.
- ~~**`Option`/`Result` phase 3** — tuples are the right shape for this and **nothing pointed at them**. The obvious reading of "a function returns one value" sends you to an `inout` out-param or a wrapper struct; `docs/spec/03-types.md:193` and `docs/spec/02-grammar.md:137` do document 2–8 element tuples with destructuring (`got, raw := f()`), but no corelib function in the tree returns one, so there is no example to copy. Both alternatives were written and compiled before the tuple was found: `inout string` works (§11.3) and costs the caller a dummy `raw := ""`; a `struct` with a `Result` field also works (verified — a `Result` *can* be a struct field, even though it cannot be a tuple literal element). Three shapes, one documented, none demonstrated.~~ **CLOSED, the friction plan — and the premise was FALSE when it was written, which is the finding worth carrying.** "No corelib function in the tree returns one" was wrong by five: `strings.split_once -> (string, string)` (`corelib/strings/strings.ty@split_once`) and `path.split_path -> (string, string)` (`corelib/path/path.ty@split_path`) have been there since the language was renamed (`39d75be`), `datetime.parse_offset -> (int, bool)` — the value-**and**-verdict shape this entry needed, exactly — since `4c7f8a5`, plus `bignum.divmod -> (Big, Big)` and `datetime.civil_from_days -> (int, int, int)`. So three alternatives were not written for want of a demonstration; they were written because **nothing at the place you look points anywhere**: §5.3.3 was four sentences, no example, no citation, no statement of what the shape is *for*. The fix is therefore documentation and it is now gated: §5.3.3 says outright that a tuple is the shape for "a value AND a classification", says why the two alternatives cost more (a dummy local; a nominal type per call site), notes that since §6.2(7) a `Result` element may be written inline (`return (Err(Timeout), buf)`), carries a **runnable** worked example — `scripts/spec_check.sh` compiles and runs it: `ok docs/spec/03-types.md:231`, taking the spec from 7 runnable examples to 8 — and then lists the five corelib functions by `path:line` so the next reader has code to copy. It also points at `14-ffi.md` §24.1.1 for the C-boundary case, since a tuple does not cross. **Lesson: "the library does not demonstrate X" and "the documentation does not point at X" are different items, and only the second one was true.**
- **`Option`/`Result` phase 3** — a **payload-free error enum cannot say "how much"**, so the `431` decision had to become its own variant. `Err(TooLarge)` tells the caller the cap was hit but not what the cap was or how far past it the peer got, and adding that payload would break the `==` comparison the whole design rests on. (Nested patterns landed in the friction plan, so `Err(TooLarge(n))` would now *match* — but every `==` call site in the tree would still break, and `corelib/` cannot use a nested pattern anyway while the frozen `tychoc0` compiles it. So this item is unchanged by phase 3.) The five-variant enum is the right call here, but the pattern does not scale: every quantitative failure needs either a variant or a second return value.

## Phase 4 of the Option/Result plan — the missing syscall

Found while writing `io.is_dir` and its test.

- ~~**`Option`/`Result` phase 4** — **nothing in Tycho can create a directory.** Verified absent, not assumed: `docs/spec/16-builtins.md` §29.10 lists five filesystem/time builtins (`read_file`, `write_file`, `list_dir`, `clock`, `now`) and none of them makes a directory, and `mkdir`/`make_dir`/`create_dir` return zero hits across `corelib/`, `src/tychoc.c` and `runtime/`. There is no remove either. So `corelib/test/io` — the test for a `stat(2)` wrapper — has to build its empty directory with `os.system("rm -rf … && mkdir -p …")`: a corelib test depending on `/bin/sh` to set up a filesystem state the corelib itself cannot reach. The asymmetry is the finding: the library can now *classify* a directory but not *make* one.~~ **CLOSED, the option-result plan.** `io.make_dir(p)` (`mkdir(2)`, no `-p`) and `io.remove(p)` (`remove(3)`, one entry, **never recursive**) both return `Result(bool, IoErr)` where `Ok(true)` is "changed it" and `Ok(false)` is "already how you asked" — `make_dir` splits `EEXIST` into `Ok(false)` (already a directory: goal met) and `Err(Exists)` (a file is in the way: goal unreachable), which is exactly the ambiguity test this plan was built on. `corelib/test/io` no longer imports `core:os` and the `rm -rf && mkdir -p` line is gone. A non-empty directory is `Err(Failed)`, which is the property that keeps `io.remove` from being `rm -rf` behind a corelib name.
- ~~**`Option`/`Result` phase 4** — `io.exists` and `io.is_dir` now answer overlapping questions by different means, and the cheaper one is the newer one: `exists` lists the whole parent directory (O(entries), and it cannot see a `.`/`..`-only leaf) where `is_dir` is one `stat`. `resolve()` ends up calling both on the same path. A `stat`-backed `exists` is the obvious follow-on and was refused on scope, but the general shape is worth recording — a missing syscall does not just block the question it names, it leaves *neighbouring* answers implemented the long way round.~~ **CLOSED, the friction plan, and the follow-on was bigger than the swap.** `exists` is now `iox_stat_kind(p)` and two comparisons (`corelib/io/io.ty:252-254`), the same shim call `is_dir` uses; it still fails closed, so `false` means "`stat` could not say yes" — the old behaviour too, since an unlistable parent yielded no entries. `corelib/test/io.out` is **byte-identical** before and after, which is the proof the swap changed the means and not the meaning. **Two things the entry did not predict.** (1) **`core:io` lost a dependency**: `path.base`/`path.dir` were needed *only* by the old `exists`, so `import "core:path"` is gone and the module written up as "the first corelib module to COMPOSE other core modules" now composes one, not two — a stale claim in three places, all corrected. (2) **`resolve()`'s double call did not just halve, it collapsed**: making the second call a `stat` is what made the pair visibly redundant rather than merely ugly, and the two calls are now ONE `match io.is_dir(fsp)` reading all three answers off the Result (`server/main.ty:293-302`) — `Ok(true)` → `301` (or `404` when `dir_form` already appended `index.html`, i.e. a directory *named* `index.html`, which used to be a `200` → `read_bytes` → `Err(IsDir)` → `404`: same status, one syscall fewer, no wrong intermediate), `Ok(false)` → `200`, `Err(_)` → `404`. Per request for a real file: **2 syscalls (an opendir/readdir walk plus a stat) → 1 stat**. `corelib/io/io.ty` **98 → 93 code lines**, `server/main.ty` shorter as well. The entry's own closing sentence turned out to be the useful half and to run both ways: a missing syscall leaves neighbouring answers implemented the long way round, **and adding it does not fix them — someone has to go back and delete the long way round**, which is a second, separately-scoped piece of work that is easy to leave undone because nothing is red.
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
- **Phase 1** — `parallel for i in 0..<N` runs only `min(N, tycho_ncpu())` iterations concurrently (`runtime/tycho_rt.c:843-862`); iterations chunked behind one that never returns never start, and nothing warns. `TYCHO_THREADS=2` silently cut a 4-worker server to 2.
- **Phase 1** — starting N workers has no direct spelling: task handles are affine and unstorable, so it is either N hand-written `spawn` lines or a recursive fan-out where each frame holds one handle.
- ~~**Phase 2** — `bytes` supports **only** `len()`, `to_str()`, and crossing the FFI: `a + b` is rejected (`arithmetic requires two ints or two floats (got bytes, bytes)`), `b[i]` is rejected (`can only index an array, a string, or a map`), `b[i:j]` is rejected (`can only slice an array, soa, or string`) — so every non-trivial `bytes` manipulation has to detour through `to_str`, do the work in `string`, and `to_bytes` back.~~ **CLOSED, the loops-cleanup plan — and the item's own diagnosis of *why* it was cheap was exactly right.** All three were **type-checking one-liners plus a widened predicate in codegen**: `bytes` is the same length-headered `char *` as `string` (`src/tychoc.c:610`), so `b[i]` is `tycho_str_get`, `b[i:j]` is `tycho_str_substr`, `a + b` is `tycho_str_concat` and `b + 'c'` is `tycho_str_concat_char` — **no new runtime function, no new C, 43 net lines in `src/tychoc.c` of which 19 are code**. `b[i]` returns **`int`**, the byte value, not a 1-length `bytes`: that is what a byte-classifying loop wants, it allocates nothing, and it keeps `b[i]` and `s[i]` (`:5385`) from meaning different things for one representation. The in-place accumulator was widened to `bytes` in the same pass (3 predicates), so `out = out + b[i:i+1]` in a loop is O(n), not O(n²). What the item could not know: the freeze bites a **fourth** time, and this time the blocked set was *enumerated* rather than assumed — closing the import graph from every `tychoc0` input reaches **13** corelib packages (`cli` `datetime` `http` `httpd` `io` `json` `markdown` `net` `path` `result` `sha256` `sort` `strings`) which may not use these operators, and leaves **24** free, including `base64` `compress` `crypto` `hash` `hex` `image` `md5` `raster` `tls` — the packages that would most want them. Recorded in `docs/spec/appendix-e-conformance.md`; `frontparity` still **288 / 0**.
- ~~**Phase 2** — the "arithmetic requires two ints or two floats" message for `bytes + bytes` suggests `to_float(x)`/`to_int(x)`, neither of which applies to a buffer; the useful advice would be "`bytes` has no operators — use `to_str` to concatenate".~~ **CLOSED, the friction plan.** `bytes + bytes` now compiles, so the message the item complained about is unreachable for that spelling; what remains is `bytes - bytes` and friends, and those get a 4-line arm that names the operator set instead of a numeric conversion: `bytes has no arithmetic (got bytes, bytes) -- bytes supports `a + b` and `b + 'c'` (concat), `b[i]` (the byte value, an int) and `b[i:j]` (a sub-buffer); for anything else use to_str(b)`. `bytes + int` and `bytes + "s"` get their own message (`cannot concatenate bytes with int -- to_bytes(x) to widen, or to_str(b) to work in strings`), because the fix there is a conversion and naming it is the whole job of the diagnostic.
- **Phase 2** — `string` is already fully byte-safe (interior `0x00` survives concat, index, slice, and `len`, measured), so `httpd`'s old header comment claiming an interior `0x00` truncates the body was **wrong**; the string/bytes split buys type-level intent and FFI shape, not binary safety the string model lacked.
- ~~**Phase 2** — `to_bytes("")` is the only spelling for an empty `bytes`; there is no `bytes` literal and no zero value, so every struct default and early return carries the call.~~ **REFUSED with the number, the loops-cleanup plan — settled, not fixed, and the measurement is what settled it.** The premise that this costs anything at run time is **false**: `T_BYTES` lowers to `char *`, "the same length-headered buffer as string" (`src/tychoc.c:1435`), and `to_bytes` on a string is a **zero-cost reinterpret**, not a conversion (`:9613-9614`) — so `to_bytes("")` emits the same interned `""` a literal would, and the whole complaint is 11 characters of spelling at **10 sites in the entire tree** (4 in `corelib/test/`, 2 in `examples/corelib/result`, 1 each in `corelib/httpd/httpd.ty:142`, `corelib/image/image.ty:37`, `corelib/test/compress`, `server/main.ty`). Two candidate spellings measured against the current compiler: `b: bytes = ""` → `error: declared type bytes but value is string`; `b: bytes = []` → `error: cannot type a bare [] here -- no expected type`. The cheapest landing is a checking-mode arm in `resolve_exp` grounding a string *literal* against an expected `T_BYTES` — **~6 code lines**, the same shape as phase 3's 11-line `E_TUPLE` arm, needing no codegen and no runtime because the representation is already identical. Refused on three counts: it puts an **implicit string→bytes conversion into the type system** (a language change, ~25 lines of spec text in `04-inference.md` §6.1 / `03-types.md` / Appendix E, and a redesign this plan's Anti-scope forbids); **it cannot be used at the site this entry names** — "every struct default" is `corelib/httpd/httpd.ty:142`, and `core:httpd` is compiled by the frozen `compiler/tychoc0.ty` through `examples/webserver/run.sh` (fourth phase running that this constraint has bound); and a **real** `bytes` literal, byte-exact and `\xNN`-capable, is already costed at ~35 lines across 3 functions plus a new runtime entry point in the `\r` item above, where it belongs to the `bytes`-operators phase. **Net: 0 lines, 0 bytes of emitted code, one number written down.**
- **Phase 3** — no `ends_with` without importing `core:strings`, and a corelib package taking a dependency for one predicate is worse than the six-line `has_ext` helper it needs, so the helper gets rewritten per package.
- **Phase 4** — `core:net` had no way to bound a blocking read; `time.sleep_ms` cannot help because it cannot interrupt a `recv` already in progress. The idle timeout required a new shim call (`SO_RCVTIMEO`), which means "do not let a peer pin this worker" was not expressible in Tycho corelib until this commit.
- **Phase 4** — a socket read timeout is indistinguishable from EOF at the Tycho level (both yield empty `bytes`); fine for a server, but a client that needs to retry a timeout while giving up on an EOF cannot tell them apart.
- ~~**Phase 0** — six non-gated runners still build tychoc0 and compare against it (`examples/fetch`, `examples/sqlite`, `examples/webserver`, `examples/weblog`, `bench/run.sh`, `tools/prof/profile.sh`); none is in `make ci`, so none can redden, but each will drift as tychoc0 does.~~ **CLOSED, the Odin-loops plan's phase 1 (`1b93727`, 2026-07-29) — by retiring the legs, not by gating them.** A breaking loop-syntax change means the frozen `compiler/tychoc0.ty` can no longer parse the corpus, so every `tychoc0` leg in every runner was removed rather than fixed. Verified at HEAD: no `run.sh` in the tree builds `tychoc0`, and each header now records the retirement in the past tense — `examples/webserver/run.sh:36` ends `webserver: ok (tychoc == golden; the tychoc0 leg was retired 2026-07-29)`. **The item's worry is answered by deletion, and the cost is recorded in `CLAUDE.md`, "Two gates that used to be here": nothing replaces them, so a change that silently narrows what `src/tychoc.c` accepts no longer has a second implementation to disagree with it.** The item was right that the runners would drift; what it could not know is that the drift would be settled by removing the comparison rather than by protecting it.
- **Phase 0** — the harness scripts of the removed gates are still on disk unreferenced (`compiler/run.sh`, `compiler/fixpoint.sh`, `compiler/pkg-split.sh`, `scripts/frontparity.sh`, `tests/rtparity/`, `fuzz/run_pkg.py`, `fuzz/run_typeparity.py`, `run_parforparity.py`, `run_eqparity.py`, `run_unaryparity.py`); kept deliberately so the method behind the recorded self-hosting result stays readable.
- **Phase 0** — the 15 `tests/diag/*.h0err` tychoc0-diagnostic goldens are now orphaned; kept because three archived internals docs cite them.
- **Phase 0** — prepending a 50-line banner to `compiler/tychoc0.ty` invalidated every `:N` self-citation in its own comments (the citation gate only checks docs→source, not source→source), so the file now carries a "+50" correction note instead.
- ~~**Phase 0** — `docs/bootstrap.md` is cited by `compiler/tychoc0.ty`'s original header and by `Makefile`'s old `bootstrap` comment, but the file does not exist anywhere in the tree; the citation gate never caught it because it only validates Markdown-to-Markdown links.~~ **CLOSED, the loops-cleanup plan — the document is written, and the GATE now checks the direction that missed it.** Two corrections to the entry first: the `Makefile` no longer mentions `bootstrap` at all (`grep -c bootstrap Makefile` → `0`; phase 0 removed the target), and the live citations are **three**, not two — `compiler/tychoc0.ty:617`, `compiler/run.sh:3` and `compiler/fixpoint.sh`. Because `compiler/tychoc0.ty` is **frozen**, "remove the citations" was never available: the only way to make no live file cite a missing document was to write it. `docs/bootstrap.md` now names the stages those two script headers cite by number (Stage 1 = `compiler/run.sh`'s differential over the 51 `compiler/tests/` fixtures; Stages 2–3 = the A/B/C self-emission chain; Stage 4 = `fixpoint.sh`'s byte-identical `B == C`; Stage D = the package programs; Stage E = `pkg-split.sh`), states that **none of them is a gate any more**, and carries the two consequences that keep costing time — the freeze reaching 13 corelib packages, and tychoc0's own `:N` self-citations being off by −50. **The gate's general form: `scripts/check_citations.py` now checks SOURCE → DOC**, scanning every tracked non-Markdown file under a wider prefix set (including `Makefile`, `bench/`, `fuzz/`, `server/`) for `docs/<...>.md` mentions and requiring the document to exist, with line bounds when a `:N` is present. **Proven against the pre-fix state**: with `docs/bootstrap.md` moved aside it reports `NO SUCH DOCUMENT` for all three live citations by `path:line`. **And on its first run it found four more of the same bug** — `docs/memory-model.md`, `docs/ffi.md` and `docs/map-mutation.md` (twice) were cited by `bench/prongB/iter_transform.ty:10`, `corelib/crypto/crypto_shim.c:19`, `src/tychoc.c:4793` and `tests/map_mutation.ty:1` after all three documents moved into `docs/guides/`; all four repaired. Gate green at `22 anchored, 1549 bare, 76 source->doc`.

## Found by the friction plan's gate sweep, out of its scope

- ~~**the loops-cleanup plan** — `scripts/tools_check.sh`'s `bytes-rehome` lane has been **silently vacuous since `eefc609`** and is red at HEAD. Its inline fixture writes `d := io.read_bytes(p)` then `len(d)`, which stopped compiling when that commit gave `read_bytes` a `Result` return (`error: len(...) takes an array, a string, bytes, a map, or a soa`), and `scripts/tools_check.sh:273` discards the compile's exit status — so the lane guarding a real use-after-free (`copy_into` missing `T_BYTES`) reports its own breakage as `grep: .../brh/main.c: No such file or directory`. A gate that throws away an exit code cannot tell "the invariant broke" from "my fixture no longer compiles", and it chose the scarier of the two messages. Left unfixed on scope; the loops-cleanup planb.~~ **CLOSED, the loops-cleanup planb.** 10 lines of shell, no compiler change: the fixture is now `d := result.unwrap_or(io.read_bytes(p), to_bytes(""))` — the one-liner that phase 1 made spellable, so the un-rotting is the first *use* of that fix outside its own regression test — and the compile is an `if !` with its stderr captured, with a third branch that says `bytes-rehome FIXTURE STALE: it no longer compiles, so this lane asserts NOTHING`. Both halves were reddened deliberately and restored: dropping `case T_BYTES` from `copy_into` (`src/tychoc.c:8987`) gives `bytes field NOT re-homed -- copy_into missing T_BYTES (dangling UAF!)`, and re-injecting the stale spelling gives the STALE line with the real compiler error indented under it. **The general lesson, and it is not about `bytes`: a gate is two claims — "the invariant holds" and "I am still able to ask" — and discarding an exit status silently merges them.** This one spent three commits reporting a missing file, which reads like a broken script rather than a broken guard, so it was believed and skipped. Any lane whose fixture is a program must fail on the fixture failing to build, distinctly from the assertion failing; a green gate that cannot articulate what it checked is worth less than no gate, because it is trusted.
- ~~**the loops-cleanup plan** — **new language syntax can no longer be given a `tests/` fixture.** `compiler/fixpoint.sh` and `scripts/frontparity.sh` both feed every `tests/*.ty`, `tests/pkg/*/main.ty`, `examples/*.ty` and `tools/*.ty` to the frozen `tychoc0`, so a fixture exercising anything `tychoc0`'s frontend does not know reddens two runners at a compiler that must not be edited. Phase 2's `\r` escape and adjacent-literal join are therefore covered by `corelib/test/` and `server/` (golden-validated, but not by `tests/run.sh`) and the gap is written into `docs/spec/appendix-e-conformance.md`. Not a defect in either runner — a consequence of the freeze, and the first time it has cost a fixture rather than a gate. Whoever un-freezes or retires `tychoc0` should re-home those fixtures into `tests/`.~~ **CLOSED, the Odin-loops plan's phases 1 and 2 (`1b93727`, `f7da4b1`) — and the entry's own last sentence is what happened.** The freeze lanes were retired, and the interim `tests/postfreeze/` lane built to hold new-syntax fixtures was **folded back into `tests/`** in the next phase; `tests/postfreeze/` no longer exists. New syntax now gets an ordinary `tests/` fixture again: `tests/nested_pattern.ty` and `tests/result_tuple.ty` are the two this file's own items were denied, and `corelib/test/result/main.ty:15-28` records where they came home from. **The lesson the entry named is the one that held: the constraint was never a defect in a runner, so it could only be closed by changing what the runners are for.**
- **the loops-cleanup plan** — `corelib/net/net_shim.c` does not compile standalone under `-std=c11`: `getaddrinfo` and `struct addrinfo` are hidden by strict ISO mode without `_POSIX_C_SOURCE`/`_DEFAULT_SOURCE` (`resolve4`, `scripts/frontparity.sh`, 4 errors). Pre-existing, not phase 5's — a `git archive HEAD` copy fails identically — and invisible in practice because `tychoc` invokes plain `cc` (`src/tychoc.c:12913`), whose default is `gnu17`. It means a shim's portability claim cannot be checked with the same flags the repo checks `src/tychoc.c` with. Left unfixed on scope. **Postscript, 2026-07-30 — still open, and it is TWO shims, not one.** Re-measured by running `cc -std=c11 -c` over all 11 shims directly, since `scripts/frontparity.sh` (the route that found it) is no longer a gate: `corelib/net/net_shim.c` fails with the same 4 errors, and **`corelib/tls/tls_shim.c` fails with 9 of the same kind** from `corelib/tls/tls_shim.c:38` — the identical `getaddrinfo`/`struct addrinfo` cause, never noticed because `core:tls` was not in the failing run. Seven shims pass; `corelib/image/image_shim.c` fails only on a missing `png.h`, which is item 9's environmental skip and not this. **And the fix is already in the tree four times**, as an `#ifndef`/`#define` pair with its reason on the line: `corelib/io/io_shim.c:10-11`, `corelib/os/os_shim.c:9-10`, `corelib/datetime/datetime_shim.c:10-11`, `corelib/time/time_shim.c:22-23`. So "~1 line" was right per file and wrong about the file count, and the thing that kept it open — that no gate compiles a shim standalone — is now the *only* thing keeping it open. Open list item 1.
- **the loops-cleanup plan** — the same freeze is why `httpd.crlf()` and `tools/lsp.ty@crlf`'s `"" + '\r' + '\n'` survive the item that made them unnecessary: `core:httpd` is imported by `examples/webserver/main.ty`, which `examples/webserver/run.sh:20-27` builds with `tychoc0` and requires to match byte for byte. **A frozen compiler in a comparison gate freezes the source it reads, not just itself** — the corelib is now, in effect, written in the intersection of two languages, and nothing in the tree said so before this line. **Postscript, 2026-07-30: the reason expired and the workarounds did not.** The freeze lanes were retired 2026-07-29, so the corelib is written in one language again — but `httpd.crlf()` is still defined (`corelib/httpd/httpd.ty@crlf`) with **4 call sites in its own package**, `tools/lsp.ty:280` still returns `"" + '\r' + '\n'`, and both still carry present-tense comments explaining that the frozen compiler forbids the literal. They survive now only because nobody swept them. **That is the generalisation this entry was missing: a workaround outlives its reason by default, because nothing goes red when the reason dies.** Folded into the open list as item 5 (the comments) — the code itself is a smaller, separate sweep.
- **the friction plan** — `docs/bootstrap.md` (written in that phase) is **not linked from `docs/README.md`**; `scripts/check_links.sh` checks that links resolve, not that documents are reachable, so an orphan document is invisible to every gate. Left unfixed on scope; a docs-index pass should list it.
- **the friction plan** — a 17-line growth in `src/tychoc.c` staled **11 anchored `path:line@token` citations** into it, across `docs/spec/15-program.md` and two internals docs. This is the citation gate working exactly as designed (it named every one, with the line the token actually moved to), and it is the argument for a compiler phase running `make check-links` even when it changed no Markdown: the docs cite the compiler by line, so *every* patch to `src/tychoc.c` is a documentation change.
- **the loops-cleanup plan** — **two in-tree comments still assert that the language has no nested patterns**, three phases after the loops-cleanup plan added them: `corelib/net/net.ty:20-21` ("Tycho has no nested patterns -- `Err(net.Timeout)` does not parse -- so `==` is the only way") and `examples/corelib/httpd/main.ty:54-55` ("`Err(httpd.Malformed)` cannot be a match arm (Tycho has no nested patterns)"). Phase 3's evidence lists the files it swept — `httpd.ty`, `result.ty`, `io.ty`, `docs/guides/corelib.md` — and `core:net` was not among them. `corelib/test/io/main.ty` got it right ("`compiler/tychoc0.ty`, whose grammar still has no nested pattern") — that comment has since been rewritten and now records the opposite, that nothing builds tychoc0 and nested patterns are writable (`corelib/test/io/main.ty:51`) — which is the distinction both stale comments miss: the *language* has them, the *frozen compiler that reads the corelib* does not. `examples/corelib/httpd` is one of the two files phase 8 excluded from `frontparity` **because** it is outside the freeze, so there the comment is not just mis-attributed — the nested arm would compile. Left unfixed on scope. **Postscript, 2026-07-30 — still open, now three files, and the distinction this entry drew has itself expired.** Both named comments survive verbatim (`corelib/net/net.ty:20`, `examples/corelib/httpd/main.ty:55`) and a **third** was found here: `corelib/result/result.ty:29-31`, which does not merely mis-attribute but instructs — "this package is compiled by the frozen `compiler/tychoc0.ty` … so nothing in `corelib/` may use one". Since the freeze lanes were retired 2026-07-29 that sentence is false in *both* halves, so the careful language/frozen-compiler distinction `corelib/test/io/main.ty` was praised for is no longer a distinction at all — there is one compiler, and it has nested patterns. This entry's original grep missed nothing; the two comments simply wrap the phrase across a line break, which is why a later search for `no nested patterns` finds neither. Widened and re-costed as open list item 5, which folds in three more sites of the same shape (`corelib/httpd/httpd.ty:100-109`, `corelib/httpd/httpd.ty:281-289`, `tools/lsp.ty:279`).
- **the loops-cleanup plan** — **this file's own `path:line` citations drift silently, and no gate can see it.** 30 of the 71 spot-checked in the CLOSED notes no longer point at what they name; the worst are into `src/tychoc.c`, where every compiler phase shifts everything below it — the literal-interning emit site cited by the `\r` item as `src/tychoc.c:9107` is now `src/tychoc.c:9916`, `copy_into`'s `T_BYTES` case cited by phase 1b as `src/tychoc.c:8643` is now `src/tychoc.c:8987`, and `instantiate_generic` cited by the qualified-name item as `src/tychoc.c:7239` is now `src/tychoc.c:7864`. Every closure is still *true*; the coordinates are not — and the three `as` values above are now the pre-repair record, the loops-cleanup plan having repointed this file's live citations to today's lines. `scripts/check_citations.py` cannot catch it by construction: it verifies the 22 **anchored** `path:line@token` citations against the token and only bounds-checks the 1646 **bare** ones, and every citation in this file is bare. The fix is a mechanical pass to anchored form, after which the gate polices them — and the reason it matters is that this file is the place a future reader goes to find out why something is the way it is. Left unfixed on scope. **Postscript, 2026-07-30 — re-measured, and the repair phase 6 performed has already been undone by sixty commits.** Fifteen citations opened and checked at `afa67da`: **11 are wrong again**, every one of them into `src/tychoc.c`, and the four that survived are all into files that barely moved. So the repair-in-place strategy has now been tried once and measured to last about four days of active work on the compiler. The gate's blind spot is unchanged and structural — bounds-checking a bare ref into a 12k-line file can never fail. **A second dimension of the same defect was found here and repaired a different way:** 51 "`plan.md` phase N" references in this file now name an unrelated plan, because both plans they meant were archived; that was fixed with one definitional note at the top rather than 51 rewrites, on the reasoning that a rewrite would go stale at the next archive and a definition will not. **The two together make the case: repointing is not the fix, re-anchoring is** — and where a stable name exists, use it instead of a coordinate. Open list item 10.
- **the loops-cleanup plan — the bare `src/tychoc.c:N` citation population, retired here by decision rather than swept.** Re-derived at `b5c8406`: **1457** refs name `src/tychoc.c` — 660 inside the frozen `docs/internals/plan-*-DONE.md` archives, 797 live. Of the whole population **139 are anchored `path:N@token` and every one of them is correct** (checked by re-running the anchor test over all of them: 0 mismatches), so the anchored half is not the problem and a sweep would not move it. The other **1318 are bare**, which the gate checks for bounds only — and bounds is exactly the property a drifted citation keeps, because `src/tychoc.c` is 12774 lines and almost any stale number is still *inside* it. **Two classes were considered for repointing and both were deliberately refused.** (1) The **127 refs in dated design records** — 90 in non-archived `docs/internals/*.md`, 37 in `docs/rfc/*.md`, led by `generics-stage2-body-cloning.md` (52), `generics-gap-fixes-plan.md` (44) and `ffi-threading-design-review.md` (26). A study that dates itself in its own filename is a photograph of the tree on that day; repointing its citations yields a document whose prose is dated and whose coordinates are current, and nothing tells the reader the two halves disagree. A stale ref in a dated record is legible; a fresh one is a lie the reader cannot detect. (2) The refs inside `plan.md`'s **completed-phase evidence blocks** — renumbering these makes a phase claim it verified something it never looked at, which is the same rule phase 4 settled for the archives. **What would actually fix the bare population is not a sweep but a conversion**: each ref re-read against the line it names and rewritten anchored, after which the gate polices it forever. That is 1318 hand-verified citations. Batch 10's phase-44 work is a 42-ref instance of exactly that job and it took a batch. Recorded, not actioned — and note this is the same defect as the entry above it, counted across the whole tree instead of this file.

## The signal plan, 2026-07-31 (head `5428fa1`) — closed for one case, narrowed for the rest

`server/main.ty@stopped` prints `tycho-httpd: stopped after N requests` and was
**unreachable**. Nothing in the tree installed a `SIGTERM` or `SIGINT` handler, because
Tycho had no signal surface at all, and `server/run.sh` asserted wait status **143** — it
asserted the *absence* of clean shutdown and called that a passing gate. `core:signal`
closes that. The honest score is that it closes the **shutdown case**, not signal
handling, and the difference is deliberate rather than unfinished.

- **CLOSED — a Tycho program can shut down cleanly on `SIGTERM`/`SIGINT`.**
  `signal.on_shutdown(fd)` (`corelib/signal/signal.ty:84@on_shutdown`) installs one
  handler for both signals whose only action is `shutdown(fd, SHUT_RDWR)` on the
  listening socket. **One call arms an entire worker pool**
  (`server/main.ty@on_shutdown`) because the handler is per-process and acts on the
  shared listener, so which thread the kernel delivers to never matters. **No new control
  flow was needed anywhere**: every thread blocked in `accept` gets `Err`, the wind-down
  arm that already existed (`server/main.ty@accept_loop`'s `Err` arm) retires each loop, and the count
  line prints. Measured at `--workers 4`, four connections held so all four loops were
  provably busy: exit status **0**, the line, `/proc/<pid>` gone, shutdown in **1 ms**.
  The gate now asserts the clean exit it used to assert the absence of, plus `SIGINT`,
  plus `SIGKILL` as the control — 52 assertions to 57.
- **STILL OPEN, narrowed — there is no general handler, and "narrow" is the design and
  not the backlog.** `signal.on(sig, handler)` was refused, with the reason written into
  the package header (`corelib/signal/signal.ty:41-56`; the previous pass cited
  `:19-31`, which the `register_conn` / `retire_conn` additions pushed down): calling a Tycho function from
  handler context is a *language* feature, because every Tycho value lives in a
  bump-allocated arena that is not re-entrant and channel operations park behind a mutex
  (`runtime/tycho_rt.c:940@mu`) — a handler that interrupts the allocator or the lock
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
- ~~**NEW, measured — clean shutdown is not *prompt* shutdown, and `--idle-ms` is what
  bounds it.**~~ **CLOSED, the signals plan — and it
  took two phases because the entry named the wrong worst case.** Phase 15 gave
  `signal.shutdown_requested()` its caller (`server/main.ty@shutdown_requested`, in the
  keep-alive loop condition) and found a case the entry had not: a client keeping its
  connection *busy* held a worker for **102215 ms**, serving `MAX_REQS` requests at its own
  pace, because nothing told the loop to stop. That went to 6–8 ms. **The entry's own
  case — a worker already parked in a blocking read — did not move at all**, and phase 15
  said so rather than claiming it: nothing wakes a `recv` on a *connection* fd when the
  handler shuts down the *listener*, so the loop condition cannot run until
  `SO_RCVTIMEO` expires. Phase 19 closed it by letting the handler reach accepted fds too:
  a `static volatile sig_atomic_t sigx_conns[256]`, one slot per worker, written only by
  that worker from ordinary context and only *read* in handler context, with `fd + 1`
  stored so static zero-initialisation is already correct — no lock, because the design
  removes the need for mutual exclusion rather than trying to take a mutex safely in a
  handler. `signal.register_conn` / `signal.retire_conn` are the Tycho surface; the server
  calls them either side of `serve_conn` (`server/main.ty:915`, `server/main.ty:925`).
  Measured `--workers 4 --idle-ms 5000`, before from a clean worktree at `61a66b0`:
  **4 parked idle clients 4878 ms → 1 ms**, 5 runs of 5, exit 0, the stopped line, and
  `w1..w4` all present in the access log. `server/run.sh` gained the assertion that
  reddens on the old binary with `wait status 137`. The entry as it stood:
  A worker can only notice a shutdown between requests: one parked in
  `serve_conn` on an idle keep-alive connection waits out `SO_RCVTIMEO` first. At
  `--workers 4 --idle-ms 5000`, 0 or 1 held connections shut down in **1 ms** and four
  held connections take **5141 ms** — exit 0 with the line in every case, so bounded and
  correct rather than hung. The 1-connection case is fast only by luck of routing: this
  kernel delivered `SIGTERM` to the main thread, which is accept loop 1, which was the
  busy one, so `EINTR` released it directly. The fix already has its API and no caller —
  `signal.shutdown_requested()` (`corelib/signal/signal.ty:88@shutdown_requested`) exists
  for exactly this. Filed as the signals plan.
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

## Retired citation drift — three phases closed by decision, 2026-07-31

> **How this section and open list item 10 fit together, since they are about the
> same defect and used to read as if they disagreed.** Item 10 is the *observation*
> — this file's coordinates rot and no gate can see it — and it is still true and
> still measured. This section and the one after it are the *decisions* taken about
> what to do with the population: correct-but-bare refs stay bare, unresolvable
> ones are retired, and only already-false refs are work. Item 10 was narrowed at
> the 2026-07-31 re-score to say exactly that, so the item no longer prescribes the
> sweep these sections decline.

Phases 21, 23 and 25 of the signals plan are **stale
`path:line` pointers, not wrong claims**, and they are retired here rather than
swept. Recorded so the next reader knows they exist and why nobody fixed them:

- **20 citations across four spec/guide files**, shifted by the batch that added
  §22.1 and the `ncpu()` correction. The shift bands are mechanical and were
  written down at the time (`docs/guides/concurrency.md` old ≥105 → +13;
  `docs/spec/13-concurrency.md` old 83..112 → +8, and so on).
- **`src/tychoc.c:3513`** points at `gen_parfor` 98 lines short of where it is.
- **The package-mode comment above `dup_other_file`** cites two sites and both
  are wrong — one lands in array-copy codegen, the other in an enum comment.

**Why retired.** Every one is bounds-valid, so `scripts/check_citations.py`
passes it; and every one will be re-staled by the next commit that inserts lines
above it. This tree has now measured that twice: a repointing pass found 11 of 15
spot-checked citations had drifted **again** four days after the previous
repair, and a later phase found a reference that had been repointed four times
(§24.2 → §17.3 → §15.2 → E.2 rows), each fix setting up the next.

**What actually closes this class**, and it is not a sweep: the anchored
`path:N@token` form, which reddens the gate when its target moves instead of
rotting silently — and, since this was written, the `` `path@SYMBOL` `` form for
a *definition*, which carries no line number at all and so cannot rot in the
first place. The bare refs are bounds-checked and unverified; the anchored ones
are checked. **Run `python3 scripts/check_citations.py --stats` for the split;
the numbers this paragraph originally quoted have been removed rather than
updated**, because `CLAUDE.md` says not to copy a figure the gate prints into
prose and this paragraph was doing it in the middle of an argument about stale
numbers. Converting the load-bearing ones is real work with a real payoff;
repointing numbers by hand is a treadmill, and this file is where this repo
records the difference.

Distinguish these from the two filed alongside them that were **fixed** rather
than retired: `docs/reference/` asserting an `ncpu()` behaviour the spec had just
corrected, and a gate comment claiming one known-bad fixture where its own
heredoc lists two. Those were false statements about behaviour. A stale line
number misdirects; a false claim misinforms.

## Retired citation phases, second set — 2026-07-31

Three more phases from the citation-gate work are retired here rather than done.
Unlike the first set, two of these are not *expensive* — they are **unresolvable
as posed**, and saying so is worth more than leaving them open to look thorough.

**A basename with no directory cannot be resolved by anything.** 470 refs across
40 distinct names (`05-generics.md` 67, `03-types.md` 65, `tychoc0.ty` 51,
`02-grammar.md` 33, and 36 more) name a file with no path. No prefix list fixes
this, because the author never said which directory they meant, and several of
those names exist in more than one place. Resolving them means reading each
citing sentence and inferring intent — which is not a gate, it is an editing
project, and a wrong inference produces a confident citation to the wrong file.
The gate skips them, and skipping is the correct behaviour.

**A line reference into the live plan names a document that no longer exists.**
`plan.md` is renumbered from line 1 every time a plan is archived, so 25 such
refs — 11 already out of bounds — point at content that has moved to an archive
under a different name. The number cannot be repaired: repairing it would mean
guessing which archive, and it would go stale again next phase, because the live
plan grows every phase. Bounds-checking `plan.md` would be a permanent
unclearable red. This is the same rotation problem the plan-ref gate solves for
*prose* references, and the line-numbered form has no equivalent fix.

**The third spelling, and why the chase stops.** A possessive form joining the
filename to a phase number — 12 refs in four shapes — is invisible to a gate that
now matches two word orders in three spellings each. One of its shapes names no
file at all, so catching it means keying a pattern on a common English word and
accepting false positives. The refs are genuinely stale; the *pattern* is the
part that stops here. A fourth spelling exists and nobody has found it, which is
the argument: matching English prose is not a mechanism that converges. What
prevents new ones is the archiving discipline in `CLAUDE.md` — the commit that
archives a plan rewrites the references it created — and that is enforcement at
the point of creation rather than detection after the fact.

**What was fixed instead**, so the split is legible: phases 35, 38 and 39, all
inconsistencies the same day's work introduced — a false-positive class created
by widening the gate's reach, figures in `CLAUDE.md` left stale by a phase that
updated only its own copy, and a genuine contradiction between the anchor check
and the record-line rule. Cleaning up after yourself is different from chasing a
population.

## Re-scored against a batch, data-shaped program, 2026-07-31 (head `bb6c43d`)

Everything above this line came from two programs of the same shape as each other:
`server/` is socket-shaped and `tools/prunner/main.ty` is concurrency-shaped, and
both spend their time moving messages rather than transforming data. **Neither one
imports `core:sha256` or `core:compress`** — `grep -n 'core:sha256\|core:compress'
server/main.ty tools/prunner/main.ty` returns nothing — and prunner never crosses
`bytes` and `string` at all (zero hits for `to_bytes`, `to_str` and `read_bytes`).
`server/main.ty` does cross it, seven times, but every one is `to_bytes` on a
string literal or an error page on its way to a socket: it hands bytes along, it
never hashes them, inflates them or parses a length field out of them. So every
judgement in this file about hashing, about compression, and about what the
byte/text split costs a caller who is *working* on the bytes was written by
programs that did none of those things.

`tools/tycho-ar/main.ty` does. 854 lines: it walks a directory, hashes every file,
gzips it, writes one archive, and reverses that byte for byte. It is gated by
`make ar-check` (`tools/tycho-ar/run.sh`) on create-twice byte-identity, a `t`
listing against a recorded golden, a `diff -r` round trip, and four kinds of
refusal. Below is what writing it surfaced, **ranked**, worst first. The last
group is the entries that got smaller as they were written down, and they are
labelled as such rather than padded.

### 1. The one-shot digest is what the language makes natural — this is a language default steering library shape

**The whole corelib hashes messages the caller already holds entire**, and it is
not an oversight. `core:sha256` is `digest(msg)` and `hex(msg)`, both over one
`string`; `corelib/sha256`, `corelib/md5`, `corelib/crypto` and `corelib/hash`
grepped together for `sha256_(init|update|final)`, `EVP_DigestUpdate` and
`fn (init|update|final)` return **zero hits**; and `core:crypto`'s `cx_sha256_hex`
is one-shot too, over a message the caller must hex-encode first.
`compress.compress` (`corelib/compress/compress.ty@compress`) is `bytes -> bytes`
with the same shape.

**The reason is one compiler diagnostic.** `fn bump(a: [int]): a[0] = 1` is
`error: cannot mutate parameter 'a' (it is borrowed read-only; copy it with
`y := a` first)`, and the suggested copy is a genuine copy — `c := a; c[0] = 999`
leaves `a[0]` alone, so a container parameter is a value, not a reference. **A
streaming state therefore cannot be threaded through calls by default.** `update`
is not a function you can casually write; it is a function you must first decide
to spell `inout`. A one-shot `digest(msg)` needs that decision from nobody, so
that is the interface that gets written.

`inout` is a complete answer and it works well — on `[u32]`, on `bytes`, and it
forwards (`tools/tycho-ar/main.ty@sha_feed` hands its own `&H` to
`@sha_block`). Nothing here is broken. What is true is that **the default steers
the library**, and a whole family of interfaces went one-shot because of it.

**What it cost, measured.** Hashing a file in bounded memory meant writing
SHA-256: ~60 lines across `tools/tycho-ar/main.ty@sha_block`, `@sha_feed`,
`@sha_finish`, `@sha_bytes` and `@sha_file`. Digests match `sha256sum` on 14 sizes
straddling the 64-byte block, the padding overflow and the 64 KiB chunk. And the
saving is real but partial: `sha256.digest` expands its message into `buf := []int`
— **one machine int per byte** — so hashing an n-byte file allocated ~8n bytes of
int array on top of the n bytes of file, and that term is now gone. But `c` still
holds each file whole, because the compressor is one-shot in exactly the same way.
**Peak memory for `c` is still O(file size); chunking removed the 8x multiplier on
top of the file, not the file.**

**Cost to fix:** an `inout`-threaded `init`/`update`/`final` beside `digest` in
`core:sha256` is ~80 lines of Tycho and no language change — the block loop and
the padding already exist in this tree, written twice now. Bounding the last term
needs a streaming deflate, which is a `core:compress` interface plus a shim change
and is the larger job. **This ranks first because it is not a missing function; it
is a missing habit**, and it will reproduce in the next package anyone writes.

### 2. ~~The wrong `string` compiles silently, and there is no wrong-looking output~~ — **the mechanism did not reproduce, 2026-08-10**

**The predicted friction did not materialise, and the real one is worse.** This
file's existing entry on the split says a `string` is fully byte-safe and that the
`bytes`/`string` boundary buys "type-level intent and FFI shape, not binary
safety"; the plan predicted that crossing it per file would be the seam this
program hit first. It was not. The crossing is `sha256.hex(to_str(raw))` — one
call, no copy, `to_str` a reinterpret of the same length-headered buffer
(`docs/spec/06-conversions.md`) — and eight fixture digests including a file with
interior NULs match `sha256sum` byte for byte. **Half of a prediction in this file
was wrong again, and it is the half that sounded expensive.**

What is expensive is that **nothing at the type level distinguishes "this `string`
is text" from "this `string` is raw bytes".** `io.read` and `io.read_bytes` are one
keystroke apart, both plausible at the call site, and reaching for the wrong one
gives a digest over a NUL-truncated prefix: no diagnostic, no exception, and an
output that looks exactly like a digest. In an archiver that is silent corruption
of the field whose entire job is detecting corruption.

**THE HAZARD ABOVE DOES NOT REPRODUCE — re-probed 2026-08-10, and this entry was
wrong about its own mechanism.** A 5-byte file `AB\0CD` read three ways:

| path | `len` | digest |
|---|---|---|
| `io.read` (text) | 5 | matches `sha256sum` |
| `io.read_bytes` -> `to_str` | 5 | matches `sha256sum` |

`io.read` does not truncate at an interior NUL, so "reaching for the wrong one
gives a digest over a NUL-truncated prefix" is not a thing that happens. The
entry contradicts itself: two paragraphs earlier it says a `string` is fully
byte-safe and length-headered, which rules out exactly the truncation it then
asserts.

**The proposed fix is therefore withdrawn**, not built: `sha256.hex_bytes` and
`base64.encode_bytes` would have added surface against a hazard that is not
there.

**What the probe DID find, which this entry missed.** The two readers are not
"one keystroke apart, both plausible" — they have different SHAPES.
`io.read(p) -> string` has no error channel at all, while
`io.read_bytes(p) -> Result(bytes, IoErr)` does. You cannot silently swap them;
the compiler stops you. What `io.read` actually hides is **I/O failure** — a
missing or unreadable file — and that is a real asymmetry worth its own entry,
about error reporting rather than about bytes.

### 3. ~~`compress.decompress` cannot distinguish empty from corrupt~~ — **CLOSED 2026-08-10**

`decompress` and `raw_decompress` return `Result(bytes, ZErr)`, with `Corrupt`,
`Truncated` and `Failed`. The entry was right that the information was already
there and being discarded: every failure branch in the shim set `*outlen = 0`
and returned, so the branch that knew threw the knowledge away. It now sets a
status through an `inout int` out-param — the same FFI shape `core:io`'s
`iox_read_file` already used, since a `bytes` return cannot also carry a code.

The three answers are now distinct, and the corelib golden is the record:

```
empty=Ok len=0            <- a real zero-byte payload
corrupt=Corrupt len=-1
truncated=Truncated len=-1
```

`tools/tycho-ar` reports the cause directly instead of inferring damage from a
length mismatch, and its `run.sh` leg 4c now expects `payload is corrupt`. The
stored-length check STAYS: a payload that inflates cleanly to the wrong thing is
forgery rather than damage, and no inflate status can catch that.

One nuance found while testing, and documented at `raw_decompress`: raw DEFLATE
has no wrapper and no checksum, so junk is a valid-looking bit stream that runs
out, and zlib reports it as no-progress — which decodes to `Truncated`. On a RAW
stream, `Truncated` means "not a deflate stream" as often as it means "the rest
exists somewhere". The gzip/zlib entry point has a header and a checksum and
tells the two apart.

The original text follows.

### 3. `compress.decompress` cannot distinguish empty from corrupt

Measured, not reasoned. A probe compressing `""`, a 65-byte payload with byte 12
XORed by `0xFF`, and that payload cut in half:

```
legit-empty: gzip len=20 inflated len=0
legit-empty: sha=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
good      : gzip len=65 inflated len=51
corrupt   : gzip len=65 inflated len=0
corrupt   : sha=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
truncated : inflated len=0
truncated : sha=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
```

Legitimate-empty, corrupt and truncated are **byte-identical answers**. The return
value carries no discriminator at all, and the shim is why: every error path in
`corelib/compress/compress_shim.c@zx_decompress` sets `*outlen = 0` and returns.

**An archive can legitimately contain a zero-byte file**, so for any container
format this means a corrupt member reads as an empty one — data loss that looks
like data. `tycho-ar` survives it only because the format carries the original
length out of band and checks it (`tools/tycho-ar/main.ty@cmd_x`: `inflated to 0
bytes, header says 12 (corrupt payload)`), and `tools/tycho-ar/run.sh` leg 4c
forges the payload digest specifically to prove that check, not the digest, is
what catches it. A caller who trusts the return value has no such recourse.

**Cost to fix: small, and the information is already there.** `zx_decompress`
knows which branch it took; it discards that on the way out. Returning
`Result(bytes, string)` — the shape every other fallible corelib call now uses —
is a shim return value, a signature and the call sites. This ranks third only
because the workaround exists; for a caller without an out-of-band length there is
no workaround at all.

### 4. ~~`strings.parse_int` fails open, so no format parser can use it~~ — **ALREADY FIXED; the last duplicate is gone 2026-08-10**

`strings.parse_int_checked(s) -> Result(int, IntErr)` exists and has since before
this was re-read — `EmptyInput`, `Garbage`, `OutOfRange`, with ten cases in
`corelib/test/strings`. The entry asked for `parse_int_strict`; the same thing
shipped under a different name and the entry was never struck. `parse_int` is
untouched, as the entry wanted.

What remained was the entry's other half: the hand-rolled copies. `server/main.ty`
had already been converted. `tools/tycho-ar/main.ty@parse_uint` was the last one,
and it is now a thin wrapper — **not a straight swap**, because a straight swap
would have WEAKENED it: `parse_int_checked` accepts a leading `-` and the whole
int64 range, and an archive length field is non-negative and must stay far below
int64 so a forged length cannot drive a huge allocation. The split is the one
parser-safety asks for: the LEXICAL rule is shared, the DOMAIN rule stays with
the format that owns it.

Checked at the boundaries, including the entry's own case — `"1x4"` is `-1`, not
`1` — plus empty, leading junk, trailing space, negative, at-cap, over-cap and
int64 overflow. A `gap:` in the source records that the lane forges a damaged
header but not a negative or over-cap one.

The original text follows.

### 4. `strings.parse_int` fails open, so no format parser can use it

`corelib/strings/strings.ty@parse_int` returns 0 for `""`, 0 for a leading
non-digit, and **stops at the first non-digit without objecting** — a damaged
length field of `"1x4"` parses as `1`. That is the right behaviour for user input,
where 0 is a fine default, and the wrong one for a length field, where a wrong
length that *parses* is precisely how a reader ends up hashing the wrong span of
bytes. `tools/tycho-ar/main.ty@parse_uint` is the strict version this program had
to carry: returns `-1`, no silent prefix.

There is no strict or `Result`-returning counterpart in `core:strings` — this call
predates the `Result` convention the rest of the corelib converged on.
**Cost to fix:** ~15 lines for `parse_int_strict(s) -> Result(int, string)`,
leaving `parse_int` alone so no caller moves.

### 5. `bytes` slices clamp, so a slice is not a bounds check

`data[p:p + 8]` past the end of a `bytes` value yields three bytes rather than
trapping (`docs/spec/03-types.md:139`). For a format parser that is a trap dressed
as a convenience: a reader that treats the slice as its bounds check reads a short
footer, compares it against the real one, and gets the **right answer by accident**
— then slices a payload, hashes a prefix, and gets the wrong one. Every read in
`tools/tycho-ar/main.ty@parse` therefore tests `len(...)` of what came back
against what it asked for.

**The hazard is the spelling, not the semantics.** An array slice aborts; a
`string` slice and a `bytes` slice clamp; all three are written `x[a:b]`. Clamping
is right for a text tool and the identical syntax is what makes it invisible.
**Cost to fix: documentation, and it is already partly paid** — the behaviour is
specified. What is missing is the warning beside it, one paragraph in
`docs/spec/03-types.md` saying in words that a clamping slice cannot be used as a
bounds check.

**Re-probed 2026-08-11: every claim above reproduces.** A five-byte `bytes`, one
program, one run:

```
len(b)=5
b[2:10] len=3        <- three bytes, no diagnostic
b[7:9]  len=0        <- entirely past the end, no diagnostic
b[-3:2] len=2        <- negative start clamps to 0
t[2:10] len=3        <- the identical string slice, identical clamp
t[7:9]  len=0
```

and the array half of the "hazard is the spelling" claim, same shapes, same
syntax, different outcome — `a := [1,2,3,4,5]; a[2:10]` dies:

```
tycho: slice [2:10] out of bounds (len 5)
exit=1
```

**Answered 2026-08-11, in corelib rather than in prose.**
`corelib/strings/strings.ty@slice_bytes` and `@slice_str` take the same
`(start, stop)` the language does — `strings.slice_bytes(d, p, p + 8)` reads like
`d[p:p + 8]` — and return `Result(_, SliceErr)`: `OutOfBounds` when `start < 0`
or `stop > len`, `Inverted` when `start > stop`. Clamping stays the default and
no existing caller moves; this is the opt-in a format parser wants, so the
`len(...)`-of-what-came-back test `tools/tycho-ar/main.ty@parse` hand-rolls at
every read has a name now. Locked by ten lines in `corelib/test/strings.out`
(`sb past=OutOfBounds` is the `b[2:10]` case above). The one-paragraph spec
warning is still unwritten and still worth writing.

### ~~6. There is no `eprintln`, and the missing channel removed a feature~~ — **the channel was never missing; `eprint` has shipped since 2026-06-14, 2026-08-11**

The entry's load-bearing sentence — "the builtins are `println`, `die` (stderr,
then exit 1) and `exit(n)`", so "**a non-fatal warning is inexpressible**" — is
**false, and was false when it was written**. `eprint(s)` is a builtin: registered
at `src/tychoc.c:5166@eprint`, emitted as `tycho_eprint`, and defined as
`fputs(s, stderr)` in `runtime/tycho_rt.c@tycho_eprint`. It is specified —
`docs/spec/16-builtins.md:74@eprint` says "Write `s`'s bytes to stderr; no
newline, **no exit**" — and it was added on 2026-06-14 in `61fa0dc`
("+ eprint primitive"), i.e. before this file existed. Nine non-frozen `.ty`
files in the tree already call it, `corelib/log/log.ty` among them.

**The probe that decided this**, a `t`-shaped listing that complains about one
member on stderr, keeps listing, and exits 0:

```
$ cat warn.ty
fn main():
    names := ["a.txt", "BAD.bin", "c.txt"]
    for i := 0; i < len(names); i += 1:
        if names[i] == "BAD.bin":
            eprint("tycho-ar: " + names[i] + ": payload digest mismatch, skipped\n")
            continue
        println(names[i])
$ ./warn 2>/dev/null            # stdout: data only, no diagnostic
a.txt
c.txt
$ ./warn 2>&1 >/dev/null        # stderr: the warning, on its own channel
tycho-ar: BAD.bin: payload digest mismatch, skipped
$ ./warn >/dev/null 2>/dev/null; echo $?
0
```

So the "removed a feature" half does not stand: **`t`'s all-or-nothing interface
was a choice, not a consequence.** Nothing prevented a per-member note on stderr
beside a clean listing on stdout. The interface is still defensible on its own
merits — one verification pass before any output is a stronger promise than
`tar t`'s partial listing — but it must be argued as a decision. It now is:
the header comment at `tools/tycho-ar/main.ty:218-232` was rewritten on
2026-08-11 to drop the false premise and state the decision.

**What is genuinely true is only the heading's first four words.** There is no
`eprintln`, confirmed: `eprintln("x")` gives
`error: unknown procedure 'eprintln'; did you mean 'println'?` and exit 1. The
whole residue is that a warning costs a `+ "\n"` the tree writes today in nine
places. That is sugar, not a missing channel, and it does not rank on this list.

### ~~7. gzip byte-determinism is real, undocumented, and load-bearing~~ — **entry was right; the guarantee is now written down, 2026-08-11**

Every archive this program writes is reproducible **because zlib writes zero into
RFC 1952's MTIME header field unless the caller supplies a `gz_header`, and
`corelib/compress/compress_shim.c@zx_compress` supplies none.** A compressor that
filled that field in — as the `gzip(1)` command does by default — would make every
archive differ from the last, and `make ar-check`'s first leg would redden on a
program that had done nothing wrong.

Nothing in `docs/spec/18-library.md`'s `core:compress` entry says the output is
byte-deterministic. So the tree's one reproducibility gate rests on a property
that is true, verified by reading the shim, and **promised by nobody** — a second
implementation of `core:compress` could set MTIME and still conform.
**Cost to fix: one sentence in the spec**, which converts an accident into a
contract. Cheapest item on this list by a wide margin, and the only one where the
fix is purely making an existing truth binding.

**Answered 2026-08-11. The claim reproduced in every part**, and the guarantee is
now stated in three places a caller actually reads: the spec entry
`docs/spec/18-library.md` §33.3 (binding on any implementation, so a second one
that filled MTIME no longer conforms), the package header
`corelib/compress/compress.ty`, and `docs/guides/corelib.md`'s `compress` bullet.

**The probe.** A program that gzips a fixed 2,400-byte payload and writes the
result to a file, run twice three seconds apart in different time zones:

```
$ ./gzdet p1.gz                       # 09:05:43 UTC, TZ=UTC
wrote 82 bytes to .../p1.gz
$ ./gzdet p2.gz                       # 09:05:46 UTC, TZ=Pacific/Auckland
wrote 82 bytes to .../p2.gz
$ cmp p1.gz p2.gz && echo identical
identical
$ sha256sum p1.gz p2.gz
f6429240f0406b23e1c1bc472208144cf15d8ed5463d1a62c52f313ec11895a1  p1.gz
f6429240f0406b23e1c1bc472208144cf15d8ed5463d1a62c52f313ec11895a1  p2.gz
$ od -An -tx1 -N10 p1.gz               # MTIME is bytes 4..7
 1f 8b 08 00 00 00 00 00 00 03
```

MTIME is four zero bytes, exactly as reading the shim predicted. **The negative
control that proves the `cmp` can fail**: `gzip -c` on the identical payload, on
this host, gives `1f 8b 08 00 f1 e5 7a 6a 00 03` — a filled MTIME — and
`cmp p1.gz g1.gz` reports `differ: byte 5`, i.e. the first byte of the MTIME
field and nothing before it. (One correction to the entry's parenthetical: gzip
1.13 here fills MTIME from its *input file's* mtime, not from the clock, so two
`gzip` runs of the same file agree with each other. Either way it is non-zero and
input-dependent, which is what would break reproducibility.)

**What depends on it, enumerated rather than assumed.** Grepping the tree for
`core:compress` gives twelve files; the ones that are gates are:

- `tools/tycho-ar/run.sh` — leg **[1] create twice, byte-identical**, a `cmp -s`
  of two archives built from one tree. **This is the only gate in the tree that
  would redden**, and `make ar-check` is the only lane that runs it.
- `corelib/test/compress/main.ty` → `corelib/test/compress.out` — asserts only
  boolean invariants (`roundtrip=1`, `smaller=1`, `empty=Ok len=0`, …) and no
  compressed bytes or lengths, deliberately, per its own header comment. It does
  **not** depend on determinism and would not catch a loss of it.
- `corelib/zip/zip.ty` → `corelib/test/zip.out` — locks `method=` and `usz=`
  (uncompressed size), never `csize`. Also independent of it.
- `tools/tycho-ar/expected.out` — the `t` listing golden holds each member's
  **uncompressed** size and the sha256 of the **original** payload, so a change
  in zlib's tuning does not move it. Only leg [1] is exposed.

So the entry's "load-bearing" was accurate but narrow: one gate, one leg, and
nothing else in the tree can see the property at all. That is the argument for
writing it down rather than relying on a reviewer to notice.

### 8. ~~mtime is captured faithfully and cannot be restored~~ — CLOSED 2026-08-11 in `core:io`

**Re-probed and reproduced exactly before the fix.** `io.mtime` read one;
`io.set_mtime` did not exist — `tychoc` on a probe calling it said
`error: package 'io' has no symbol 'set_mtime'`, and `utime`/`utimensat` did not
appear anywhere in `corelib/io/`. The archive format was never the gap: every
member header does carry a real `st_mtime` (`tools/tycho-ar/main.ty:36`), so this
was **data captured correctly that the extractor had no call to apply.**

**Closed by the write side of the same stat field**: `corelib/io/io_shim.c@iox_set_mtime`
plus `corelib/io/io.ty@set_mtime`, which returns `Result(bool, IoErr)` — the shape
`write_bytes` and `make_dir` already use. `utimensat` with `UTIME_OMIT` on the
access time, so restoring a modification time does not stamp an access time the
caller never mentioned; Windows has no `utimensat`, so there the atime is read
back and passed through. A directory is `Ok`, matching `io.mtime`'s read side.

Fixture: `corelib/test/io/main.ty`, four assertions in `make corelib` — an exact
epoch the run did not produce (`exact`), `Err(NotFound)` on a path that is not
there (`set_missing`), that the failing call created nothing (`made_nothing`),
and the directory round trip (`dir_back`). Three of the four flip when the shim
is stubbed to a no-op, which is what makes them a test.

**The caller followed the same day.** `tycho-ar`'s `x` now calls it
(`tools/tycho-ar/main.ty:769-773`), fatally rather than with a warning, matching
every other partial failure in that program. The gate needed the same
work: `diff -r` does not compare mtimes, so the round-trip leg was green over the
hole and would have stayed green over a broken restore. `tools/tycho-ar/run.sh`
leg 3b compares the times themselves, and was confirmed to fail — naming all
eight members — with the `set_mtime` call stubbed out.

### Smaller than they looked once written down

Recorded because they were hit, ranked below the line because writing them out
shrank them. Padding this list would make the eight above harder to act on.

- **No `io.write_bytes`.** Writing bytes is `io.write(p, to_str(b))`, which is
  correct — `runtime/tycho_rt.c@tycho_write_file` fwrites the length header to a
  `"wb"` handle — but a caller has to read the runtime to know that, because the
  signature says `string`. **Smaller than it looked:** it is one signature away
  from symmetric with `io.read_bytes` and it never actually cost this program a
  bug, only a paragraph of comment justifying a line. Legibility, not safety.
- **No `mkdir -p`.** `corelib/io/io_shim.c@iox_make_dir` is one `mkdir(2)`, which
  is the correct primitive, and `Ok(false)` for "already a directory" is exactly
  the right interface — it is what makes the loop idempotent. Every caller writing
  into a tree it does not own rebuilds the component chain;
  `tools/tycho-ar/main.ty@mkdir_p` is 18 lines of it. **Real, and 18 lines.**
- **A package cannot mark a top-level function internal.** Every `fn` in
  `corelib/sha256/sha256.ty` is callable as `sha256.<name>` from an importing
  program — `k_table`, `h_init`, `ch`, `maj`, `pow2`, `hex2` all probed and
  returned. **Both halves are true and they cancel:** this is what made item 1's
  60 lines cheap, since not one constant or round table is duplicated; and it means
  every corelib helper is public API by accident, so any rename is a breaking
  change to callers the author never knew existed. No cost estimate, because the
  fix is a visibility rule and that is a language question, not a corelib one.
- **`core:io` is path-based, with no file handles.** The compressor's read and the
  digest's reads are separate `open(2)`s over the same path, so an archiver cannot
  read a file atomically. A writer racing between them is **detectable** — a read
  returning zero before the expected length is fatal in
  `tools/tycho-ar/main.ty@sha_file` — but not preventable. Unlike the two items
  above this is the shape of the whole package rather than one missing call, which
  is why it is an entry here and not a proposal.
- **No expression line continuation.** `x := a + b +` followed by a continuation
  line is `error: expected an expression`, caret at the column after the trailing
  `+`. Every header build in this program has that shape, so one two-line
  expression became three statements. **The only thing that changed the code rather
  than the comments**, and it changed it by two lines.
- **`chr(n)` is the only route from a number to a byte.** There is no `bytes`
  builder from integers; `to_bytes` takes a `string`. SHA-256's padding is
  therefore assembled as a `string` and converted. Harmless at ≤120 bytes built
  once per file, and it would not be in a hot loop. Pairs with `io.write_bytes`:
  **`bytes` is a good type to receive and an awkward one to construct.**
- **A match arm cannot be empty.** An arm with no body is `error: expected an
  indented block`, with the caret on the *next* arm's line, so a success case with
  no work still needs a statement (`Ok(_): continue`). Kept only because it is the
  same family as the line-continuation entry — the grammar has no way to say
  nothing. **And it is a correction:** the first draft of this claimed there was no
  `_` wildcard binding, which is false — `Ok(_)` compiles and a whole-arm `_:`
  wildcard is at `docs/spec/12-aggregates.md:653`. An unverified absence claim
  nearly shipped into this file, which is the one file here where that is
  expensive.
- **`push` has no inverse.** No `pop`, so the directory walk is a queue with a
  cursor rather than a stack. **Not a defect** — the queue reads better — but the
  data structure was chosen by the corelib rather than by the program.

### What did not go wrong, which is also data

- **It compiled first try, three phases out of three.** The list/extract phase and
  the chunked-hashing phase each built with no errors on the first `./tychoc`,
  including a hand-written SHA-256 with `inout` state threading through four
  functions. This matches what the concurrent-program re-scoring above found and
  contradicts the picture the older half of this file paints.
- **The `bytes`/`string` crossing cost one call and no copy** (item 2).
- **`sort.asc` over `[string]` is unsigned-byte lexicographic and locale-free**,
  because string comparison is `memcmp` over the length header
  (`runtime/tycho_rt.c@tycho_str_cmp`). That is what a format needs, and a
  locale-aware comparison would have quietly broken reproducibility across hosts.
  `runtime/tycho_rt.c@tycho_list_dir` returns filesystem order, so the sort is the
  entire determinism story on the walk side.
- **`path.safe_join` fails closed and was asserted, not assumed.** A hand-built
  archive whose member path is `../a.txt` — the same eight bytes as `xx/a.txt`, and
  no digest in the format covers the path, so the substitution leaves a
  structurally valid archive — is refused before the first write, destination
  never created. That is leg 4a of `tools/tycho-ar/run.sh`, and neutering the
  substitution reddens it, so the leg is measured rather than assumed.

## Re-scored against a type-system-shaped program, 2026-08-01 (head `75e8175`)

The section above re-scored this file against a batch, data-shaped program and
found one language default steering a whole family of library interfaces. But
`tools/tycho-ar/main.ty` is structurally flat: **one struct, no enum of its own,
no closure, and no recursion except a directory walk.** So the half of the
language the documentation is loudest about — recursive sum types, generics with
`where` constraints, first-class function values — had still never been exercised
by anything in this tree but `corelib/json/json.ty` and the test corpus. Every
judgement in this file about what it costs to *model* something was written by
programs that modelled nothing.

`tools/tycho-q/main.ty` does. 2059 lines: a lexer, a recursive-descent parser
producing a recursive `Expr` enum, a `Value` sum type that every row cell and
every intermediate result flows through, an evaluator that is an exhaustive
`match` over the AST, a stable multi-key merge sort taking a comparator as a
first-class value, and two readers. It runs
`select name, qty * price as total from sales.csv where region == 'eu' and
qty > 10 order by total desc limit 5` and returns the right rows. It is gated by
`make q-check` (`tools/tycho-q/run.sh`) on a 31-query transcript, `select *`
being byte-identical to its input, CSV and JSON agreeing under `cmp`, and ten
failure legs refusing with empty stdout.

Below is what writing it surfaced, **ranked**, worst first. The two corelib
defects at the top were filed as unchecked phases in the plan that produced this
section and were **deliberately not fixed here** — they are corelib changes and
this was a tools phase. **Finding 1 has since been fixed, in two plans; finding 2
is still open.** Each carries its own status banner below, and the finding texts
are left exactly as they were written: they are the record of what was wrong, and
a repaired description of a fixed defect describes nothing. The last group is the
entries that got smaller as they were written down, and they are labelled as such
rather than padded.

### 1. `core:json` accepts input it cannot represent, three ways, and cannot report any of them

> **[FIXED, 2026-08-01.]** Both halves are closed, by two plans in sequence. The
> finding below is left **verbatim**, including the parts that are no longer true
> of the tree — `json_guard` is gone, `parse` is no longer the only entry point,
> and none of the three probe lines reproduces. It stays because it is the only
> record of what the package did and why `tycho-q` is shaped the way it is.
>
> **The error channel** — which the finding names as the root cause, correctly —
> was closed by the json-error plan, whose phase 1 landed as
> commit `e291d49`: `corelib/json/json.ty@parse_checked` returns
> `Result(Json, JsonErr)`, every variant carries the byte offset of the byte that
> failed, and the non-termination is unreachable rather than merely caught
> (`corelib/json/json.ty@parse_value` no longer falls through to a number parse
> for a byte that begins no value, and both container loops carry a
> cursor-must-advance guard). That is what let `tools/tycho-q/main.ty@json_guard`
> — most of a second JSON parser, written only because the first one could not
> speak — be **deleted** rather than maintained.
>
> **The float path**, which the finding correctly separates as "a separate,
> larger question", was closed by the plan this banner was written under:
> `76d5c3d` added `corelib/json/json.ty@JFloat`, carrying the binary64 value
> **and the original lexeme**, so a number the parser cannot represent exactly
> keeps its digits and `stringify` re-emits them byte-for-byte; the same commit
> ended the silent 64-bit integer wrap, which was the last silent-wrong-value
> path in the package. `62b7a0c` added `\uXXXX` and surrogate-pair decoding, and
> `d571b16` made the grammar exactly RFC 8259's — trailing commas, leading zeros,
> trailing text after the document and raw control bytes inside a string are all
> refused now, each naming the byte the writer has to change.
>
> **And the interaction the finding predicted with item 2 resolved the other
> way.** It expected the float path to be blocked on `core:decimal`, since
> "`core:decimal` is the only exact numeric tower here and `JNum` is an `int`".
> It was not: `core:json` stores the **lexeme**, so `tycho-q` maps it straight
> through `decimal.from_str` (`tools/tycho-q/main.ty@json_float_cell`) and never
> touches the binary64 value. A JSON `1.50` and a CSV `1.50` are now the same
> `VDec` — they compare equal, sort together, multiply exactly and render
> identically, which `make q-check` asserts with `cmp` over the same query run
> against both fixtures. Two spellings are refused rather than rounded (`1e3`,
> `-0.0`), for the stated reason that the same text is already refused in a query
> literal and in a CSV cell. **Finding 2 is untouched by any of this and remains
> open**: there is still no `decimal.div`. *(That last clause was true when it was
> written and stopped being true the next day — `decimal.div` landed 2026-08-02 in
> commit `a8c761c`, and finding 2 is closed. Left in place because the sentence it
> qualifies — that nothing in the `core:json` work depended on division — is still
> the point.)*
>
> **Re-probed 2026-08-11 along the axis the title claims — "input it cannot
> represent" — and the round trip is faithful on all five.** Measured, not
> inferred: an integer stays `JNum` and re-emits as itself, and every number the
> lexeme covers (`1.0`, `9223372036854775808`, a 30-digit integer) comes back
> byte-identical; object keys keep insertion order; duplicate keys are ALL kept,
> so `{"a":1,"a":2}` round-trips unchanged; a `\uXXXX` escape decodes to UTF-8
> bytes and is a fixed point rather than an identity, with an embedded NUL
> surviving as `61 00 62` and re-emitting as the escape `\u0000`; and `null`,
> absent and the empty string
> are three distinct states in the tree. **Nothing was fixed, because nothing was
> broken.** What the probe did find was two decisions that were real and written
> down nowhere: duplicates are kept with `get` answering the FIRST, and `get`
> returns JNull for both a null member and an absent one, so membership is a
> `keys` walk. Both are now in the corelib header under OBJECTS, both are asserted
> by `corelib/test/json/main.ty`, and the stale `core:json` bullet in
> `docs/guides/corelib.md` — which still said "integers (no floats)" and never
> mentioned `parse_checked` — was corrected against the source.

**This is the most serious thing this program found, and it has no symptom.**
Measured by probe, not inferred. `corelib/json/json.ty@parse_number` takes an
optional `-` and then digits, and never consumes a `.`:

```
$ ./pj '1.5'          → kind:num  out: 1                      exit 0
$ ./pj '[1.5]'        → tycho: out of memory                  exit 1
$ ./pj '[{"a":1.5}]'  → kind:arr  out: [{"a":1,"5}]":null}]   exit 0
```

Three different failures, and **two of them exit 0**:

- **Bare `1.5` truncates silently** to `JNum(1)`.
- **`[1.5]` exhausts memory from five bytes of input.** `parse_value` consumes
  nothing at the `.`, and `corelib/json/json.ty:81-92` advances only on `,` or
  `]`, so `JNum(0)` is pushed forever. Not a slow parse — an unbounded one, from
  input a user could paste by accident.
- **`[{"a":1.5}]` exits 0 with a fabricated column.** The leftover `.5}]` is read
  as the next KEY. **This is the shape a table reader actually meets**, and it is
  the one with nothing to notice.

**The root cause is not the missing float path — it is the missing error
channel.** `corelib/json/json.ty@parse` returns `Json`, not `Result(Json, E)`, so
no caller can ask whether a parse succeeded. `corelib/json/json.ty:12-13`
documents the parser as lenient and "failing closed to `JNull`", and it is
lenient in the sense that it always returns something; closed is not what an OOM
or an invented key is.

**What it cost, measured.** `tycho-q` cannot inherit any of that — a query tool
whose worst case is returning wrong rows cannot be built on a reader that invents
columns — so `tools/tycho-q/main.ty@json_guard` validates the raw bytes *before*
handing them over. Two layers, and the second is load-bearing rather than tidy: a
token alphabet, and **bracket nesting**, because the non-termination needs a byte
`parse_value` consumes none of sitting at a value position inside an array, and
requiring brackets to nest is what makes the two survivors (`}` and `:`)
unreachable there. That is most of a second JSON parser, written to check the
first. What it does **not** claim is full validation: `[1 2]` passes it and
`core:json` reads two elements — lenient, but it terminates and invents nothing.

**Cost to fix:** make `parse` fallible and make `parse_array` unable to loop
without advancing. Both are small and neither needs a language change. A float
path is a separate, larger question that interacts with item 2, since
`core:decimal` is the only exact numeric tower here and `JNum` is an `int`.
**This ranks first because every other item on this list is a cost paid by the
programmer; this one is paid by the person reading the output.**

### 2. `core:decimal` has no `div`, so the ordinary averaging query has no answer

> **[CLOSED 2026-08-11 — fixed 2026-08-02 by commit `a8c761c`; this banner had
> gone stale for nine days.]** The finding below is left verbatim. `div` landed
> in exactly the shape the finding asked for and nothing here re-litigated it:
> `corelib/decimal/decimal.ty@div` is
> `div(a, b, scale, mode) -> Result(Decimal, DivErr)`, with **both** the target
> scale and the rounding mode named by the caller and no default for either.
> `corelib/decimal/decimal.ty:91-92` gives the two modes the finding demanded —
> `HALF_UP` and `TOWARD_ZERO` — and the second is there for the stated reason,
> that `corelib/decimal/decimal.ty@rescale` already truncates toward zero and a
> lone half-up policy would silently disagree with it. Because a `const` does not
> cross a package boundary here, both are also readable as calls
> (`corelib/decimal/decimal.ty@half_up`), which is what `tools/tycho-q` writes.
> A zero divisor is `Err(DivByZero)`, not the process abort the finding called
> "not an error message"; a negative scale and an unknown mode are errors too.
> One bignum long division does the work, so no float touches the path.
>
> **`tycho-q`'s exact-only `/` is gone with it.** `tools/tycho-q/main.ty:240-253`
> records the change: the query the finding said had no answer now has one, and
> the zero divisor it had to pre-check is an `Err` it can attribute to a row.
>
> **Re-probed 2026-08-11 before this banner was written**, because the entry had
> already been re-read once and pronounced open while the code existed:
> `1/3` at scale 5 half-up is `0.33333`, `2/3` at scale 2 is `0.67` half-up and
> `0.66` toward zero, `-2/3` is `-0.67` (ties away from zero on the magnitude,
> sign reapplied after), and the three refusals return `Err(DivByZero)`,
> `Err(BadScale -1)` and `Err(BadMode 9)` rather than aborting. The fixture that
> pins all of this is `corelib/test/decimal/main.ty:67-94`, and breaking the
> half-up comparison in `div` on purpose reddens `make corelib` — recorded under
> the phase in `plan.md`.

`corelib/decimal/decimal.ty` has `from_int`, `from_str`, `add`, `sub`, `mul`,
`cmp`, `rescale`, `to_str`, `neg`, `abs` and `is_zero`. **No division.** The
package's own header says why, and the reason is right: division needs a target
scale and a rounding policy and it has neither.

Three options, and all three lose. Rejecting `/` at parse time refuses `6 / 2`,
which has a perfectly good exact answer. Converting to `float` puts a lie in
exactly the place `core:decimal` exists to prevent one, and puts it there
quietly. Integer truncation is honest for `VInt / VInt` and has no answer at all
when either side is a decimal, so it decides half the question.

`tycho-q` chose **exact-only**: `/` computes when the result is exact and errors
when it is not, with the zero check *before* the operator because division by a
zero value aborts the process (`docs/spec/09-expressions.md:27-28`) and an abort
is not an error message.

**The cost is large and this is not pretending otherwise.** `select total /
count` — the single most common reason anyone writes `/` in SQL — fails on almost
all real data:

```
$ tycho-q 'select name, qty / 5 as h from sales.csv'
tycho-q: `/` is exact-only: 12 / 5 is not a whole number, and core:decimal has
no div, so there is no rounding policy to apply
```

**Cost to fix:** not `div(a, b)` but `div(a, b, scale, mode)`, with **both named
by the caller**, plus the rounding modes spelled out — at minimum half-up and
toward-zero, since `corelib/decimal/decimal.ty@rescale` already truncates toward
zero and a second policy must not silently disagree with it. It ranks second
rather than first because the failure is loud: a caller who hits it knows.

### 3. ~~`core:sort` has no comparator-taking sort at all~~ — **CLOSED 2026-08-10**

`corelib/sort/sort.ty@sort_by` takes `cmp: fn($T, $T) -> int`. Bottom-up merge,
stable, the ~35 lines this item said already existed — lifted from
`tools/tycho-q/main.ty@merge_sort` and made generic over the element instead of
over an index.

The stability point was the one worth testing, not the sorting: the test orders
`"b1", "a1", "b2", "a2"` by first character descending, and a total-by-index
comparator passes every other case here and returns `b2, b1` on that one.
Flipping `<= 0` to `< 0` in the merge was run on purpose and reverses both that
case and the two-key struct case.

`tools/tycho-q/main.ty@merge_sort` still exists and is untouched — converting it
to the corelib call is separate cleanup.

The original text follows.

Read against the signatures, not assumed. `corelib/sort/sort.ty@by_key` takes
`key: fn($T) -> int` — **the key is an `int`**, so it can express no order that
does not fit in one machine word, and this program's order ranks by kind and then
by value across `VStr` and `decimal.Decimal`. There is no order-preserving
injection of a string into an `int`. `corelib/sort/sort.ty@asc` and
`corelib/sort/sort.ty@desc` are `where comparable(T)` over the values themselves
and take ONE array and ONE direction, so `order by a asc, b desc` — two keys, two
directions, same rows — fits no signature in the package.
`corelib/sort/sort.ty@argsort` has the same ceiling.

So `tools/tycho-q/main.ty@merge_sort` exists: bottom-up, iterative, stable,
comparator as a first-class function value, ~35 lines. **Stability is a property
of the merge, not of a tiebreak** — the merge takes the left run on `<= 0`, and a
total-by-index comparator would look identical on every test but reverse tie order
under `desc`, so `order by k desc limit 3` on a column with ties would return
different rows by a rule nobody wrote down.

**Cost to fix:** one `sort_by(xs, cmp: fn($T, $T) -> int)` beside the existing
entry points, ~35 lines that already exist in this tree. **Any program ordering by
more than one key, in more than one direction, or over a type with no `comparable`
instance writes its own sort today**, and each one gets its own answer to the
stability question.

### 4. ~~`Result(void, E)` is not expressible, and a bare `or_return` is not a statement — one defect, three sightings~~ — **CLOSED 2026-08-09**

**Both halves landed together, because this item was right that they are one
defect.** `Result(void, E)` is now writable: `void` is spellable as a Result's
ok payload (and nowhere else), constructed by a zero-argument `Ok()` and matched
by a bare `Ok:` arm; and `f() or_return` IS a statement, admitted exactly when
the ok payload is void — which is the case this item describes and no other. An
`or_return` statement over a real payload still fails, now with a message that
names the type it would have dropped rather than the general "no effect" rule.

The item's own diagnosis held up under the fix. What it did not see is that the
enabling change was two-layered: `T_VOID` doubled as the generic bind-vector's
"unbound" sentinel, so `void` could not become a real type until that moved to
`T_UNBOUND` (`d868083`). ROADMAP.md priced that re-sentinel as buying the
spelling; it bought the *possibility* of the spelling.

`tools/tycho-q/main.ty@check_cols` — one of the three sightings — is untouched
and still carries `_ := check_cols(l, hdr) or_return`. Converting it is
optional cleanup, not part of the fix, and is deliberately not bundled here.

The original text follows.

A helper that can only succeed or fail must still return *something*, so it
returns `Result(int, E)` and ends in a meaningless `Ok(0)`. Then that meaningless
value must be **bound at every call site**, because `eat_kw(...) or_return` alone
on a line is refused: "a statement must be a declaration, assignment, or call — a
bare expression has no effect". `docs/spec/10-statements.md:16-18` names
`or_return` among the forms refused as a bare-expression statement, so this is
specified rather than a compiler quirk — but the specification is of the general
rule, and the interaction with a void-returning fallible function is what bites.

Three independent sightings, in code with nothing else in common: the parser's
keyword eater, and `tools/tycho-q/main.ty@check_cols`, a validator that only
succeeds or fails and must therefore carry `_ := check_cols(l, hdr) or_return`
through its own recursion. **It is not a parser-shaped problem; it is what happens
to every validator.**

**Cost to fix:** either a unit-like type usable as a `Result`'s success payload,
or admitting `or_return` as a statement when the value is discarded. Both are
language changes, which is why this ranks below three corelib items that are not.

### 5. ~~An enum cannot be asked which variant it is without binding a payload~~ — **FIXED 2026-08-11: the `is` operator**

> **Closed.** `v is Variant` is now a `bool`-valued operator covering both
> halves of the gap — nullary and payload-carrying variants alike, binding
> nothing. `is` is a reserved word; the grep that decided that found **zero**
> uses of `is` as an identifier in any `.ty` file in the tree, so unlike `pass`
> it did not need to be contextual. The right operand is a variant name spelled
> exactly as a `match` arm spells it, including the qualified `pkg.V` form, and
> a name that is not a variant of that enum is a compile error naming both.
>
> Specified at `docs/spec/12-aggregates.md` §19.8 and
> `docs/spec/09-expressions.md` §13.2; fixtures `tests/enum_is.ty`,
> `tests/reject/enum_is_unknown_variant.ty`,
> `tests/reject/enum_is_not_an_enum.ty`. Full evidence — the identifier grep,
> the emitted C, the negative control — is under the ticked `is` phase in
> `plan.md`.
>
> **Two things this did NOT do**, both deliberate and both recorded rather than
> hidden. `v == VNull` still works for a nullary variant, so that one question
> has two spellings; removing it would break working code for tidiness.
> And `is` does not yet reach `Option` or `Result` — `r is Ok` is refused — which
> is filed as its own unchecked phase in `plan.md`, because those two do not use
> the `->tag` discriminator user enums do and widening it is not a one-liner.
>
> The confirmation triage that preceded the fix follows.

> **Triage 2026-08-11.** Probed against the compiler at `77bd826`. The finding
> holds: there is no `is` operator (`if v is VInt:` → `error: expected ':'
> before the block`) and no tag accessor — `src/tychoc.c` has no `tag_of` or
> equivalent, and `git log -S'tag_of' -- src/tychoc.c` is empty, so one never
> existed. A hand-written `kind()` is still the only way to get a variant as a
> **value**.
>
> **Two things below are wrong and are corrected here.**
>
> 1. *"a match arm must name a binder it never uses"* — it must name the right
>    **arity**, not a name. `_` is accepted: `VInt(_): println("int")` compiles
>    and runs. The decoration is one underscore per arm, not an invented
>    identifier. What is refused is dropping the parens entirely —
>    `VInt: return 1` → `error: VInt binds 1 value(s), got 0`
>    (`src/tychoc.c:8104`).
> 2. *"without binding a payload"* — a **nullary** variant needs no match at
>    all: `if v == VNull:` compiles and runs. `==` is a working discriminator
>    for the payload-free half of an enum. It stops at the other half:
>    `if v == VInt:` → `error: VInt carries a payload — write VInt(...)`
>    (`src/tychoc.c:5738`).
>
> So the real gap is narrower than the heading: **a payload-carrying variant
> has no value-level discriminator**, and `match` is the only test for it.
> `tools/tycho-q/main.ty@kind` is still the workaround, and the arms in it are
> `(_)`-cheap rather than name-cheap.

The original text follows.

There is no `is`, no tag accessor, and a match arm must name a binder it never
uses. `Value` needs its tag **constantly** — every comparison rule, every
arithmetic rule and the whole kind-rank order are "what kind is this" — so
`tools/tycho-q/main.ty@kind` is the tag accessor the language does not have,
hand-written once, with binders that are pure decoration.

Writing it once and switching on an `int` everywhere is much the lesser evil, and
that is the finding: **a sum type used as data rather than as control flow wants a
discriminator, and the only way to get one is to build it.** The alternative is
that decoration at every site, which is where a reader stops being able to see
which arms actually use their payloads.

### 6. ~~Two error types cannot share an `or_return` chain~~ — **WRONG MECHANISM, corrected 2026-08-11; CLOSED by `result.map_err_with` the same day**

> **Triage 2026-08-11.** The load-bearing sentence below — "**no function can
> propagate across the boundary**" — is false, and was false when it was
> written. `result.map_err` (`corelib/result/result.ty:120`) already converts a
> `Result`'s error type, and the converted value feeds `or_return` directly.
> Probed:
>
> ```
> fn both(s: string) -> Result(int, RErr):
>     n := result.map_err(parse(s), FromParse) or_return   # parse returns Result(int, PErr)
>     return Ok(n)
> $ ./tychoc q6b.ty -o q6b
> built q6b
> ```
>
> What genuinely reproduces is the **unconverted** chain, and only that:
>
> ```
> n := parse(s) or_return          # inside fn both(...) -> Result(int, RErr)
> error: or_return propagates a PErr error, but the function's error type is RErr
> ```
>
> — `src/tychoc.c:5702-5704`. So the rule is "no *implicit* conversion", not
> "no conversion".
>
> The second, smaller true claim: `map_err` takes a **constant** replacement
> (`corelib/result/result.ty:120`), so the original cause is discarded — the
> package's own header states that cost deliberately. But a cause-**preserving**
> combinator is four lines of ordinary Tycho, needs no compiler change, and
> composes with `or_return`. Probed end to end, `Syntax(7)` arriving as
> `FromParse(7)`:
>
> ```
> fn map_err_with(r: Result($T, $E), f: fn($E) -> $F) -> Result($T, $F):
>     match r:
>         Ok(v): return Ok(v)
>         Err(e): return Err(f(e))
> $ ./q6c
> off=7
> ```
>
> **This is therefore not a language item.** It is at most one generic function
> added to `core:result`. The entry's closing worry — three error types paying
> "at every boundary, with no language feature to make it cheaper" — costs one
> wrapped call per boundary today, and the collapse-into-one-type pressure it
> predicts does not follow.
>
> **CLOSED 2026-08-11 — the function is `result.map_err_with`**, exactly the four
> lines probed above, sitting beside `map_err` in `corelib/result/result.ty` with
> the same name shape, the same argument order and the same doc voice. Its
> regression is `with_pay` in `corelib/test/result/main.ty`, which asserts the
> **payload**, not the variant: `Err(Sized(9, "nine"))` must arrive as
> `Err(FromSized(9))` and print `cause 9`. Swapping that one call to plain
> `map_err(pick(k), Plain)` flips it to `plain` and nothing else — measured — so
> the assertion is one a constant replacement provably cannot satisfy.

The original text follows.

`PErr` (parse — carries a byte offset) and `RErr` (runtime — carries a column name
or nothing) genuinely differ, and folding them would put a meaningless `off: 0` on
two thirds of the messages. But `Result(T, PErr)` and `Result(T, RErr)` are
different types and there is **no `From`-style error conversion**, so no function
can propagate across the boundary.

Here they meet only in `main` and the cost is one extra `match`. That is why it
ranks sixth and not higher — but the cost is structural, not proportional: **a
program with three error types and a call graph that mixed them would pay this at
every boundary, with no language feature to make it cheaper**, and the pressure
would be to collapse them into one type carrying fields that are meaningless for
two thirds of its values.

### 7. ~~`core:iter` is unusable for a fallible pipeline stage~~ — **FIXED 2026-08-11**, both halves

> **Second half fixed 2026-08-11.** `filter`/`count`/`any` take `fn($T) -> bool`
> and `try_filter`'s `keep` takes `fn($T) -> Result(bool, $E)`; the nonzero-is-true
> convention is gone. The two diagnostics quoted further down are the OLD
> behaviour — `iter.filter(xs, fn(x: int) -> bool: x > 1)` is now the spelling
> that compiles. The paragraphs below are left as filed.

> **Fixed 2026-08-11.** `corelib/iter/iter.ty@try_map` and
> `corelib/iter/iter.ty@try_filter` ship the fallible siblings, with exactly the
> arguments and order of `map` / `filter` and a callback returning `Result`. Both
> short-circuit: the **first** `Err` ends the walk and is returned unchanged, so
> there is no partial array and no later error shadowing the real one. The
> fixture is `corelib/test/iter/main.ty`, whose two must-fail inputs each carry
> two failing elements plus a `999` that calls `die`, so an implementation that
> continued past the first failure either reports the wrong element or kills the
> program. Both were observed: swallowing the `Err` in `try_map` reddened `make
> corelib` with `try_map walked past the first error` on stderr and three golden
> lines missing; a last-error variant probed separately reports `7` where the
> golden demands `5`.
>
> **Not added, deliberately:** `try_reduce`, `try_count`, `try_any`. No caller in
> this tree needs them, and the entry's own motivating shape — a query engine's
> row filter and its projection — is `try_filter` and `try_map`. The second half
> of this entry, `keep`/`pred` spelled `fn($T) -> int` in a language with `bool`,
> is untouched: it is a breaking change to a shipped signature and belongs in its
> own revertible commit.

> **Triage 2026-08-11 — CONFIRMED, unchanged, and correctly sized.** Both
> diagnostics below reproduce **verbatim** against the compiler at `77bd826`;
> `corelib/iter/iter.ty` still declares `keep: fn($T) -> int`
> (`corelib/iter/iter.ty:20`) and still has no fallible counterpart — its five
> functions are `map`, `filter`, `reduce`, `count`, `any` and nothing else.
> Its last two commits (`4e58e14`, `43eb558`) touched neither.
>
> The entry's "cost to fix: a signature, not a language change" is now
> **verified rather than argued**: the fully generic fallible map compiles and
> runs today, with no compiler change, inferring `$T`, `$U` and `$E` at once —
>
> ```
> fn try_map(xs: [$T], f: fn($T) -> Result($U, $E)) -> Result([$U], $E):
>     out := []
>     for v in xs:
>         push(out, f(v) or_return)
>     return Ok(out)
> $ ./q7d
> 2
> ```
>
> The `bool` half is equally cheap: the int-predicate signature has **five call
> sites in the whole tree**, all under `corelib/test/iter/` and
> `examples/corelib/iter/`, so flipping `keep`/`pred` to `bool` costs those
> five lines and two goldens.

**Labelled a correction, because the first version of this finding was too broad
and a probe disproved it.** The row filter is the exact shape `core:iter` exists
for, and it cannot be written with it:

```
$ ys := iter.filter(xs, fn(x: int) -> bool: x > 1)
error: argument 2 of 'iter__filter' is fn(int) -> bool, which does not fit the
       parameter pattern

$ ys := iter.filter(xs, fn(x: int) -> int: chk(x) or_return)
error: or_return requires the enclosing function to return a Result, but it
       returns int
```

The first draft read those two diagnostics as a **language** rule against
fallible higher-order functions. It is not. A later probe declared the parameter
type explicitly:

```
fn msort(xs: [int], f: fn(int, int) -> Result(int, E)) -> Result([int], E)
```

and it compiles, accepts **both** a named function and a lambda in that position,
and `or_return` propagates a comparator failure out through it. So the true claim
is narrower and lands somewhere else: **it is `corelib/iter/iter.ty@filter`'s
signature.** `keep` is declared `fn($T) -> int`, which pins the lambda's return
type — a lambda's body is a single expression (`docs/spec/11-functions.md:70`) and
its return type is fixed by the parameter pattern it is passed to, so there is no
way to widen it to a `Result` and no way to write the handling `match` inline.

Two things follow, and the second is the one worth having. Every caller spells its
predicate as a 0/1 `int` in a language that has `bool`. And **`core:iter` has no
fallible counterpart at all** — no `try_filter`, no `filter` over
`fn($T) -> Result(int, $E)` — so every stage of a query engine that can fail, which
is most of them, is a plain `for` loop with `or_return` in the body instead.

**Cost to fix: a signature, not a language change**, which is precisely what the
correction bought. The broad version of this claim would have sent someone to the
compiler.

### Smaller than they looked once written down

Recorded because they were hit, ranked below the line because writing them out
shrank them. Padding this list would make the seven above harder to act on.

- ~~**There is no no-op statement, and the workaround is now known.**~~ —
  **CLOSED 2026-08-10.** `pass` is the no-op. Contextual rather than reserved,
  which this entry did not anticipate and the ROADMAP got wrong: `pass` was
  already a variable name in `corelib/test/testing` and `tools/prunner`, so
  reserving it would have broken the runner that scores `make test-fast`. The
  workaround below (bind something nobody reads) still parses; nothing had to
  change. Original text:
- **There is no no-op statement, and the workaround is now known.** An absent
  `where` should print nothing, so a `match` arm has no work — and an empty arm
  cannot be spelled (`pass` is not a keyword; same "must be a declaration,
  assignment, or call" error). The section above already records that a match arm
  cannot be empty. **What is new is the cheap answer for an arm in the middle of a
  statement sequence: a declaration IS a legal statement where a bare expression
  is not, so bind something** — `ok := sz`, `_ := 0`. Only when the match is the
  whole body is the more expensive lift needed, giving every arm a `return`;
  `tools/tycho-q/main.ty@where_line` and `tools/tycho-q/main.ty@limit_line` exist
  for that reason and no other. **Real, and two lines each.**
- **A cursor threaded by `inout` cannot also be passed by value in the same
  call.** `lex_string(s, n, pos, &pos)` is refused: "variable 'pos' is passed to
  an inout parameter and also by value in the same call ... the by-value copy
  would alias the inout'd value". **The diagnostic is good and the rule is
  right** — this is the aliasing bug it exists to prevent, caught at compile time.
  What it leaves is a shape rule for anyone writing a lexer here: a function
  wanting both "where I started" and "where I am" derives the first from the
  second on entry. **Cost: one line per lexer function, and no bug.**
- **A bare path in `from` cannot be absolute.** `from /tmp/x.csv` lexes the
  leading `/` as division and fails with "expected a source path after `from`" —
  a diagnostic about division for a problem about quoting. **A tool wart, not a
  language one**: `from '/tmp/x.csv'` works, the escape hatch predates the
  problem, and the fix is in this program's lexer. Recorded only because the
  diagnostic points away from the cause.

### What did not go wrong, which is also data

- **Recursive enums work, in both positions, and the phase that depended on it
  probed before writing.** `EBin(int, Expr, Expr)` (direct) and
  `ECall(string, [Expr])` (through an array payload) both compile in a
  `package main` program, with a `match` that recurses into itself from either.
  Zero diagnostics. The AST is therefore direct recursion and **not** the
  index-into-a-node-array fallback the plan had budgeted for.
- **`Option` and `Result` plumbing needed no workaround anywhere.** `Option(Expr)`
  and `Option(int)` are usable as struct fields over a program-local recursive
  enum, `None` infers its type from the field it is assigned to, and
  `Some(parse_expr(...) or_return)` composes. Given items 4 and 6 above, this is
  worth stating: what is missing from the `Result` story is a *void* payload and a
  *conversion*, not the plumbing.
- **First-class functions carry their weight.** A lambda with a `Result` return
  type is accepted; a closure capturing two arrays compiles and works; capture is
  by deep copy at creation, so the sort's key matrix is copied once rather than
  per comparison. `tools/tycho-q/main.ty@merge_sort` takes its comparator as a
  value and that is the ordinary way to write it here.
- **A qualified enum from another package works as a payload and as a parameter
  type** — `VDec(decimal.Decimal)` and `json.Json` — and nested patterns over
  another package's enum match directly, qualified: `Err(io.NotFound)` works,
  bare `NotFound` is "not a variant of io__IoErr", which is the right answer.
- **`core:decimal` preserves scale through a round trip.** `decimal.from_str("1.50")`
  renders back as `1.50`, so a decimal literal survives byte-exactly and `0.1 +
  0.2` is the three characters `0.3` — not `0.30000000000000004` and not `0.30`.
  There is no float anywhere in this program, and that is the reason a query tool
  could be written here at all.
- **The last of three phases compiled first try, zero diagnostics** — including
  the comparator sort, the closure and the JSON reader. This matches what the two
  re-scorings above found and continues to contradict the picture the older half
  of this file paints.

## Adversarial pass over the surface that shipped 2026-08-11 (head `fa4f5fc`)

Not a program being written — a hunt. Every feature that landed on 2026-08-11
had roughly one happy-path fixture and one reject fixture, so the combinations
no fixture covers were probed directly: ~45 programs, compiled with `./tychoc`
and run. One real defect came out (`struct Ok` and its four siblings declaring
cleanly and being unreachable — fixed in the same commit as this entry, see
`tests/reject/struct_named_ok.ty`). The rest of what turned up is below.

### 8. `iter.try_map` has no `Result(void, E)` shape, and says so from inside corelib

A callback answering `Result(void, E)` is the natural spelling for "walk these
and stop at the first failure, I want no values back" — validate each row, chmod
each path, `set_mtime` each extracted file. `try_map` cannot express it, because
its `$U` becomes `void` and its accumulator has no element type:

```
fn check(x: int) -> Result(void, string):
    if x < 0:
        return Err("neg")
    return Ok()
fn main():
    r := iter.try_map([1, 2], check)
```

```
corelib/iter/iter.ty:30: error: cannot infer the type of 'out' from this use
    30 |         push(out, f(v) or_return)
./main.ty:8: note: required from here -- this call instantiated the generic
     8 |     r := iter.try_map([1,2], check)
```

**The `note:` is the day's other feature working exactly as intended** — without
`4a7cca0` this was a bare corelib line and nothing else. It is still a message
about `out`, a local the caller has never heard of, for a problem that is "there
is no `try_each`". The workaround is a hand-written loop with `or_return`, three
lines, which is what `try_map` existed to remove. **Not fixed here: adding
`try_each` is a design decision about the package's shape, not a defect.**

> **Decided 2026-08-12: `try_each` is DECLINED. The loop is the answer.**
>
> Re-probed first, because the entry above predates `4a7cca0`. The diagnostic
> half is already fixed: the refusal now prints the corelib line *and* a
> `note: ... this call instantiated the generic` naming the caller's own line,
> so a reader is no longer sent only to a file they cannot edit. What remains
> is purely the question of whether the function earns its place.
>
> It does not. The whole of `try_each` is the loop it would wrap:
>
> ```
> for x in xs:
>     check(x) or_return
> ```
>
> Built and run: this compiles today, short-circuits at the first `Err`, and
> propagates it — `[1,2,3]` gives `ok`, `[1,-2,3]` gives `err neg`. Two lines,
> against one for `iter.try_each(xs, check) or_return`. That is the entire
> saving, and it buys a callback indirection in place of a loop body that can
> read the surrounding scope.
>
> `try_map` and `try_filter` earn theirs because they *build* something — an
> accumulator, a push, an order to preserve, and a documented first-error rule
> that is easy to get wrong by hand (swallowing the `Err` reddened `make
> corelib` when it was written). `try_each` returns `Result(void, $E)`: no
> accumulator, nothing to get wrong, nothing to preserve. It is the loop with a
> function call in front of it.
>
> **And no caller wants it.** Across `corelib/`, `tools/`, `examples/` and
> `server/` there are 129 `or_return` sites; the number that are a bare
> `f(x) or_return` directly inside a `for` loop — the exact shape `try_each`
> would replace — is **zero**. The motivating examples in this entry (chmod each
> path, `set_mtime` each extracted file) are hypothetical; none of them is in
> the tree. `6d498cf` declined `try_reduce`, `try_count` and `try_any` on
> precisely this ground, and declining here is the same rule applied to the same
> evidence, not a new policy.
>
> Reopen this if a real caller appears. A second one in the same file would be
> the honest trigger; one hypothetical is not.

### 9. A `string` across the FFI truncates at its first NUL, silently

> **Scoped wrong when filed, and fixed 2026-08-12.** This was entered as a
> `[string]` defect. It is not: the **scalar `string` parameter and the `string`
> return have the identical hazard**, so it is the whole `char*` boundary, in
> both directions. The narrow framing would have produced a narrow fix.

A Tycho string carries a length and may hold a NUL; a C `char*` carries no length
and ends at its first NUL. Nothing in the ABI can hold the difference. Probed on
one program (`--shim` over a C file reporting `strlen`):

```
A [string] C strlen sum = 4      # ["a\0c", "", "ee\xc8"] -- C sees 1 + 0 + 3
A tycho len sum         = 6      # Tycho sees 3 + 0 + 3
A elem[0] byte 2 via C  = 99     # 'c' IS there; only the length is lost
B scalar C strlen       = 1      # the SAME hazard without an array in sight
B scalar tycho len      = 3
C to_str len tycho      = 3      # to_str(to_bytes([104,0,105]))
C to_str C strlen       = 1
D bytes len C sees      = 3      # bytes crosses as (ptr,len) -- INTACT
D bytes byte 1          = 0
```

The **return** direction truncates too: a C function handing back a static
`{'h',0,'i',0}` yields a Tycho string of `len` 1, because the arena copy is
`strlen`-bounded (`runtime/tycho_rt.c@tycho_str_from_c`, whose own comment
already said so). A string *literal* cannot carry a NUL — `\0` is rejected as an
unsupported escape — so the value always arrives from `chr(0)`, from `to_str`
over a constructed `bytes`, or from C.

**Verdict: documented, not enforced.** Refusing at compile time is impossible
(the content is a runtime value; refusing the *type* would ban the FFI's commonest
parameter). Refusing at run time was measured and rejected: the borrow costs
**0.0 ns** — nothing is copied, which is its entire point — and a `memchr` guard
over a 2000×64-byte array costs **5.5 µs**, turning an O(1) borrow into an
O(total bytes) one, while covering only the outbound half; a `char*` out of C
offers nothing to compare against. A parallel length vector gives up the same
property and breaks every existing callee.

`bytes` crosses as `(ptr, len)` and preserves interior NULs exactly — verified
above — so it is the supported route, and `docs/spec/14-ffi.md` §24.1 now says
all of this under "Interior `NUL`s", covering the scalar parameter, the array
element and the return.

**The verdict holds for the boundary and does NOT hold for a validator built on
it — `core:regex` was fixed 2026-08-12, not documented.** All eight subject-taking
entry points answered "no match" for a payload sitting behind a NUL
(`find` = -1 where the same-run NUL-free control gave 4), which is a wrong answer
to a security question, not a stated limit. The fix is the shape this entry says
is supported: the subject crosses as `(pointer, length)` and `REG_STARTEND`
bounds `regexec` by it (`corelib/regex/regex_shim.c@rx_exec`); where that
extension is absent the shim dies loudly rather than truncating. The cost
objection above does not transfer — the length is `len(s)`, an O(1) header read,
not a `memchr`, and 200,000 calls measured 57-60 ms after against 59-62 ms
before. A NUL-bearing *pattern* is refused instead (`regcomp` has no
length-bearing form anywhere). **The general rule: a package whose answer is a
yes/no about untrusted bytes cannot inherit "documented, not enforced" from the
boundary it is built on.**

### What did not go wrong, which is also data

- **`is` is single-eval and short-circuits correctly.** `make(&c) is VB` on a
  call with an `inout` side effect calls once; `a is Some and boom(&c) is Some`
  leaves the counter at 0 when the left side is false. Codegen emits
  `gen_expr(e->lhs)` exactly once (`src/tychoc.c@TK_IS`).
- **`is` inside a generic body substitutes per instantiation, and refuses the
  wrong enum at the call site that caused it.** `fn isFirst(v: $T) -> bool:
  return v is A1` instantiated at `B2` gives "'A1' is not a variant of B" plus
  the `required from here` note. `v is Some` in a generic works at `Option(int)`
  and `Option(string)` from one body.
- **The uppercase-binding rule has no holes worth finding.** Probed at: match
  arm binder, tuple destructuring, `for` loop variable, `keys()` loop variable,
  lambda parameter, `:=`, and a `spawn` task binding — all refused with the same
  message. A struct FIELD and an `extern fn` parameter name may still be
  uppercase, and neither is a binding.
- **`[string]` over the FFI is right everywhere else probed.** Empty array, empty
  string elements, a 2000-element array built by `push` in a loop, an array
  literal written inline, a `[string]` read out of a struct field, and the same
  array passed to two parameters of one extern (the callee sees one pointer, not
  two copies — `p_two` returned `1303`). A `bounded[4]string` is refused at the
  call, which is right: it stores in `.v`, not `.data`.
- **`Result(void, E)` composes.** Three `or_return` steps in a chain stop at the
  failing one; `result.is_ok`/`is_err`/`err_or` and `r is Ok`/`r is Err` all
  answer at a void payload.
- **`try_map` short-circuits at the first, last and only element, and returns
  `Ok([])` for an empty input.** Nested `try_map` inside `try_map` over `[[int]]`
  propagates the inner `Err` unchanged.
- **`io.set_mtime` behaves at every edge probed.** A directory is `Ok` (as
  documented), a symlink follows to its target, a missing path and `""` are both
  `Err(NotFound)`, a mode-444 file the caller owns is `Ok` (POSIX: the owner may
  always set times), and a negative or year-2100 stamp round-trips through
  `io.mtime`.
- **Operand and argument evaluation order is NOT left-to-right, and that is
  correct.** `str(bump(&c)) + "|" + str(c)` prints `1|0` and `pair(bump(&e), e)`
  prints `1,0`, while the f-string spelling prints `1|1`. This looked like the
  same defect `77bd826` had just fixed; it is not.
  `docs/spec/09-expressions.md:168` makes argument and operand order
  *unspecified* deliberately, and `docs/spec/appendix-f-impl-defined.md:14` lists
  the f-string holes as the named exception. The feature that shipped is exactly
  as narrow as it says it is.

## Re-scored against five programs written 2026-08-12 (head `77bd826`)

Five ordinary programs, written the way a newcomer would write them rather than
to probe anything. Two of the three findings below are **combinations of
deliberate decisions**, not defects in either half — recorded because the cost
only appears when the two meet, and neither entry proposes reversing either
decision. The third is the opposite of a defect: an API that already exists and
was not found.

### 10. A two-key comparator cannot be written inline, and the composition that replaces it is invisible

Two decisions, each right on its own. Closures are **single-expression only** —
`fn(x: int) -> int: x + n`, no block body:

```
    f := fn(x: int) -> int:
        if x > 0:
            return x
        return 0
```
```
./blk.ty:3: error: expected an expression
     3 |     f := fn(x: int) -> int:
       |                            ^
```

And a ternary is refused **by decision** (`CONTRIBUTING.md:168-169`, on the
"please don't propose them" list). Neither is in question here.

Their combination is: `sort.sort_by(xs, cmp)` (`corelib/sort/sort.ty@sort_by`)
takes a comparator that must be one expression, and a two-key sort — count
descending, ties alphabetical — needs a branch. A **named** function cannot
substitute, because the comparator has to see the map being sorted on and Tycho
has no globals, so the capture must be a closure. That closes every direct
route, and the honest conclusion a reader reaches is "sorting by two keys is
not expressible".

**It is expressible, by composing two stable sorts** — sort by the weaker key
first, then stably by the stronger:

```
    ks := arrays.sort(keys(m))                                   # weaker key: alphabetical
    out := sort.sort_by(ks, fn(a: string, b: string) -> int: m[b] - m[a])
```
```
fig 5
apple 3
pear 3
date 1
```

`apple` before `pear` at equal counts: the alphabetical pass survives the stable
count pass. Each sort now needs only one key, so each comparator is one
expression and the restriction never binds.

**This is already the idiom in this tree** — `corelib/test/wordfreq/main.ty:26-30`
does exactly it with `arrays.sort` then `sort.argsort_desc`, and its header calls
it out ("lexicographic sort first, then a STABLE `argsort_desc` on counts").
**Nothing is proposed here.** The cost is purely that the technique is invisible
before you know it: the failure mode is a reader concluding the language cannot
do something it does routinely, and neither the closure error nor `sort_by`'s
signature points at the composition.

### 11. A first `--shim` C file must hand-declare `tycho_int`, because there is no header to include

A shim is its own translation unit, compiled standalone — which is the whole
point of `make shim-check` (`Makefile:298`, "every corelib `<pkg>_shim.c` must
compile ON ITS OWN under `-std=c11`"). There is no generated `tycho.h` in the
tree; `find . -name 'tycho*.h'` returns only `build/tycho_rt_embed.h`, which is
the embedded runtime source, not a public header. So an author's first
`--shim` file cannot include anything that defines the FFI integer type, and
must write the guarded typedef by hand:

```c
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif
```

`corelib/strings/strings_shim.c:77-80`. **All 13 shims in the tree carry it**
(`grep -l tycho_int corelib/*/*_shim.c | wc -l` is 13, against 13 shims total),
and its own comment says why the guard and the fixed width are both load-bearing:
"to match the FFI ABI on ILP32/LLP64, not just LP64".

What a first-time author has to know, none of it written down in one place: the
typedef, the `#ifndef` guard (two shims in one link would otherwise collide), the
exact width, and `<stdint.h>` ahead of it. A generated `tycho.h` shipped beside
the corelib would remove all four. **Not built here** — it is a new installed
artifact with its own versioning and layout questions, and this entry only
records the toll, which is one copied block per shim and is paid once.

### What did not go wrong, which is also data

- **`core:strings` is not missing `split`; `split` is a builtin.** This was
  written up as the day's most likely newcomer-bouncer — a word-frequency
  program hand-rolled a character scanner because
  `grep -E '^fn .*-> \[string\]' corelib/strings/strings.ty` returned only
  `lines`. That grep cannot find it: `split(s, sep) -> [string]` is a **language
  builtin**, specified at `docs/spec/16-builtins.md:150` and registered at
  `src/tychoc.c:5179@.name="split"`, so it is in no package at all.
  `corelib/strings/strings.ty:254` says so in a comment one line above `lines`
  — "(split(s, sep) and find(s, sub) are language builtins -- not duplicated
  here.)" — and `corelib/test/wordfreq/main.ty:22` is a word-frequency program
  in this tree using it, commented "# split is a builtin". Fifteen-plus files
  across `corelib/`, `examples/` and `tools/` call it.

  Behaviour, probed directly: `split("a,b,c", ",")` -> `[a, b, c]`,
  `split("", ",")` -> `[]`, `split("a,,b", ",")` -> `[a, , b]` (the empty field
  is kept), `split("a,", ",")` -> `[a, ]` (a trailing separator DOES add an
  empty element, unlike `lines`), `split("a::b", "::")` -> `[a, b]`. An empty
  separator aborts at run time by design (§29.12).

  **Adding `strings.split` would have been actively harmful**, not merely
  redundant: `split` is on the shadowed-builtin list
  (`src/tychoc.c@shadows_builtin`), so a `fn split` in `corelib/strings` would
  have emitted the exact warning entry #B of this session removed — into every
  build importing `core:strings`, one of the most-imported packages in the tree.
  The friction is real but it is **discoverability, not surface area**: the
  builtins live in the spec and in the compiler, and a reader looking for a
  string operation looks in the string package. A `fields(s)` whitespace split
  was considered on the same evidence and declined for the same reason —
  `split(s, " ")` plus the `strings.trim` already used at
  `corelib/test/wordfreq/main.ty:23` is the composition, and the tree's own
  dogfood is written that way.

### 12. A `for` binding does not destructure a tuple, and a tuple is not indexable

Found writing a dependency resolver: `for name, src in [("resolvable", ok),
("cyclic", bad)]:` is refused with `expected ':' before the block`, pointing at
the comma. The same destructuring in an assignment is fine and is used earlier
in the same program — `item, deps := strings.split_once(line, ":")` compiles,
and `corelib/strings/strings.ty@split_once` exists to be consumed that way. So
the construct is available in one binding position and not the other.

The obvious fallback does not work either: `for p in xs:` binds, but `p[0]` is
refused with `can only index an array, a string, bytes, or a map (as a place)`.
A tuple has no positional accessor, so a single-binder loop cannot reach the
elements directly.

**There is a working form, one line longer**, and it is worth knowing because
the two refusals above read like "arrays of tuples are unusable":

```tycho
for p in xs:
    k, q := p            # destructure the loop variable in the body
    println(k + str(q))
```

Probed at head `2f407ed`: the `for k, q in xs` form fails to parse, the `p[0]`
form fails to resolve, and the body-destructure form builds and prints `a1 b2`.

Not proposed as a change. `:=` destructuring already carries the feature, so
extending it to a `for` binding is a parser question rather than a type-system
one — but the cost today is that a reader meets two different refusals before
finding the form that works, and neither message mentions it.

### 13. A package-qualified function was not a value, and the message said the name did not exist — FIXED

Found writing a fold over `core:decimal`. A local function is a first-class
value and the tree's own fixture says so (`tests/pkg/fnval/main.ty` covers the
same-package case). The cross-package spelling was refused:

```
    println(str(apply2(math.min, 2, 3)))
```
```
./main.ty:9: error: package 'math' has no variant or const 'min'
```

**The asymmetry is what made it expensive.** `math.min` exists, and calling it
one line earlier compiles. The message names the one thing that is not wrong, so
the reader goes looking for a typo. Worse, it was the SAME message for three
different situations — a genuine typo, a generic function, and a perfectly valid
non-generic one — so it could not distinguish "you misspelled it" from "this
form is not supported here".

The first guess was that genericity was the cause: `math.min` is
`fn min(a: $T, b: $T) -> $T where comparable(T)`, and a generic function
genuinely has no single value form. **That guess was wrong.** A non-generic
package function was refused identically:

```
    println(apply1(strings.to_upper, "hi"))    # strings.to_upper(s: string) -> string
```
```
./main.ty:9: error: package 'strings' has no variant or const 'to_upper'
```

So the restriction was incidental, not designed. `docs/spec/09-expressions.md`
§13.6 says a lambda "and a named function" both produce function values and
lists exactly two exclusions — an `inout` parameter, and comparability. A
package-qualified name was never excluded; the resolver's `E_FIELD` arm simply
tried const, then variant, then died, and never asked whether the name was a
function. The machinery it needed was already three hundred lines away in the
`E_IDENT` arm, which had handled the *same-package* mangled form since
`tests/pkg/fnval` was written — including the `__clo` thunk, which is emitted per
mangled name and so needed nothing new.

**Verdict: CONTAINED, implemented.** One block in one arm, reusing that
machinery. A non-generic `pkg.fn` is now a function value
(`tests/pkg/fnvalcross/`). A generic one is still refused, because there is no
instantiation to take — but it now says so and names the workaround:

```
error: 'math.min' is generic, so it has no single function value -- there is no
instantiation to take. Wrap it in a lambda that fixes the types, e.g.
fn(a: int, b: int) -> int: math.min(a, b)
```

The genuine-miss message was widened to "variant, const or function" and gained
the did-you-mean the call path already had. Both are pinned by `# expect:`
fixtures (`tests/reject/pkg/fnval_pkg_generic/`, `.../fnval_pkg_missing/`).

**Still open, filed rather than fixed:** a *local* generic used as a value dies
`unknown variable 'mymin'; did you mean 'main'?` — the same false-message shape,
in the `E_IDENT` arm, which does not consult the generics registry. And
`(pkg.fn)(x)` — the parenthesised immediate call — dies `unknown variable 'opsx'`,
because a call-on-expression resolves its callee before the package logic runs.
Neither is the reported shape; both are in `plan.md`.

## Re-scored against `tools/tycho-db`, 2026-08-12 (head `b7c58a36`)

Six packages, ~3k lines, a WAL, an index and a TCP server. It found one real
corelib gap (no `fsync`, closed by `io.sync` in `4a0a6116`) and two papercuts.
It also produced two *false* findings — the package-import rule and the
cross-package match-arm rule were both already documented, and were reported
as discoveries by people who had not read `docs/guides/packages.md:118` and
`docs/spec/12-aggregates.md:831`. That is the same failure as a stale
document, pointed the other way, and it is why both are named here.

### 14. ~~`pass` exists and nobody reaches for it~~ — **CLOSED 2026-08-12 by placement**

Writing `tools/tycho-db/main.ty`, a match arm that ignores a flush result was
written as a call to a hand-rolled `fn nop(): return`. `pass` is the language's
no-op statement, shipped for exactly this, and reads as intent:

```tycho
match store.flush(db):
    Ok(x): pass
    Err(e): return Err(store.err_str(e))
```

Probed at head `c56fbf6d`: the `pass` form builds. Nothing was broken — the
dummy function worked — so no gate could ever have caught it.

The shape is `split`'s (entry #8 of the previous scoring): the feature exists,
and nothing points at it from where a writer needs it. The remedy is
documentation placement, not syntax.

**Where it was, checked 2026-08-12.** The claim above that "the language
reference for statement positions is not where `pass` is introduced" was half
wrong, and the true half is worse. `pass` WAS in the statements chapter — as a
subordinate clause of one sentence about blocks not being empty
(`docs/spec/10-statements.md:12-21`), with no heading, no example and no entry in
any list of statement forms. A reader scanning §14's headings for what they may
write saw `if`, `match`, `for`, `break`, `continue` and `return`, and never saw
`pass`. Its only other homes were `docs/spec/appendix-b-keywords.md` (a
contextual-keyword table read by nobody looking for a statement) and
`docs/spec/appendix-e-conformance.md` (a fixture index). It appeared nowhere in
`docs/reference/`, the reader-facing guide, at all — including
`docs/reference/basics.md`'s "Control flow" section, which lists `break` and
`continue` and is precisely where the writer who hand-rolled `nop` would have
looked.

**Where it is now.** `docs/spec/10-statements.md` §14.1.1 — its own numbered
subsection, with the grammar line, a compiled `match`-arm example, and the
contextual-not-reserved rule; §14.1's clause now points at it. And
`docs/reference/basics.md`'s "Control flow" section, where it joins `break` and
`continue` in the keyword block and gets the same example plus a link to
§14.1.1. Numbering below §14.1 is untouched, so no cross-reference moved.
Nothing about the language changed.

### 15. ~~`or_return` on a `Result(bool, E)` is a three-time papercut~~ — **CLOSED 2026-08-12; the entry's own reasoning was wrong**

`or_return` yields the `Ok` payload, so on a `Result(bool, E)` it produces a
`bool` the statement discards, and the compiler refuses:

```
error: `or_return` here produces bool, which this statement discards -- bind it (x := ... or_return)
```

The message is good — it names the remedy on the same line. It is recorded
because of the *rate*: it was hit three separate times on 2026-08-12, by three
different pieces of work (the `hexed` probe program, the `io.sync` wiring at
five sites, and `tycho-db`'s parser), each time by someone who had already read
the rule. `io.write_bytes`, `io.write_at` and `io.make_dir` all returned
`Result(bool, IoErr)` when this was written, so the common shape "do the write,
propagate the error, ignore the bool" cost a binding every time.

The paragraph that stood here said this was "not proposed as a change, because
the alternatives are worse: a `Result(void)` `io` would lose the 'did it write'
answer". **That was wrong, and nobody had read the four bodies.** `write_bytes`,
`write_at`, `set_mtime` and `sync` each returned `Ok(true)` on the success path
and an `Err` on every other, so there was no "did it write" answer to lose —
`Ok(false)` was unreachable in all four, and the bool the caller bound could
only ever be `true`. The cost was not known and deliberate; it was a placeholder
nobody had questioned.

**Closed 2026-08-12**: the four now return `Result(void, IoErr)` and
`io.write_bytes(p, b) or_return` is a statement. Six call sites moved
(`tools/tycho-fetch`, `tools/tycho-ar`, `tools/tycho-kv`, `tools/tycho-db`'s
store and log, `corelib/test/io`), each a `match` arm losing an unread binding —
no guard was deleted, because no caller had ever written one against the
impossible `Ok(false)`, which is itself the evidence that the payload said
nothing. `corelib/test/io/main.ty@durable_write` chains all four under bare
`or_return`s so the shape is a compiled assertion rather than a claim.

`is_dir`, `make_dir` and `remove` KEEP their `Result(bool, IoErr)`, and the
distinction is the whole point: their `Ok(false)` is a real second answer — "it
was already a directory", "there was nothing there" — reached by a real input.
The test is not "does the bool look redundant" but "can the false arm happen".

## Re-scored against `tools/tycho-flow`, 2026-08-12 (head `47b6d5b7`)

A concurrent pipeline engine over generic channels: stages, fan-out, typed
hand-offs. Two findings, one a real compiler defect with the worst failure mode
a transpiler has, one a deliberate guard whose cost had never been written down.

### 16. ~~`parallel for` did not capture a function value — the error escaped to `cc`~~ — **CLOSED 2026-08-12**

Calling a fn-typed local from a `parallel for` body compiled to C that would not
build:

```tycho
fn apply_all(f: fn(int) -> int, xs: [int]) -> int:
    total := 0
    parallel for i in 0..<len(xs):
        total = total + f(xs[i])
    return total
```

```
x.c: In function 'h___par0': error: 'h_f' undeclared
```

tychoc accepted the program and the user was shown a diagnostic about generated
source they never wrote, naming a mangled C identifier and a line in a file they
had not asked for. That is the failure mode a transpiler exists to prevent, and
no gate covered it: `tests/conc/parfor.ty` and the other parfor fixtures all
call *global* functions, which resolve inside the lifted chunk proc and need no
capture at all.

**Why the scan missed it.** A call's callee is not a child expression of the
E_CALL node. `f(x)` keeps the name in `sval`, and `o.f(x)` keeps the receiver in
`qual`, because the parser cannot tell that form from a package call
(`src/tychoc.c@parse_postfix`). `src/tychoc.c@pf_scan_expr` walked `lhs`, `rhs`
and `args`, so it saw neither — an int or an array in the same body was captured
correctly, because those appear as E_IDENT children. The asymmetry is the tell:
the *lambda* capture analysis had covered the `sval` half since it was written
(`src/tychoc.c@collect_idents`, with a comment saying exactly why), and the
parfor scan — written later, against the same AST — did not.

**Refusal was never on the table.** Nothing about a fn value resists lifting:
`src/tychoc.c@type_is_heap` already answers 1 for `IS_FUNC` and `copy_into`
re-homes a closure's env into the chunk task's arena, which is precisely why the
`fs := [f]` array workaround people found by accident worked. The fix is one
`E_CALL` arm feeding a new `src/tychoc.c@pf_capture_name`, which takes a name
rather than a node and applies the same task-handle and handle rejections the
E_IDENT path does; non-locals (global fns, builtins, enum constructors, real
package qualifiers) fail `vars_find` and are dropped, so nothing that used to
compile stops.

`tests/conc/parfor_fnval.ty` covers all four spellings the report named — a
parameter, a local holding a plain fn reference, a local holding a closure with
a heap capture, and a struct field — as reductions, so the answers are
chunk-count independent, and it runs under ASan/UBSan/TSan like the rest of the
lane. Stashing the fix and rebuilding reddens it on every spelling
(`h_f`, `h_g`, `h_scale`, `h_lbl`, `h_o` undeclared).

### 17. `Item($A)` is rejected where `Item($T)` is accepted — **verdict: deliberate and permanent, now documented**

A generic aggregate may be applied only to its own declared parameter *names*:

<!-- fence-skip: this is the REFUSED program -- compiling it is the finding -->
```tycho
struct Item($T):
    v: $T

fn relabel(it: Item($A), f: fn($A) -> $B) -> Item($B):    # refused
    return Item(f(it.v))
```

The diagnostic is explicit and names both legal forms, so this reads as a guard
rather than an oversight — and it is one (`src/tychoc.c@parse_type`, the
`has_typaram(args[i])` test after the self-reference shortcut).

**What the guard actually tests, checked at the source.** Not scope: each
argument is compared against the type parameters of the *declaration being
applied*. `Item($T)` inside a generic function passes only because a `$Name`
interns to one program-wide type (`src/tychoc.c@typaram_of`), so the function's
`$T` and the struct's `$T` are literally the same `Type`, and the application
takes the recursive-self-reference early return that keeps the template for
`subst_type` to finish. Rename either and it fails: `Item($U)` in
`fn relabel(it: Item($T), f: fn($T) -> $U) -> Item($U)` is refused on the return
type, verified 2026-08-12. So the accepted spelling works by name coincidence,
which is exactly why the restriction is invisible until a stage needs to change
the parameter.

**The cost, measured in `tools/tycho-flow`.** Every generic stage had to be
endomorphic — same instantiation in and out — because a type-CHANGING stage
(`Item($A)` -> `Item($B)`) cannot be spelled. Tuples were the obvious escape and
are not one (see below).

**Verdict (b): deliberate and permanent, and it is now in the spec.**
`docs/spec/05-generics.md` §7.3 previously stated the rule in one sentence and
said nothing about why or what it costs; it now carries the reason (an applied
aggregate type is one interned id naming the template or a completed instance,
with no third form holding pending arguments), the endomorphic-stage
consequence, and the note that `Item($T)`'s acceptance is the recursive path
rather than scoping. Lifting it means introducing a deferred-application type
and teaching every consumer of an aggregate type about it — not the five-arm
shape of `4a0a6116`-era fixes, and not planned.

### 18. ~~`(int, $T)` does not match a `(int, int)` argument~~ — **CLOSED 2026-08-12**

Found while looking for a way around #17. This one is **not** deliberate:

<!-- fence-skip: this is the REFUSED program -- compiling it is the finding -->
```tycho
fn first(p: (int, $T)) -> int:
    return p.0

first((1, 2))    # error: argument 1 of 'first' is (int, int),
                 # which does not fit the parameter pattern
```

`Channel((int, $T))` against a `Channel((int, int))` fails the same way, and the
channel is incidental — the bare tuple above fails on its own, so the miss is
tuples, not the `Channel` arm added in `f0fd19e5`.

**Size: the same five-arm shape as that commit, and its diff is the template.**
`src/tychoc.c@match_type`, `@subst_type`, `@has_typaram`, `@type_mentions_tp`
and `@type_mangle_ident` each descend into arrays, `Option`, `Result`, maps,
channels and fn types; none has an `IS_TUP` case, though `src/tychoc.c@type_is_heap`
shows the recursion is already written elsewhere in the file. Expect one arm
each plus a fixture in the shape of `tests/generic_channel.ty` — a tuple
parameter at a scalar and at a heap element, and a nested `Channel((int, $T))`.
Filed rather than implemented: it is its own change with its own gate run.

**Closed by the arm audit below**, which found the estimate right on shape and
short by one: the five arms landed as written, and `soa` needed the same
treatment plus three emit-loop guards. `tests/generic_tuple_param.ty` is the
fixture.

### 19. A generic struct's `[fn($T) -> $T]` field escaped to cc — **fixed**

Found writing `tools/tycho-flow/graph/`, whose header recorded it as a reason
the pipeline holds four separate fn fields instead of an array of steps.

<!-- fence-skip: this is the program that failed -->
```tycho
struct Plan($T):
    steps: [fn($T) -> $T]        # x.c: unknown type name 'FnC0'
```

**The asymmetry, in one line.** A `fn($T) -> $T` typedef is deliberately NOT
emitted — `src/tychoc.c:12750` skips any function type mentioning a type
parameter, because `$T` lowers to `void` and a `void` parameter is invalid C.
But the composite-array BODY loop emitted `struct TychoArrC0_ { FnC0 *data; }`
for the template's dead `[fn($T)->$T]` anyway, naming the typedef that was just
skipped. Every other `g_arrtypes` loop in the file — the op prototypes, and the
three body loops — already guarded with `has_typaram(T_ARRC_BASE + i)`; the
body loop was the one that did not. That is the same shape as `f0fd19e5`'s five
missing `IS_CHAN` arms: a recursion complete everywhere but one site.

**The fix is that guard, added.** The tag stays forward-declared and simply
never completed, which is what a template that is never instantiated wants;
instances intern their own concrete `FnC` and are unaffected. The stale comment
claiming the recursive-generic-enum case NEEDED that dead body was corrected in
the same commit — it did not.

**The SIGSEGV in that header was never real.** `graph/graph.ty` claimed the same
declaration reached from a second package made tychoc die with SIGSEGV before
emitting. It does not. Re-probed three ways — across packages, with the struct
never instantiated, and with four fn fields alongside the array — every
arrangement produced the identical `FnC0` cc error and exit 1, never a crash.
The header now says so; a false crash report in a source comment costs more
than the bug it describes.

`tests/generic_fn_array_field.ty` pins two instantiations (`int` and `string`,
proving each instance gets its own `FnC`) plus an uninstantiated template, which
failed the same way with no value of the type anywhere in the program.

## Type-walker arm audit, 2026-08-12 (head `75ef1466`)

Three compiler bugs in one day shared one shape — a recursion or a guard
present at four sites and missing at the fifth (`f0fd19e5`'s five `IS_CHAN`
arms, `75ef1466`'s one `has_typaram` guard of five loops, `58134fcd`'s callee
case in one AST walk but not its sibling). This is the deliberate sweep for the
rest, done by rebuilding the type-kind matrix from the source and writing a
probe program for every hole rather than reasoning about which look suspicious.

**Eight holes probed, three real.** The ragged matrix was a bad predictor: the
two holes the brief flagged as "silent-wrong-answer, probe hard" turned out to
be cosmetic, and the one it flagged as merely-generalising turned out to have a
sibling nobody had listed.

| Hole | Probe | Verdict |
|---|---|---|
| `TUP` in the five generic walkers | `second((1, 7))` | **real** — FRICTION #18, fixed |
| `SOA` in the same five | `soa [Box($T)]` param | **real** — reached `cc` as `S_Box`, fixed |
| `qual` in `collect_idents` | `m.get(k)` inside a lambda | **real** — reached `cc` as `h_m undeclared`, fixed |
| `CHAN` in `type_is_heap` | `d := c; send(d, 5)` | deliberate-and-correct, commented |
| `FUNC` in `type_mangle_ident` | `gid(add)` | benign: unique but unstable name |
| `STRUCT`/`ENUM`/`FUNC` in `type_mentions_tp` | `unbox(Box(7))` | benign: over-names, cannot collide |
| `soa [$T]` | direct | unreachable — refused at parse (`src/tychoc.c@parse_type`) |
| channel in an aggregate | `[Channel(int)]` | unreachable — refused by `chan_container_err` |

### 20. `soa [S($T)]` escaped to cc — **fixed**

The `soa` column looked like a non-question: a `soa` element **must** be a
struct, so `soa [$T]` cannot be written and there is nothing for a generic
walker to descend into. That is true and it is not the whole answer — a generic
struct *is* a struct at parse time, so `soa [Box($T)]` parses, and then:

<!-- fence-skip: this is the program that failed -->
```tycho
fn count(s: soa [Box($T)]) -> int:
    return len(s)
```

```
x.c: error: parameter 3 ('v') has incomplete type
     static void Soa0_push(Arena *a, Soa0 *s, S_Box v) {
```

`has_typaram` had no `IS_SOA` arm, so the template-only type was not transient
and the three `g_nsoatypes` emit loops stamped out helpers naming `S_Box`, the
template struct, which has no C body. `subst_type` left the parameter a template
`soa` and `match_type` could not bind `$T` from a `soa [Box(int)]` argument, so
even the accepted half was wrong.

This is `75ef1466` again at a different table: the `g_nchantypes` emit loop has
carried a `has_typaram` skip since `f0fd19e5`, and every `g_arrtypes` loop got
one at `75ef1466`; the `soa` loops had none. **Checking the emit loops is now
part of the fix, not a follow-up** — an arm added to `has_typaram` only helps
where something consults it.

`tests/generic_soa_param.ty` pins two element types plus a never-instantiated
generic, which is the emit-loop guard specifically.

### 21. A lambda did not capture a method-call receiver — **fixed**

The mirror image of `58134fcd`, and the reason to compare sibling walks
arm-for-arm rather than fix the one that was reported:

<!-- fence-skip: this is the program that failed -->
```tycho
m : [string: int] = ["a": 1]
f := fn(k: string) -> int: m.get(k, 0)
```

```
x.c: In function 'h___lam0': error: 'h_m' undeclared
```

`58134fcd` taught `src/tychoc.c@pf_scan_expr` both names a call carries outside
its child expressions — the callee in `sval` and a method receiver in `qual` —
and its commit message says in as many words that the lambda side "already does
the `sval` half". It did. It did not do the `qual` half, and nothing in the
tree tested it. So the fix that closed the asymmetry in one direction left it
open in the other, one function away.

The surface is wide, not a corner: every receiver form routes through `qual` —
builtin UFCS (`xs.len()`), map `.get`, user UFCS methods, fn-typed struct field
calls, user subscripts, and channel/task methods. Any of them, read inside a
lambda over a captured local, produced a `cc` error naming a mangled identifier
in a file the user never wrote. `tests/lambda_capture_qual.ty` takes one case
per resolver arm, because only the capture list is shared between them.

### The three that were not defects, so the next audit does not re-litigate them

Each now carries a one-line comment at its site.

**`type_is_heap` has no `IS_CHAN` arm, and must not.** A channel is a shared
handle: `d := c` has to alias the one queue, and a deep copy would silently
split a rendezvous in two — the probe sends on `d` and receives on `c`. It is
also unreachable through the aggregate arms, since arrays, tuples and structs
all refuse a channel element.

**`type_mangle_ident`'s `t%d` tail is unique, just unstable.** The brief
expected mangling collisions from the missing `FUNC` and `TUP` arms. There are
none and there cannot be: the type ranges are disjoint, so no two types share a
number. What the fallback costs is a name that moves with unrelated edits
(`gid__t16384`), which is the same reason `f0fd19e5` added the `CHAN` arm — so
`TUP` and `SOA` arms went in with the rest, and `FUNC` is left as the one
remaining case with a comment saying the tail is a naming choice, not a hazard.

**`type_mentions_tp` cannot collide by missing an arm.** Its answer decides
whether an unpinned `$T`'s binding is appended to the instance name. A false
negative therefore *adds* a redundant suffix (`unbox__Box__int__int`) and a
collision would need a false positive, which no missing arm can produce. `TUP`
and `SOA` arms went in to drop the redundancy; `STRUCT`/`ENUM`/`FUNC` stay out,
because recovering `$T` from a generic instance's provenance is `match_type`'s
job and duplicating it here buys nothing.

### What the audit says about the method

Rebuilding the matrix was worth the time and reading it was not. Of the four
candidates the ragged matrix suggested, two were real and two were noise, and
the third real defect — #21 — is not in the matrix at all, because it is on the
expression side. **The generalisable rule is the one `58134fcd` and #21 share:
when a fix closes an asymmetry between two walks, check the other direction in
the same commit.** Both of this audit's expensive findings escaped to `cc`,
which is the failure mode a transpiler exists to prevent, and neither had a
gate.

## Found by `tools/tycho-sheet`, 2026-08-12 (head `6f7954a9`)

A spreadsheet is the first program in this tree that has to **save a float and
read it back**. It could not use `str`, and the workaround it shipped —
`tools/tycho-sheet/cell/dtoa.ty@render`, a strtod-verified shortest-decimal
renderer written in Tycho — is the evidence: a tool does not reimplement a
builtin unless the builtin is wrong.

### 22. `str(float)` emitted text the program could not read back — **fixed**

`runtime/tycho_rt.c@tycho_float_to_str` formatted with `%.15g`. Fifteen is
`DBL_DIG`, which guarantees **text -> double -> text**; binary64 needs
`DBL_DECIMAL_DIG` = 17 for **double -> text -> double**, which is the direction
`str` is used in. Three measured consequences, worsening:

| `str(x)` | emitted | reading it back |
|---|---|---|
| `0.1 + 0.2` | `0.3` | a **different** double |
| `9007199254740992.0` (2^53) | `9.00719925474099e+15` | not an integer any more |
| `DBL_MAX` | `1.79769313486232e+308` | **above** `DBL_MAX` — `strings.parse_float` refuses it as `Overflow` |

The last one is the sharp edge: `str` produced a decimal that this project's own
parser rejects, so a value at the top of the range could be printed and never
read. Nothing warned; the text looked fine.

**Fixed by shortest-round-trip, not by `%.17g`.** The renderer now tries
`%.15g`, `%.16g`, `%.17g` and returns the first that `strtod` reads back
unchanged. Both options fix every row above; the choice was about what happens
to the values that were *already* correct.

- `%.17g` is one `snprintf` and makes every float print at full width —
  `str(0.1)` becomes `0.10000000000000001`. `str` is user-facing output here.
- Escalation keeps the short text wherever 15 digits already round-tripped.
  **Measured: of 660 `make test` fixtures, exactly one golden line moved**
  (`0.1+0.2`, in `tests/float_str_locale.out`, once per locale block), and
  `make corelib`, `make corelib-examples`, `make conc`, `q-check`, `ed-check`,
  `db-check` and `flow-check` all stayed green with no re-record at all. A
  blanket `%.17g` would have moved every one of those that prints a float.

  The only other movement in the tree was **five lines in
  `tools/tycho-sheet/expected.out`**, all in the transcript that *reported* this
  bug — three `str` columns next to a `render` column that already had the right
  digits, and the two-line probe whose whole job was asserting `str` was lossy.
  That probe is **inverted, not deleted**: it now reddens if the fix is reverted.
  It is the one lane in the tree that could have caught this, and it did.

**Cost, measured** (300000 values, gcc -O2, three runs, this box): `%.15g`
134-154 ns/call, `%.17g` 145-149, escalating 423-448. About 290 ns, paid only by
values that need it — a first-try hit costs one `strtod` more than the old code.
The candidate is always **checked**, so the digit count never has to be guessed
right; a wrong guess costs a retry, never a wrong number.

**What did not change.** The `uselocale` guard and the `".0"` suffix for
integral floats are both untouched, and both still have to be: `make
locale-check` passes, `str(3.0)` is still `3.0`, and the suffix scan is still
sound only because the separator is known to be `.`. Formatting and re-parsing
happen inside the same locale on both legs, so the fallback leg compares
comma-decimal text against comma-decimal `strtod`.

`f32` prints longer now (`str` promotes to `double`, and an f32's exact double
value usually needs 17 digits). No golden in the tree moved for it, and the new
text is the honest one: it round-trips the `double` `str` was handed.

Locked by `tests/float_roundtrip.ty`, whose `rt15=` column is the negative
control — it re-runs the same check against a forced `%.15g` through
`tycho_test_float_roundtrip_prec`, so the five zeroes in that column are the old
defect reproduced in the golden. Without it a column of `rt=1` would only prove
the hook is cheerful.

### 23. `strings.parse_float` refused every subnormal — **fixed**

Measured before the change: `parse_float("2.2250738585072014e-308")` was `Ok`
and **everything smaller was `Err(Underflow)`** — `5e-324`, `1e-320`, the lot.
That boundary is DBL_MIN, the smallest *normal*. Every subnormal, a third of a
binary64's exponent range, was unreadable.

**Was it deliberate?** Half. The `Underflow` variant carried a comment saying
the library "would round it to 0 or to a subnormal that has already lost
digits, and either is a silent wrong value" — so somebody thought about it and
wrote it down. The reasoning does not hold, and its own file says why: the
`PRECISION` paragraph three lines above accepts
`parse_float("1234567890123456789012345")` as `Ok(1.2345678901234568e24)`.
Losing digits to rounding is the documented, accepted behaviour at the top of
the range. A subnormal is the *correctly-rounded nearest binary64*, exactly like
that one. Two ends, two policies.

**The cause was a flag test where a value test belonged.** The shim did
`*status = isinf(v) ? OVERFLOW : UNDERFLOW` for any `ERANGE`, and glibc raises
`ERANGE` for gradual underflow as well as total underflow. Now the **value**
decides: an infinity is `Overflow`, a `0` is `Underflow`, and anything else is
returned. `1e-400` is still refused — it rounds to `0`, and `0` is not what the
caller wrote.

That test is also right about a case a `issubnormal` check would get wrong.
glibc raises `ERANGE` for a decimal merely *near* the boundary even when the
rounded result is normal — `strtod("2.2250738585072012e-308")` sets `ERANGE`
and returns `DBL_MIN`. The old code refused that string; testing the value
accepts it.

**It was the other half of #22.** `str` now emits the shortest decimal that
reads back unchanged, so `str(5e-324)` is `4.94065645841247e-324` — a string the
parser in the same standard library would not read. Neither fix alone closes the
round trip; `corelib/test/strings.out` now asserts the pair with
`str -> parse_float -> the same double` over `0.1+0.2`, `DBL_MAX`, `5e-324`,
`2^53` and `-0.0`.

**What moved.** `make corelib` gained eleven lines and changed none. The visible
consequence is in the spreadsheet that reported it: `tools/tycho-sheet`'s demo
had `=1e-308` rejected as a *malformed number* and `=1e-300/1e10` rendering
`#NUM!`, both purely because of this. Both now hold their value, its
98411-value round-trip corpus lost its one permitted failure, and `render()`'s
`#NUM!` arm became unreachable from any input. **The arm stays** — it is the
fail-closed answer to "these digits do not name this value" — and the gate now
declares it unreachable *and asserts nothing reaches it*, so the declaration
reddens in both directions rather than becoming a quiet exemption.

## Found by `tools/tycho-sim`, 2026-08-13 (head `0d97b61f`)

### 24. the `sink` consume diagnostic described a rule the compiler does not implement — **fixed**

`sink` is the third feature no program in this tree uses. Reaching for it in a
simulation's entity pools produced a refusal whose stated remedy was already
satisfied. `can_move_into_sink` (`src/tychoc.c@can_move_into_sink`) declines
adoption for four separate reasons and `sink_arg_into` collapsed all of them
into one sentence:

    'w' is consumed by a `sink` parameter but used again (or inside a loop);
    pass a copy you keep (`y := w`) or make this its last use

Measured before the change: each of `len(w)`, `w[0]`, `bump(&w)`, `w[0] = 9` and
`push(w, x)` written **before** the sink call produced exactly that text — and in
every one of them the sink call **was** the variable's last use. The rule the
compiler implements is not last-use at all; it is `count_reads_b(whole body) == 1`,
one textual mention anywhere in the function. "Reads" is a misnomer: `count_reads_e`
(`src/tychoc.c@count_reads_e`) counts every `E_IDENT` occurrence and `count_reads_b`
walks `s->target`, so an assignment target and a mutating call argument each count.
A forwarded `sink` parameter, mentioned once and not in a loop, got the same
sentence — neither clause of it true.

**Fixed by splitting the string, not the rule.** One arm per condition, in
`can_move_into_sink`'s own order: a `sink` parameter (never adopted, however
declared), a call inside a loop, and a mention count that is not 1 — the last
printing the count and naming one textual mention as *how* last-use is proved
without dataflow. The escape route is now one that works: the loop arm no longer
suggests a copy made in the loop, which is refused for the same reason
(probed 2026-08-13). Three reject fixtures pin the three texts separately
(`tests/reject/sink_consume_mentions.ty`, `tests/reject/sink_consume_in_loop.ty`,
`tests/reject/sink_consume_param.ty`); each was proved to redden on its own arm
and only its own. The fourth condition, `!cv_arena(name)`, is **unreachable**:
Tycho has no globals (a top-level `G := [1]` is a parse error), and both locals
and params are `cv_push`ed, so every `E_IDENT` arriving here has an arena. It
gets a fourth arm that claims nothing rather than a fixture.

**Not fixed: the rule itself.** One mention is a coarse stand-in for last use,
and it is why an entity pool built with `push` cannot be `sink`-passed at all.
Relaxing it needs a real last-use analysis; that is a language change, out of
this scope.

### 25. ~~a keyword used as a field or a variable does not say it is a keyword~~ — **CLOSED 2026-08-13**

Writing the systems in `tools/tycho-sim/sys/` wanted a struct of flags saying
which systems run, one per system. The obvious spelling does not compile, and
the refusal names the wrong thing:

    struct Cfg:
        spawn: bool
    ->  error: expected a field name

`spawn` is a keyword, so the field-name parser rejects it — but the message
describes the token as not being a field name at all, which is what you would
also get from a stray `,` or a number. Nothing in it suggests the identifier is
fine and merely taken. The same holds for a local: `spawn := 1` gives
`expected an expression`, pointing at the `:=` rather than at the name.

**The compiler already has the right sentence and uses it one place out of
three.** A procedure gets it (probed 2026-08-13, all four in a clean directory):

| written as | diagnostic |
|---|---|
| `fn spawn(n: int)` | `'spawn' is a reserved keyword and cannot be used as a procedure name` |
| `spawn: bool` (field) | `expected a field name` |
| `spawn := 1` (local) | `expected an expression` |
| `parallel: bool` (field) | `expected a field name` |

So this is not a missing analysis, it is one existing string not reused at the
field-name and short-declaration sites.

**Fixed by moving the guard down, not by copying it.** The predicate is unchanged
— a reserved word is still exactly a token whose lexeme `keyword()` maps back to
its own kind — but it now lives in `eat` (`src/tychoc.c@reserved_kw`), which is
the common path for *every* identifier position and already carries the human
phrase for each one ("a field name", "a parameter name", "a variant name", "a
struct name", …). One arm, templated on that phrase, covers all 33 of them; the
special case in `parse_fn` was deleted and the procedure sentence it used to
print is reproduced byte-identically by the template.

The local is the one site that genuinely needed its own arm. It never reaches an
identifier position at all: every binding branch of `parse_stmt` gates on
`TK_IDENT`, so a keyword falls straight through to the expression parser — which
is why the caret sat on the `:=`. Its guard sits at the top of `parse_stmt` and
covers `kw :=`, `kw =`, `kw ,` and `kw : <type>`; the colon form requires a
non-NEWLINE after it, or `else:` and `select:` would be caught by it (both
re-probed and unchanged).

Three fixtures. `tests/reject/kw_as_field_name.ty` and
`tests/reject/kw_as_local_name.ty` pin their own text, and each was proved to
redden on its own arm and only its own. `tests/kw_contextual_names.ty` is the
positive one and is the regression this change could plausibly have caused:
§3.7's contextual identifiers (`where`, `soa`, `range`, `sink`, `extern`,
`package`) are used as field, parameter and local names in one program, and a
predicate broadened past `keyword()` would refuse all of them.

Worked around in the program by naming the field `reinforce`
(`tools/tycho-sim/sys/sys.ty@reinforce`) — which is a fair name for per-tick
spawning, so the cost here was the minutes spent learning why, not the rename.

## Found by `tools/tycho-make`, 2026-08-13 (head `680d30d`)

### 26. a foreign enum's variant in a `match` arm is refused as if it did not exist

Matching a corelib enum by its bare variant name is refused, and the message
says the variant is not a variant of the type — which is untrue, and sends you
looking at the enum rather than at the spelling:

```
$ ./tychoc /tmp/a.ty -o /tmp/a
/tmp/a.ty:5: error: 'NotFound' is not a variant of io__IoErr
     5 |         NotFound: return "nf"
```

`NotFound` **is** a variant of `io.IoErr` (`corelib/io/io.ty@NotFound`). The
rule is that a foreign enum's variants must be written qualified, and
`io.NotFound` compiles and runs. Both halves of that were probed: the same
program with every arm qualified builds and prints `nf`, and the identical
shape over a *locally* declared enum accepts the bare form, so the qualification
requirement is real and specific to the cross-package case.

Not fixed here, and it is a diagnostic, not a miscompile: the compiler accepts
exactly the right programs. What it costs is that the message names the wrong
suspect. `'NotFound' is not a variant of io__IoErr -- write it qualified, as
'io.NotFound'` would have ended the detour, and the compiler already knows the
package prefix because it printed the mangled `io__IoErr`.

Nobody in the tree had hit it because every package that owns an error enum also
owns the `err_str` that matches it, so the match is always in-package. This is
the first consumer that needed to read a corelib enum from outside.

Worked around in the program by qualifying the arms
(`tools/tycho-make/main.ty@io_str`), which is one character per arm.
