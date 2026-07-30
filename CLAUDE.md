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
| `sh scripts/tools_check.sh` | ~1 min | `tools/tychofmt.ty`, `tools/lsp.ty` |
| `sh scripts/asan_self.sh` | minutes | `src/tychoc.c` under ASan/UBSan over the whole corpus |
| `make test` | minutes | compiler or runtime behaviour, any fixture or golden |
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
| `[3] corelib` and its dogfoods | `make corelib` / `make corelib-examples` |
| `[3b] entrypoints` | `sh scripts/entrypoints.sh` |
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

## Plans

Substantial work runs through `plan.md`: one phase at a time, each phase
verified and committed on its own, evidence appended under the phase rather
than pasted into chat. Work discovered outside a phase's scope is appended to
`plan.md` as a new unchecked phase — never silently absorbed into the phase
that found it.

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
