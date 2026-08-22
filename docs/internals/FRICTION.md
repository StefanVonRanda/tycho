# FRICTION

One line per moment the language got in the way while writing a real web server.
Non-blocking by construction: anything that blocks the server earns a phase in
`plan.md` instead. This file is a deliverable; fixing everything in it is not.

The program these notes came from is `server/` — `tycho-httpd`, ~440 lines,
serving `server/www` to a real browser. Everything below was hit while writing
it, not imagined about it.

> **A bare `path:line` in this file is as-of its entry's date.** Measured
> 2026-08-13 on a fresh random sample: 7 of 15 no longer point at their subject,
> because every insertion into `src/tychoc.c` shifts everything below it. They are
> deliberately NOT being re-anchored — open-list item 10 has the measurement and
> why a mechanical pass would certify the wrong line rather than fix it. **When
> you write a NEW citation here, anchor it**: `path@SYMBOL` for a definition, or
> `path:N@token`. Both survive an insertion; a bare line number does not.

> **An entry that claims to be closed carries `> Pinned-by:`, since 2026-08-22.**
> The line is a shell command; `make friction-check` runs every one of them,
> deduped and in parallel, and names the entry whose fix stopped holding. Write
> `> Pinned-by: none -- <reason>` when nothing can assert it (#86 is a timing
> claim). An entry with no pin at all is REPORTED, never a failure: 62 of 85 had
> none the day the gate landed. Scoring these by hand took most of a session and
> the mapping from entry to lane was inference; this makes it the author's claim
> instead, and runs it.

> **Plan references, removed 2026-08.** This file used to point every closure at
> the plan that closed it (`docs/internals/plan-*-DONE.md`). Those archives were
> pruned; the pointers now say "the X plan" with no phase numbers. The full
> archives remain at `git show docs-archive:docs/internals/plan-*-DONE.md`.

## The headline

**The language has a good answer for fallible calls and the standard library
does not use it.** `Option`/`Result` are real types and `or_return` is a real
postfix operator that unwraps or short-circuits (`docs/spec/10-statements.md:75`).
Exactly **1 of the corelib's 386 functions returns an `Option`** —
`io.read_line` (`corelib/io/io.ty:17`). Every other fallible call in this server
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
> At `53dd937` the corelib is **406** package functions, **1** returning an `Option` and
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
the plan that worked this file as a list (10 phases, head `536b257`). This section is
the second one's closing act, and it is a re-score rather than an update: **every
"CLOSED" note in this file was checked against the thing it names before the counts
below were written** — 89 content checks (does that function / flag / document /
diagnostic exist at HEAD and say what the note claims?) and 8 live runs of the code.

**No stale CLOSED was found.** Two stale *OPENs* were, and they are named below.

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
  its prose: `FRICTION.md` went from 38 bullets at `fd97211` to 57 at `796dd90`, of
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
  380 (`796dd90`) → 378 (phase 1) → 378 (1b, 2) → 380 (phase 3) → 376 (4) → 372 (5) →
  **341** (6) → 341 (7, 8, 9). One phase did nearly all of it: `core:cli` learning
  `--root DIR` deleted a 59-line hand-rolled parser (`-31`).
- **What it cost.** `src/tychoc.c` **+359 raw lines, +176 non-comment** (11795 → 12154;
  9161 → 9337 code) — over half of it phase 3's nested patterns. Corelib packages
  **+57** Tycho code lines and **+17** C shim lines; corelib test programs **+111**;
  `docs/` +590/−84 with one new document; `scripts/` +203/−8,
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

**And its phase 5** (the `Option`/`Result` plan's — `796dd90`, not this plan's) closed
the directory-creation gap with `io.make_dir` / `io.remove` (35 library lines,
`Result(bool, IoErr)` both, non-recursive on purpose) and deleted the `os.system`
shell-out from `corelib/test/io`. It found **no new language friction at all**, and it
cost the application **+9 lines**, all in one startup check, to replace a single wrong
message ("`--root` is empty or not a directory", said for an empty directory, a plain
file, a missing path and an unreadable one alike) with four accurate ones. `Result`
made the distinctions *available*; **spending them costs one branch per cause**, and
nothing about the error model makes accuracy free at the call site.

### The real remaining debt — the open list, re-scored in place

**Re-scored 2026-08-18 by PROBE, not by reading.** Every entry below was scored
by writing a program and compiling it; the verdict is the compiler's own message
or the program's own output. Two moved.

| # | the claim | what a probe returned today | verdict |
|---|---|---|---|
| 1 | the whole corelib hashes messages held entire | `corelib/sha256/sha256.ty` now has `init`/`update`/`final`/`final_hex`; `corelib/md5`, `corelib/hash`, `corelib/crypto` have none | **DOWNGRADED** — true for 3 of 4, false for the one that matters most |
| 3 | `compress.decompress` cannot tell empty from corrupt | round-tripped empty → `Ok len=0`; a halved payload → `Err`; zero bytes in → `Err` | **CLOSED** |
| 5 | a `bytes` slice clamps, so it is not a bounds check | `b[1:99]` on a 3-byte value returns length 2, no trap | **CLOSED** — deliberate, now pinned by `tests/bytes_slice_clamp.ty` |
| 11 | a first `--shim` C file must hand-declare `tycho_int` | no generated header existed; 13 of 14 shims declared it themselves | **CLOSED** — `corelib/tycho.h` ships and tychoc passes `-I` |
| 12 | `for` does not destructure a tuple, and a tuple is not indexable | *"a `for` binds one name"*; *"can only index an array, a string, by…"* | **CLOSED** — deliberate, both halves pinned |
| 37 | `sink` cannot express a builder | *"'f' is consumed by a `sink` parameter but is mentioned 2 times"* | **CLOSED** — deliberate, now pinned |
| 48 | a subscript cannot read two fields of its receiver | `make grid-check` green, and its own report says the flat 2-D limit *"still refused"* | **CLOSED** — recorded as a judgement call in its own entry, and already gated |
| 56 | `decimal.from_str` fails open to a WRONG number | `from_str("12.5x")` returns **1.25** — not 12.5, not 0, not an error | **CLOSED** — the lax name is deprecated and every call site warns |

**All eight are closed as of 2026-08-18.** Two took code (11, 56). Four were
already-made decisions filed as if they were open (5, 12, 37, 48) — the verdict
existed in the spec, in a reject fixture, or in the source, and what was missing
was that three of the four rules had NOTHING asserting them, so a silent change
could have landed. Each has a pin now. 1 downgraded and 3 closed on the probe
alone.

**What none of this is:** evidence the language is easy. Every entry here was
found by its own designer writing his own program. The list being empty means
the known edges are recorded and pinned, not that a stranger would not find
ten more in an afternoon.

**What this pass did NOT re-score:** the other 86 numbered entries in this file,
and the per-item prose below, which still carries its 2026-07-31 framing.


**Re-scored 2026-07-31 against `3ddc8fd` — 11 open items.** 43 commits since the
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

> **Re-scored 2026-07-30 against `53dd937` — 10 open items.**
> Every item below was **re-verified against the tree at `53dd937`** before it was
> scored: the compiler was run by hand on the shape the item describes, or the cited
> source was opened, or the gate/shim was invoked directly. **All ten reproduce.**
> Nothing on the previous list turned out to be already closed — but four of them are
> *bigger* or *cheaper* than the previous list said, and every citation on the list has
> been re-derived, because the old ones had drifted.

The 2026-07-30 list was ordered so the top was what to pick up first, and that
order is preserved. **The two appended items are not last in priority just
because they are last in the list** — item 11 is the cheapest thing here. The
pick-up order is written out in full under "What moved this pass" below.

