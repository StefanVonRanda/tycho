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
| `make server-check` | ~7s | `server/main.ty`, `server/www/`, `server/run.sh`, and the `core:net` accept/recv/send path |
| `sh scripts/tools_check.sh` | ~1 min | `tools/tychofmt.ty`, `tools/lsp.ty` |
| `sh scripts/asan_self.sh` | minutes | `src/tychoc.c` under ASan/UBSan over the whole corpus |
| `make test-fast` | ~1 min | the same 560 fixtures as `make test`, over a worker pool — **advisory, see below** |
| `make test` | **~8 min** (473 s, measured 2026-07-31) | compiler or runtime behaviour, any fixture or golden |
| `make ci` | **~19 min** | a new CI step, or a release |

### The rule

- **Markdown, comments, evidence blocks** → the two doc gates. Nothing else.
  They cannot affect a compiled artifact, so `make test` cannot tell you
  anything `check_citations.py` did not.
- **A `.ty` fixture or a corelib change** → `make test`.
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
| `[2] make test`, `[2b] ilp32`, `[2c] asan-self` | `make test` (`sh scripts/asan_self.sh` for the ASan-specific case) |
| `[3] corelib` and its dogfoods | `make corelib` / `make corelib-examples` / `make fetch` |
| `[3b] entrypoints` | `sh scripts/entrypoints.sh` |
| `[3c] server-check` | `make server-check` (~7s; it starts tycho-httpd for real — a red here is a behaviour change in `server/main.ty` or `core:net`, not a build break, which `[3b]` would have caught first) |
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

### The bare-ref count is not a backlog

The gate's green line reports thousands of bare refs against a couple of hundred
anchored ones, and that ratio has twice been mistaken for work to do. It is
split on that line now so it cannot be again. Of 2802 bare refs: **1800** are in
the frozen `docs/internals/plan-*-DONE.md` set, where every rule in the gate
already refuses to demand an edit; **17** are in the live plan's own evidence;
**196** are the deliberately-exempt `> Provenance:` ranges; **789** are
reachable narrative prose.

**Those 789 stay bare.** Requiring anchors on them is the hand sweep this repo
has declined three times with measurements each time (`FRICTION.md`: 11 of 15
spot-checked refs drifting again four days after a repair pass). And the one
construct where anchoring is mandatory is already at 100% — zero of its
single-line refs are bare — so there is no second context left to name.

**Anchoring more is not anchoring better**, and the gate now measures the
difference: `python3 scripts/check_citations.py --stats` prints how many anchors
name a token that recurs within ±25 lines of their range (32) or anywhere in the
file (76). Those anchors survive a drift by accident. 17 of the 97 mandatory
`> Provenance:` anchors are in that state — four of them anchor
`@parse_value_ctrl` to four different lines of the same function. This is
**counted, never failed on**: clearing it under gate pressure means inventing 17
replacement tokens, which is how false anchors get made.

**There is no ratchet and no budget on the bare count, on purpose.** Pressure to
shrink it would eventually point someone at a before/after record block, whose
line numbers are *data* — `"was 846, now 848"` is right precisely because it is
stale, and "repairing" it destroys the evidence. Nothing in the tree marks those
blocks yet. Until something does, treat a bare ref inside an evidence or
before/after block as a record, not a citation, and leave it alone.

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
singular and plural, any case, and a reference that wraps onto a continuation
line — four separate surveys of this class each under-counted it by missing one
of those. `compiler/tychoc0.ty` is exempt because it is frozen and unfixable.

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
