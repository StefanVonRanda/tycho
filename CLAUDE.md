# Working in this repo

## Gate budget — read this before running anything

This tree has a lot of gates and some of them are very expensive. **Run the
cheapest gate that can actually redden for your change.** Running a broader one
"to be safe" is not caution, it is twenty minutes of someone else's day.

| Gate | Cost | Reddens for |
|---|---|---|
| `python3 scripts/check_citations.py` | <1s | any `path:line` written in Markdown, comments, or evidence blocks |
| `sh scripts/check_links.sh` | <1s | relative Markdown links |
| `sh scripts/spec_check.sh` | ~6s | runnable examples in `docs/spec/`, Appendix A vs §3/§4, **and every backticked `tests/…` path in Appendix E resolving to a real file** — so any commit that moves or deletes a fixture directory must run this, not just the two doc gates |
| `make goldens-check` | ~0.07s | any new `run.sh`, any change to how one names its golden, and **any newly recorded golden** — it asserts every golden a runner names is tracked by git. `.gitignore` ignores `*.out` broadly and un-ignores per directory, so a new lane's golden is green on your disk and absent from a fresh clone; `make test` reads the copy that exists and cannot redden for it |
| `make shim-check` | <1s | any corelib `<pkg>_shim.c` — compiles each one standalone under `-std=c11`. **`make corelib` cannot redden for this**: the real build appends a shim to the generated `.c` on one `cc` line with no `-std`, so a missing feature-test macro compiles there and only here |
| `make ar-check` | ~3s | `tools/tycho-ar/`, and any `core:compress`/`sha256`/`io`/`path` change that moves a digest, the walk order or the archive round trip — **the only lane that runs anything under `tools/tycho-ar/`** |
| `make q-check` | ~3.5s | `tools/tycho-q/`, and any `core:csv`/`core:json`/`core:decimal`/`core:sort` change that moves a header, a cell's classification, a decimal's scale or a sort order — **the only lane that runs anything under `tools/tycho-q/`** |
| `make locale-check` | ~1.5s (1.73 / 1.46 / 1.44 s, measured 2026-08-02) | any change to how a float is read or written as text — `src/tychoc.c@c_strtod`, `src/tychoc.c@c_dtoa`, `runtime/tycho_rt.c@tycho_float_to_str`, or the `".0"` guard beside any of them. It compiles **and** runs `tests/float_lit_locale.ty` and `tests/float_str_locale.ty` with the process locale genuinely comma-decimal, forced by an `LD_PRELOAD` constructor calling `setlocale`. **`make test` cannot redden for the two compiler sites**: it runs tychoc in the grader's locale, which is `"C"` — the case that never broke — and an `LC_ALL=` prefix is *inert*, because a C program stays in `"C"` until something calls `setlocale`. Skips loudly, exit 0, on a host with no comma-decimal locale |
| `make vm-check` | ~2.3s | `tools/tycho-vm/` — **the only lane that runs anything under `tools/tycho-vm/`**. Asserts asm determinism, the `dis`→`asm` byte round trip, the three programs' output and listings against a golden, trace determinism, and that all seven runtime traps and four malformed-source diagnostics exit non-zero with empty stdout |
| `make server-check` | ~7s | `server/main.ty`, `server/www/`, `server/run.sh`, and the `core:net` accept/recv/send path |
| `make weblog webserver` | ~4s (1.9 s + 2.1 s, three runs each, measured 2026-08-02) | `examples/weblog/`, `examples/webserver/` — including `content/`, whose rendered pages are half the golden — and any `core:datetime`/`core:strings`/`core:sort` or `core:markdown`/`core:httpd` change that moves a parsed timestamp, a bucket order or rendered HTML. **The only lanes that run either program**; `scripts/entrypoints.sh` compiles them and asserts nothing. Neither binds a socket, so neither is `server-check` |
| `make corelib-examples` | ~44s (43.7 s, measured 2026-08-01) | `examples/corelib/**`, and any corelib change with a worked example — it compiles and runs each one against its golden. `sh examples/corelib/run.sh` is the same work at the same cost (43.5 s in the same session) |
| `make corelib` | ~49s (49.4 s, measured 2026-08-01) | **any `corelib/` change.** Builds and runs every `corelib/test/<pkg>/main.ty` against `corelib/test/<pkg>.out`. **`make test` cannot redden for a corelib change** — see the rule below — so this is the gate, not a supplement to one |
| `sh scripts/tools_check.sh` | ~1 min | `tools/tychofmt.ty`, `tools/lsp.ty` |
| `sh scripts/asan_self.sh` | minutes | `src/tychoc.c` under ASan/UBSan over the whole corpus |
| `make test-fast` | ~1 min | the same 560 fixtures as `make test`, over a worker pool — **advisory, see below** |
| `make test` | **~8 min** (473 s, measured 2026-07-31) | compiler or runtime behaviour, any fixture or golden |
| `make ci` | **~19 min** | a new CI step, or a release |