1. ~~**Three shims do not compile under `-std=c11`** (*Found by phase 1's gate sweep*)~~ —
   **CLOSED 2026-07-31 at `251ec39` by the shim-gate phase, gate first.**
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
   - `corelib/net/net_shim.c` — **4 errors** (`corelib/net/net_shim.c:52` storage size
     of `hints`, `corelib/net/net_shim.c:56` implicit `getaddrinfo`,
     `corelib/net/net_shim.c:57` invalid use of undefined `struct addrinfo`,
     `corelib/net/net_shim.c:58` implicit `freeaddrinfo`).
   - `corelib/tls/tls_shim.c` — **9 errors** of the same kind from
     `corelib/tls/tls_shim.c:38`.
   - **NEW at this pass — `corelib/signal/signal_shim.c`, 3 errors**: storage size of
     `sa` (`corelib/signal/signal_shim.c`), implicit `sigemptyset`
     (`corelib/signal/signal_shim.c`), implicit `sigaction`
     (`corelib/signal/signal_shim.c`). `struct sigaction` and its two helpers are
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
   `corelib/time/time_shim.c:1-2` (`_POSIX_C_SOURCE 200809L`) — so it is **3 lines
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
   `3ddc8fd` with a scratch program: `spawn work(1)` gives `error: a statement must be a
   declaration, assignment, or call -- a bare expression has no effect`
   (`src/tychoc.c:3876`, unmoved since the previous pass), which still never states the
   real rule — a task handle must be *bound* so the compiler can hang the implicit join on
   it. **One line of diagnostic text at a known line.** Open only because nobody has spent
   it, through two re-scores.
3. ~~** is not reachable from `docs/README.md`**~~ — **CLOSED
   2026-07-31: `docs/README.md` now lists it** under "How the docs are organized", beside
   `architecture.md`. The real question behind it — nothing checks that a document is
   *reachable*, only that its links resolve — is **not** closed and is filed as item 13
   below. A seventh site of item 5's class was found in the same file while adding the
   link and is recorded there. The record of what the item said when open follows.
   (*Found by phase 1's gate
   sweep*) — re-verified at the time: `grep -c bootstrap docs/README.md` → `0`. Sharper than the old
   entry: the index deliberately points at *directories* (`docs/reference/`, `docs/guides/`,
   `docs/spec/`, `docs/internals/`, `docs/rfc/`), so almost every unlisted file is covered
   by its directory — and is **the only top-level `docs/*.md` no index
   entry reaches**, the other five (`docs/architecture.md`, `docs/from-c-to-arenas.md`,
   `docs/thesis.md`, `docs/tutorial.md`, `docs/README.md` itself) all being named. **One
   link**, plus the real question behind it: `scripts/check_links.sh` checks that links
   *resolve*, not that documents are *reachable*, so an orphan is invisible to every gate.
   Three files under `docs/internals/` are additionally mentioned by no Markdown at all.
4. ~~**The `send` collision** (*Phase 7*)~~ — **CLOSED 2026-08-13: the definition is
   diagnosed now.** `fn send(a: int, b: int) -> int` warns where it is WRITTEN --
   "`send` collides with the builtin of the same name -- every unqualified `send(...)`
   calls the BUILTIN, so this procedure is unreachable by that name (§3.7). Rename it,
   or call it qualified as `pkg.send(...)`" -- which is the definition-time diagnosis
   this item asked for; the caller still fails, but no longer as the first sign.
   The record of what the item said when open follows.
   Reproduced at `3ddc8fd` with two scratch
   programs, and every citation on this entry re-checked and still correct.
   `fn send(a: int, b: int) -> int` compiles silently and dies at the *call* with
   `error: send(ch, v) takes a channel, got int`, while `fn die(s: string) -> int` is
   rejected at the *definition* with `error: 'die' is already defined`. **The reason is
   pinned:** the definition-time duplicate check is
   `if (sig_find(pr->name) || consts_find(pr->name)) die_dup_proc(...)`
   (`src/tychoc.c:8625`), and `sig_find` searches `g_sigs` — which holds `die` and `exit`
   as real entries (`src/tychoc.c:4875-4876`, inside `src/tychoc.c@register_builtins`)
   but **holds no entry for `send`, `recv` or `close` at all**; those three are recognised
   ad hoc during resolution (`src/tychoc.c:6172`, `src/tychoc.c:6181`,
   `src/tychoc.c:6200`). So it is not a table that omits three rows, it is three builtins
   that were never in the table. **The code is ~1 line** at `src/tychoc.c:8625`; the open
   part is the decision — which builtin names are shadowable — because landing it newly
   rejects any program defining `send`/`recv`/`close`. Note the generic path a line above
   (`src/tychoc.c:8619`) consults the same two tables plus `generic_find`, so whatever is
   decided has to be written twice.

   And the "corelib layering decision" this item wanted taken **was already
   taken, four times**: `core:io`, `core:json`, `core:markdown` and `core:toml`
   all `import "core:strings"` today, and `corelib/strings/` carries no `deps`
   file, so the import costs no pkg-config dependency. Nothing was ever in the
   way. The record of what the item said when open follows.

   Reproduced at `3ddc8fd`:
   `corelib/strings/strings.ty@ends_with` exists and `core:httpd` still hand-rolls its own
   `corelib/httpd/httpd.ty@has_ext` rather than import the package for one predicate.
   `core:httpd`'s imports are `core:net` and `core:result` and nothing else
   (`corelib/httpd/httpd.ty:42-43`), which is the shape the decision is about.
   **Not lines — a corelib layering decision** about whether a leaf package may depend on
   `core:strings`. Note the precedent that has since landed: `core:io` *dropped* a
   dependency (`core:path`) when a syscall made it unnecessary, so the tree's current
   direction is fewer inter-package edges, not more. (The old entry cited `has_ext` at
   `corelib/httpd/httpd.ty:248`; it is 40 lines further down now, which is why this
   citation is a `path@SYMBOL` — the definition has a name and its line number was only
   ever a record of how much prose sat above it.)
7. ~~**`parallel for` caps concurrency at `min(N, ncpu)` and nothing warns** (*Earlier
   phases*)~~ — **CLOSED 2026-07-31 at `3ddc8fd`, by the spec catching up with the
   compiler.** Both live halves are answered in `docs/spec/`, verified by reading it:
   - **The 64-chunk ceiling is documented.** `docs/spec/13-concurrency.md:81-83` now
     reads "the reference implementation uses `min(ncpu(), N)` chunks, MAY expose an
     override (`TYCHO_THREADS`), and MAY impose a fixed upper bound on the chunk count —
     the reference bounds it at **64**, so above 64 the fan-out is narrower than `ncpu()`
     reports". The old text's "uses `ncpu()` chunks" — false on both counts, the `min`
     and the cap — is gone. The compiler side is cited anchored from the spec's own
     provenance block, `src/tychoc.c:11476@_pk > 64`, so the gate now polices it.
   - **`ncpu()`'s false definition is corrected**, which was the other half:
     `docs/spec/16-builtins.md:236` states outright that it is "the *requested* worker
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
     (`src/tychoc.c:5407@ncpu`, lowering at `src/tychoc.c:10512@tycho_ncpu`), so a program can at least
     ask. Measured on this box: `ncpu()` → 16.
   - There was an **undocumented hard ceiling of 64 chunks** — `if (_pk < 1) _pk = 1; if
     (_pk > 64) _pk = 64;` (`src/tychoc.c:10732`, inside `src/tychoc.c@gen_parfor`) —
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
8. ~~**No direct spelling for N workers** (*Earlier phases*)~~ — **CLOSED
   2026-08-13, BOTH halves, and (b) needed no type-system change at all.**
   (a) is `parallel(W) for …`, 1..64, refused as a literal outside that range and
   aborting at run time when computed (`b188feb2`). (b) — "N long-lived workers,
   each carrying its own identity" — turned out to be the loop variable the
   counting form already binds: `parallel(N) for wid in 0..<N:` gives each of the
   N chunks its own `wid`. Probed directly (sum of ids over 4 workers = 6 =
   0+1+2+3), and then PROVED ON THE PROGRAM THE ITEM CITED: `server/main.ty`'s
   recursive fan-out — worker k spawning worker k+1 into a frame-local — is gone,
   replaced by `parallel(n) for wid in 0..<n:`, and `make server-check` starts the
   real server and passes. The item asked for task handles in a container to get
   here; nothing in this tree needs them for this.

   What the item's evidence really wanted is worth recording: `server`'s `wid` is
   used for ONE thing, the log prefix `"w" + str(wid)` — not a shard key, not an
   index into per-worker state. A want that reads as "the type system must grow"
   was a label in a log line.**
   `docs/rfc/parallel-for-width.md` carries what shipped and what it asserts —
   including why the emitted `_pk` is read by `tests/conc/run.sh`: no output can
   show the width was honoured while a worker cannot observe its own identity,
   which is (b). The original costing follows.

   **COSTED 2026-08-13, and half of it was never a type-system change:** The channel-drain form already lowers to
   `parallel for __pw in 0..<ncpu()` with the width synthesised as an ordinary
   `E_CALL` node in `r_stop` — the same slot a user's `0..<N` fills — so want (a),
   "the program cannot choose N", is a grammar slot plus three call sites
   (`src/tychoc.c@task_container_err` is not among them), not a new type. Want
   (b), per-worker identity, stays open AND got harder to spell the same day: the
   natural `parallel(4) for x, wid in work:` collides with the one-binder rule
   gated by `tests/reject/for_two_binders.ty`, and both neighbour languages spend
   a second binder on the index. The RFC has the spelling, the four decisions it
   needs, the gates that can redden, and why a fixture that COUNTS workers is the
   load-bearing one. The record of what the item said when open follows.

   Reproduced at
   `3ddc8fd` with a scratch program: `hs := [spawn work(1), spawn work(2)]` is refused with
   `tychoc: a task handle cannot be stored in a container or aggregate -- wait(t) first`
   (`src/tychoc.c@task_container_err`, fail-closed at the type-intern choke points so a
   task cannot escape and be waited twice or never). `server/main.ty@worker` still pays the
   recursive fan-out — worker k spawns worker k+1 into a frame-local, then runs its own
   accept loop. **An array of handles is a type-system change, not an item-sized fix.
   Uncosted, and still the honest core of what is left.** *(This entry cited
   `src/tychoc.c:862` and `server/main.ty:495-497` at the previous pass; the first drifted
   by one line and the second by 440, because `server/main.ty` roughly doubled — 1088 lines
   now. Both are `path@SYMBOL` here, which is why they will not drift again.)*
   **NARROWED, 2026-07-31, and the item reads stronger than it is** (the prunner plan).
   The first program in this tree to actually run a worker pool started **16 workers in one
   line and stored no handle**: `parallel for` is a direct spelling for N workers, and
   `parallel for x in ch:` — specified at `docs/spec/13-concurrency.md:99-100`, worked at
   `docs/guides/concurrency.md:86-112`, fixtured at `tests/conc/parfor_chan.ty:11` — is a
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
   **Re-checked at `3ddc8fd` and the narrowed form holds in both halves.** The grammar
   still has no width slot — `docs/spec/02-grammar.md:248-249` is two productions, a
   counting one over a literal `0..<Expr` and a foreach one over a bare name, and neither
   carries a count. And `server/main.ty` still spells its pool as a recursion carrying
   `wid`, which the file now uses for more than logging: the shutdown registry is indexed
   by `wid - 1` (`server/main.ty`), so worker identity is load-bearing there and
   `parallel for` remains unable to supply it.
 9. **`corelib/test/image` is skipped without libpng** (*the friction plan
    phase 7, non-blocking*) — confirmed environmental and confirmed *live*:
    `corelib/image/deps` names `libpng`, `pkg-config --exists libpng` still fails on this
    machine at `3ddc8fd`,
    and `corelib/run.sh:39` prints `skip <name> (missing dependency: ...)` and continues. Its
   golden therefore asserts nothing here. **Not closable in-tree** — it is a property of
   the machine, and the skip is the deliberate design that keeps `make ci` green on
   platforms without the lib. Listed so nobody re-derives it a third time.
10. ~~**This file's own coordinates drift silently, and no gate can see it**~~
    (*phase 10*) — **the drift REPRODUCES and the proposed fix is UNSAFE. Verdict
    recorded 2026-08-13; nothing mechanical was run.**

    Re-measured on a fresh random sample of 15 bare `path:line` refs at
    `e9d05224`: **7 no longer point at their subject** (2026-07-31 measured 11 of
    15), so the defect is real and undiminished.

    But "a mechanical pass to anchored form, after which the gate polices them" —
    what this item asks for below — **would make it worse.** An anchor is checked
    by CONTENT: `path:N@token` passes when `token` appears on that line. Anchoring
    a ref that has ALREADY drifted therefore freezes the WRONG line and hands it a
    green gate, where a stale line number at least reads as wrong the moment
    someone opens it. Measured over all 128 bare refs in this file: 58 sit on a
    line carrying a token unique in its file, so 58 anchors could be generated —
    and **nothing distinguishes which of those lines are still the intended
    subject**, because that lives only in the author's head. Deriving the symbol
    from the surrounding prose instead is no better: it collapses distinct refs
    onto one anchor (`corelib/net/net_shim.c` lines 84, 88, 89 and 90 all yield
    `@hints`) and picks generic words (`corelib/io/io.ty:17` yields `@Option`).

    **What is safe is the FORM, at the moment of writing**, and it is already the
    house rule: cite a definition as `path@SYMBOL` with no line number, or write
    `path:N@token` yourself, where you know the subject. Both are position-
    independent or content-checked, so neither can drift into a green lie. The
    ~105 existing bare refs stay as they are, as-of-their-date coordinates in a
    historical record — see the note at the top of this file. The record of what
    the item said when open follows. Fifteen `path:line`
    citations were opened at HEAD and checked against what this file says is there:
    **11 of the 15 no longer point at their subject.** All eleven are into
    `src/tychoc.c` — the `\r` escape set, the literal-intern emit site, the
    adjacent-literal join, `is_place`, the `exit` builtin registration, `copy_into`'s
    `T_BYTES` case, `instantiate_generic`, the zero-cost reinterpret, `detect_package`,
    the `bytes` representation and the channel-handle type syntax — because every compiler
    phase shifts everything below it, and `src/tychoc.c` is now 754 KB. The four that
    survived are all into files that barely moved (`corelib/httpd/httpd.ty:336`,
    `corelib/httpd/httpd.ty:229`, `server/main.ty:560`, `runtime/tycho_rt.c:541`) — which
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

**What moved this pass, 2026-07-31 at `3ddc8fd` — and the previous paragraph's finding
was overturned.** It said a list nothing pulls from does not get shorter. Something did
pull from it: item 7 was closed, and it was closed *because* the concurrency work went
looking for what the list said was open. Four more of this file's unnumbered items closed
the same way. So the mechanism works; what the previous pass had actually measured was a
batch that happened to be aimed elsewhere.

**Pick-up order, cheapest first — SPENT 2026-08-13.** Every item this ordering
names is now struck above: the last two were item 6 (withdrawn, `has_ext` is not
`ends_with`) and item 8 (closed in both halves, the width slot plus the loop
variable). The order is kept as the record of how the batch was scheduled, and
because entries below address items by number.

**It is not a to-do list, and it reads like one.** A numbered line here is an
ITEM REFERENCE (`1. **Item 11** — …` orders item 11 first), not an open item;
a detector that greps this section for unstruck numbers reports nine open items
and is wrong every time — mine did, three times in one session, before reading
the header two lines up. The open items are the numbered `N.` entries in the
list ABOVE this paragraph, and as of 2026-08-13 there are none.

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

## Re-scored against a real concurrent program, 2026-07-31 (head `0d0dacc`)

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
  depends on.**~~ **CLOSED 2026-07-31, verified at `3ddc8fd` by reading §22.**
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
  **CLOSED 2026-07-31, verified at `3ddc8fd`:** `corelib/iter/iter.ty:14` is now
  `fn map(xs: [$T], f: fn($T) -> $U) -> [$U]`, two type variables, so `[Res] -> [int]` is
  expressible. **And it answers the question the entry flagged as unverified** — the entry
  said "whether the inference reaches a **function-typed** `fn($T) -> $U` parameter is
  **not verified**, and is the thing to check before costing this". It does; that was the
  whole cost. `filter` is unchanged (`corelib/iter/iter.ty:5`, `fn($T) -> int`), as the
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
  parameter on `map`. Multi-parameter generics work (`corelib/result/result.ty` takes
  three), but that is a *value* parameter — whether the inference reaches a
  **function-typed** `fn($T) -> $U` parameter is **not verified**, and is the thing to
  check before costing this. `filter` is unaffected (`fn($T) -> int` already).
- **NEW, small — `cli.has` answers a narrower question than its name, and there is no
  diagnostic possible.** **PROMOTED 2026-07-31 to open list item 12** — still open, still
  unchanged at `3ddc8fd` (no `supplied` exists), and numbered there because an unnumbered
  bullet in a dated section is not something anyone picks up. *(Item 12 is struck
  as of 2026-08-13; this bullet's "still open" is as-of its own date.)* A bare `--stats` lands in `Cli.flags`, not `Cli.keys`, so
  `cli.has(c, "stats")` returns **false** and `cli.flag(c, "stats")` returns true;
  measured, both spellings compile, both return `bool`, and the failure is a missing line
  of output with nothing printed. **It is not a defect** — the doc comment at
  `corelib/cli/cli.ty:138` says outright "Was option `key` (a `--key=value`) supplied at
  all?", and `has` (`corelib/cli/cli.ty:139`) / `flag` (`corelib/cli/cli.ty:146`) scan
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
  desugaring.**~~ **CLOSED 2026-07-31, verified at `3ddc8fd`:** `docs/guides/concurrency.md`
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
  the sugar is `tests/conc/parfor_chan.ty:11` and the guide does not name it. **This is
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
  sleeps; the real corpus ran at `ncpu()` = 16, nowhere near `src/tychoc.c:10732`.
- **No nested parallelism** — no `parallel for` inside a spawned task, and no pool inside
  a pool.

## Phase 7 — writing the server

## Phase 1 of the Option/Result plan — converting `core:net`

Found while converting `core:net`'s fallible TCP surface to `Result(T, net.NetErr)` and
rewriting `server/`, `corelib/httpd`, both corelib tests and both examples against it.

## Phase 2 of the Option/Result plan — the combinators, `io.read_bytes`, `httpd.read_request`

Found while adding `core:result` and converting the two genuinely ambiguous calls.

## Phase 3 of the Option/Result plan — acting on the cause, and deleting `read_head`

Found while moving the cap and the raw buffer into `core:httpd` so `server/` could
stop reimplementing the read loop.

## Phase 4 of the Option/Result plan — the missing syscall

Found while writing `io.is_dir` and its test.

- ~~**`Option`/`Result` phase 4** — **nothing in Tycho can create a directory.** Verified absent, not assumed: `docs/spec/16-builtins.md` §29.10 lists five filesystem/time builtins (`read_file`, `write_file`, `list_dir`, `clock`, `now`) and none of them makes a directory, and `mkdir`/`make_dir`/`create_dir` return zero hits across `corelib/`, `src/tychoc.c` and `runtime/`. There is no remove either. So `corelib/test/io` — the test for a `stat(2)` wrapper — has to build its empty directory with `os.system("rm -rf … && mkdir -p …")`: a corelib test depending on `/bin/sh` to set up a filesystem state the corelib itself cannot reach. The asymmetry is the finding: the library can now *classify* a directory but not *make* one.~~ **CLOSED, the option-result plan.** `io.make_dir(p)` (`mkdir(2)`, no `-p`) and `io.remove(p)` (`remove(3)`, one entry, **never recursive**) both return `Result(bool, IoErr)` where `Ok(true)` is "changed it" and `Ok(false)` is "already how you asked" — `make_dir` splits `EEXIST` into `Ok(false)` (already a directory: goal met) and `Err(Exists)` (a file is in the way: goal unreachable), which is exactly the ambiguity test this plan was built on. `corelib/test/io` no longer imports `core:os` and the `rm -rf && mkdir -p` line is gone. A non-empty directory is `Err(Failed)`, which is the property that keeps `io.remove` from being `rm -rf` behind a corelib name.
- ~~**`Option`/`Result` phase 4** — `io.exists` and `io.is_dir` now answer overlapping questions by different means, and the cheaper one is the newer one: `exists` lists the whole parent directory (O(entries), and it cannot see a `.`/`..`-only leaf) where `is_dir` is one `stat`. `resolve()` ends up calling both on the same path. A `stat`-backed `exists` is the obvious follow-on and was refused on scope, but the general shape is worth recording — a missing syscall does not just block the question it names, it leaves *neighbouring* answers implemented the long way round.~~ **CLOSED, the friction plan, and the follow-on was bigger than the swap.** `exists` is now `iox_stat_kind(p)` and two comparisons (`corelib/io/io.ty:154-156`), the same shim call `is_dir` uses; it still fails closed, so `false` means "`stat` could not say yes" — the old behaviour too, since an unlistable parent yielded no entries. `corelib/test/io.out` is **byte-identical** before and after, which is the proof the swap changed the means and not the meaning. **Two things the entry did not predict.** (1) **`core:io` lost a dependency**: `path.base`/`path.dir` were needed *only* by the old `exists`, so `import "core:path"` is gone and the module written up as "the first corelib module to COMPOSE other core modules" now composes one, not two — a stale claim in three places, all corrected. (2) **`resolve()`'s double call did not just halve, it collapsed**: making the second call a `stat` is what made the pair visibly redundant rather than merely ugly, and the two calls are now ONE `match io.is_dir(fsp)` reading all three answers off the Result (`server/main.ty:165-174`) — `Ok(true)` → `301` (or `404` when `dir_form` already appended `index.html`, i.e. a directory *named* `index.html`, which used to be a `200` → `read_bytes` → `Err(IsDir)` → `404`: same status, one syscall fewer, no wrong intermediate), `Ok(false)` → `200`, `Err(_)` → `404`. Per request for a real file: **2 syscalls (an opendir/readdir walk plus a stat) → 1 stat**. `corelib/io/io.ty` **98 → 93 code lines**, `server/main.ty` shorter as well. The entry's own closing sentence turned out to be the useful half and to run both ways: a missing syscall leaves neighbouring answers implemented the long way round, **and adding it does not fix them — someone has to go back and delete the long way round**, which is a second, separately-scoped piece of work that is easy to leave undone because nothing is red.
- **`Option`/`Result` phase 4** — reordering two guards to make room for a new one silently changed a security answer: hoisting `hidden_segment(path.clean(rel))` above the `index.html` append made `GET /` return **403**, because for the root target `rel` is `""` and `path.clean("")` returns `"."` (`corelib/path/path.ty:91-92`), which `hidden_segment` reads as a dotfile. Nothing in the compiler or the corelib could have caught it — `clean("")` returning `"."` is documented POSIX behaviour and both spellings type-check identically. It was caught by the live matrix (`50-request flood 0/50 200`), which is the argument for keeping that matrix.

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

**Postscript, 2026-07-30 (head `53dd937`).** Two numbers in the verdict above have
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

## Found by the friction plan's gate sweep, out of its scope

## The signal plan, 2026-07-31 (head `7926da5`) — closed for one case, narrowed for the rest

`server/main.ty@stopped` prints `tycho-httpd: stopped after N requests` and was
**unreachable**. Nothing in the tree installed a `SIGTERM` or `SIGINT` handler, because
Tycho had no signal surface at all, and `server/run.sh` asserted wait status **143** — it
asserted the *absence* of clean shutdown and called that a passing gate. `core:signal`
closes that. The honest score is that it closes the **shutdown case**, not signal
handling, and the difference is deliberate rather than unfinished.

- **CLOSED — a Tycho program can shut down cleanly on `SIGTERM`/`SIGINT`.**
  `signal.on_shutdown(fd)` (`corelib/signal/signal.ty:14@on_shutdown`) installs one
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
  the package header (`corelib/signal/signal.ty`; the previous pass cited
  , which the `register_conn` / `retire_conn` additions pushed down): calling a Tycho function from
  handler context is a *language* feature, because every Tycho value lives in a
  bump-allocated arena that is not re-entrant and channel operations park behind a mutex
  (`runtime/tycho_rt.c:915@mu`) — a handler that interrupts the allocator or the lock
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
  calls them either side of `serve_conn` (`server/main.ty`, `server/main.ty`).
  Measured `--workers 4 --idle-ms 5000`, before from a clean worktree at `e35052b`:
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
  `signal.shutdown_requested()` (`corelib/signal/signal.ty:18@shutdown_requested`) exists
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
- **`src/tychoc.c:3633`** points at `gen_parfor` 98 lines short of where it is.
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

## Re-scored against a batch, data-shaped program, 2026-07-31 (head `ce0609e`)

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

### 1. ~~The one-shot digest is what the language makes natural~~ — **DOWNGRADED 2026-08-18: `core:sha256` streams now**

`corelib/sha256/sha256.ty` has `init`, `update`, `final` and `final_hex`, so the
package this actually matters for — `tycho-ar` stakes file integrity on it —
can hash something it does not hold entire. `core:md5`, `core:hash` and
`core:crypto` remain one-shot, and that is the verdict rather than a backlog:
md5 is kept for reading other people's formats, `core:hash` is a hash-table
hash over a key already in hand, and AEAD is defined over a whole message. The
language default the item describes is real; it now has an exception where the
shape called for one. The record of what the item said when open follows.


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
forwards (tycho-ar's own `sha_feed` handed its own `&H` to `sha_block`).
Nothing here is broken. What is true is that **the default steers
the library**, and a whole family of interfaces went one-shot because of it.

**What it cost, measured.** Hashing a file in bounded memory meant writing
SHA-256: ~60 lines across `tools/tycho-ar/main.ty` — `sha_feed`,
`sha_finish`, `sha_bytes` and `sha_file`, all four hand-rolled and none of them
in that file any more (commit 2b226aa2 moved it to `core:sha256`'s streaming
API). Digests match `sha256sum` on 14 sizes
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

### 3. ~~`compress.decompress` cannot distinguish empty from corrupt~~ — **CLOSED 2026-08-18, by probe**

Re-probed today and it distinguishes them: a round-tripped empty payload gives
`Ok` with length 0, a payload cut in half gives `Err`, and zero bytes in gives
`Err`. Empty is an answer and corrupt is an error. The record of what the item
said when open follows.


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

### ~~4. `strings.parse_int` fails open, so no format parser can use it~~ — **the strict counterpart EXISTS and the ask is paid; one caller was still fail-open and is fixed. Verdict 2026-08-14**

`corelib/strings/strings.ty@parse_int` returns 0 for `""`, 0 for a leading
non-digit, and **stops at the first non-digit without objecting** — a damaged
length field of `"1x4"` parses as `1`. That is the right behaviour for user input,
where 0 is a fine default, and the wrong one for a length field, where a wrong
length that *parses* is precisely how a reader ends up hashing the wrong span of
bytes. `tools/tycho-ar/main.ty@parse_uint` is the strict version this program had
to carry: returns `-1`, no silent prefix.

**The entry's ask is stale.** `corelib/strings/strings.ty@parse_int_checked`
returns `Result(int, IntErr)` with `EmptyInput` / `Garbage` / `OutOfRange`, and
six real programs already call it. Measured 2026-08-14 over 14 inputs, lax
against strict:

```
[]      lax=0   strict=Err(EmptyInput)     [3x]   lax=3   strict=Err(Garbage)
[-]     lax=0   strict=Err(Garbage)        [1x4]  lax=1   strict=Err(Garbage)
[ 7]    lax=0   strict=Err(Garbage)        [7 ]   lax=7   strict=Err(Garbage)
[+3]    lax=0   strict=Err(Garbage)        [007]  lax=7   strict=Ok(7)
[9223372036854775808]  lax=0  strict=Err(OutOfRange)
[-9223372036854775808] lax=-9223372036854775808  strict=Ok(-9223372036854775808)
```

`parse_int` is unchanged and still lax, deliberately — 0 is the right answer for
a CLI option. What the entry got right is that a *format* parser must not use it,
and one still did: `tools/tycho-make/build/build.ty@stamps_read` validated the
tab structure, the name and the 64-char hash, then read the mtime with the lax
call. A stamp of `x<TAB>17x<TAB><hash>` was accepted as mtime 17, and the mtime
decides staleness — so a corrupted stamp file shipped a stale output as current.
With the fix reverted the new lane leg reports `EXITED 0 -- a damaged mtime was
accepted`, i.e. the build succeeded off the corrupt stamp. `tools/tycho-make/run.sh`
leg [13b] pins it; the pre-existing `StampBroken` leg could not, because it writes
`garbage`, which has no tab and dies at the first validation.

### ~~5. `bytes` slices clamp, so a slice is not a bounds check~~ — **REPRODUCES, and the clamp is DELIBERATE; the entry's own residual ask was already paid. Verdict recorded 2026-08-13**

Re-probed 2026-08-13 against the reference languages the entry never consulted.
**Every factual claim below still holds** — this closure is a verdict, not a
correction. What changed is that the behaviour is now *decided* rather than
merely observed, and the entry's closing sentence ("the one-paragraph spec
warning is still unwritten") is **stale**: it was written in commit `a335b8db`
on 2026-08-11, hours after the note that says it is missing.

**The probe, four out-of-range shapes on a 5-byte value, all bounds routed
through a `fn at(n: int) -> int` so nothing folds at compile time:**

```
b past=3 far=0      <- b[2:10] is 3 bytes; b[7:9] is 0
b neg =2 inv=0      <- b[-3:2] clamps to 0; b[4:1] inverts to empty
s past=3 far=0      <- the identical string slice, identical clamp
s neg =2 inv=0
```

`bytes` and `string` are **the same** on all four — that half of the entry is
confirmed, not a divergence between them. An array aborts on all four, and the
check is **purely a runtime one**: `a[2:10]` on a 5-element array compiles
cleanly (exit 0 from tychoc) and dies only when run, so there is no
compile-time arm to strengthen. The array check is emitted inline into the
generated C by the compiler — `src/tychoc.c:11014-11016` for an ordinary array
(the path the probe above took) and the same test again at
`src/tychoc.c:10995-10997` for the SoA variant, both spelling it
`_lo < 0 || _hi > len || _lo > _hi`, which is why all four shapes abort and not
just the two that overrun. The clamp is `runtime/tycho_rt.c@tycho_str_substr`,
whose three lines are exactly `start<0 -> 0`, `end>n -> n`, `end<start -> start`.

**Both reference languages fault where Tycho clamps, and that divergence is the
finding.** Measured here, not recalled — go1.26.5 panics on all six shapes tried,
**including the `string` slice**:

```
[]byte[2:10]  PANIC slice bounds out of range [:10] with capacity 5
[]byte[4:1]   PANIC slice bounds out of range [4:1]
string[2:10]  PANIC slice bounds out of range [:10] with length 5
```

and Odin (dev-2026-04-nightly) traps by default, naming the source line —
`m.odin(10:8) Invalid slice indices 2:10 is out of range 0..<5`. Note that
`-no-bounds-check`, documented as disabling bounds checking "program wide",
does **not** lift this one: the slice-index check survives it, so Odin's slice
bound is stronger than its own opt-out suggests.

**Verdict: keep the clamp.** The divergence is real but it is not the same
choice being made differently — a Tycho `b[i:j]` *is* `substr`, the function
form, which clamps by definition and is right for the text processing it exists
for. Go has no clamping substring operator to keep consistent, so its panic
costs it nothing; changing Tycho's operator would either desynchronise it from
`substr` or drag `substr` along with it. Against that, the operator is not the
only spelling available, and the fail-closed one already exists.

**What a caller wanting a real bounds check should use:**
`corelib/strings/strings.ty@slice_bytes` and `@slice_str`, which take the same
`(start, stop)` the operator does and return `Result(_, SliceErr)`. Verified
running, same four shapes: `OutOfBounds`, `Inverted`, `OutOfBounds`, then `ok
len=3` for the in-range case.

**What this closure adds beyond the verdict: a gate.** The clamp was pinned by
nothing — `tests/string_slice.ty` covers only in-range slices, so a change making
Tycho match Go and Odin would have passed `make test` green. A deliberate
divergence from both reference languages with no fixture watching it is the exact
shape of a behaviour that gets "fixed" by accident. `tests/slice_clamp.ty` now
locks all eight cells above; it was proved able to fail by replacing the three
clamp lines with a Go-style abort, which reddens it (`FAIL slice_clamp (native
exit 1)`) before the runtime was restored.

The original entry follows.

### 5. ~~`bytes` slices clamp, so a slice is not a bounds check~~ — **CLOSED 2026-08-18: deliberate, and PINNED**

The clamp is the documented rule (`docs/spec/03-types.md`, the `b[i:j]` row: it
clamps exactly as a string slice does) and it is not changing. What was missing
is that nothing asserted it, so a change to trapping could have landed in
silence. `tests/bytes_slice_clamp.ty` pins all three cases now. The hazard is
real and the answer is a rule, not a fix: compare `len` first, never use a slice
as a bounds check. The record of what the item said when open follows.


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
(`sb past=OutOfBounds` is the `b[2:10]` case above). ~~The one-paragraph spec
warning is still unwritten and still worth writing.~~ **It is written: read
2026-08-13, `docs/spec/03-types.md:144-154` carries "A `bytes` slice is not
a bounds check" — the silent clamp with both worked examples, the contrast with
an array slice that ABORTS, the MUST NOT infer-from-return rule, and
`strings.slice_bytes` named as the checked form. Nothing left to write.**

### ~~6. There is no `eprintln`, and the missing channel removed a feature~~ — **the channel was never missing; `eprint` has shipped since 2026-06-14, 2026-08-11**

The entry's load-bearing sentence — "the builtins are `println`, `die` (stderr,
then exit 1) and `exit(n)`", so "**a non-fatal warning is inexpressible**" — is
**false, and was false when it was written**. `eprint(s)` is a builtin: registered
at `src/tychoc.c:5402@eprint`, emitted as `tycho_eprint`, and defined as
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
(`tools/tycho-ar/main.ty`), fatally rather than with a warning, matching
every other partial failure in that program. The gate needed the same
work: `diff -r` does not compare mtimes, so the round-trip leg was green over the
hole and would have stayed green over a broken restore. `tools/tycho-ar/run.sh`
leg 3b compares the times themselves, and was confirmed to fail — naming all
eight members — with the `set_mtime` call stubbed out.

### Smaller than they looked once written down

Recorded because they were hit, ranked below the line because writing them out
shrank them. Padding this list would make the eight above harder to act on.

- ~~**No `io.write_bytes`.**~~ — **CLOSED by commit 2b226aa2; struck 2026-08-21.**
  `corelib/io/io.ty@write_bytes` takes `bytes` and returns
  `Result(void, IoErr)`, classified as `read_bytes` is, and `write_at` landed
  beside it. The record of what the bullet said when open follows.
  Writing bytes is `io.write(p, to_str(b))`, which is
  correct — `runtime/tycho_rt.c@tycho_write_file` fwrites the length header to a
  `"wb"` handle — but a caller has to read the runtime to know that, because the
  signature says `string`. **Smaller than it looked:** it is one signature away
  from symmetric with `io.read_bytes` and it never actually cost this program a
  bug, only a paragraph of comment justifying a line. Legibility, not safety.
- ~~**No `mkdir -p`.**~~ — **CLOSED 2026-08-21: `corelib/io/io.ty@make_dir_all`.**
  Pure Tycho over `make_dir`, no shim; `tools/tycho-ar/main.ty@mkdir_p` is the
  wrapper that names the error and is now 6 lines. **The entry was half wrong and
  the correction is the interesting part:** `docs/guides/corelib.md` already
  refused this in writing — *"one entry, never recursive, no `mkdir -p` and no
  `rm -rf` behind a corelib name"* — so the bullet was asking for something the
  tree had decided against, and neither document knew about the other. The rule
  was narrowed rather than dropped: what it protects is that no name is SECRETLY
  recursive, `_all` is the disclosure, and there is still deliberately no
  `remove_all`. The record of what the bullet said when open follows.
  `corelib/io/io_shim.c@iox_make_dir` is one `mkdir(2)`, which
  is the correct primitive, and `Ok(false)` for "already a directory" is exactly
  the right interface — it is what makes the loop idempotent. Every caller writing
  into a tree it does not own rebuilds the component chain;
  `tools/tycho-ar/main.ty@mkdir_p` is 18 lines of it. **Real, and 18 lines.**
  **Re-scored 2026-08-21 — reproduces, and the number was wrong in the
  cheap direction:** `@mkdir_p` is **39 lines**, not 18. "Every caller" was
  also too strong — it is the tree's ONLY recursive one; `tools/tycho-fetch`
  and `tools/prunner` call `io.make_dir` once, for a parent they already own.
  So the cost is 39 lines in one place rather than a little in many, which
  makes it a smaller argument for a corelib entry point than the bullet
  implies, not a larger one.
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
  returning zero before the expected length was fatal in tycho-ar's `sha_file`
  — but not preventable. **Closed as written, 2026-08-21:** the archiver stopped
  re-reading. `member_of` hashes the buffer it already read and compressed
  (`tools/tycho-ar/main.ty@member_of`), so there is one `open(2)` per file and no
  window for a writer to race into. Unlike the two items
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

## Re-scored against a type-system-shaped program, 2026-08-01 (head `721e646`)

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
is still open.** *(Both are closed as of 2026-08-13: finding 2 is the
`core:decimal` `div`, fixed 2026-08-02 by commit `a8c761c` and banner-corrected
2026-08-11. This sentence is left as written, with the correction beside it, for
the same reason the finding texts are: it is the record of what was believed.)* Each carries its own status banner below, and the finding texts
are left exactly as they were written: they are the record of what was wrong, and
a repaired description of a fixed defect describes nothing. The last group is the
entries that got smaller as they were written down, and they are labelled as such
rather than padded.

### 1. ~~`core:json` accepts input it cannot represent, three ways, and cannot report any of them~~ — **FIXED 2026-08-01, re-probed 2026-08-11; see the banner below**

> **[FIXED, 2026-08-01.]** Both halves are closed, by two plans in sequence. The
> finding below is left **verbatim**, including the parts that are no longer true
> of the tree — `json_guard` is gone, `parse` is no longer the only entry point,
> and none of the three probe lines reproduces. It stays because it is the only
> record of what the package did and why `tycho-q` is shaped the way it is.
>
> **The error channel** — which the finding names as the root cause, correctly —
> was closed by the json-error plan, whose phase 1 landed as
> commit `84bcfa2`: `corelib/json/json.ty@parse_checked` returns
> `Result(Json, JsonErr)`, every variant carries the byte offset of the byte that
> failed, and the non-termination is unreachable rather than merely caught
> (`corelib/json/json.ty@parse_value` no longer falls through to a number parse
> for a byte that begins no value, and both container loops carry a
> cursor-must-advance guard). That is what let `tools/tycho-q/main.ty`
> — most of a second JSON parser, written only because the first one could not
> speak — be **deleted** rather than maintained.
>
> **The float path**, which the finding correctly separates as "a separate,
> larger question", was closed by the plan this banner was written under:
> `b260333` added `corelib/json/json.ty@JFloat`, carrying the binary64 value
> **and the original lexeme**, so a number the parser cannot represent exactly
> keeps its digits and `stringify` re-emits them byte-for-byte; the same commit
> ended the silent 64-bit integer wrap, which was the last silent-wrong-value
> path in the package. `0f74a7b` added `\uXXXX` and surrogate-pair decoding, and
> `42047a1` made the grammar exactly RFC 8259's — trailing commas, leading zeros,
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
>
> **Re-probed again 2026-08-13 — all three sightings, SEPARATELY, and none of
> them reproduces.** One program per sighting in its own `mktemp -d`, each
> calling the lenient `parse` the finding used as well as `parse_checked`: `1.5`
> is `float` and re-emits as `1.5`; `[1.5]` returns `[1.5]` and exits 0 under
> `ulimit -v 1000000`, so the unbounded loop is gone rather than merely faster;
> `[{"a":1.5}]` re-emits itself and invents no key. All three were already pinned
> by `corelib/test/json/main.ty:126-135`, each with the original symptom in a
> comment beside it, so no test was added — a fourth would have been a copy.
>
> **What that probe DID add is the reference-language verdict, which nothing here
> had.** The same twelve inputs went through Go's `encoding/json` and Odin's
> `core:encoding/json` (forced to `.JSON`; Odin's default is `JSON5`, which
> accepts more still). Tycho refuses, naming a byte, in every case where either
> of them corrupts in silence: Go rounds a 30-digit integer to
> `1.2345678901234568e+29` and Odin WRAPS it to `-4362896299872285998`; both read
> `1e-400` as `0`, and Odin reads `1e400` as `Inf`; Go swaps a raw `0xFF` for
> U+FFFD and Odin ABORTS on it (an assertion inside its own parser); and Odin
> under `.JSON` still accepts trailing text, a leading zero and a trailing comma,
> and silently TRUNCATES `"a<TAB>b"` to `"a"`.
>
> **Duplicate keys is the one axis that matches neither reference, and it stays.**
> Go keeps the last and drops the first; Odin refuses the whole document
> (`Duplicate_Object_Key`). Tycho keeps both with `get` answering the first — the
> only one of the three that is lossless, and the only one where a caller can
> still choose either reference behaviour afterwards, by reading `keys`.
> Deliberate, not changed: both alternatives destroy something the document
> contained.

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
columns — so `tools/tycho-q/main.ty` validates the raw bytes *before*
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

### 2. ~~`core:decimal` has no `div`, so the ordinary averaging query has no answer~~ — **CLOSED 2026-08-11 (fixed 2026-08-02); see the banner below**

> **[CLOSED 2026-08-11 — fixed 2026-08-02 by commit `a8c761c`; this banner had
> gone stale for nine days.]** The finding below is left verbatim. `div` landed
> in exactly the shape the finding asked for and nothing here re-litigated it:
> `corelib/decimal/decimal.ty@div` is
> `div(a, b, scale, mode) -> Result(Decimal, DivErr)`, with **both** the target
> scale and the rounding mode named by the caller and no default for either.
> `corelib/decimal/decimal.ty:59-60` gives the two modes the finding demanded —
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
> pins all of this is `corelib/test/decimal/main.ty:62-89`, and breaking the
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
>    (`src/tychoc.c:8491`).
> 2. *"without binding a payload"* — a **nullary** variant needs no match at
>    all: `if v == VNull:` compiles and runs. `==` is a working discriminator
>    for the payload-free half of an enum. It stops at the other half:
>    `if v == VInt:` → `error: VInt carries a payload — write VInt(...)`
>    (`src/tychoc.c:6003`).
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
> written. `result.map_err` (`corelib/result/result.ty`) already converts a
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
> — `src/tychoc.c:5989-5991`. So the rule is "no *implicit* conversion", not
> "no conversion".
>
> The second, smaller true claim: `map_err` takes a **constant** replacement
> (`corelib/result/result.ty`), so the original cause is discarded — the
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
> (`corelib/iter/iter.ty:5`) and still has no fallible counterpart — its five
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

> **~~Both halves are STALE, re-scored by probe 2026-08-22.~~** `filter` takes
> `keep: fn($T) -> bool` (`corelib/iter/iter.ty:18`), not an int, so the 0/1
> spelling is gone; and the fallible counterparts exist —
> `try_filter(xs, keep: fn($T) -> Result(bool, $E)) -> Result([$T], $E)`
> (`corelib/iter/iter.ty:27`) beside `try_map` (`corelib/iter/iter.ty:12`).
> The paragraph above is kept because the REASONING is still the lesson: the
> first draft read this as a language rule against fallible higher-order
> functions, and the correction to "it is one signature" is what stopped someone
> being sent to the compiler.

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
  **Re-scored by probe 2026-08-22 — REPRODUCES, with two corrections.** The
  entry's own example no longer parses at all: the query is `select … from …`
  now, not `from … select …`, so the shape to run is
  `tycho-q "select a from /tmp/x.csv"`. And the diagnostic has stopped pointing
  away from the cause — it reads `unexpected token / (expected a source path
  after from)`, which names the right thing and merely shows `/` as the
  offending token. The quoted form returns the row. What is left is a lexer wart
  with a diagnostic that now describes it, which is a smaller entry than it was.

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

### 8. ~~`iter.try_map` has no `Result(void, E)` shape, and says so from inside corelib~~ — **CLOSED 2026-08-12: deliberate; re-probed 2026-08-13**

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
>
> **Re-probed 2026-08-13: the decline stands and its evidence grew.** The repro
> above still fails byte-identically, and the loop still prints `ok` then
> `err neg`. Re-counted over the same four trees: `or_return` is now on 158
> lines of code (was 129), yet the count that decides this — a bare
> `f(x) or_return` directly inside a `for` — is still **0**, of 21 bare
> `or_return` statements tree-wide. **Neither Go nor Odin is a precedent
> either way**: Go's `slices` and `iter` have no fallible-callback helper at
> all, Odin's `core:slice` `mapper`/`filter` take an infallible `proc(U) -> V`
> (the `err` in their signature is the allocator's, not the callback's), and
> neither language has `Result` to shape one from.

### 9. ~~A `string` across the FFI truncates at its first NUL, silently~~ — **verdict upheld and gated 2026-08-12, re-probed 2026-08-13; see the banner below**

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

**Re-probed 2026-08-13: reproduces exactly, both directions, and the verdict
stands.** A fresh `--shim` program: `"h" + chr(0) + "i"` has `len` 3 while the
callee's `strlen` returns 1; a C `{'h',0,'i',0}` comes back at `len` 1; a
`strlen`-fold over `["h\0i"]` sees 1, not 3. The same run shows the supported
route intact — the identical value as `bytes` crosses at `len` 3, sums 209
(104+0+105), returns from C at `len` 3 with byte 1 == 0, and `to_str` brings it
back at `len` 3. Those four legs were prose only; they are now pinned by
`tests/ffi/main.ty@nulparam` (`nulparam=3/1 nulret=1 nularr=114 ctl=2`), which
reddens if the boundary ever starts carrying the length.

**Precedent, measured rather than recalled** (go1.26.5, Odin
dev-2026-04-nightly; same string, a real file named `h` in the cwd as the
positive control):

```
Go   C.CString("h\0i") -> strlen        = 1        # truncates, silently
Go   syscall.BytePtrFromString          = EINVAL   # refuses
Go   os.Open("h\0i")                    = EINVAL   # refuses, though `h` exists
Odin strings.clone_to_cstring -> strlen = 1        # truncates, silently
Odin strings.unsafe_string_to_cstring   = 1        # truncates, silently
Odin os.open("h\0i")                    = nil      # OPENS `h`
```

**Neither refuses at the FFI boundary**, so the verdict above matches both; and
the split this entry already drew — boundary documented, validator enforced — is
Go's, one layer up, at the stdlib call that names a resource. **`core:io` was on
Odin's side of that line until 2026-08-13**: `io.exists("h" + chr(0) + "i")` was
`true` and `io.read` of it returned the contents of `h`. That is the same class as
the `core:regex` finding, not a defect of the boundary, and it moved to Go's side
along with `core:net` and `core:os` — 21 path-taking entry points, 4 host-taking
and 4 command-taking ones now refuse before anything is attempted
(`corelib/io/io.ty@has_nul`), naming `Err(BadPath)` / `Err(BadAddr)` where there
is an error channel and giving each call's documented "this did not happen"
sentinel where there is not. `core:path` is exempt: lexical, reaching no `char*`.
The three fixtures carry live positive controls — strip the guards and `core:io`
reads `HELLO` through a truncated path, `net.listen` binds `127.0.0.1` and
`os.system` runs `printf NULBAD` reporting exit 0 (measured 2026-08-13 against a
guard-stripped copy of `corelib/`).

**The length is at the boundary but is not in the ABI.** A Tycho string is
length-headered, so a shim reading `((const int64_t *)s)[-1]` recovers 3 where
`strlen` says 1 — probed at a constructed string, a literal and `""`. That is a
private runtime layout, not a contract: a third-party C function cannot use it,
and `bytes` is how the ABI says the same thing. It does not weaken the cost
objection above either, because deciding *whether* a string holds an interior
NUL still costs a scan, header or no header.

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

### 10. ~~A two-key comparator cannot be written inline, and the composition that replaces it is invisible~~ — **WITHDRAWN 2026-08-13**

Re-probed by running programs at `49f49ae8`. The entry's premise holds and its
conclusion does not. **A two-key comparator is written directly, as one named
function with as many branches as it likes**, and `sort.sort_by` takes it:

<!-- fence-skip: excerpt -- the name tie-break is elided as `...` and `fruit`/`freq` are locals of the program it was cut from; the compiled twin is `corelib/test/sort/main.ty@count_then_name`, gated by `make corelib` -->
```tycho
fn count_then_name(ac: int, bc: int, an: string, bn: string) -> int:
    if ac != bc:
        return bc - ac
    ...
byc := sort.sort_by(fruit, fn(a: string, b: string) -> int: count_then_name(freq.get(a, 0), freq.get(b, 0), a, b))
```
```
capt=[fig, apple, pear]
```

The step this entry missed is the last line: the closure does not have to *be*
the comparator, only to **reach** the captured data and forward it. One
expression is enough to call a function, so the single-expression restriction
constrains nothing here — it never touches the branching, which lives in the
named function. "A named function cannot substitute, because the comparator has
to see the map being sorted on" is a non sequitur, and it is the whole basis of
the claim below.

Where no capture is needed the closure disappears too, and **the tree already
proved this two days before the entry was written**: `corelib/test/sort/main.ty`
has ordered a struct by dept ascending and pay descending since `e40f32d6`
(2026-08-10) with a plain named comparator and no closure at all —
`corelib/test/sort/main.ty@dept_then_pay`, passed by name. The entry cites
`sort_by` while overlooking its own package's test of exactly the case it
declares impossible, and `sort_by`'s doc comment already advertises it
(`corelib/sort/sort.ty:66`, "several keys, mixed directions").

The capture case the entry did reason about is now pinned as well, at
`corelib/test/sort/main.ty@count_then_name`. Its negative control: delete the
name tiebreak and `capt` becomes `[fig, pear, apple]` while every other line of
the golden, `bytwo` included, is unchanged.

**Precedent, run rather than recalled** (go1.26.5, odin dev-2026-04-nightly;
all three languages agree on `eng9cy eng3ann eng3dee ops5bo`):

| | two-key comparator | can a comparator capture a local? |
|---|---|---|
| Go | `slices.SortStableFunc` — block closure, or **one expression** via `cmp.Or(cmp.Compare(x.Dept, y.Dept), cmp.Compare(y.Pay, x.Pay))`, or a named func | yes |
| Odin | `slice.stable_sort_by` — block-bodied `proc` literal, or a named `proc` | **no** — `proc` literals are not closures; the map probe fails at compile time with `Error: Undeclared name: m` |
| Tycho | named `fn`, passed by name or called from a one-expression closure | yes |

So on the axis this entry is actually about, Tycho sits **with Go and ahead of
Odin**: Odin cannot express the captured-map comparator at all without a global
or a `_with_data` variant, and Tycho can.

**What survives.** Two smaller facts, neither of which supports the heading.
First, the block-closure refusal still reproduces verbatim at `49f49ae8` — that
is a real restriction, just not one that blocks this. Second, the stable-sort
composition the entry documents is correct and remains a good technique for the
argsort-style parallel-array shape `corelib/test/wordfreq/main.ty:26-30` uses;
it is simply not the *only* route, and calling it "the composition that
replaces it" overstates a workaround into a necessity.

The original entry is preserved below.

### 10 (original, 2026-08-12). A two-key comparator cannot be written inline, and the composition that replaces it is invisible

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

### 11. ~~A first `--shim` C file must hand-declare `tycho_int`, because there is no header to include~~ — **WITHDRAWN 2026-08-13**

It does not have to declare anything. The entry reasoned from the corelib shims
instead of writing one, and a shim written from scratch needs no typedef, no
guard and no header — six lines, of which none are Tycho-specific:

```c
#include <stdint.h>
int64_t shim_add(int64_t a, int64_t b) { return a + b; }
```

`tychoc probe.ty --shim probe_shim.c` builds and runs that, printing
`1099511627777` for `shim_add(1099511627776, 1)`. The reason is in the emitted
C: the **caller** declares the prototype, `extern tycho_int shim_add(tycho_int
, tycho_int );`, so the shim is never the place where the type is introduced. It
only has to define a function of matching shape, and `int64_t` matches
`tycho_int` by construction (`docs/spec/appendix-f-impl-defined.md:45`).

All 13 shims do carry the guarded typedef, and stripping it from one reddens
`make shim-check` — but only because they *spell their signatures* `tycho_int`.
That is house style, seven lines each (three comment, four code), chosen so they
read like the spec's prototypes; `int64_t` throughout would compile identically.
So the toll on a first-time author is **zero lines**, not the four claimed here.

**What was really wrong is one row of a table.** `docs/guides/ffi.md` mapped
Tycho `int` to C `long` — right on LP64, 32-bit on Windows and ILP32. An author
following it writes a truncating shim that no gate can see, because the shim and
the generated prototype are separate translation units and C never compares
them: the same probe against `int shim_add(int, int)` prints `1`, not
`1099511627777`, with no diagnostic at any stage. Corrected there on 2026-08-13,
along with the `char` row and the `bytes` length, both of which said `long` too.

`make shim-check` could not have caught any of this. It globs
`corelib/*/*_shim.c` (`scripts/shim_check.sh:6`), so no `--shim` file outside
the corelib is in its corpus at all.

**Precedent, run rather than asserted** (go1.26.5, odin dev-2026-04). Odin is the
same shape as Tycho: `foreign import` plus a `foreign` block declaring
`shim_add :: proc(a, b: i64) -> i64 ---`, against a plain `int64_t` C file with
nothing Odin-specific in it. Go's cgo is *worse* on this axis, not better — the
C function must be visible to the preamble, so with no header the author
hand-writes the prototype inside the `import "C"` comment, and a mismatch
between that prototype and the `.c` file is equally silent: preamble `int64_t`
against `int shim_add(int, int)` compiles clean and prints `1`. Neither
toolchain ships the "header that defines the language's integer type" this entry
wanted, because neither needs one.

The original text follows.

### 11. ~~A first `--shim` C file must hand-declare `tycho_int`, because there is no header to include~~ — **CLOSED 2026-08-18**

`corelib/tycho.h` exists and tychoc passes `-I` at the corelib root, so any
shim anywhere can `#include <tycho.h>`. Proved from a temp directory outside
the repo: a two-line shim with no typedef of its own builds and returns 42.
The 13 in-tree shims include it by relative path instead, because
`tests/run.sh` rolls its OWN cc line with no `-I` -- discovered by two pkg
fixtures going red, which is the reason the relative form is the one in the
tree and `<tycho.h>` is the one a user writes. The record of what the item
said when open follows.


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

`corelib/strings/strings_shim.c:27-30`. **All 13 shims in the tree carry it**
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
  `src/tychoc.c:5415@.name="split"`, so it is in no package at all.
  `corelib/strings/strings.ty:170` says so in a comment one line above `lines`
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

### 12. ~~A `for` binding does not destructure a tuple, and a tuple is not indexable~~ — **all three halves reproduce; the rule stays and the MESSAGE was the defect, fixed 2026-08-13**

Re-probed at `c45df1bb`, each half its own program: `for k, q in xs:` was refused
with `expected ':' before the block` (caret on the comma), `p[0]` with `can only
index an array, a string, bytes, or a map (as a place)`, and the body-destructure
form built and printed `a1 b2`. A fourth probe the entry did not run:
`for i, s in xs:` — an index binder, the thing a second name usually means — is
refused by the same message, so the foreach head takes exactly one name in every
spelling.

**The precedent the entry never checked runs against extending it.** Measured
2026-08-13 (go1.26.5, odin dev-2026-04-nightly, over an array of a two-field
struct), neither neighbour destructures the element in loop-binding position —
they spend the extra binder on the **index**:

```
go   for name, src := range xs   -> binder1=int(0)  binder2=main.pair({resolvable true})
odin for v, i in xs              -> i is `int` (odin check, on a deliberate `s: string = i`)
go   a, b := two()               -> destructures, like Tycho's `:=`
```

So `for k, q in xs:` would not be "the `:=` feature extended to a loop"; it would
take the position both neighbours read as the index and give it a different
meaning. `:=` destructuring already carries the feature, and the entry's own
`k, q := p` is one line.

What was a real defect is the sentence this entry ends on — that a reader "meets
two different refusals before finding the form that works, and neither message
mentions it". The first message now does:

```
error: a `for` binds one name -- write `for p in COLL:` then `k, q := p` in the body
```

The names in the fix are the ones the author wrote, and any binder count reaches
it (`for a, b, c in xs:` too). Pinned by `tests/reject/for_two_binders.ty`;
`p[0]` is left alone, since a positional accessor on a tuple is a type-system
question this entry does not argue for. The original entry follows.

### 12. ~~A `for` binding does not destructure a tuple, and a tuple is not indexable~~ — **CLOSED 2026-08-18: deliberate, and both halves PINNED**

The one-binder `for` was decided on 2026-08-13 with its own diagnostic, measured
against both neighbours (Go's first binder and Odin's second are the INDEX, so a
second binder meaning something else would take a slot every reader expects),
and pinned by `tests/reject/for_two_binders.ty`. The second half was unpinned
until today: `tests/reject/tuple_index.ty` now holds it. Destructure in an
assignment, which is supported. The record of what the item said when open
follows.


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

<!-- fence-skip: quoted from the program that hit this friction; `xs` belongs to that program, not to this document -->
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
emitted — `src/tychoc.c:12973` skips any function type mentioning a type
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

## Found by `tools/tycho-sim`, 2026-08-13 (head `8654391f`)

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

## Found by `tools/tycho-make`, 2026-08-13 (head `77bd826`)

### 26. ~~a foreign enum's variant in a `match` arm is refused as if it did not exist~~ — **FIXED 2026-08-13**

Matching a corelib enum by its bare variant name is refused, and the message
said the variant is not a variant of the type — untrue, and it sends you
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
requirement is real and specific to the cross-package case. Nobody in the tree
had hit it because every package that owns an error enum also owns the `err_str`
that matches it, so the match is always in-package.

**Fixed at both sites that could make the false claim**, through one shared
`src/tychoc.c@die_not_variant`: the `match` arm and the `is` operator each
compared the written name against the *mangled* variant name only, so a bare
foreign name missed and fell into the "not a variant" arm. It now recognises the
name as this enum's, says the rule, and prints the spelling that works:

```
error: 'NotFound' is a variant of io.IoErr, but a variant of another package's
enum must be written qualified -- write 'io.NotFound'
```

A name that is a variant of nothing still reports as unknown — qualifying it
would not help — and every enum in a diagnostic is now named as it was written
(`io.IoErr`, not `io__IoErr`). The nested-pattern site (`Err(NotFound)`) was
left alone: `enum_variant_index` there already accepts the bare form, so it
cannot make the false claim, and a bare unknown name is caught one step earlier
by the binding-name rule. Both probed at the fix.

Three reject fixtures pin the new text (`tests/reject/pkg/foreign_variant_bare/`,
`foreign_variant_is/`, `foreign_variant_unknown/`), each proved to redden on its
own `# expect:` line. `tests/enum_bare_variant_local.ty` is the positive one and
is the regression this could have caused: a bare variant of a *local* enum, in a
match arm and after `is`, must still be accepted, which no reject fixture sees.

### 27. ~~bounded parallelism over a graph read at runtime has exactly one expressible shape~~ — **WRONG, WITHDRAWN 2026-08-13**

This entry claimed that a work queue over a DAG read at run time was not
expressible, and that a **wavefront** — `parallel for` over one depth of the
graph, depths in order — was all the language allowed. That was false, and
`tools/tycho-make` now runs a work queue.

**What is actually expressible**, and is what
`tools/tycho-make/build/build.ty@build` does today: **one coordinator and N
stateless workers**. The pool is a single `spawn`ed task whose body is a
`parallel for` over a slot list, every chunk looping on the same jobs channel
(`tools/tycho-make/build/build.ty@pool`). The coordinator keeps the indegree
table, sends a job when a node's last dependency reports, and files outcomes by
node index. A node starts when its own dependencies finish, not when its level
does.

**Where the reasoning went wrong.** The two rules quoted below are real. The
error was looking for a design in which the WORKERS share a mutable indegree
table, finding that forbidden, and concluding the shape was impossible. Go's
worker pool does not share that table either — it lives in the one coordinator
goroutine, and workers are stateless pullers on a jobs channel that report on a
results channel. Neither rule reaches that:

- `Task(T)` is affine and non-storable (`docs/spec/03-types.md:398`), so
  `hs := [spawn work(1), spawn work(2)]` is still refused with "a task handle
  cannot be stored in a container or aggregate". But a pool is not N handles.
  It is **one** handle whose task fans out internally, which satisfies affinity
  by naming it individually.
- A `parallel for` body still may not write a captured variable — `out[i-1] = i
  * 2` inside one is still refused with "parallel for cannot mutate captured
  variable 'out' in place". But no worker needs to. The indegree array is a
  local of the coordinator thread, mutated only by its owner, so the capture
  rule never applies to it. What a worker needs travels with the job: `Job`
  carries each dependency's current hash, filled in by the coordinator.

Three probes on 2026-08-13, before any of this was written, each ran clean:
four `parallel for` bodies all `recv`ing from one shared channel; two spawned
workers sharing one jobs channel, splitting eight items **2+6** — uneven, so
pulled on demand rather than statically partitioned; and a full coordinator loop
over a diamond DAG with `deps`/`indeg` arrays, which finished in order with no
deadlock.

**What the mistake cost.** A build tool that idled its pool. Under the
wavefront, one long chain sitting inside a wide level held up every node behind
it however free the workers were. `tools/tycho-make/race.mk` is the fixture that
shows it: a 3-node instant chain beside three one-second sleepers at the same
depth. The wavefront started the chain's second node at trace line 9, after all
three sleepers had finished; the work queue starts it at line 6, about a second
earlier, and `tools/tycho-make/run.sh` [8b] asserts that ordering rather than
any duration. The entry also stood as a general claim about the language, which
is the part worth withdrawing loudly: it named an "honest ceiling" that was not
one.

**The determinism claim survived intact.** Completion order is a wider race now
— the ready set is no longer bounded by a level — and the log is byte-identical
anyway, at `TYCHO_THREADS` 1, 2 and 8 over six cold runs, because reassembly was
never a property of the scheduler. The golden did not move: the transcript the
wavefront produced is the transcript the work queue produces, which is exactly
why the [8b] leg had to be an assertion in the runner and could not be a
recorded one.

**The one thing here that stands** is that `os.run` is thread-safe in practice:
12 concurrent subprocesses returned the right exit code and the right captured
stdout at three different pool widths (probed 2026-08-13). `plan.md` had this
down as unverified with a fallback to internal actions; the fallback was not
needed, and tycho-make runs real recipes. That mattered more under the work
queue, not less — more subprocesses are now in flight at once.

### 28. ~~a struct field cannot name a type declared later in the same file~~ — **FIXED 2026-08-13**

Functions in a package are order-free — `tools/tycho-make/graph/graph.ty@order`
calls `find_cycle`, declared 30 lines below it. Types are not, and nothing in
`docs/spec/03-types.md` says so:

```
$ ./tychoc probe/main.ty -o probe/x
probe/main.ty:3: error: unknown type 'E'; did you mean 'S'?
     3 |     e: E
```

The same for a struct field naming a later struct (`did you mean 'A'?`). The
diagnostic is the ordinary unknown-type one, so the "did you mean" suggests the
enclosing type — which is never the answer — and says nothing about ordering.
The fix is to move the declaration up, which is what `build/build.ty` does.
Cheap to live with, and cheap to improve: the name IS in the file, so the
message could say "declared below; a type must be declared before it is used".

**Closed by making types order-free instead**, which is what Go and Odin both do
at package scope. Improving the message would have documented a limit the
standing default says should not exist.

A field's type is resolved as it is parsed, which is the whole cause; function
bodies escape it because they resolve in a later pass. So `parse_program`
pre-scans the token stream for the start of every top-level
`struct`/`enum`/`type`/`handle` (`src/tychoc.c@scan_type_decls`), and a field
naming one that has not been parsed yet parses that declaration on demand from
its own sub-parser, then retries the lookup (`src/tychoc.c@force_type_decl`).
The main loop steps over a declaration already forced this way. Source order is
otherwise untouched, so no program that compiled before takes a different path:
forcing only fires on a name that was an error the day before.

Three things the change had to get right, each pinned by a fixture:

- **The knot the language cannot have.** `struct A: b: B` with `struct B: a: A`
  now *parses*, where before it died on the forward reference. Two structs
  cannot contain each other by value, so it must still be refused — and it is,
  by the containment DFS that already refused a direct self-reference
  (`src/tychoc.c@emit_aggregate`), with the same `infinite type: A contains
  itself by value` message. Nothing new was needed; the DFS was already general.
  `tests/reject/struct_mutual_by_value.ty` pins it, message included.
- **Self-reference must keep working.** A struct registers its own name before
  its fields, so `[Node]` inside `Node` resolves without forcing anything; the
  on-demand parse skips a declaration already in progress rather than recursing
  into it. `tests/type_forward_ref.ty` carries a recursive child list alongside
  the forward references.
- **A name declared nowhere is still unknown.** `tests/reject/type_forward_unknown.ty`.

The re-entrancy that on-demand parsing introduces was the real risk, not the
lookup: `parse_struct` held a `StructDef *` across the field loop, and a nested
declaration reallocs `g_structs` underneath it. Those pointers are now
re-derived from an index after every `parse_type` call, in both the struct and
enum parsers. That hazard predates this change — `struct_instantiate` could
already realloc the table from inside `parse_type` — so the fix stands on its
own.

The "did you mean" is left alone. It reads badly in the report above only
because `E` existed and was not found; for a name that genuinely does not
exist, suggesting the enclosing struct is a fair guess, since a struct may name
itself in a field through `[S]`.


## Found by `tools/tycho-snap`, 2026-08-13 (head `56709b34`)

A **dogfood audit**, chosen by measurement rather than taste: six corelib
packages had ZERO consumers outside `corelib/` — `intern`, `log`, `sqlite`,
`testing`, `toml`, `zip` — and a package nobody calls is a package whose
ergonomics nobody has tested. `tools/tycho-snap` is a real program against four
of them: read a TOML manifest, walk a tree, archive it as a zip, and prove the
archive by reading it back and checking every CRC against the bytes that went
in. Every finding below was hit while writing it, in order.

### 29. ~~`cli.parse_spec`'s schema names must be UNDASHED, and getting it wrong is silent~~ — **FIXED 2026-08-13 (`c46952de`); see the banner below. The residue — `unknown()` is advisory — is not fixed.**

The schema lists and `cli.get`'s key take the option name **without** its
leading `--`; the parser strips the dashes before matching
(`corelib/cli/cli.ty@parse_into`). Writing them the way you type them at a shell
— `parse_spec(argv, ["--manifest"], ["--quiet"])` — compiles, parses, runs, and
**silently uses every default**. Measured on the same argv:

```
dashed  get=[DEFAULT]    unknown=2
bare    get=[real.toml]  unknown=0
```

The tokens are not lost: they land in `cli.unknown(c)`, which exists for exactly
this. But nothing makes a caller read it, so the failure mode is a program that
runs to completion on a configuration nobody asked for. This is the
`core:regex`-class shape recorded at open-list item 12 for `cli.has`: the answer
is not wrong, the question was silently a different one. `tycho-snap` now reads
`unknown()` and `missing()` and exits 2, which is the workaround, not a fix.

**FIXED 2026-08-13 by accepting both spellings.** `corelib/cli/cli.ty@undash`
strips a leading `--` (or a single `-` on a short flag) from every schema entry
in `parse_into`, and from the key `get`, `has` and `flag` are given. It cannot
break an existing caller: an entry beginning with `--` matched no token before
this line existed. The same probe now reads

```
dashed  get=[real.toml]  unknown=0
bare    get=[real.toml]  unknown=0
```

~~What is NOT fixed is the general shape: `unknown()` is still advisory, so a
typo'd option (`--maifest`) still runs the program on defaults unless the caller
reads it.~~ **FIXED 2026-08-14 by a shape that cannot be ignored:
`corelib/cli/cli.ty@parse_checked` returns `Result(Cli, CliErr)` and refuses to
hand back a `Cli` carrying anything in `unknown` or `missing`. Not a `die` --
a typo'd option is INPUT, and corelib dies only for a use-after-free
(`corelib/pool/pool.ty@get`) -- but a `Result` has to be matched.

Measured before writing it: 6 programs call `parse_spec` and 5 already read
`unknown()` by hand. The sixth, `tools/tycho-kvsrv`, did not — `--pot 9000`
started the server on the default port with nothing said. It now reports
`unknown option --pot`, and `--port` with nothing after it reports
`--port needs a value`.**

### 30. ~~`[string] + [string]` is refused with a reason that is false~~ — **FIXED 2026-08-13 (`c46952de`), both sites; see the banner below**

`out = out + [s]` on a `[string]` — the append every Go and Python habit reaches
for — gives:

```
error: `+` is not defined element-wise on [string], because `+` is not defined on string
```

The clause after the comma is **untrue**, and one line of the same program
disproves it: `"x" + "y"` prints `xy`. Probed at `56709b34`, `[1, 2] + [3, 4]`
prints `4,6`, so `+` on arrays is element-wise by design and the real rule is
that there is no element-wise `+` for `string` elements. The message was built
from the element type at two sites, where `arr_elem(lt)` was spelled into a
sentence that reads as a claim about the language.

**FIXED 2026-08-13, both sites** (`src/tychoc.c:7355@element-wise` and
`src/tychoc.c:7410@element-wise`, anchored per this file's header rule). The
false clause is gone and `+` now names the operation the caller actually wanted:

```
error: `+` is not defined element-wise on [string] -- there is no element-wise `+` for string (to APPEND one element, write `push(xs, x)`)
```

The user's actual mistake is not arithmetic at all — it is that append is
`push(xs, x)`. A message naming `push` would end the confusion in one line
instead of sending the reader to check whether `+` really works on strings.

### 31. ~~A generic struct is constructed from its FIELDS, in field order, not from its type parameters~~ — **RECORDED, not proposed: this is the ordinary struct-literal rule applied to a generic**

`intern.Interner($K, $V)` is instantiated by writing its two field values — an
empty `[$V]` and an empty `[$K: int]` — so a `string -> int` interner is:

```tycho
ei := intern.Interner([]int, []string: int)      # V's array first, K's map second
```

Reading the declaration `struct Interner($K, $V)` and writing
`Interner(string, int)` gives `error: expected an expression`, pointing at
`string`. The order is not the declaration's — it is whatever order the fields
happen to sit in (`nodes: [$V]` before `lookup: [$K: int]`), so the two type
parameters appear reversed. Nothing in the diagnostic connects the two.

Not proposed as a change: this is the ordinary struct-literal rule applied to a
generic, and inference from the field values is what lets the type parameters go
unwritten. Recorded because the first thing a caller reads is the `($K, $V)`
header, and it is a false friend.

### 32. ~~Mangled instantiation names reach the user~~ — **RECORDED, cosmetic; one pass over `type_name`/callee spelling, not a phase**

Two diagnostics from the same session, verbatim:

```
error: argument 1 of 'intern__count__intern__Interner__string__int__string__int' is not inout; remove the '&'
error: cannot compare intern__Handle with int
```

The first names a symbol the programmer never wrote and cannot find in any
source file; the source spelling is `intern.count`. The second is closer but
still prints `intern__Handle` for `intern.Handle`. Both messages are otherwise
good — the first even names the fix. Purely cosmetic, and worth one pass over
`type_name`/callee spelling at some point, not a phase of its own.

### What did not go wrong, which is also data

- **`core:zip` is interoperable, and nothing in this tree had ever checked.**
  Its output is read by Python's `zipfile`: `testzip()` returns `None` (every
  CRC verified by a second implementation), the member list matches, and one
  member's SHA-256 out of the archive equals the file on disk. An **empty**
  archive is a valid 22-byte end-of-central-directory record that `zipfile`
  reads as `[]`, rather than a zero-byte file or a crash.
- **Two runs are byte-identical** once the walk sorts each directory level —
  `io.list` order is `readdir(3)` order and a snapshot that inherits it is not a
  snapshot. That is the program's job, not the package's, and `sort.asc` made it
  one line.
- **`core:toml`, `core:log` and `core:intern` did what their headers say** on
  first reading, with no probe needed: dotted-path `get`, an `inout` logger with
  four levels, and handle identity across repeated keys.
- **A directory is a package, including a scratch directory.** Two unrelated
  `package main` files in one throwaway folder make the compiler pull the
  sibling into the build: `tychoc: plus.ty is in package `main` but has no
  `package` declaration`. Correct, documented, and still a five-second stumble
  when the folder is a scratchpad rather than a project.

### 33. ~~`_` is an ordinary variable, not a discard — and it reads like one~~ — **the DIAGNOSTIC is fixed 2026-08-13; the language is unchanged and deliberately so**

Found writing `tools/tycho-snap/run.sh`'s fixture, where a call's result was
genuinely not wanted. Every spelling below was run at `bf18a560`:

```
_ = f(1)            error: assignment to unknown variable '_'
_ = r(1)            error: assignment to unknown variable '_'      (Result, same)
_ := f(1)           OK -- DECLARES a variable whose name is one underscore
_ = f(2)            OK, now: it is an ordinary assignment to that variable
_ = r(3)            error: cannot assign Result(int, E) to '_' of type int
println(str(_))     prints 2 -- the value is live and readable
for _ in [7, 8]     OK, and the body can read `_`; it is the loop variable
match ...: _: pass  OK -- but this `_` is the PATTERN wildcard, a different thing
f(4)                OK -- a bare call statement drops the value, no name needed
```

The last error is the sharpest: `_` has a **type**, inferred from whatever was
first assigned to it, so a `_` that was an `int` cannot later absorb a `Result`.
Nothing about it is special-cased.

**Two things make this worth an entry rather than a shrug.** The wildcard `_`
in a `match` arm IS special, so the language uses the same character for a real
wildcard in pattern position and for an ordinary identifier in expression
position — the asymmetry is invisible at the call site. And the tree's own
corpus reads as though the discard exists: `corelib/test/io/main.ty:303` is
`_ := io.make_dir(sdir)` followed at `:441` by `_ = io.remove(cf)`, which looks
exactly like Go's blank identifier and is in fact a live variable holding the
last `Result` assigned to it. A reader who copies that line into a file where
`_` was never declared gets "assignment to unknown variable", and a reader who
copies it into one where `_` holds a different type gets the type error instead.

**FIXED 2026-08-13: the diagnostic says it, in both positions.** No blank
identifier was added — a real one is a language decision, not a papercut fix, and
the spelling that works costs nothing: call the function as a statement.

```
_ = f(1)      error: `_` is not a discard -- it is an ordinary variable, and this one
                     was never declared. To drop a result, call it as a statement
                     (`f(x)`); to keep it, name it (`x := f(x)`)
a, _ = two()  error: `_` is not a discard -- it is an ordinary variable, and this one
                     was never declared. Name every element, or declare `_` once with
                     `:=` and reuse it
```

The two differ because the fixes differ: a tuple element cannot be called as a
statement, so there every name has to exist. Pinned by
`tests/reject/underscore_not_discard.ty` and
`tests/reject/underscore_tuple_not_discard.ty`; `make test` went 683 -> 685.
A `_` that WAS declared still assigns in both positions, checked both ways, and
an ordinary typo keeps its did-you-mean (`conut = 2` still suggests `count`).

**The original ask follows.** What the entry asked for was "assignment to unknown variable '_'"
(with a `did you mean 'c'?` suggesting an unrelated local, observed) is true and
useless; "`_` is not a discard here -- call `f(x)` as a statement to drop its
result" would end it in one line, and is the same shape as the `push` hint added
to the element-wise message in #30.

## Found by `tools/tycho-tally`, 2026-08-13 (head `b188feb2`)

The second dogfood audit, against the last two corelib packages with no consumer
outside `corelib/`: `core:sqlite` and `core:testing`. `tools/tycho-tally` is an
expense ledger whose `--selftest` is 15 `core:testing` assertions over a real
SQLite file — a test framework nothing tests being the sharper of the two
subjects.

### 34. ~~The obvious `parse_int` is the fail-open one, and the safe one is longer~~ — **the NAME stands (a rename is a 40-caller flag day); the two callers it was actually hurting are fixed, 2026-08-14**

`strings.parse_int` stops at the first non-digit and answers with what it got.
Measured at `b188feb2`, beside its checked sibling:

```
[350]                    parse_int=350  parse_int_checked=Ok(350)
[35x]                    parse_int=35   parse_int_checked=Err(Garbage)
[x35]                    parse_int=0    parse_int_checked=Err(Garbage)
[]                       parse_int=0    parse_int_checked=Err(EmptyInput)
[9999999999999999999999] parse_int=0    parse_int_checked=Err(OutOfRange)
```

A ledger that reads an amount with `parse_int` books `35x` as **35** and `abc`
as **0** — a free entry — and nothing anywhere says so. This is not the old
entry 4 ("`strings.parse_int` fails open, so no format parser can use it",
struck as ALREADY FIXED): that was closed by ADDING `parse_int_checked`, and the
trap it names is still there, under the shorter and more inviting name. The
package's own comment says the checked one exists because "`server/main.ty` each
hand-rolled a strict parser for want of this".

**Not proposed as a rename, but the call sites were audited, 2026-08-14.** Of the
40 `parse_int` calls, exactly two were fed unvalidated input:
`tools/tycho-kvsrv` (`--port abc` became port 0 — a kernel-chosen port — and
`--port 80x` became 80) and `examples/weblog` (`--top=9x` meant 9; its `< 1`
guard caught `abc` but not that). Both now use `parse_int_checked`. The rest pass
digits they produced themselves. So the harm was two programs, not the name.

`parse_int`'s fail-open behaviour is documented on
its own line and something in the tree may want it. What is worth recording is
that the audit reached for the short name first, and that the two differ by a
suffix rather than by anything a reader would notice at the call site — the same
shape as #29, where the plausible spelling was the silent one.

### What did not go wrong, which is also data

- **`core:testing` fails honestly, and this is now GATED.** The whole risk of a
  test framework nobody tests is that it prints `ok` unconditionally. Probed by
  changing one expected total in a copy of the program: it printed
  `FAIL: coffee total folded by SUM (got 625, want 999)`, then
  `FAIL tycho-tally (1 of 15 checks failed)`, and exited **1**. That control is
  `tools/tycho-tally/run.sh` leg [2] — it edits a COPY, never the tree, so the
  proof runs on every `make tally-check`.
- **`core:sqlite`'s parameter binding holds against a hostile value.**
  `o'brien'); DROP TABLE entry; --` inserted through `exec_params` reads back as
  one row with its bytes intact, and the table is still there afterwards — three
  assertions in the suite, not a claim in a comment.
- **`sqlite.exec` on malformed SQL is `Err` with a message**, not a silent
  `Ok(0)`: `THIS IS NOT SQL` is asserted in the suite.
- **A `Db` handle is an ordinary struct over a `ptr`**, so `open` -> `close` is
  the whole lifecycle and nothing in the language stops a use-after-close. Not a
  finding — the package says `h: ptr # the sqlite3* handle; 0 = closed` — but
  worth knowing before someone reaches for `defer`-shaped reasoning that does not
  exist here.
- **The compiler's copy warning fires on a live `bytes`/`string` reused after an
  aggregate takes it.** `[hostile]` in a query parameter list, with `hostile`
  still live afterwards, warns and names the fix ("make this its last use").
  Correct, and the message is the useful kind.

## Found by `tools/tycho-agg`, 2026-08-13 (head `097f7e16`)

The third dogfood audit, and the first chosen by a LANGUAGE FEATURE rather than a
package — every corelib package had a consumer by then. `tools/tycho-agg`
declares its own generics in `pipe/` and instantiates them across a package
boundary at its own `Row` type.

> **THE MEASUREMENT THAT PICKED IT WAS WRONG, and the review caught it the same
> day.** This section first said "`$T` appeared in ZERO files under `tools/`,
> `examples/`, `server/` and `bench/`" and built the program's whole
> justification on it. `$` is an end-of-line anchor in a POSIX regex, so
> `grep "$T"` searches for *end-of-line followed by T* and cannot match anything;
> the zero was an artifact of the instrument, not a property of the tree. A
> fixed-string count finds **~50 uses across six files** —
> `tools/tycho-flow/stage/stage.ty` (21, `struct Item($T)`),
> `tools/tycho-flow/graph/graph.ty` (16, `struct Plan($T)`), `tools/tycho-q`,
> `tools/prunner`, and `examples/generics_tour.ty`, which exists for exactly this
> subject. **Writing generics outside `corelib/` was not new here.** The findings
> below stand on their own — they are properties of the type system, not of who
> had written a generic first — and the lane's one defensible uniqueness is
> narrower: no other `run.sh` greps the emitted C for a `pkg__fn__type` mangling.
>
> It is the same failure mode this file records against everything else: a proxy
> measured instead of the thing, and a green (here, a zero) believed because it
> was convenient. `grep -F` is what should have run.

### 35. ~~A `where hashable(K)` constraint does not admit `K` as a map key~~ — **FIXED 2026-08-13: it does now, and the constraint is enforced at instantiation instead**

Six lines, run at `097f7e16`:

<!-- fence-skip: this fence is the REPRO -- it must not compile, and the error it produces is quoted directly below it -->
```tycho
fn counts(xs: [$K]) -> int where hashable(K):
    m := []K: int          # the constraint says K is a legal map key
    for x in xs:
        m[x] = 1
    return len(m)
```

```
error: map keys must be string, int (directly or through a newtype), a fieldless
       enum, or a hashable struct/tuple/array
```

The constraint exists to say exactly what the message asks for. A generic that
wants a map keyed by its own parameter therefore cannot build one, and the shape
this forces is visible in the corelib itself: `corelib/intern/intern.ty`'s
`Interner($K, $V)` is **constructed by the caller**, where `K` is concrete, and
every function there only ever receives it. `tycho-agg` hit the wall and took the
same way out (`tools/tycho-agg/pipe/pipe.ty@group_into` groups INTO a tally the
caller built, rather than returning one it made).

**FIXED the same day.** The DECLARED type `[$K: $V]` already routed a type
parameter past the key check (the `has_typaram` test beside `mapc_of`); the empty
map LITERAL did not, and called `map_of` on an unsubstituted parameter. It now
takes the same route. The constraint is not weakened — it is checked where the
type is known: `distinct([1.5, 2.5])` reports `'distinct' instantiated with
K = float, which does not satisfy \`hashable(K)\``, which is a better place for it
than the declaration, because the message can name the type that was chosen.
Pinned by `tests/generic_map_key.ty` (string, int and a struct key) and
`tests/reject/generic_map_key_unhashable.ty`; `make test` went 685 -> 687.

### 36. ~~Two spellings a generic cannot use, and one it cannot infer~~ — **all three resolved 2026-08-13: two spellings fixed, and the third was a FALSE CLAIM — the explicit type-argument form always existed; its diagnostic is what was missing**

Hit in order while writing `pipe/`:

- **`[]$T` is not an empty-array literal.** ***FIXED 2026-08-13: it is now.***
  The empty-literal parse site accepted every type-token EXCEPT `TK_DOLLAR`, so
  the literal fell through to the untyped marker and the `$` died on the next
  token. `parse_type` had always handled `$T`. `out := []$T` is
  `error: expected newline` at the `$`. Outside a generic `[]string` and `[]int` are the ordinary
  spellings, so the analogy is the first thing a writer reaches for. The working
  form is a bare `out := []`, whose element type comes from a later `push` —
  which is what every corelib generic does (`corelib/iter/iter.ty`,
  `corelib/arrays/arrays.ty`).
- **A bare `[]` needs an expected type.** In `Tally([], [])` it is
  `error: cannot type a bare [] here -- no expected type (write []T, or use it
  where the element type is known)`. The message's `[]T` advice is right and is
  the OTHER spelling: bare `K`, not `$K`, works in expression position — `$` is
  the declaration form. Nothing says so at the point of use.
- **A type parameter appearing only in the RETURN type cannot be inferred.**
  `fn tally() -> Tally($K)` compiles, and the call `t := tally()` fails one line
  later with `argument 1 of bump is pipe__Tally__t28, expected
  pipe__Tally__string` — an unbound type variable escaping into the next call's
  message. ~~There is no explicit-instantiation syntax to rescue it, so such a
  constructor cannot be written at all.~~

  **THAT LAST SENTENCE WAS WRONG, retracted 2026-08-13.** The explicit
  type-argument form `name$(T, …)` is specified (§7.1, §7.5 —
  `docs/spec/05-generics.md:39`) and has always worked: `tally$(string)()` builds
  the tally and `bump` accepts it. I asserted an absence without grepping the
  spec I had open, which is the same failure as the `$T` count this section's
  preface retracts.

  **What was real is the DIAGNOSTIC, and it is fixed.** The call that could not
  be inferred used to compile, leaving the unbound cell to surface one call later
  under a bind-vector name. `instantiate_generic` now checks for a parameter no
  argument bound and dies at that call:

  ```
  error: no argument fixes 'empty''s type parameter $T -- it appears only in the
         return type. Give it explicitly: empty$(<the type for $T>)(...)
  ```

  Pinned both ways by `tests/generic_explicit_typearg.ty` (the form working at
  two instantiations) and `tests/reject/generic_return_only_unbound.ty`.

### What did not go wrong, which is also data

- **Cross-package generic instantiation works, at a type the corelib has never
  seen.** `pipe.keep`, `pipe.to` (two parameters, `$T` -> `$U`), `pipe.fold`
  (three types in one signature) and `pipe.group_into` all instantiate at
  `main`'s own `Row`, with the function values written inline as lambdas at the
  call site. `make agg-check` asserts the mangled instantiations appear in the
  emitted C, because the counts alone cannot show a generic ran.
- **The `required from here` note is the reason these were diagnosable at all.**
  Every generic error above pointed at the instantiating call in `main.ty` as
  well as the failing line in `pipe/`, which is what turned "an unbound type
  variable" into "this call did it".
- **`where` clauses compose with an `inout` generic struct.** `bump(t: inout
  Tally($K), k: $K) where hashable(K)` takes the tally by reference, mutates the
  map, and the caller's copy sees it — no aliasing surprise and no annotation
  beyond the one on the parameter.

## Found by `tools/tycho-tmpl`, 2026-08-14 (head `08f50a5f`)

The fourth dogfood audit. Subject picked by measurement, **with `grep -F` this
time**: `sink` appears 18 times in the tree and **all 18 are in `tests/`** — no
program under `tools/`, `examples/`, `server/` or `bench/` uses it, and neither
does `corelib/`. A parameter mode exercised only by its own fixtures.
`tools/tycho-tmpl` is a line-oriented template renderer whose document builder
(`tools/tycho-tmpl/doc/`) consumes at every step.

### 37. ~~`sink` cannot express a builder~~ — **CLOSED 2026-08-18: deliberate, and PINNED**

The rule and its reasoning are in the source (`src/tychoc.c@sink_arg_into`):
rather than silently copy, require the move-vs-copy to be visible. The cost is
real — the composition must be point-free and a value observed before it is
consumed has to be recomputed — and it is the price of the rule, not a defect.
`tests/reject/sink_builder_two_mentions.ty` pins the refusal now, so a change
reddens instead of going quiet. The record of what the item said when open
follows.


Four refusals, in the order a builder hits them. Each is the compiler's own
message, at `08f50a5f`:

1. **Accumulate in a loop** — `d = add(d, s)` inside `for`:
   *"'d' is consumed by a `sink` parameter inside a loop, where the next
   iteration would consume it again; no named variable can be adopted in a loop,
   a copy made in the loop included."*
2. **Collect then consume** — `push(lines, s)` in a loop, then `of(lines)`:
   *"'lines' … is mentioned 2 times in this function: a sink argument must be the
   variable's ONLY mention."*
3. **Create, grow, consume** — `f := empty()`, `f = add(f, …)`, `join(d, f)`:
   three mentions, same refusal, and this one is **outside any loop**.
4. **Count before consuming** — `n := len(lines)` then `of(lines)`: the count is
   the second mention.

**So the only expressible shape is point-free.** Nothing may be named:

<!-- fence-skip: quoted from the program that hit this friction; `doc` belongs to that program, not to this document -->
```tycho
doc.render(doc.join(doc.of(expand_all(text, keys, vals)),
                    doc.add(doc.empty(), "-- rendered " + str(n) + " line(s)")),
           sep)
```

and a value that must be *observed* before it is consumed has to be recomputed —
`tycho-tmpl` calls `expand_all` twice, once to count and once to adopt, which is
the honest cost of rule 4.

**The rule is deliberate and the reasoning is in the source**: `src/tychoc.c@sink_arg_into`
— "Rather than silently copy, require the move-vs-copy to be visible
(Hylo-style)", and one textual mention "is how the compiler proves the consume is
the last use on every path, with no dataflow analysis". Nothing here argues with
that. What is recorded is the CONSEQUENCE, which no fixture showed because a
fixture consumes once and stops: **a consuming API cannot have a builder, and its
callers must be written point-free.**

**THE SPEC WAS WRONG, and is corrected.** `docs/spec/11-functions.md` §15.2 said
a copy "is made only where value semantics require independence (the variable is
read again after the call, **is used inside a loop**, or is captured by a
closure)". No implementation ever made that loop copy — the call is refused. The
paragraph now states the implemented rule and says what it used to claim.

### What did not go wrong, which is also data

- **Every one of the four messages named the variable, the rule AND the fix.**
  Rule 2's even lists the disqualifying forms (`len(lines)`, `lines[0]`,
  `lines[0] = …`) and says "even before this line", which is what turned a
  puzzling refusal into a five-second edit.
