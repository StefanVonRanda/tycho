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
  Write full paths in evidence blocks, or the gate resolves your citation
  against whatever file you happened to mention last. Four separate phases have
  reddened the gate on their own write-ups this way.
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