### The rule

- **Markdown, comments, evidence blocks** → the two doc gates. Nothing else.
  They cannot affect a compiled artifact, so `make test` cannot tell you
  anything `check_citations.py` did not.
- **A `.ty` fixture** → `make test`.
- **A `corelib/` change** → `make corelib`, plus `make corelib-examples` if the
  package has a worked example, plus `make shim-check` if a `<pkg>_shim.c` moved.
  **Not `make test`, which cannot redden for it**: `tests/run.sh:113` globs
  `examples/*.ty tests/*.ty` at the top level and never descends, so no file
  under `corelib/` is in its corpus. This line used to read "a `.ty` fixture or a
  corelib change → `make test`", which sent every corelib change to an eight-minute
  gate that could not fail for it — the expensive kind of wrong, because it looks
  like caution. Run `make test` for a corelib change only when something *outside*
  `corelib/` changed too.
- **`src/tychoc.c`** → `make test`.
- **`make ci` runs once**, at the end of a chain of related work, or when a
  phase adds a CI step. Not per phase. Not "to confirm". Once.
- If you are unsure which gate covers your change, that is a question to ask,
  not a reason to run the expensive one.

### `make test-fast` is the fast lane; `make test` is still the answer

`make test-fast` runs the identical 560 fixtures through `tools/prunner/main.ty`,
a Tycho program with a bounded worker pool: **473 s → 62 s, 7.6x**, and its report
is byte-identical to `tests/run.sh`'s over the whole corpus, unsorted
(`docs/internals/plan-prunner-DONE.md` phases 2 and 4). Both were measured on a
16-core box; width is
`ncpu()`, narrowable only by launching with `TYCHO_THREADS=N`.

Use it to iterate. **Do not use it as the gate**, and do not put it in
`scripts/ci.sh`. prunner is compiled by the compiler it tests, so one tychoc
regression — string comparison, `os.run`'s exit code, the `parallel for` fan-out —
lands inside the judge and can turn all 560 verdicts green at once. `tests/run.sh`
scores with `cmp`, `grep` and `test`, which no change in this repo can break, and
it is the only independent implementation left after the `tychoc0` differential
was retired (see "Two gates that used to be here"). When the two disagree,
`tests/run.sh` is right by definition; `./build/prunner --mode=seq` re-runs the
same jobs one at a time, which separates "the pool did it" from "the judge did
it". A FAIL from prunner prints the same parenthesised reason but not the log
dump — re-run `make test` for that.

### `make ci` is confirmation, not discovery — never debug with it

The expensive failure mode is not running `make ci` too often for no reason. It
is running it as a **feedback loop**: sweep, hit a red, fix it, sweep again, hit
the next red, fix it, sweep again. Each iteration costs nineteen minutes to learn
one thing that the individual gate would have reported in seconds. Observed on
this repo: four sweeps, seventy-six minutes, failing at `[4/13]`, then `[9b/13]`,
then `[12/13]` — three facts that `make conc`, `make editors-check` and
`scripts/spec_check.sh` together would have produced in under two minutes.

When `make ci` reddens, **do not re-run `make ci`.** Read which step failed, run
that step's own gate, fix, re-run *that gate*. Spend the full sweep once, at the
end, to confirm what you already believe.