- **`cli.parse_checked` earned its keep on its first outside caller.** This
  program used it from the start and `--sett x=1` was refused by name — the
  shape FRICTION #29 asked for, working in a program written after it.
- **A `sink` on a plain array works exactly as specified.** `doc.of(xs: sink [string])`
  adopts a call's result with no copy; only NAMED values are the problem.

---

## Found by `tools/tycho-stat`, 2026-08-14 (head `6037a235`)

All three are **FIXED**, with the compiler's own message quoted as it stood
before the fix. The program that found them is gated by `make stat-check`. `tests/pkg/variadic_qual/` pins the working forms; its two
reject siblings and `tests/reject/variadic_empty_untyped.ty` pin the refusals.

### 38. ~~A variadic called through a package qualifier did not pack~~ — **FIXED 2026-08-14**

<!-- fence-skip: quoted from the program that hit this friction; `vp` belongs to that program, not to this document -->
```tycho
vp.sum(1, 2, 3)     # error: 'vp__sum' takes 1 argument(s), got 3
local_sum(1, 2, 3)  # fine -- the same declaration, called unqualified
```

The fold that turns trailing arguments into one array ran under
`if (!e->qual && !e->lhs)`, so a qualified call reached the arity check UNPACKED
and was measured against the one declared parameter. The `...T` was not
mis-typed or mis-inferred; it was never applied. Dropping `!e->qual` is the
whole fix — the lookup name needs no prefixing, because `e->sval` is already the
mangled `vp__sum` at that point (a first attempt that prefixed it searched
`vp__vp__sum` and failed identically, which is what the probe caught).