| `make ci` step | run this instead while fixing |
|---|---|
| `[1b] goldens-check` | `make goldens-check` (~0.07s; it names the `run.sh:line` and the untracked file. The fix is a `.gitignore` un-ignore line for that lane's directory plus `git add`, not a re-record) |
| `[2] make test`, `[2c] asan-self` | `make test` (`sh scripts/asan_self.sh` for the ASan-specific case) |
| `[2b] ilp32` | `make ilp32` — **not `make test`**, which cannot redden for it. It rebuilds the same fixtures under `gcc -m32 -msse2 -mfpmath=sse`, so a red here is a 64-bit assumption in the emitted C, not a golden change. A fixture that passes at 64-bit and fails here is usually an `int` width or an FP-evaluation difference, and both are invisible to the 64-bit lane |
| `[2e] locale-check` | `make locale-check` (~1.5s; it `LD_PRELOAD`s a `setlocale` constructor and rebuilds the two locale fixtures under a comma-decimal `LC_ALL` — a red here is a float literal or a `str(float)` picking up the ambient decimal separator, and there is no `RECORD=1`: the goldens belong to `make test`, this lane only reads them harder) |
| `[3] corelib` and its dogfoods | `make corelib` / `make corelib-examples` / `make fetch` / `make weblog` / `make webserver` |
| `[3b] entrypoints` | `sh scripts/entrypoints.sh` |
| `[3c] server-check` | `make server-check` (~7s; it starts tycho-httpd for real — a red here is a behaviour change in `server/main.ty` or `core:net`, not a build break, which `[3b]` would have caught first) |
| `[3d] shim-check` | `make shim-check` |
| `[3e] ar-check` | `make ar-check` (~3s; it builds and runs `tycho-ar` over a fixture it writes itself — a red here is a digest, a walk order or a round-trip change, and `RECORD=1 sh tools/tycho-ar/run.sh` re-records the golden once you know why) |
| `[3f] q-check` | `make q-check` (~3.5s; it builds and runs `tycho-q` over fixtures it writes itself — a red here is a changed row, order, header or cell classification, and `RECORD=1 sh tools/tycho-q/run.sh` re-records the transcript once you know why) |
| `[3g] vm-check` | `make vm-check` (~2.3s; it builds and runs `tycho-vm` over the three `progs/*.tasm` plus fixtures it writes itself — a red here is a changed listing, program output, trace length or trap message, and `RECORD=1 sh tools/tycho-vm/run.sh` re-records the golden once you know why) |
| `[4] conc` | `make conc` |
| `[5] ffi` | `make ffi` |
| `[6]/[7] fuzz` | `python3 fuzz/run.py <small N>` |
| `[9] tools-check` | `sh scripts/tools_check.sh` |
| `[9b] editors-check` | `make editors-check` |
| `[10] bench-guard` | `sh bench/guard.sh` |
| `[11] recursion` | `make recursion` |
| `[12] spec-check` | `sh scripts/spec_check.sh` |
| `[12b] docs-fences` | `make docs-fences` |
| doc gates | `python3 scripts/check_citations.py`, `sh scripts/check_links.sh` |

And if the *same* step reddens twice, stop patching and read the source that
governs it. Three different steps reddening in a row means the change is touching
more than its scope claims — say so and narrow it, rather than grinding the suite
until it goes quiet.

### Two gates that used to be here

`sh scripts/frontparity.sh` and `sh compiler/fixpoint.sh` were **retired on
2026-07-29** and are no longer runnable gates. Both built the frozen
`compiler/tychoc0.ty` and checked the live compiler against it; the breaking
loop-syntax change of that date means the frozen compiler can no longer parse
the corpus. The scripts are still on disk and their headers record what they
proved and what their loss costs — `ROADMAP.md` and `docs/architecture.md` carry
the same in prose. **Nothing replaces them**: a change that silently narrows what
`src/tychoc.c` accepts no longer has a second implementation to disagree with it.

### Why this file exists

A ten-phase plan in this repo spent most of its wall-clock waiting on `make ci`
runs that could not have failed — phases that edited only Markdown, each
re-running a nineteen-minute suite. The evidence is in
`docs/internals/plan-friction-DONE.md` and in `plan.md`'s "Gate ladder" section.
The gates are good; running all of them every time is not using them, it is
avoiding thinking about which one applies.

## Environment

`~/.zshenv` drops `LD_PRELOAD` when it is the tmux `block-nnp.so` shim. If you
ever see `ASan runtime does not come first in initial library list`, that shim
is back in the environment and **the tree is not at fault** — it scored 251/527
spurious failures before this was found. Re-run under `env -u LD_PRELOAD` and
say so rather than changing anything in the repo.

## Citations

`path:line` references are load-bearing here and `scripts/check_citations.py`
gates them. Two things that bite every time:

- A **bare** `:N` binds to the **previously named path in the same document**.
  Name the path in evidence blocks, or the gate resolves your citation against
  whatever file you happened to mention last. Four separate phases have reddened
  the gate on their own write-ups this way.
- **Repo-relative, never absolute.** `src/tychoc.c:402` — never that same path
  with the checkout's absolute prefix in front of it. (Spelling the absolute form
  here would itself redden the gate, which is the point.) An earlier version of this file
  said "write full paths", which is satisfied by an absolute path — and an
  absolute path used to be skipped by the gate entirely, so it looked careful
  while being checked by nothing. 187 such refs accumulated before anyone
  noticed. Absolute paths are now a hard failure with a message naming the
  relative form; the wording here is the reason they existed.
- Inside a `> Provenance:` block, a **single-line** ref must be anchored
  `path:N@token`; a **range** stays bare, deliberately — a range has no single
  subject token and forcing one produces a false anchor. Do not "fix" the
  exemption.
- **An anchor must name one line.** If the token appears on more than one line
  of the cited range it identifies none of them: the region can drift inside
  itself and the check still passes. That is a hard failure. Fix it by anchoring
  a token that occurs once, by tightening the range to its construct, or by
  dropping the anchor — a bare range is honest, a false anchor is not.
- **Citing a definition? Write `` `path@SYMBOL` ``, with no line number.** A
  region needs an address; a definition has a name, and its line number is only
  a record of how much prose sits above it. `` `corelib/signal/signal.ty@shutdown_requested` ``
  is checked by finding the token in that file, so it survives every insertion
  and still reddens on a rename or a deletion — the only two events that make it
  wrong. The `ARCHIVED` constant's refs were repointed three times across two
  phases before this existed, and not one repair carried information.
  - **The check is deliberately weak, and knowing that is the point:** it proves
    the symbol is still spelled that way *somewhere* in that file, not that the
    definition is still there. Uniqueness is **not** required, unlike a line
    anchor — a symbol appears at its definition and at every call site, and
    demanding one occurrence would reject every symbol anyone actually calls.
  - **Use it for a definition, not for a region.** Pointing at a loop body or a
    table still wants `` `path:N-M` ``; there is no name to use.
  - **"Definition" was the case, not the property — a line whose identity is a
    distinctive token counts too.** What makes the symbol form right is that the
    target *has a name of its own*, so the line number carries nothing the name
    does not. A region fails that test because it has no name. The ilp32 recipe's
    `@echo "ilp32: ASan lane SKIPPED for ilp32 …"` passes it: `SKIPPED` is a word
    in an echo string rather than a definition, and it is still what every
    citation of that line is *about*. So `` `Makefile@SKIPPED` `` is correct, and
    so is `` `Makefile@TYCHO_NO_ASAN` `` for the recipe's last line. Two
    conditions, both the writer's judgement rather than the gate's: the token must
    be **what the citation is about**, and it must be **distinctive in its file**
    (`grep -c SKIPPED Makefile` answers 1 as of 2026-08-02).
  - **What that widening does not sanction, and what it costs.** It does *not*
    license picking a word out of a region and citing it as a symbol — that is the
    false anchor the range exemption exists to prevent, one grammar over. And
    because the check is file-wide existence with no uniqueness requirement, an
    ordinary English word can be kept alive by an unrelated new occurrence: delete
    the ilp32 echo while some other recipe gains its own "SKIPPED" message and the
    citation passes while pointing at nothing. A symbol like `shutdown_requested`
    cannot collide that way; a message word can. That is a real loss against the
    line form, and it is the smaller one — the line form's measured behaviour on
    this exact citation was twelve mechanical repointings in eight days and one
    silent error (its companion bare `:N` drifted onto a **blank** line and no
    pass could see it, because a bare `:N` in a source file names no path).
  - **Converting correct old-form refs is not work.** 22 live refs would qualify
    (37 counting frozen archives, of 218 anchored). They are not wrong and there
    is no sweep to do — write the new form for new definition citations, and
    convert an old one when it next breaks.