This is a **cross-package** defect, which is why three files of fixtures never
saw it: every variadic call in the tree was same-package.

### 39. ~~Explicit type arguments did not parse on a qualified name~~ — **FIXED 2026-08-14**

<!-- fence-skip: quoted from the program that hit this friction; `vp` belongs to that program, not to this document -->
```tycho
vp.ident$(int)(5)   # error: expected ')'   <-- pointing at the '$'
```

Nothing to do with variadics: the qualified-call arm tested only for `(` after
the name, so a `$` fell through to a FIELD ACCESS and the spelling was a parse
error in every form — one type argument or several, with value arguments or
without. The bare form `ident$(int)(5)` has always worked, so the two spellings
of the same call disagreed. The grammar named `IDENT` in that production and was
therefore *accurate*; fixing the parser is a language change, and §3/§4,
Appendix A, §7.5 and §15.3 all moved with it.

### 40. ~~An empty generic variadic refused the type it was handed~~ — **FIXED 2026-08-14**

<!-- fence-skip: quoted from the program that hit this friction; `names from its own program` belongs to that program, not to this document -->
```tycho
count$(int)()   # error: cannot infer the element type of an empty variadic
                #        call to generic 'count'; pass at least one argument
```

The diagnostic was right that there was nothing to infer FROM and wrong that
this left no answer: the caller had just named the type outright. The packing
site never consulted `e->ntypeargs`, so the one spelling that could have
supplied the element type was the one it rejected. It now binds the element from
the explicit list in declaration order — the same rule `instantiate_generic` uses
— and the message, for the case where nothing was named, points at the cure:
*"…pass at least one argument, or name the type: `count$(<type>)()`"*.