### The bare-ref count is not a backlog

The gate's green line reports thousands of bare refs against a couple of hundred
anchored ones, and that ratio has twice been mistaken for work to do. It is
split on that line now so it cannot be again, into four buckets: the frozen
`docs/internals/plan-*-DONE.md` set, where every rule in the gate already
refuses to demand an edit; the live plan's own evidence; the deliberately-exempt
`> Provenance:` ranges; and reachable narrative prose. Only the last is a bucket
any policy could act on, and it is much the smaller half.

**The reachable ones stay bare.** Requiring anchors on them is the hand sweep
this repo has declined three times with measurements each time (`FRICTION.md`:
11 of 15 spot-checked refs drifting again four days after a repair pass). And
the one construct where anchoring is mandatory is already at 100% — zero of its
single-line refs are bare — so there is no second context left to name.

**Anchoring more is not anchoring better**, and the gate measures the
difference: `python3 scripts/check_citations.py --stats` prints how many anchors
name a token that recurs within ±25 lines of their range, and how many name one
recurring anywhere in the file. Those anchors survive a drift by accident, and
a share of the *mandatory* `> Provenance:` anchors are among them — four of them
anchor `@parse_value_ctrl` to four different lines of the same function. This is
**counted, never failed on**: clearing it under gate pressure means inventing a
replacement token per red, which is how false anchors get made.

### Never copy a figure the gate prints into prose

Run `--stats`; do not quote it. Every count on that line changes when any phase
adds a citation, so a number typed into a paragraph is stale by the next commit
and nothing checks it — the same defect as a stale `path:N`, in the documentation
about stale `path:N`s. This file and the gate's docstring between them carried
four such figures, and the weak-anchor pair went stale **inside one day**, in the
commit whose subject was updating it.

The boundary is one question: **can a command produce this number today?**

- **Yes** → name the command, not the number.
- **No** → it is a one-time measurement that decided something, and it *stays*,
  with its date or its commit. "45 refs versus 16", "11 of 15 spot-checked refs
  drifting" are evidence for choices, true of the tree they were taken on.
  Deleting those destroys the reasoning; being about a past tree is exactly what
  makes them safe to write down.

**A "no" is not by itself a licence.** This list used to open with "271 record
lines across 9 of 13 files", and that figure was wrong on the very tree it was
taken on — the record-line section below withdrew it. No command could produce
it, and that is exactly why nobody checked it. Non-recomputability is the
signature of a protected measurement *and* of a number nobody can falsify, and
from the outside the two are indistinguishable. So the "no" branch carries a
second obligation: state how it was measured, tightly enough that a reader could
repeat it. A measurement whose method is unstated is not evidence, and "no
command produces it" will not save it.

**There is no ratchet and no budget on the bare count, on purpose.** Pressure to
shrink it would eventually point someone at a before/after record block, whose
line numbers are *data* — `"was 846, now 848"` is right precisely because it is
stale, and "repairing" it destroys the evidence.

### A record line is not a citation — recognise it by shape

A **record line** states what a ref *said at a past moment*. Repairing one does
not fix a stale pointer, it falsifies evidence, and the loss is irreversible
because the old number exists nowhere else. Two shapes carry that meaning here,
and both are recognisable without reading the prose around them:

- **A repair log** — two refs joined by an arrow: `` `:494` `` → `` `:494-495` ``.
- **A before/after table row** — a table row with **two or more ref-bearing
  cells**, usually with a delta column beside them.

**If a line has either shape, leave every number in it alone.** That includes
numbers that are provably wrong today; being wrong is what they record.

#### A record protects its numbers, not its anchors

This rule and the anchor rule used to contradict each other, and the
contradiction was live for hours before anything hit it. The rule above says
leave *every number* in a record line alone. The anchor rule says an anchored
ref is never exempt, in a frozen archive or anywhere, because it promised a
token. A before/after table row carrying `` `:1841@tok` `` satisfies both
descriptions and they demand opposite things.

**The record rule gave way, and here is the seam it gave way along.** A record
line records *what a ref said*. The number is that record — it is a quotation of
a past observation, and the past cannot be edited. An anchor is not a quotation:
`@token` is a claim that the token is in those lines **in the tree you are
reading now**. It was never part of what the record recorded; it is a live
promise that someone bolted onto a dead number. A live promise inside a frozen
record is still a live promise, so the anchor rule wins on the anchor, and the
record rule keeps everything it was actually written to protect.

**So the repair is always: drop the anchor, keep the number.** Never repoint the
number to make an anchor match — that is the falsification the record rule
exists to prevent. Never leave a failing anchor in place either. A bare number on
a record line is honest: it says "this is what it said then", which is true.

**Measured before this was written:** 41 anchored refs sit on record lines across
the tree, 40 of them reaching the gate's content check, and **zero fail it
today** — the two that did were repaired this way, by dropping their anchors, in
`docs/internals/plan-signals-DONE.md`'s six-row table. So this is a rule for
whoever meets the 41st, not a sweep. Nothing needs doing now.

**No marker is inserted, and the distribution is why.** The question was whether
to tag these blocks explicitly. They are heavily concentrated in frozen
archives — `docs/internals/plan-postfreeze-rawstring-DONE.md`,
`docs/internals/plan-front-door-DONE.md` and
`docs/internals/plan-signals-DONE.md`, plus a fourth cluster in
`docs/internals/frontend-restriction-audit-2026-07-25.md`. The live files hold
almost none: a couple in `plan.md`, and in `FRICTION.md` — the one file in this
set a person edits by hand — **none at all**. Tagging them *is itself the hand
sweep this repo has declined three times*, and nearly every tag would be an edit
to a frozen record. The shape already marks them; a tag would only restate it,
at the cost of touching every archive to say so.

**This paragraph used to quote a total, and the total was wrong.** It read "271
record lines across 9 of 13 files", counted over the twelve archived plans plus
the live one. The file list asked "which *plans* have record lines" when the rule
it justifies asks "which *lines* have the record shape" — so it never looked at
the audits under `docs/internals/`, or at `FRICTION.md`. And the count is not
reproducible: three later attempts to detect the same two shapes over every
tracked Markdown file returned three different totals, none of them 271 and none
of them each other, differing only in how strictly "joined by an arrow" and
"ref-bearing cell" were read. Run the strictest of them against the commit that
introduced the sentence and it returns exactly what it returns against this
tree — the population never moved, so this was not a true measurement that went
stale. It was wrong when written, which is why it is repaired rather than
protected.

**The total is withdrawn rather than corrected, because the shapes are semantic
and no regex settles them.** The proof is this section's own repair-log bullet
above: a detector implementing it literally — a ref, an arrow, a ref, adjacent —
does not match that bullet, because the escaping that displays its backticks
inserts backtick characters between each ref and the arrow. The canonical example
of the shape fails the literal reading of the rule that describes it. Looser
readings do worse in the other direction: allowing any arrow anywhere on the line
counts C pointer dereferences (`e->sval`), function types (`-> Result(T)`),
before/after line counts and command output, which is how `FRICTION.md` was
credited with record lines it does not have. A rule that cannot match its own
example, and whose relaxations match prose, does not define a countable set — any
number published for it is a number about one unrecorded detector. The no-marker
decision never needed a total, and it stands on the distribution above.

**No rule *targets* record lines** — none counts, budgets or ratchets them, so
nothing pushes anyone to sweep one. That is the load-bearing defence, and the
rule above exists so that a human who goes looking anyway can tell what they are
holding.

This used to be stated more widely, as "what the gate does about it: nothing,
deliberately", and that was **disproved**. Widening `SRC_PREFIX` put gate
pressure on a frozen before/after table without any rule being aimed at record
lines: the widening made a previously-skipped path reachable, and the ordinary
anchor content check then fired inside a record. A rule does not have to be about
record lines to redden one. Expect that again on the next widening, and reach for
"drop the anchor, keep the number" when it happens.