### What did not go wrong, which is also data

- **Spread, fixed-then-variadic, and generic variadics all worked once the call
  packed.** `vp.sum(arr...)`, `vp.tagged("n", 7, 8)` and `vp.count("a","b","c")`
  needed no further change — #38 was one missing fold, not a broken feature.
- **`zero$(T)` under `where defaultable(T), numeric(T)` worked first time**, which
  is the feature the package was written to exercise. The constraint list is
  comma-separated; `and` is not a spelling the grammar has.

---

## Found by `tools/tycho-ledger`, 2026-08-14 (head `d0e72f6e`)

Sixth audit, same method: measure what the tree does not exercise, then write
the first real consumer. `type X = U` is declared in 46 files and **43 are under
`tests/`**. The three genuine consumers — `tools/tycho-vm/main.ty`,
`corelib/intern/intern.ty`, `corelib/pool/pool.ty` — each declare a newtype over
`int` and use it INSIDE their own package, and exactly one file in the tree ever
named a foreign package's newtype (`examples/corelib/pool/main.ty:12`). The
cross-package surface was effectively untested.

It held. What broke was how the compiler TALKS about it, twice. Both are
**FIXED**; `tests/pkg/newtype_qual/`, `tests/reject/pkg/newtype_qual_mix/` and
`tests/reject/map_for_in.ty` pin them, and the program is gated by
`make ledger-check`.

### 41. ~~Every imported type and callee printed its MANGLED name~~ — **FIXED 2026-08-14**

```
declared type int but value is pool__Handle        # the user wrote pool.Handle
argument 2 of 'money__add' is int, expected money__Cents
```

`type_name` returned the stored name verbatim, and a nominal type from another
package is STORED mangled. So every diagnostic about a corelib type — not just a
newtype: struct, enum and handle alike — named something the reader cannot type
and cannot grep for. A same-package type was unaffected (`got Pc, Slot`), which
is why the tree never noticed: the three real newtype consumers all stay inside
one package.

Fixed centrally in `type_name` plus the `die_at` sites that quote a callee, via
one helper that rewrites the first `__` to `.` **only when the prefix names an
imported package** — so a local type containing `__` is left alone. Zero fixtures
and zero goldens pinned a mangled name (measured before the change), and
`make test` was 693/0 after it.

### 42. ~~`for k in m` reported an index the user never wrote~~ — **FIXED 2026-08-14**

<!-- fence-skip: quoted from the program that hit this friction; `names from its own program` belongs to that program, not to this document -->
```tycho
for k in m:        # error: map key must be string, got int
```

A map is not directly iterable; `keys(m)` is the way (§18.6). But `for x in COLL`
desugars at parse time into an indexed loop, so the failure surfaced as a KEY
TYPE error about `m[_fi0]` — an expression the user never wrote, naming neither
the loop nor the cure. The synthetic index now carries a `forin` marker and the
message says what to do:

> a map is not directly iterable -- `for k in m` indexes it by position. Loop
> over `keys(m)` instead: `for k in keys(m):` (the keys come back as X, wrapped)

**A genuine wrong index still gets the precise old message** (`m[0]` on a
string-keyed map: *"map key must be string, got int"*), which is the negative
control — the new arm must not swallow it.

> Writing that guard is also where this audit's one self-inflicted bug lived: the
> new branch went in without braces, so the original `die_at` fell OUTSIDE
> `if (kt != wantk)` and **every** map index died with `got X` equal to `want X`.
> Caught by reading the probe output, before `make test`.

### What did not go wrong, which is also data

- **The whole cross-package newtype surface worked on the first build**:
  arithmetic and ordering in the consumer, a map KEYED by a foreign newtype over
  string, `keys()` handing the keys back wrapped, arrays of a foreign newtype,
  `to_int`/`to_str`/`to_under`, and a struct field typed as one.
- **`sort.asc` instantiated at a foreign newtype**, so `where comparable(T)`
  admits a newtype over string across two package boundaries at once.
- **All nine distinctness violations were refused**, including a bare `string`
  used to index an `Account`-keyed map. The type system's half of the feature is
  sound; only its vocabulary was wrong.

---

## Found by `tools/tycho-fh`, 2026-08-14 (head `ff7894f3`)

Seventh audit, and the last feature whose only caller was its own fixture:
`handle Name:` was declared in exactly **10 files, all under `tests/`** — nine
rejects and `tests/ffi/main.ty`. Nothing under `tools/`, `examples/`, `server/`,
`bench/` or `corelib/` declared one. Writing the first consumer found a **double
free**.

Both are **FIXED** as hard errors, on the user's instruction ("hard error not a
warning") after being told the copy fix refuses programs that compile today —
blast radius nil, since no program outside `tests/` declares a handle.

### 43. ~~A handle could be COPIED, and the copy double-freed~~ — **FIXED 2026-08-14**

```tycho
f := fh_open("/etc/hostname", "r")
g := f                              # accepted
```
```
free(): double free detected in tcache 2
```

Both names got a scope-exit destructor call on one pointer. This is memory
corruption in a language whose claim is memory safety without a GC, reached by
two lines of ordinary-looking code.

**Reassignment was already guarded** (`g = f` → *"a handle variable cannot be
reassigned"*), and so was the exact sibling one type up: a task, two lines
earlier in the same switch, refuses `u := t` with *"a task handle cannot be
copied or re-bound"*. Only the handle DECL path was open. The fix mirrors the
task rule — a handle-typed declaration is legal only when its RHS is a call,
which is the one thing that can produce a handle (an `extern` opener).

### 44. ~~A BARE handle as a struct field was accepted~~ — **FIXED 2026-08-14**

<!-- fence-skip: quoted from the program that hit this friction; `File` belongs to that program, not to this document -->
```tycho
struct S:
    f: File      # accepted; every other aggregate refuses it
```

`tests/reject/affine_handle_container_type.ty` covers `items: [R]` — an ARRAY of
handles in a struct field — and passes because the **array intern helper**
refuses it. Every affine refusal in the compiler lives in such a helper
(`arr_of`, `opt_of`, `res_of`, tuple, map). A handle that *is* the field type
passes through no helper at all, so nothing checked it. The check now runs where
the field is registered, and reports the field's own line.

**This is the shape of both bugs**: the guard is on the type CONSTRUCTORS, so
every path that builds no new type slipped by. Worth remembering the next time an
affine rule is added.

### What did not go wrong, which is also data

- **RAII itself is sound.** 20000 opens, each in its own scope: `live 0`,
  `opens == closes`, and a re-read checksum of exactly 20000 x 19 — so no fd
  leaked and no open silently began failing.
- **A borrow is still a borrow.** Passing a handle twice in one expression
  leaves exactly one live owner, and the callee does not free it (spec §25).
- **Four affine rules already held**: returned from a Tycho fn, in an array, in
  an `Option`, and `close()` on a call result are each refused by name.

### 45. ~~A channel can be copied too, and it makes the compiler's own warning lie~~ — **FIXED 2026-08-14**

```tycho
c := channel(int, 2)
e := c                  # accepted
send(e, 41)
println("got=" + str(recv(c)))     # prints Some(41)
```
```
warning: nothing ever sends on channel 'c', so a receive on it parks forever
```

The send happens through the alias, so the warning is false. No double free comes
of it — the free is keyed to the creating declaration — so this is an analysis
defect rather than a memory-safety one. It was the same missing decl-path guard
as #43, in the channel's switch arm rather than the handle's, and it is closed
the same way: a channel-typed declaration is legal only from `channel(...)`
itself. **The spec already said so** and nothing enforced it —
`channel(T, cap)` is "legal only as the direct right-hand side of a declaration"
(`docs/spec/13-concurrency.md:176`). Passing a channel as an argument is
untouched, which is how a consumer still receives one. Locked by
`tests/reject/affine_chan_copy.ty`.

Three members of one family now refuse the same shape at the same place — a
task, a typed handle, a channel — and each of the three was open on the DECL
path while reassignment was already guarded. That is the pattern worth
remembering rather than the three fixes.

### 49. ~~`sink` and `inout` on an affine type were accepted and silently ignored~~ — **FIXED 2026-08-14**

Carried for a day as an open question — the spec said passing a handle BORROWS
but said nothing about `sink`, so "intended" and "unguarded" looked alike.
**Measuring it answered it.** With a counting C shim, `take(h: sink File)` left
the handle **live and unclosed at return** (`live=1 closes=0`): the callee
borrowed exactly like a default parameter. The declared mode did nothing at all.

Refused rather than implemented, at the declaration. A consuming callee would
have to free at ITS scope exit while the destructor is emitted at the owner's —
which is precisely the double free of #43. `inout` goes with it: there is no
copy-back for a value with one owner. The rule covers all three affine types,
and §25 now states it.

A mode that reads as ownership and delivers a borrow is worse than no mode: it
is a promise in the signature that nothing keeps.

### 50. ~~The spec claimed the `sink` consume rule applied to every type~~ — **SPEC CORRECTED 2026-08-14**

Found by probing #49's neighbours. `sink` on a NON-HEAP type is accepted and
enforces nothing: `f(n: sink int)` then reading `n` compiles and prints, where the
same shape on a `string` or `[int]` is refused. §15.2 stated the rule with no
qualification.

**The compiler is right and the spec was wrong**, which is the reverse of the
first three findings in this section and worth recording as such. Two existing
fixtures — `tests/reject/sink_arg_scalar.ty` and `sink_arg_newtype.ty` — use
`sink int` and `sink Id` as SETUP to test argument checking, so refusing `sink` on
a scalar would have left both passing for the wrong reason: they would be rejected
at the declaration and never reach the call-argument check they exist to pin. That
is the vacuous-test disease, and nearly shipping it is the reason this entry
exists.

The boundary is `type_is_heap`, measured: enforced for `string`, `[T]`, a struct
with any heap field and a newtype over one; inert for `int`, `float`, `bool`, an
all-scalar struct and a newtype over a scalar. §15.2 now carries that table and
the consequence a reader cannot see from a signature — `fn take(p: sink Point)`
enforces nothing over `Point{int,int}` and starts enforcing it on every caller the
day `Point` gains a `string`. `tests/sink_scalar_noop.ty` prints the proof.

---

## Found by `tools/tycho-grid`, 2026-08-14 (head `8c06eb8a`)

Eighth audit, three features at once — each had exactly **one** real consumer in
the tree, measured 2026-08-14:

| feature | sole consumer before this |
|---|---|
| `subscript` | `tools/tycho-sim/world/world.ty` |
| `bounded[N]T` | `tools/tycho-vm/main.ty` |
| `# deprecated:` | `corelib/sort/sort.ty` |

`subscript` and `bounded[N]T` came through clean — every rule in their spec
sections holds, including the four capacity rejections, the element-type
restriction, the runtime overflow abort, and all five subscript declaration
rules. The deprecation MARKER had a defect in each direction.

### 46. ~~Prose that merely mentioned the marker deprecated the function below it~~ — **FIXED 2026-08-14**

<!-- fence-skip: the fn header alone is the point -- the marker's placement above it is what mattered, and a body would only obscure it -->
```tycho
# this replaces the old deprecated: thing we removed
fn ordinary(x: int) -> int:
```
```
warning: `ordinary` is deprecated: thing we removed
```

`dnote_scan` walked the comment looking for `deprecated:` **anywhere in the
line**, so any sentence containing the word marked the next `fn`, and every
caller got a warning nobody wrote — pointing at a function that was never
deprecated, with a message that is the tail of an unrelated sentence.

This was **already latent in the tree**: `tests/warn/deprecated.ty:1` is a
sentence containing the marker, one line away from a `fn`. The marker must now
OPEN the comment (`#`, optional spaces, then `deprecated:`), which is the form
both real users already use.

### 47. ~~A deprecated function taken as a VALUE warned nowhere~~ — **FIXED 2026-08-14**

<!-- fence-skip: quoted from the program that hit this friction; `stale` belongs to that program, not to this document -->
```tycho
f := stale        # no warning
println(str(f(1)))  # and none here either -- the call names `f`
```

The warning fires at call sites, keyed on the callee's name; a call through a
function value has no such name, so one binding laundered the whole policy. The
warning now also fires where the value is TAKEN, which is the last point the
name is visible. Locked by `tests/warn/deprecated_edges.err`, which carries
exactly one warning — on the binding line — and none for the prose case above it.

### 48. A subscript cannot read two fields of its receiver, so 2-D indexing on a flat array is inexpressible — **RECORDED, not open**

**Not fixed — recorded**, because the fix is a judgement call about double
evaluation rather than a defect.

```tycho
subscript at(g: Grid, r: int, c: int) -> inout int:
    yield &g.cells[r * g.w + c]
```
```
error: subscript parameter 'g' is used more than once in the yielded place
```

The rule exists "so no argument is double-evaluated when substituted", and for
`r` and `c` that is exactly right — either could be a call with side effects.
But the RECEIVER is mentioned twice here only because the projection reads two
FIELDS of it, and it is substituted as a place, not re-evaluated as an
expression. The rule is stricter than its own rationale, and what it rules out
is the single most obvious use of a subscript: indexing a flat array by row and
column.

The way out is a nested array (`yield &g.rows[r][c]`), which mentions the
receiver once and is what `tools/tycho-grid` uses — at the cost of the flat
layout the feature would otherwise let you hide. Lifting the rule for the
receiver alone would need care: a call-site receiver can itself be an arbitrary
expression (`make_grid().at(1, 2)`), which would then be evaluated twice. That is
a decision, not a bug fix, so it is written down rather than guessed at.
`tools/tycho-grid/run.sh` leg [5b] pins the current behaviour, and says so.

## Found by probing GENERIC INSTANTIATION, 2026-08-14 (head `81588f75`)

Not an audit of one feature but of one seam: a rule enforced on the TEMPLATE and
never re-checked on the INSTANCE. Four hits, all the same shape. The census that
pointed here — function values had one real consumer, and no fixture anywhere
combined `bounded[N]T` with a generic.

### 51. A generic struct with a `[K: fn($T)->$T]` field emitted an undefined `FnC0` — **FIXED**

> Pinned-by: make test

The map sibling of the array bug `tools/tycho-flow/graph/graph.ty` records as
fixed. The array body loop skips a typaram'd composite; the map body loop never
had that skip, so the dead template map named an `FnC<id>` that was deliberately
not emitted and cc rejected the program. Every other map loop already had the
guard. `tests/generic_fn_map_field.ty`.

### 52. `bounded[N]T` in a generic struct silently became `[N]T` — **FIXED**

> Pinned-by: make test

`subst_type` rebuilt every instantiated array through `fixarr_of`, which hardcodes
`bnd=0`, so the SIZE survived and the capacity RULE did not. The declaration said
bounded and the program got a fixed array: passing a real bounded value
type-errored, while a plain `[N]T` was accepted with nothing on stderr. It hit a
concrete element type too (`bounded[3]int` in a generic struct), which is what
proves it was the array rebuild rather than the substitution of `$T`.
`tests/generic_bounded_field.ty`, `tests/abort/generic_bounded_overflow.ty` for
the trap a golden cannot show, and `tests/reject/generic_bounded_field_degraded.ty`
for the silent half — the two positive fixtures pass real bounded values, so they
fail loudly and cannot catch it.

### 53. A generic field or payload bound at an affine type escaped the ban — **FIXED**

> Pinned-by: make test

`struct Box($T): c: $T` instantiated at `Channel(int)` compiled and ran: CC-4's
scan runs over the aggregates that exist when `resolve_program` runs, and an
instance is stamped out later. Then `b2 := b` copied the struct and gave two
owners one channel — a `send` through one was received through the other. The
enum payload had the identical hole, and once a struct held a channel `[b, b]`
put it in an array, so the whole containment ban fell. A **Task has no type
syntax at all**, so a `$T` binding is the only way one can reach a field, which
makes instantiation the only place it could ever be caught. Three reject
fixtures. Every other storable position (array, map, Option, tuple, and a generic
fn returning `Option($T)`) was already refused by a separate value-level guard.

### 54. `sink $T` bound to an affine type escaped the rule the parse site names — **FIXED**

> Pinned-by: make test

Not a memory-safety bug **today**: measured against the counting
shim in `tools/tycho-fh/fh.c`, `sink $T` at a handle gives `opens=1 closes=1
live=0`, identical to a plain borrow — `sink` on such a type currently enforces
nothing, so nothing double-frees. `inout $T` is caught by a value-level guard;
`sink` is not. It was an inconsistency in a rule the parse site states by name, and would
become a real double free the day `sink` starts consuming affine types, so it is
closed at the instance signature rather than left to that day.
`tests/reject/generic_sink_affine.ty`.

### 55. A generic returning `$T` at a handle DOUBLE FREED — **FIXED**

> Pinned-by: make test

Found by finishing the sweep of #51-54 rather than stopping at four: every
declaration-time type rule, re-probed through `$T`. `fn ident(x: $T) -> $T` at a
handle compiled, because the ban on returning one is checked against the WRITTEN
return type, which is `$T`. `g := ident(f)` then freed the same pointer twice --
the callee at its scope exit, the caller on the copy -- and glibc aborted with
`double free detected in tcache 2`, exit 134, against the counters in
`tools/tycho-fh/fh.c`. A channel and a task were already refused out of the same
generic by their own guards; only the handle was open, the same asymmetry as #54.
`tests/reject/generic_ret_handle.ty`.

The rest of that sweep came back clean, and the negative results are the point:
`soa` element, map-key hashability, the bounded element ban, recursive-by-value
(caught on the instance, `Node__int contains itself by value`), and handle
containment in a struct, enum, Option or array are all re-checked when the type
arrives through `$T`. `[$N]T` in a struct field stays unreachable that way --
the type cannot be named at a call site to infer it.

### 56. ~~`decimal.from_str` fails open to a WRONG NUMBER, not to zero~~ — **CLOSED 2026-08-18: the lax name is deprecated**

`from_str` is marked `# deprecated:` and every call site now warns at compile
time, naming `from_str_checked` and quoting the failure (`"12.5x"` returns
1.25). Behaviour is unchanged, so no program breaks; the silence is what was
removed. The parse body moved to a private `parse_unchecked` so the checked
path does not warn on its own delegation. All three `tools/tycho-q` sites and
the worked example moved to the checked call; `corelib/test/decimal` keeps the
lax one deliberately, since testing it is the point. The record of what the
item said when open follows.


Found by sweeping every corelib parse function for FRICTION #4's shape after
closing it. `core:decimal` is the money type — the package header calls itself
"the right type for money and any base-10-exact arithmetic" — and its only
constructor from text failed open **worse than `parse_int` does**. `parse_int`
fails to 0, which is at least obviously a default. `from_str` returns a plausible
wrong amount, because it splits on the point and lets `bignum.from_str` skip
non-digits while the SCALE still counts them as digit positions:

```
"1.5x"  -> coefficient 15, scale 2  =  0.15
"1.2.3" -> coefficient 12, scale 3  =  0.012
"1,5"   -> 1                            (the comma-decimal spelling, silently)
" 1.5"  -> 0.0
```

Nothing in the tree was wrong because of it — `tools/tycho-q` guards both of its
data paths with a `decimal.to_str(d) == cell` round trip, deliberately, and its
third call site takes an already-shape-checked lexer token. The gap was the
missing API: `core:strings` has `parse_int_checked`, and `core:decimal` had a
`Result`-returning `div` with a `DivErr`, so the convention was present in the
package and `from_str` was the outlier.

`from_str_checked(s) -> Result(Decimal, DecErr)` accepts exactly
`[-|+]digits[.digits]` and nothing else; `from_str` is unchanged and still lax,
so no caller moves. 17 inputs are asserted in `corelib/test/decimal/main.ty`,
including the two wrong numbers above **against the lax answer**, so a regression
in either direction moves that line. Arbitrary precision survives the check: a
20-digit coefficient round-trips exactly.

### 57. `core:http` cannot be pointed at a private CA — **FIXED 2026-08-15**

> Pinned-by: make http-verify

Found 2026-08-15 while building `scripts/tls_verify.sh`, which proves `core:tls`
verifies certificates. The same three-way check was written for `core:http` and
then **removed**, because its positive control cannot be made to pass.

`http_shim.c` sets no `CURLOPT_SSL_VERIFY*`, so libcurl's verifying defaults
apply and HTTPS is checked. But there is no way to add a CA:

- `CURL_CA_BUNDLE` is read by the curl **tool**, not by libcurl.
- `SSL_CERT_FILE` is not honoured by this build (libcurl 8.21). Measured against
  a local `openssl s_server`: an untrusted CA and the correct CA **both** give
  `status 0`.
- The shim exposes no `CURLOPT_CAINFO`.

Two consequences, and the second is why this is filed rather than shrugged at:

1. A program cannot talk to an internal service with a private CA. The only
   workaround is installing the CA into the system store, which is a machine-wide
   change to make one program work.
2. **`core:http`'s verification is therefore ungated.** A
   `CURLOPT_SSL_VERIFYPEER, 0L` added while debugging would pass every lane in
   this tree — the same hole `tls-verify` just closed for `core:tls`, still open
   here. Not because the check is hard, but because without a leg that must
   SUCCEED, "the untrusted server was refused" is indistinguishable from "nothing
   connected at all".

The fix is an API decision, not a patch: expose a CA path (an option on `get`, or
an honoured environment variable) so a caller can trust a private CA, at which
point the gate follows immediately from `tls_verify.sh`'s existing shape.

**Fixed the same day, by the environment variable rather than a new argument.**
`http_shim.c` reads `SSL_CERT_FILE` into `CURLOPT_CAINFO` and `SSL_CERT_DIR` into
`CURLOPT_CAPATH` — the two `core:tls` already obeys through OpenSSL's own
defaults, so the two HTTPS clients in this tree stop trusting different stores
from the same environment. No signature moved and no caller changed.

They **redirect** trust and cannot disable it: no `CURLOPT_SSL_VERIFY*` is set
anywhere in that file, so libcurl's verifying defaults stand whatever the
environment says, and an unreadable path fails closed. Empty is treated as unset,
so `SSL_CERT_FILE=` cannot degrade into a request with no CA at all.

`make http-verify` (`scripts/http_verify.sh`) is the lane this unlocks, and the
measurement is worth keeping in both directions. **Before**, run against the
unfixed shim: `[2]` and `[2b]` both `status 0` — the positive control was
unreachable exactly as this entry said. **After**: `[1] 0, [2] 200, [2b] 200,
[3] 0`.

The run before the fix also settled something this entry could only assume. Its
control leg `[C]` — a probe built against a COPY of corelib with
`SSL_VERIFYPEER`/`VERIFYHOST` off — returned **200 against the same untrusted
server**. So `core:http` was verifying correctly all along; what was missing was
any way to prove it. That is the distinction worth carrying: an ungated property
is not a broken one, it is one that can break silently later.

Four negative controls, each reddening exactly one leg and nothing else:

| break | leg that reddens |
|---|---|
| `CURLOPT_SSL_VERIFYPEER, 0L` | `[1]` alone — the untrusted chain is ACCEPTED |
| `CURLOPT_SSL_VERIFYHOST, 0L` | `[3]` alone — a valid cert for another name is accepted |
| the `CAINFO` line deleted | `[2]` alone — `[2b]` still 200 |
| the `CAPATH` line deleted | `[2b]` alone — `[2]` still 200 |

The last two are the ones that matter for honesty: they prove `SSL_CERT_FILE` and
`SSL_CERT_DIR` are two code paths rather than one option documented twice.

**A note on the controls themselves, since it nearly produced a false result.**
The first control script restored the shim with `git checkout --` between runs.
That restores **HEAD**, which did not carry the still-uncommitted fix, so three of
the four controls silently ran against the unfixed shim and every one of them
"reddened" for the wrong reason. It looked like a clean result. Restoring from a
copy of the working-tree file, and asserting the backup contains the fix before
scoring anything, is what the script does now.

### 58. `uuid.v4` looks like 122 random bits and carries at most 32 — **documented 2026-08-15**

> Pinned-by: grep -q 'NOT unguessable' corelib/uuid/uuid.ty && grep -q 'not unguessable' docs/guides/corelib.md

`core:uuid` produces RFC 4122 version-4 UUIDs, and the whole point of v4 is that
it is the random one. The source is `core:rand`'s xorshift32, whose state is
**32 bits**. So the reachable set of UUID streams is 2^32, not 2^122, and the
whole seed space is enumerable. Measured: the same seed reproduces the same
sequence exactly, across processes.

That is correct behaviour for the generator -- `rand`'s own header says "NOT
cryptographic" in its first line, and it is right to be deterministic. The gap
was that **`uuid` never repeated it**, and neither did the corelib catalogue,
which described them as "random version-4 UUIDs". A reader who knows what v4
means will assume unguessable, and the API gives them no reason not to.

Both now say so and point at `crypto.random_hex`, which is `RAND_bytes`-backed.

> **Half of that was false until 2026-08-22, re-scored by reading.** The
> catalogue said it (`docs/guides/corelib.md:243`); the PACKAGE did not —
> `corelib/uuid/uuid.ty@v4`'s own comment read "A random version-4 UUID,
> threading the core:rand state via inout", with no warning and no pointer, and
> that is the line a caller lands on when they jump to the definition. Carrying
> the warning on one surface and not the other is the same failure this entry is
> about, one level up. Now on both.

`tools/tycho-rsa` was checked and is fine -- it seeds from a fixed constant on
purpose and says so in its header, being a teaching implementation.

**Not fixed, deliberately: there is still no CSPRNG-backed UUID.** Adding one is
an API decision (a `v4_secure` taking bytes, or a `uuid` that depends on
`core:crypto` and inherits its OpenSSL dependency), not a patch, and it would put
a hard OpenSSL requirement on a package that currently has none. Documenting the
limit is the honest interim; the decision is still open.

### 59. A strict check downstream of a lenient parse never runs — **FIXED 2026-08-15**

`server/main.ty@bad_len` already refused a Content-Length that is not a plain
decimal, and its comment already named request smuggling as the reason. It
answered 400 for `-1`. It did nothing at all for `4x`.

The refusal was correct and unreachable. `corelib/httpd/httpd.ty@to_uint` reads
leading digits and stops silently -- `parse_int`'s fail-open shape (FRICTION #34)
reimplemented locally -- and that lenient value is what the READ LOOP uses to
decide how many body bytes to wait for. `4x` framed a 4-byte body, the bytes
never came, and the strict check sat downstream of a read that could not finish.
The connection held a worker until the 15 s deadline instead of being refused in
microseconds.

**The general shape is worth more than the instance: a validator placed after a
blocking parse of the same field is not a validator.** The lenient parse decides
whether control ever reaches it.

Framing is now strict at the point that blocks, and three ambiguous shapes answer
400 promptly: a non-decimal length, two Content-Lengths that disagree
(`httpd.content_length_conflict`), and Content-Length alongside Transfer-Encoding
(RFC 7230 3.3.3). Two controls sit beside them so the fix cannot become "refuse
anything with two headers": a duplicate Content-Length that AGREES is not a
conflict, and a lone Transfer-Encoding is not ambiguous. Both still reach 405.

Impact was bounded before the fix and it is worth being accurate about: this
server answers no method that takes a body, so nothing was smuggled INTO an
application. What it cost was a worker per malformed request for the full idle
deadline -- the same resource-holding shape as the slowloris already recorded in
`corelib/httpd/httpd.ty`'s header.

**The class was then swept, and it is bounded.** Grepping `parse_int(` would never
have found this -- `to_uint` does not call it -- so the search has to be for the
SHAPE: a digit loop that returns its accumulator on the first non-digit. Exactly
one such function exists in `corelib/` and `server/` (`httpd.to_uint`), and its
one dangerous use site is the one fixed here. Every lenient `parse_int` call site
on untrusted input was checked by hand and each already guards: `tycho-ledger`
validates every byte before parsing and cites #34 while doing it, `tycho-stat`
refuses a non-numeric field by name, `tycho-tally` records the measured
`parse_int("35x") == 35` in a comment beside its own check. The remaining call
sites take CLI arguments, not untrusted input.

### 60. `markdown.render` escaped the text and not the SCHEME — **FIXED 2026-08-15**

`core:markdown`'s header said "all text is HTML-escaped; only the constructs above
emit tags", which reads as a promise that the output is safe to serve. It escapes
`&`, `<`, `>` and `"` -- so an attribute cannot be broken out of -- and then put
the link target straight into `href` with no check on what kind of URL it was.

Measured 2026-08-15, all three emitted verbatim and clickable:

```text
[x](javascript:alert`1`)          -> <a href="javascript:alert`1`">
[x](data:text/html;base64,...)    -> <a href="data:text/html;base64,...">
[x](vbscript:msgbox)              -> <a href="vbscript:msgbox">
```

Escaping and scheme-checking are different jobs and the header conflated them.
Anyone rendering untrusted markdown with this package had stored XSS.

Now allowlisted: no scheme at all (relative, absolute-path, protocol-relative),
or http / https / mailto. A colon counts as a scheme separator only before any
`/`, `?` or `#`, so `a/b:c` stays a relative path. The comparison is
case-folded -- `JaVaScRiPt:` was the obvious way past a naive check. Anything
else fails soft to the link text, matching what this module already does with an
unmatched delimiter.

Nothing in the tree rendered a rejected scheme: `make corelib`,
`make corelib-examples` and `make weblog webserver` were all green BEFORE the
golden was re-recorded, which is what says the change is behaviour-preserving for
real content rather than merely re-blessed. The new cases assert both directions
-- the three dangerous schemes unlinked AND seven safe forms unchanged -- because
"drop every link" would pass a one-sided test. Reverting `safe_url` to `return
true` reddens the golden.

### 61. `parse(stringify(rows)) == rows` was false for one shape — **FIXED 2026-08-15**

> Pinned-by: make format-diff

`corelib/csv/csv.ty@stringify` states that identity in its own doc comment. It
held for 413 of 414 differentialed row-sets and failed for exactly one: a row
that is a SINGLE EMPTY FIELD.

Unquoted, `[[""]]` writes a bare newline, and a bare line parses back as a row
with NO fields. One cell in, zero cells out. Python's `csv` module writes `""`
for the same input, and for the same reason -- it is the only way to tell "one
empty field" from "an empty row" in the format.

Found by differentialing `csv.stringify` against Python's `csv.reader` over 14
hostile edge cases and 400 generated row-sets drawn from an alphabet of comma,
quote, newline, tab, space, `=` and a non-ASCII byte. 14 mismatches, ONE distinct
input shape. After the fix: 0 of 414.

The fixture asserts the fix and both controls, because "quote every empty field"
would also make the mismatch go away and would be wrong: an EMPTY ROW must still
read back with no fields, and a two-field row of empties must still write a bare
`,`. Reverting the condition reddens the golden.

`tycho-q` and `tycho-agg` both write CSV through this function; q-check and
agg-check are green either way, which is what makes this a silent data loss
rather than a visible one.

### 62. `core:toml` guessed, in both directions — **FIXED 2026-08-15**

The package header says `parse` "returns Err(what-was-wrong) on any malformed
input (fail-closed; nothing is guessed)". Differentialled against Python's
`tomllib`, it guessed eight ways, and refused four documents that are valid.

**Accepted and invented a value** (now refused):

| input | was | why it matters |
|---|---|---|
| `a = [1, 2` | `[1]` | the closing `]` was never checked, so the last byte was stripped as if it were one and an element vanished |
| `a = "x" y` | `"x"` | everything after the closing quote was discarded -- the fail-open shape of #34 and #59 |
| `a = 1` then `a = 2` | `2` | a duplicate key overrode silently; in a config file that is an override nobody can see |

**Refused although valid** (now accepted). One cause: `split(body, ",")` knew
nothing about nesting or quoting.

    a = [1, [2, 3]]        -> "bad number: 3]"
    a = ["x,y"]            -> "unterminated basic string"
    a = ["a,b", [1, 2]]    -> the same
    a = [1, [2, [3, 4]]]   -> the same

`[[1],[2]]` worked, which is what kept this hidden: it is the nested case with no
comma inside an element.

**Still lenient, deliberately deferred:** `a = 01`, `a = 1__0`, `a = 1.`,
`a b = 1` and a repeated `[t]` header are all accepted where tomllib refuses.
They are number-grammar and header rules rather than invented values or lost
data, and each needs its own decision about how much of TOML v1.0 this subset
means to enforce. **The header's "nothing is guessed" is still too strong for
those five and should be narrowed or the cases fixed** -- naming them here so the
choice is made rather than defaulted into.

**The choice was made 2026-08-15, and measuring first moved one of the five out
of the list.** The sentence above calls a repeated `[t]` header a header rule
"rather than invented values or lost data". Measured, it is lost data, and worse
than the duplicate key this same entry fixed:

```
[t]        ->  ACCEPTED, and t.x is ABSENT
x = 1          the second [t] REPLACED the first table
[t]            rather than merging with it
y = 2
```

The duplicate key at least kept the last value; this deletes a key nobody
mentioned twice. It is refused now, by the same argument and with the same shape
of message. Two non-regressions are gated beside it, because a blanket ban on a
second header would pass the new case and break real documents: `[[arr]]` must
still repeat, since that is how an array of tables is spelled, and `[a]` followed
by `[a.b]` must not collide.

**The other four stay accepted and are now WRITTEN DOWN in the header** rather
than left to the reader's assumption -- `a = 01`, `a = 1__0`, `a = 1.` and
`a b = 1`, each with what it yields. The claim was narrowed to what the package
actually guarantees: no input is accepted that would LOSE data or INVENT a value
the text does not carry. "Nothing is guessed" was false for those four and is
gone.

That line is drawn where it is because a config parser's job is to not lie about
what the file said; refusing a document over a leading zero is a validator's job,
and the header now says which of the two this is.

Control: dropping the duplicate-header check reddens `toml` and only `toml`.

### `core:markdown` attribute break-out: probed, clean, and now pinned — 2026-08-15

**Not a defect entry.** A negative result, recorded because the surface is one
this file has already caught once (#60, `javascript:` and `data:` hrefs emitted
live) and because a clean security probe is worth nothing without the control
that proves it could have failed.

#60 fixed the SCHEME. It says nothing about a quote closing an attribute early,
which is the other half of the same surface: `![" onerror="alert(1)](a.png)` puts
attacker text where the `alt` value lives, and one unescaped `"` turns the rest
into markup. Ten payloads across link href, image src, image alt, link text,
code-fence language, `data:text/html` and `vbscript:` — **every one already
neutralised.**

The control is what makes that a result. Defeating `esc()` (identity) and
`safe_url()` (always true) in a corelib copy emits, from the same ten inputs:

```
<img src="a.png" alt="" onerror="alert(1)">      the attribute genuinely broken out of
<a href="javascript:alert(1">x</a>
<img src="javascript:alert(1" alt="x">
```

So the probe sees holes when they exist. Four of the ten are now in
`corelib/test/markdown/main.ty`, and each of the two breaks reddens that golden
**independently**.

**Two of the ten are honestly vacuous and are labelled so in the fixture rather
than counted.** A quote in link TEXT lands in element content, where it is
harmless whether escaped or not. And a code-fence language never reaches the
output at all — there is no `class` attribute to inject into — so that leg
confirms the language is discarded, not that anything is escaped. Counting either
as an XSS test passing would inflate the result.

### 71. `core:zip` told the caller there was no traversal hazard — **FIXED 2026-08-15**

> Pinned-by: make ar-check

Probed because `docs/internals/audit-brief.md` names archive parsing as the first
place an external reviewer should look, and reading the code is what missed
`gcd` and `sign` for months.

**The offset arithmetic is sound**, measured rather than read: sixteen archives
built by Python's `zipfile` and then mutated — central-directory offset at
`0xFFFFFFFF`, past the end, and into the middle of the file; entry count at
`0xFFFF`; name length at `0xFFFF`; local-header offset at `0xFFFFFFFF` and near
the end; compressed and uncompressed sizes at `0xFFFFFF00`; a wrong CRC; an
unknown method; an EOCD with nothing behind it; a two-byte runt. **Every one
fails closed** — zero entries or empty bytes — and the unmutated archive still
round-trips. No crash, no short read, no wrong data.

**The defect is one sentence of prose.** The header ended:

> Entry names are treated as NAMES, not paths -- extracting never touches the
> filesystem, so **there is no traversal hazard here**.

The first clause is true. The conclusion is what a reader carries away, and it is
wrong the moment they use the name — which is precisely what a caller of an
archive reader does. `list` returns `../../etc/passwd` and `/abs/path` verbatim
(measured), which is CORRECT for a reader and is the entire hazard. The package
that says "no traversal hazard" is one call away from the traversal.

Same shape as #60, where `markdown` escaped the text and not the scheme: the
sentence is true about the thing it names and false about the thing that bites.

`tools/tycho-ar` was already right — it runs every name through `path.safe_join`
before writing any file, so a refusal in entry 9 cannot leave entries 1-8 on
disk. That is now the worked example the header points at, instead of a
reassurance that no check is needed.

**Pinned rather than fixed in code, deliberately.** The names must keep coming
back verbatim: silently rewriting one would hide the archive's real contents from
a caller that *is* checking, which is worse than passing it through. The fixture
asserts both hostile names survive the round trip exactly, and a plausible wrong
fix — stripping a leading `/` in the writer — reddens it.

Controls: restoring the naive split reddens the golden, and so does dropping the
array terminator check, independently.

### 63. A test lane that leaks a server degrades every other lane — **FIXED 2026-08-15**

`make ci` reddened at `hash-check` while `hash-check` passed on its own. The
cause was not in either lane's subject.

`tools/tycho-kvsrv/run.sh` sent SIGTERM and then `wait`ed, with no KILL
escalation. TERM is a REQUEST, and that server installs a handler for graceful
shutdown, so a shutdown that blocks leaves the process alive and `wait` blocks
with it. Found on this box: **ten orphaned kvsrv processes, all ~935,000 seconds
old — 10.8 days — at ~15% CPU each**, roughly 1.4 cores permanently. Nine still
named `/tmp/kvsrv`, a binary already deleted from disk; the tenth used the path
the current script uses, which is what proved the leak was live rather than
historical.

Same file, second defect: the trap set beside `mktemp -d` was silently REPLACED
by `trap cleanup EXIT INT TERM` a few lines later, so `$T` was never removed
either. `/tmp` held 624 `tmp.*` directories going back to July, 1.4 GB.

**The consequence is the general lesson: a leaked server is not confined to its
own lane.** It puts a permanent load on the machine, and every lane that asserts
a scheduling outcome then fails intermittently somewhere else entirely. That is
how a kvsrv bug presented as a tycho-hash failure.

**The class was swept.** Six scripts background a process. Two lacked escalation:
kvsrv, and `scripts/tls_verify.sh` — written earlier the same day, with the same
bare `kill`. Both now escalate TERM → KILL after 2s and are verified to leave no
process behind. The other four already had it, and `scripts/ci.sh` backgrounds
only its own lane groups, which it waits on.

The first sweep produced two false positives by matching the words `kill -TERM`
inside COMMENTS describing `core:signal`'s test. A grep for a pattern is not a
test for the behaviour — the same "a label is not a payload" mistake as the
encoded-traversal legs in #57's neighbourhood.

### 64. `gcd` returned a NEGATIVE gcd where the answer fits — **FIXED 2026-08-15**

> Pinned-by: make math-diff

`corelib/math/math.ty@gcd` documented itself "non-negative". It was not, for four
input pairs, and only three of those are excusable.

`abs(min int64)` returns min itself -- negative -- and abs says so in its own
comment, deliberately, because negating it would overflow. gcd called abs on both
arguments and returned the loop's accumulator without re-normalising, so that
negative could ride straight through:

```text
gcd(min, 0)      = min    true answer 2^63, NOT REPRESENTABLE  -- a width limit
gcd(0, min)      = min    same
gcd(min, min)    = min    same
gcd(min, min+1)  = -1     true answer 1, REPRESENTABLE         -- a WRONG SIGN
```

The last one is the defect. 1 fits in an int64 with room to spare; there was no
arithmetic reason to return -1. The fix is `return abs(x)`, which leaves the three
unrepresentable cases following abs (documented) and corrects the fourth.

Found by differentialling core:math against Python, where the interesting part
was separating the four. Python has bignums, so it disagrees on every int64 edge
and reports six differences for `abs`, `ipow` and `gcd` alike -- and `abs`'s and
`ipow`'s are both DOCUMENTED behaviour that the oracle simply cannot express.
Reading each function's stated contract is what left one real finding out of six
apparent ones.

The fixture asserts the fixed case and the three width limits separately, so a
future change that "fixes" the limits by wrapping cannot pass by moving the wrong
line. Reverting `abs(x)` to `x` reddens the golden.

### 65. `sign()` returned 0 for both infinities — **FIXED 2026-08-15**

> Pinned-by: make math-diff

Same package as #64 and the same shape as the nine before it: *a comment claims a
property, the code delivers it in one respect and not the one that matters.*

`math.sign` documents `-1 / 0 / 1`, and its comment explains the trick that makes
one body serve every numeric type — "the zero is derived as `x-x`, so this works
for int and float without a typed literal". It does work for int, and for every
finite float. For an **infinity** `x - x` is NaN, every comparison against a NaN
is false, so both tests fall through to the final `return 0`:

```
sign(inf)  = 0        sign(-inf) = 0        (measured 2026-08-15, before the fix)
```

The two float values whose sign is *least* ambiguous were the two it got wrong.
Nothing in the tree could have noticed: `sign` has no differential lane, and the
golden recorded what the code did.

**The obvious fix is a breaking change, which is why it is not the fix.**
`z := zero$(T)` reads better and gives a real typed zero — and it needs
`defaultable(T)`, which does **not** see through a newtype, while the `numeric(T)`
in the signature does. Measured: `math.sign(Cents(-7))` compiles today and stops
compiling under `zero$(T)`, with *"only int, float, bool, and string are
defaultable"* pointing at a line inside corelib. A papercut is not worth a
silent break in every newtype caller.

So the derived zero stays and the NaN case is answered where it lands. `z != z`
identifies "x is an infinity or a NaN" — a state **int arithmetic can never
reach**, so the int path is untouched by construction — and `x == x * x` then
separates them, since `+inf` is its own square while `-inf` squares to `+inf` and
NaN compares equal to nothing.

`NaN` still returns 0, now deliberately rather than by fall-through: it has no
sign to report. That is the one behaviour worth arguing about, and it is written
down instead of emergent.

Gated in `corelib/test/math/main.ty` with the finite int and float cases asserted
on their own lines, so an edge-case fix cannot pass by moving an ordinary answer.
Reverting the fix reddens `math` and only `math` — `ok fmath` printed beside
`FAIL math (output != golden)` in the same run.

**Then the package got the oracle it should have had.** Two defects found by
careful reading is not a repeatable process, so `scripts/math_diff.sh`
(`make math-diff`, ~2.2s) scores `min`/`max`/`clamp`/`sign`/`abs`/`gcd`/`ipow`
against Python. It is validated the only way a new lane can be — against the
defects that already happened: reverting `gcd`'s final `abs()` reddens it with 38
mismatches, reverting this entry's fix reddens it with 2.

**And the first version of that lane would have caught only one of the two.** It
was integers throughout, and it reported:

```
math-diff: green (1197 scored answers match Python ...)
```

with the sign-of-infinity defect sitting untouched in front of it. `min`, `max`,
`clamp` and `sign` are generic; instantiating them at `int` says nothing about
their `float` instantiation, and *no float ever entered the corpus*. The lane
was thorough, self-consistent, and blind to the entire half of the surface where
the bug lived — which is the same failure as the zip fuzzing seeds that produced
1950 clean mutants against a parser returning 0 entries.

That makes it the tenth instance recorded here of the INSTRUMENT being the
defect rather than the code, and the first one caught by asking a new lane the
right question: not "is it green" but **"would it have caught the bug I already
know about?"**

### 66. `fmath.round` was `floor(x + 0.5)`, which is not rounding — **FIXED 2026-08-15**

> Pinned-by: make math-diff

Found by pointing #65's new lane at the next package. `round` documents itself
*"round half away from zero"* and implemented it as `floor(x + 0.5)`. That
addition **rounds before `floor` ever runs**, and it was wrong two different ways:

| input | returned | half away from zero |
|---|---|---|
| `0.49999999999999994` — the largest double **below** a half | `1.0` | `0.0` |
| `4503599627370497.0` — 2^52+1, **already an exact integer** | `4503599627370498.0` | itself |

The first is not a tie being broken the wrong way; the value is strictly under a
half and it came back a whole unit out. `0.49999999999999994 + 0.5` is exactly
halfway between the two doubles below and at 1.0, and IEEE ties-to-even picks
1.0. The second is worse in kind: `round` of an integer must be the identity, and
at or above 2^52 every double *is* an integer, so the whole range was at risk.

`0.5`, `1.5`, `2.5`, `-2.5` were all correct, which is exactly why the golden was
happy — the package's own fixture asserts the cases anyone would think to write.

Decided by the FRACTION now: `t := trunc(x)`, `d := x - t`, and the answer moves
one unit when `d` reaches a half in either direction. `x - trunc(x)` is exact for
every finite x, and `t + 1.0` is only reached when x is *not* an integer, so
`|x| < 2^52` there and it cannot overflow. An infinity and a NaN both make `d`
NaN, fail both tests, and return trunc's answer, which is themselves.

### 67. `fmath.lerp` did not return its endpoints — **FIXED 2026-08-15**

> Pinned-by: make math-diff

Found in the same run, and it is the one defect in this batch that **no comment
claimed**. `lerp`'s only documentation is "linear interpolation, t in [0,1]" —
nothing about `lerp(a, b, 1)` being `b`. The oracle asserted it anyway, because
that is what interpolation *means*, and:

```
lerp(1e308, 1.0, 1.0)  ->  0.0        (expected 1.0)
```

Not a rounding wobble. `1.0 - 1e308` **is** `-1e308` in double precision — the
`b` is gone before `t` is ever applied — so `a + (b - a) * t` returns
`1e308 + -1e308`, which is zero. The same subtraction overflows to an infinity
for a far-apart pair of opposite sign, and then `t = 0` returns NaN instead of
`a`.

Both endpoints are special-cased now, which is what makes them exact. The middle
of the range is unchanged: no attempt is made at the full monotonicity guarantee
C++20's `std::lerp` carries, and that limit is stated rather than implied.

**Worth separating from the rest of this file:** #57 through #66 were all found
by taking a written claim literally. This one was found by an oracle asserting a
property the code never claimed — which is the argument for a differential over
a re-reading, since a re-reading can only ever check what somebody wrote down.

### 68. `pad_left`/`pad_right` overshot on any pad wider than a byte — **FIXED 2026-08-15**

`pad_left`'s comment reads *"left-pad to width with `pad` (one byte)"*. The
parenthesis is the whole specification of the precondition, and nothing enforced
it: the loop counted the deficit in **bytes** (`n := width - len(s)`) and
decremented by **one** per iteration while appending `len(pad)` of them.

```
pad_left("x", 5, ".")   ->  "....x"      len 5     the documented case, correct
pad_left("x", 5, "ab")  ->  "ababababx"  len 9     asked for 5
pad_left("x", 5, "é")   ->  "ééééx"      len 9     a UTF-8 pad, same overshoot
```

The UTF-8 row is the one a formatter reaches for in real code — a box-drawing
character, a middle dot, a non-breaking space — and it is the row where the
result *looks* right in a terminal while being 80% too wide. Python's `rjust`
refuses a multi-character fill outright with a `ValueError`, which is the same
judgement reached differently: do not guess what the caller meant.

The deficit is counted in bytes on both sides now. A pad that does not divide it
leaves the result **short** of width rather than over — a formatter survives a
narrow column, and stopping early is what keeps a multi-byte pad from being split
mid-codepoint:

```
pad_left("x", 5, "abc") ->  "abcx"       len 4     one pad fits, 1 byte short
```

**Every caller in the tree passes a one-byte pad** (`" "` or `"0"` — `weblog`,
`tycho-sheet`, `tycho-ed`, `examples/corelib/strings`), so all of them are
bit-identical and their lanes confirmed it. This is a fix for the case nobody had
reached yet rather than a behaviour change under anyone.

The fixture prints the **length beside every result**, and that is deliberate:
the padding characters look perfectly reasonable in a transcript, the *width* was
the thing that was wrong, and a golden carrying only the text could not have
seen it — the same reason `ed-check` asserts byte counts and `sheet-check`
asserts float text. Reverting the two loops reddens `strings` and only `strings`.

### 69. `core:sqlite`'s two halves disagree about a multi-statement string — **exec FIXED, `_params` PINNED, 2026-08-15**

> Pinned-by: make corelib

The package is **sound where it matters**, and that is worth stating first because
it is the thing a reviewer should check: `run_stmt` uses real
`sqlite3_prepare_v2` + `sqlite3_bind_text`, so `exec_params`/`query_params` are
genuine prepared statements and there is no string interpolation anywhere in the
file. The injection surface is closed by construction.

What is wrong is quieter. `exec` goes through `sqlite3_exec`, which runs **every**
statement in the string. `run_stmt` goes through `sqlite3_prepare_v2` with
`pzTail` passed **NULL**, which compiles the **first** statement and discards the
rest. Neither behaviour was documented, and they are opposites:

| call | rows that landed | what it returned |
|---|---|---|
| `exec("INSERT 'a'; INSERT 'b'")` | **both** | `Ok(1)` — under-reported |
| `exec_params("INSERT 'c'; INSERT 'd'")` | **only 'c'** | `Ok(1)` — no error at all |

**`exec`'s count is fixed.** `sqlite3_changes` describes the most recent statement
alone, so two inserted rows reported 1 against a function documenting
*"Ok(the change count)"*. It reports a delta of `sqlite3_total_changes` now, which
is exactly the rows this call changed. Single-statement callers — every caller in
the tree — see the identical number.

**`exec_params`'s discard is PINNED, not fixed**, and the distinction is the
honest part. Making it fail closed means reading `pzTail` back as a string to
test whether anything follows, and the alternative — scanning the SQL for a `;`
in Tycho — has to know about quoted literals *and* `--` and `/* */` comments, or
it refuses valid SQL. That is a parser, on the security-adjacent path, and
guessing at it is worse than naming it. The fixture asserts the current answer
(3 rows, not 4) so a future fix changes that line **on purpose** rather than
drifting, and `run_stmt` carries a `gap:` comment at the site.

**A third, separate finding in the same header.** The package's usage example
opened with:

```
db := sqlite.open("app.db") or_return
defer sqlite.close(&db)
```

`defer` does not exist — it was refused on 2026-08-10 and the refusal is recorded
in `ROADMAP.md` and twice in this file. A reader copying the package's own example
gets *"a statement must be a declaration, assignment, or call — a bare expression
has no effect"*, which does not contain the word `defer` and so does not tell them
what is wrong. Swept the tree with a grep proved able to match on a synthetic
first: **exactly one instance**, this one, now corrected in place.

That last one is the cheapest class of defect in this file and the one most likely
to greet a newcomer, since a package header is what someone reads before they
write anything. It is also invisible to every gate here — no lane compiles a doc
comment.

### 70. A bound parameter was truncated at its first NUL — **FIXED 2026-08-15**

Chased because #69 left it as an unprobed note, and it is the sharpest defect in
this batch. `sqlite3_bind_text` was given `-1` for the length, which tells SQLite
to read to the first NUL. **A Tycho string is length-carrying and may contain
one.** Measured:

```
tycho len(s) = 5          "hi\0zz"
stored       = 2 bytes    hex 6869          -- and the call returned Ok
```

Silent truncation at a trust boundary, with a success return. The failure mode is
not lost characters, it is **collision**: two values the program treats as
different become the same row. Anything storing a token, a key, a filename or a
credential inherits that, and nothing anywhere reports it.

The fix is the argument that was already in the signature — `len(params[i])`. The
value round-trips whole afterwards (`6869007A7A`), which also establishes that
Tycho's FFI was passing the entire buffer all along; only the `-1` discarded it.

The sibling `sqlite3_prepare_v2(d.h, sql, -1, ...)` has the same shape and is now
`len(sql)`. It is the cheaper case — SQL text is program-authored rather than
attacker-supplied — and **it is not covered by a test**, because no fixture
carries a NUL inside its SQL. Recorded as a limitation rather than claimed.

**Two instrument errors on the way, both of the kind this file keeps recording.**
The first control run reported `FAIL sqlite (tychoc compile)` and that read like
the control working; it was the fixture failing to build, because `_` in this
language is an ordinary variable rather than a discard and I declared it twice.
A control that cannot compile accuses working code. The second: the assertion
first asked SQLite for `length(x)`, which for TEXT is *characters before the first
NUL* by SQLite's own definition and so returns 2 on perfectly intact data — the
oracle was wrong and the code was right. `length(cast(x as blob))` asks the
question actually meant. The golden written from the intended answer was correct
both times; only the query was not.

### 72. The server's traversal defence is two layers and nothing could tell — **GATED 2026-08-15**

> Pinned-by: make traversal-check

**No defect.** A structural gap, found while probing the server for
`docs/internals/audit-brief.md`, and the more interesting kind: everything is
correct and nothing could notice if it stopped being.

Sixteen hostile requests against a real `tycho-httpd` — raw `../`, `%2e%2e%2f`,
double-encoded `%252e`, `....//`, a leading `//`, overlong UTF-8 `%c0%af`,
absolute-form, a NUL in the target, `.git/config`, duplicate `Content-Length`,
`Content-Length` beside `Transfer-Encoding`, an 8000-byte target — **all refused,
nothing leaked, and the server still served afterwards.** `resolve` decodes
percent-escapes BEFORE the traversal test, which is the right order and the one
usually got wrong.

Then the control refused to work, which is where the finding is. Replacing
`path.safe_join(root, rel)` with naive concatenation **still gave 403** — so the
probe would not have caught a `safe_join` regression, and for a while it looked
as though `safe_join` were decoration. Defeating the other guard as well produced
the leak and settled it:

| `safe_join` | `hidden_segment` | `GET /../secret` |
|---|---|---|
| intact | intact | 403 |
| **defeated** | intact | 403 |
| intact | **defeated** | 403 |
| **defeated** | **defeated** | **200, canary leaked** |

They are genuinely redundant, which is good design and a gating problem: **either
guard can be deleted with no observable change.** `server/run.sh`'s traversal
legs stay green, the whole sweep stays green, and the defence is now one layer.
The next change removes the other, and neither commit looked wrong.

`make traversal-check` (~16.4s, 16.36 / 16.35 / 16.81 s) closes it by defeating
them one at a time in a COPY of the server and requiring the refusal to hold,
with both-defeated as the control that must leak. Every `sed` asserts it changed
the file, because a patch that silently does not apply reports the unmodified
server as the broken one — a mistake already made twice in this session's own
controls.

### 73. `core:io` holds its contract, including the one nobody wrote down — 2026-08-15

**No defect.** Recorded because the header states an unusually precise contract
and because one property it does NOT state is the interesting one.

Every claim probed and every one holds. A missing path is `Err(NotFound)`, a
directory is `Err(IsDir)`, an empty file is `Ok(len 0)` — the three-way split the
header says was one empty `bytes` until 2026-07-26. A symlink loop, a
permission-denied file, a 300-character name and an empty path all return `Err`
rather than aborting, which is what *"Nothing here aborts"* claims. `read(dir)`
is `""`, `list(file)` is `[]`, `write(dir)` is `false` — the documented sentinels,
each with one meaning.

**The undocumented property is the valuable one.** `read_bytes("/proc/self/stat")`
returns **Ok with 315 bytes**, while `stat(2)` reports that file as **0 bytes**.
So the read does not trust `st_size`; it reads to EOF. That is the correct
implementation and it defeats the classic procfs trap, where a size-based reader
silently returns empty for every file under `/proc` and `/sys` — a wrong answer
that looks exactly like an empty file, on precisely the paths a monitoring or
diagnostic program reads.

**It is not gated, and deliberately not gated here.** A fixture asserting it would
need `/proc`, which does not exist on macOS or under the Windows lanes, so it
would either break those or carry a skip that makes it vacuous where it matters
least. Naming it instead: **if `read_bytes` is ever "optimised" into a single
`st_size`-sized read, every `/proc` and `/sys` read in every Tycho program starts
returning empty and no gate in this tree will notice.** That is the note a future
optimiser needs, and it is cheaper than a non-portable fixture.

### 74. `core:net` really does distinguish its four cases — 2026-08-15

**No defect.** Probed because the header rests its whole error model on one
claim — *"recv(2) distinguishes those cases; only the return type was throwing
the distinction away"* — and a claim that load-bearing is exactly where this file
keeps finding code that delivers it in one respect and not the one that matters.

It delivers. Four genuinely different real conditions, four different answers:

| condition | result |
|---|---|
| the peer closes cleanly | `Err(Eof)` |
| the peer stays silent past `SO_RCVTIMEO` | `Err(Timeout)` |
| a bad fd, and a refused connection | `Err(Failed)` |
| a host string with an interior NUL | `Err(BadAddr)` |

The probe needs no external control: **four distinct answers from four distinct
causes IS the control.** If the type were collapsing cases — the pre-2026-07-26
behaviour the header describes, where empty `bytes` meant EOF, timeout and hard
error at once — two of those rows would be identical. None are.

**`BadAddr` deserves its own note, because it is the same defect this session
found in `core:sqlite`.** A host crosses to `getaddrinfo` as a `char*`, which
ends at the first NUL, so an unguarded call resolves the PREFIX — `"evil.com\0
.trusted.com"` becomes `evil.com`. `has_nul` refuses it before any syscall, and
the variant is deliberately not `Failed` because no syscall was made and retrying
cannot help. That is #70's bug — a bound parameter truncated at its first NUL —
anticipated at the design stage in one package and shipped in another. **The
lesson is the sweep, not the fix**: any Tycho `string` reaching C as a `char*`
without an explicit length has this shape, and `core:net` proves somebody already
knew.

### 75. A password was truncated at its first NUL, so two credentials derived one key — **FIXED 2026-08-15**

> Pinned-by: sh scripts/crypto_hygiene.sh

Found by the sweep #74 argued for: *any Tycho `string` reaching C as a `char*`
without an explicit length has this shape.* 46 corelib externs take a string.
Most are safe by construction — all six `core:regex` entries pass an explicit
`n`, `core:net` refuses an interior NUL up front with `has_nul`, and
`core:sqlite` was fixed earlier the same day (#70). One was not:

```c
PKCS5_PBKDF2_HMAC(password, (int)strlen(password), ...)
```

`strlen` stops at the NUL. Measured, with a live control:

```
control: derive("alpha") != derive("bravo")   true    the probe CAN see a difference
derive("secret\0A") == derive("secret\0B")    true    two credentials, one key
derive("secret\0A") == derive("secret")       true    truncated to the prefix
```

**The failure mode is collision, not weakness.** The derived key is a perfectly
good PBKDF2 output — of the wrong input. Two distinct passwords authenticate
against each other, and any secret carrying a NUL is silently reduced to whatever
precedes it. In a key derivation that is the most expensive place in the package
for this bug to live.

`cx_pbkdf2_sha256` takes the length now and never calls `strlen`, with the length
fail-closed (`< 0` or absurdly large refuses rather than guessing). Gated in
`corelib/test/crypto/main.ty` with the control asserted **beside** it — two
plainly different passwords must differ, or an all-equal run would report the bug
as fixed. Reverting to `strlen` reddens `crypto` and only `crypto`.

**Three packages, one defect class, three different outcomes.** `core:net`
anticipated it and refuses. `core:sqlite` shipped it and truncated data.
`core:crypto` shipped it and collapsed credentials. The sweep is what made the
third one findable, and it is the sort of pass that only happens when somebody
goes looking across packages rather than down one — which is, again, the argument
in `docs/internals/audit-brief.md` §4.

**Still unswept in that class**, named rather than assumed clean: `core:http`'s
URL, `core:os`'s command string, `core:datetime`'s TZ string, and
`crypto.ct_equal`'s two hex arguments. Each takes a `char*` with no length. Their
inputs are program-authored rather than attacker-supplied in every in-tree
caller, which is why they are lower priority and not why they are safe.

### 76. `ct_equal` compared two hex strings by their prefixes — **FIXED 2026-08-15**

> Pinned-by: sh scripts/crypto_hygiene.sh

The second of the four #75 left named as unswept, and the answer is the one worth
writing down carefully: **a real collision, and not the vulnerability it looks
like.**

`hexdec` finds its own end with `strlen`, so an interior NUL shortened both
inputs. Measured, controls first:

| case | before |
|---|---|
| control `"aabb"` vs `"aabb"` / `"ccdd"` | true / false — the comparison works |
| both sides NUL-truncating to the same prefix | **true — a collision** |
| attacker supplies the NUL, the trusted MAC has none | false — fails closed |
| the same, reversed | false |

**The MAC shape was never reachable.** A computed MAC is library-generated and
carries no NUL, so its length disagrees with the truncated attacker value and the
answer was already false. Reporting this as an authentication bypass would have
been wrong, and the third and fourth rows are what establish that — without them
the first two rows read far worse than the truth.

It is still a collision for a caller comparing two **supplied** values, and this
is the one comparison in the package whose entire job is not to surprise anyone.
Lengths are passed now and a mismatch against `strlen` refuses outright, since
hex never legitimately contains a NUL. Gated with both controls beside it;
removing the length check reddens `crypto` and only `crypto`.

**Two of the four remain unswept**, unchanged: `core:http`'s URL and `core:os`'s
command string. Both are program-authored in every in-tree caller.
`core:datetime`'s TZ string is the third and is the same shape. Naming them again
rather than quietly dropping them, because a sweep that stops early and does not
say so is how #62's "deliberately deferred" list ended up containing an entry
that was really data loss (#69).

### 77. The interior-NUL rule was normative, swept once, and still missed the two worst sites — 2026-08-15

> Pinned-by: make corelib

**Not a defect. The most useful thing this session found**, and it is about
process rather than code.

Finishing #76's list turned up the history. `core:os`'s header records that all
four of its calls were probed for interior NULs on **2026-08-13**, found to run
"a DIFFERENT, SHORTER command than the one they were handed", and fixed —
`os.system("printf ok\0; printf BAD")` ran `printf ok` and **reported exit 0**, so
the caller was told its whole command succeeded. Re-probed today: `-1` from both
entry points, with a clean call returning `0`/`ok` as the live control. It holds.

That note ends: *"The rule is docs/spec/14-ffi.md's, and the sibling guards are
core:io's and core:net's."*

So the position on 2026-08-14 was:

- the rule was **normative in the spec** — a `string` holding an interior NUL
  does not survive the FFI round trip in either direction;
- a **deliberate sweep** had been run for it;
- **three packages** carried explicit guards, each naming the others.

And the sweep stopped there. `core:sqlite` truncated a bound parameter (#70),
`crypto.pbkdf2_sha256` collapsed two credentials into one derived key (#75), and
`crypto.ct_equal` compared two hex strings by their prefixes (#76). All three
were found today, in one pass, by mechanically listing **every** extern that takes
a `string` — 46 of them — rather than by thinking about which ones might be
affected.

**The lesson is not that anyone was careless.** It is that a sweep run by the
person who wrote the rule covers the sites they had in mind, and the sites they
had in mind are the ones they had already fixed. The guards in `os`, `io` and
`net` are *evidence the author understood the class completely* — and the two
packages that got it wrong are the two that were not on the list. Understanding
the bug is not the same as having enumerated its instances.

For anyone reviewing this tree: **`grep` the declarations, do not reason about
them.** The command is in `docs/internals/audit-brief.md` §6 now, and this entry
is why it is there.

### 78. The last two of the NUL sweep: a URL and a TZ string — **FIXED 2026-08-15**

> Pinned-by: make corelib

#75 and #76 named four externs as unswept and #77 explained why leaving them
named mattered. These are the last two, and both truncated.

**`core:http`'s URL.** A URL crosses to curl as a `char*`. Measured: fetching
`file://…/real.txt\0/ignored` returned **real.txt's 18 bytes** — the caller asked
for one resource and got another, with no error. A program building a URL from
user input inherits that. Guarded now, returning the null handle that already
means "this did not run".

**`core:datetime`'s TZ.** `offset_at("EST5\0UTC0", 0)` answered **-18000** — the
truncated prefix applied, not the string given. Refused as `0`, which is already
what an unparseable or empty TZ answers; a truncated zone is not a valid one.

**The http fix was wrong the first time, and the probe is what said so.**
Guarding `get`/`post` left the body at 18 bytes, because `get_body` and
`get_status` call `http_get` **directly** rather than through `get`. Three entry
points, one guard, two still open — the caller-graph rule this repo's own CLAUDE.md
states, missed by the person applying it. Re-probing after the fix is the only
reason it did not ship half-done.

That closes the sweep: **all 46 corelib externs taking a `string` are now either
length-carrying or NUL-guarded.** Written as a fact that will decay — a new
extern is one commit away from making it false, and nothing mechanical enforces
it. The check is `grep -rE '^extern (\"[a-z0-9]+\" )?fn .*: string' corelib/`,
and it belongs in a reviewer's hands rather than a lane's, because the answer for
each hit is a judgement about whether the input can carry a NUL.

### 79. `json.parse_checked`'s error channel had a hole where the attacker is — **FIXED 2026-08-15**

> Pinned-by: make format-diff

`core:json`'s header is unusually clear about which entry point to reach for:
`parse_checked` is *"the only one that can tell you the document you got back is
the document you handed in."* It cannot, for one input class, and it is the class
an attacker controls.

The parser is recursive descent, so nesting depth is C stack. Measured:

| nesting | before |
|---|---|
| 10000 | parses fine |
| 50000 | `tycho: stack overflow -- recursion too deep`, **exit 1** |

The abort is *safe* — a runtime guard, not corruption, and `tests/recursion/`
exists to keep it that way. But it is an **abort**, and a process that dies
cannot return `Err`. A server calling `parse_checked` precisely because it wants
the failure to be recoverable dies anyway, on 100 KB of `[`.

Capped at 2000 with a new `TooDeep` variant. The check is a **pre-scan, not a
counter threaded through `parse_value`**: one linear pass with no recursion of
its own, so it cannot inherit the problem it exists to prevent. It tracks string
state, because a `[` inside a string is a character rather than a level — 5000
brackets inside a string still parse, and that non-regression is gated beside the
boundary, which is asserted on **both** sides (2000 Ok, 2001 refused).

**Adding the variant broke two exhaustive matches inside `json.ty` itself**
(`err_offset`, then `err_reason`) — the consequence `tycho-verify` names as the
textbook consumer-breaking change, hit by the person who had just read that
warning. Zero consumers outside the package match on `JsonErr`, so nothing else
moved, and the compile-until-clean loop is what found both.

**The measurement was nearly wrong twice.** The first probe run reported
`2001 -> Ok` and a stack overflow at 50000 — because the build had failed on the
first broken match and the probe was the STALE binary. Reading the exit code
rather than the printed line is what caught it.

### 80. `core:decimal` holds every claim in its header — 2026-08-15

**No defect**, recorded because this is the money type and because the two claims
most likely to be wrong are wrong in most implementations.

Ten rows, every one matching Python's `Decimal` exactly:

| claim | probed |
|---|---|
| the stored scale is preserved | `1.50` stays `1.50` |
| `cmp` is exact across DIFFERENT scales | `cmp(1.50, 1.5)` is `0`, both orders |
| `0.1 + 0.2` is `0.3` | it is |
| add across scales | `1.50 + 1.5` is `3.00`, as Python gives |
| `rescale` truncates TOWARD ZERO | `-1.55 -> -1.5`, `-1.99 -> -1` |

**The two that matter are the middle and the last.** A `cmp` that compares
coefficients without aligning scales makes `1.50` greater than `1.5` — the same
number ordered wrongly, which in a ledger sorts and compares money incorrectly
while every individual value prints right. And "truncates toward zero" is a claim
about NEGATIVES: an implementation that floors instead would give `-1.6` and
`-2`, off by a cent in the direction that accumulates.

Not vacuous: `cmp(1.5, 1.6)` returns `-1`, so the comparison discriminates, and
the rescale rows disagree with each other.

`div` already has a differential (`40bac3d6`, with a mode-swap control). Between
that and this, the package's stated surface is measured rather than assumed.

### 81. `csv.stringify` writes a spreadsheet formula verbatim — **DOCUMENTED, not "fixed", 2026-08-15**

> Pinned-by: make format-diff

A field beginning `=`, `+`, `-` or `@` is EXECUTED as a formula when Excel or
Google Sheets opens the file. `=cmd|' /c calc'!A1` is the classic. Measured:
`stringify` writes all four verbatim, and the quoting that does happen is
incidental — `"@SUM(1,2)"` was quoted for its COMMA, and a quoted formula cell is
evaluated anyway.

**A differential against Python could not have found this**, which is why it is
worth recording next to `format-diff`: Python's `csv` writes it verbatim too, as
do Go's and Rust's. Agreement with the oracle is the correct result here and the
bug class survives it untouched.

**It is not fixed, and "fixed" would be the wrong word for what a fix would do.**
The only defence is to MANGLE the value — prefix an apostrophe or a space — and
that breaks `parse(stringify(rows)) == rows`, which the package states in its own
header and which FRICTION #61 exists to protect. A serializer that quietly alters
values is a worse defect than the one it prevents, and it prevents it only for the
readers that happen to be spreadsheets: for a CSV another program parses, that
apostrophe is data.

So it moves to the caller, at the point where the audience is known, and the
header now says so with the four bytes and the guard spelled out. The passthrough
is **pinned** in `corelib/test/csv/main.ty` so a future drive-by "hardening" has
to be a deliberate change: wiring an auto-escape into `stringify_delim` reddens
`csv` and only `csv` (verified — the first attempt at that control only DEFINED
the helper without calling it, and reported green while changing nothing).

The general shape, for whoever reviews next: **a differential is blind to any
defect the oracle shares.** Round-tripping, encoding and arithmetic are what it
covers. What a downstream consumer DOES with a well-formed value is not, and that
is where the injection classes live — this, and #60's `javascript:` href, and
`core:zip`'s traversal names (#71).

### 82. `decimal`'s scale is a size SQUARED, and nothing said so — **DOCUMENTED 2026-08-15**

> Pinned-by: sh scripts/bignum_diff.sh

No wrong answer. A cost, measured because `core:decimal` is the money type and
`rescale(d, scale)` takes its scale from the caller.

```
k=200000   0.86 s        each doubling of k roughly QUADRUPLES the time:
k=400000   2.47 s        this is O(k^2), not O(k)
k=800000   9.81 s
k=1600000  38.94 s
```

The first probe stopped at "k=10000000 hit the 15 s timeout" and nearly went in
as *"linear in the output size — you asked for ten million digits and got them"*,
which would have been a comfortable and wrong conclusion. Four points instead of
one is what showed the exponent. **A single timing is not a complexity claim.**

The consequence is what matters: **a scale is not a size, it is a size squared.**
A `scale` field taken from a request at 2000000 is about a minute of CPU from one
call — an amplifier, not a slow path. Every caller in this tree passes a literal
or a column width, so nothing is exposed today.

**No cap imposed.** Any number would be arbitrary, and a cap that silently
returns a differently-scaled answer is a wrong result where there was only a slow
one. The cost is written at `scale_up` instead, with the rule that the ceiling
belongs to whoever knows where the scale came from — the same division of labour
as `csv`'s formula escaping (#81) and `zip`'s traversal names (#71).

### 83. `bignum`'s division identity holds on every sign combination — 2026-08-15

> Pinned-by: sh scripts/bignum_diff.sh

**No defect**, and the last package on the sweep. Recorded because `core:decimal`
— money — is built directly on this, and because the identity is the one property
here that is not a matter of taste.

`(a/b)*b + (a%b) == a` held on **64/64** sign combinations, and all four cases
match C truncated division exactly, which is what the header claims (*"quotient
toward zero; remainder takes the DIVIDEND's sign"*):

```
  17/5   q=3  r=2          17/-5   q=-3  r=2
 -17/5   q=-3 r=-2        -17/-5   q=3   r=-2
```

**Both halves are gated, and one alone would not be enough.** The identity holds
for a FLOORED division too — Python's convention — so it cannot distinguish the
two, and the four printed cases are what pin the documented one. A package that
quietly switched to floored semantics would keep every identity check green while
changing what `-17 % 5` means, and `decimal` would inherit it.

Control: flipping `mod` to return the negated remainder reddens `bignum` and only
`bignum`. `scripts/bignum_diff.sh` (against Python's integers) and `q-check` both
stay green, which is the point — neither of them was asking this question.

### 84. `make ci N=0` silently cut a differential to 4.8% of its corpus — **FIXED 2026-08-15**

Found by re-measuring `make ci N=0` for the gate table, which is not a place
anyone expects to find a defect.

`scripts/format_diff.sh:34` read its corpus size from a bare environment
variable: `N=${N:-400}`. `make ci N=0` sets `N` as a Make variable to skip the
fuzz lanes, **Make exports it**, and the differential picked it up. The flag
documented as "skip the (slow) fuzz lanes for a quick check" also emptied the
generated half of every arm in the lane.

Measured, both directions:

```
N=0 sh scripts/format_diff.sh   ->  20 paths,   9 refused  -> lane exits 1
sh scripts/format_diff.sh       -> 420 paths, 129 refused  -> 0 violations
```

**It was caught by luck, and the luck is the point.** The lane exits 1 if fewer
than 10 of the path corpus are refused — a guard against an escape-free corpus
making the refusal half vacuous. With `N=0` the corpus fell to the 20 hand-written
cases, of which 9 are refusals. **One more refusal in that hand-written list and
`make ci N=0` would have reported a clean run over 4.8% of the corpus**, for as
long as anyone cared to use the flag. The other nine arms shrank the same way and
had no such guard: they simply scored fewer cases and said so in numbers nobody
compares between runs.

Renamed to `FMTDIFF_N`, namespaced so no sweep-level knob reaches it. Verified
both ways: `N=0` now gives 420, `FMTDIFF_N=40` still gives 60.

**Two things worth taking from this.** A gate that reads an unnamespaced
environment variable is a gate whose corpus any caller can change by accident,
and the more knobs a sweep grows the likelier the collision. And the diagnosis
went wrong twice before it went right: first "flaky, probably contention" (it was
perfectly reproducible), then "the probe output is truncated, `zip` is hiding it"
(the probe was fine; the *corpus* was short). Both wrong readings produced real
code — a checked-probe helper that stays because it closes a genuine hole in the
same file, ten `subprocess.run` calls whose exit status nobody checked.

### 85. Two things a third party would have hit first — **FIXED 2026-08-15**

Both found by *being* the third party rather than reasoning about one: install
from the published tarball, clone the repo, run the first command each document
tells you to.

**1. `SECURITY.md` advertised a channel that is switched off.** It said to use
GitHub's private vulnerability reporting — "Report a vulnerability" under the
Security tab. `gh api repos/.../private-vulnerability-reporting` returns
`{"enabled":false}`. The button is not there, and the fallback was "contact the
maintainer directly" with no address. Anyone holding a real finding had nowhere
to send it, which is a bad moment to discover while holding one. Rewritten to
describe the repository as configured, with a route that needs no
infrastructure. Enabling the feature would make the original text true and is one
API call; that is the owner's decision, not a documentation fix.

**2. `make check-links` passed here and FAILED in a fresh clone.** It is the
command `CONTRIBUTING.md` calls *"the one gate to never skip"* and the one the
pre-push hook runs, so a contributor's first action was red before they had
touched anything.

`check_citations.py` resolved a cited hash with `git cat-file`, which finds any
object in the LOCAL store — including one orphaned by a rebase, an amend or a
deleted branch, present here and absent from every clone. **28 of 55 backticked
hashes were exactly that**, plus 5 in the `commit <hash>` prose form and one
8-char hash the width-7 rule never examined. The gate checks
`git merge-base --is-ancestor` now.

All 34 were remapped rather than deleted: matching on the commit SUBJECT found a
mainline twin for every one, so this was a rebase, not lost history, and the
citations now point where the prose always meant.

**The shape is the one this file keeps recording.** A gate that verifies
something *about the machine it runs on* rather than something about the tree
will be green for the author forever. The only way to see it was to run it
somewhere else — and "somewhere else" is exactly what §1 and §7 are asking for.

### 86. A gated timer nobody ran cost 1.7x, and the gate that caught it went unread — **FIXED 2026-08-18**

> Pinned-by: none -- a timing claim, and a gate asserting a timing is a coin toss (the entry says so itself)

`3a94b652` added `TYCHO_ARENA_STATS` timing. Its own header says the bump path
is deliberately not timed "because clock_gettime is an order of magnitude more
[than a bump] and would slow the very path the design exists to keep fast". That
reasoning was right and aimed one function too high: the clock went into
`arena_free`, which runs at **every scope exit**, and `bench/binary_trees` went
from 302ms to 509ms.

**The clock never executed.** Every timing call is behind `if (g_arena_stats)`,
off by default. Merely *mentioning* `st_now_ns()` in `arena_free`'s body was
enough — a call in the body changes how the function is compiled into its
callers. Measured by deleting only the two timing lines and keeping every
counter: 511ms → 323ms. Splitting the pointer work into `arena_free_hot` and
leaving the clock in a `noinline` cold half took it to **296ms**, which is
faster than the 302ms before the regression and matches v0.7.0's 295-298ms.

| tree | tycho | ratio to C |
|---|---|---|
| v0.7.0 | 295 / 298 / 298 ms | 37-39% |
| `3a94b652^` | 302 ms | 38% |
| `3a94b652` .. `main` | 509 / 512 / 514 ms | 65% |
| after this fix | 296 / 296 / 296 ms | 37-38% |

**The gate was never the problem.** `bench/guard.sh` reported it correctly, in
the exact words `tree-alloc perf regressed`, as step `[10/13]` of `make ci`, from
the day it landed. It shipped anyway because the sweep was red at that step and
the line was skimmed. That is the failure this file keeps recording in a new
costume: a green-except-one sweep trains you to read the verdict instead of the
steps, and the one step you stop reading is the one carrying the finding.

It also went unnoticed because the standing instruction is to run the lanes that
can redden for a change rather than the sweep — correct, and it means a lane
whose subject is *the tree as a whole* has no change that obviously implicates
it. Nothing about a docs commit says "re-run the perf gate".

Found only because an unrelated branch ran every lane it had skipped.