**The one case that needs a tag is the opposite shape** — see
`docs/internals/plan-signals-DONE.md`'s `[SUPERSEDED]` note. There the number
was a *pointer*, not data, and the line reads as ordinary live prose, so nothing
about its shape warns you. A tag is worth it exactly when the shape does not
already say "record".

## Plans

Substantial work runs through `plan.md`: one phase at a time, each phase
verified and committed on its own, evidence appended under the phase rather
than pasted into chat. Work discovered outside a phase's scope is appended to
`plan.md` as a new unchecked phase — never silently absorbed into the phase
that found it.

### `plan.md` rotates, so never leave "`plan.md` phase N" behind

When a plan completes it is archived to `docs/internals/plan-<name>-DONE.md` and
a new `plan.md` starts numbering again at 1. A comment written as "`plan.md`
phase N" therefore stops meaning anything the moment the plan it was written
under is archived: it now resolves against a different document, at a phase
number that belongs to unrelated work. 167 such references across 43 files
accumulated before anyone counted them, because the reference carries no line
number and `scripts/check_citations.py` had nothing to check.

**`scripts/check_citations.py` now gates this**, as a fourth direction with no
line number involved: outside the live plan and the frozen
`docs/internals/plan-*-DONE.md` set, carrying one is a hard failure naming the
archived document you should write instead. It matches optional backticks,
singular and plural, any case, a reference that wraps onto a continuation
line, and **both word orders** — the file first, or the phase and its number
first with the file after them. Five separate surveys of this class each
under-counted it by missing one of those; the reversed order was the fifth, and
it was found because the gate's own docstring contained one and passed. What is
still NOT matched, and is filed rather than shipped: a possessive joining the
two ("the plan's phase N"). `compiler/tychoc0.ty` is exempt because it is frozen
and unfixable.

**The rule, in two halves:**

- **Writing:** cite the archived document by name — ``
  `docs/internals/plan-friction-DONE.md` phase 5 `` — whenever the plan you mean
  is already archived. Bare "`plan.md` phase N" is only ever correct for the
  plan that is live *right now*.
- **Archiving:** the commit that archives a plan rewrites the references that
  plan created. `git log --diff-filter=A -- docs/internals/plan-*-DONE.md` gives
  the rotation boundaries (the commit that *adds* `plan-X-DONE.md` is the moment
  X stopped being live); `git blame` on a citing line dates it into exactly one
  window. Check the result rather than trusting it: the phase number cited must
  be a phase that document actually declares.

`docs/internals/plan-*-DONE.md` are exempt in both directions — they are frozen
records, and inside one of them "`plan.md` phase N" self-refers unambiguously.

### Writing a phase's brief

Whoever writes a phase — in `plan.md` or in the instructions handed to an agent —
sets that phase's gate cost. Most of the waste recorded in this file was
introduced there, not by the agent that obeyed it.

- **Name the specific gates the phase's own changes can redden, and say what NOT
  to run.** A brief that lists `make ci` as "the verification" invites it as the
  debugging loop. A phase editing only Markdown should be told, in words, not to
  run `make test`.
- **`make ci` belongs in a brief only when the phase adds or changes a CI step,
  or when it is the deliberate closing sweep of a finished chain.** Nowhere else.
- **State the expected count, not just the gate.** "`make test`, which was 541 at
  the previous phase" catches a silent loss that a bare "make test passes" does
  not. Several phases here caught real regressions purely because a number moved.
- **Sequence tooling before corpus.** A change to the language that rewrites many
  `.ty` files must teach `tools/` and `editors/` the new syntax *first* —
  `scripts/editors_check.sh` parses every file in the tree and compares against a
  known-bad set, so a corpus rewrite ahead of the grammar reddens it by
  construction. This cost a full sweep and a plan reordering on 2026-07-29.
- **Do not assert facts the phase should verify.** Briefs in this repo have
  confidently mis-stated where a token was lexed, which of two files was already
  fixed, how a golden's hash was computed, and how many call sites existed. Every
  one was caught by an agent reading the source. Write the claim as something to
  check, and it will be checked; write it as fact, and time is spent disproving
  it.
