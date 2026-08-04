# What comes next

> 2026-08-04: the eight-program testing campaign and the repo-polish turn are
> both complete and archived — the program plans at
> `docs/internals/plan-*-DONE.md` (scheme, vm, kv, chess, rsa, kvsrv, sat), the
> polish at `docs/internals/plan-repo-polish-DONE.md`. The language is declared
> well tested: eight programs across six axes, zero compiler/runtime defects,
> six tool-gate lanes, a differential fuzzer, the self-hosting byte-for-byte
> proof. The repo is polished: README with the evidence-of-exercise section,
> LICENSE/SECURITY/CONTRIBUTING present, no cloud CI by the owner's explicit
> decision (`make ci` is the whole gate, run locally).

## The standing candidate

The one axis the tools never touched is systems-y I/O — subprocesses, io
metadata, a parallel dependency graph. The candidate filed for it: **a build
tool** (make-like: mtime-based up-to-date checks, a step DAG, parallel
execution of independent steps via the concurrency model), with a hermetic
fixture-tree differential (build twice, the second build is a no-op). It
stays the default if the owner ever wants to resume the campaign.

Until then, the plan is: **nothing pending**. The language is tested, the
repo is polished, and the demand rule still governs any future finding — a
finding becomes a phase only when a second, independent caller exists.

## The work -- professional-facing docs

2026-08-04, new phase: the README, docs and wiki should read like a
language project's, not a work site's. The README is restructured to the
landing-page shape (hero + tagline + links bar + key features + quick
start + condensed evidence), with the deep content moved to docs: the
benchmark and memory evidence to a new `docs/performance.md`, the costs
folded into the FAQ, the thesis already at `docs/thesis.md`. The wiki
gains its three empty pages (Installing-Tycho, FAQ, Contributing).

### Phase A -- README landing page + docs/performance.md  [DONE 2026-08-04]

The README is restructured to the landing-page shape: hero + tagline + a
links bar, a Key Features section, the quick start, a condensed "why"
pointing at the thesis, the testing campaign (the seven tools table), a
short performance teaser, the FAQ (with the costs folded in), and the
build/docs/license sections. The deep content moved out: the benchmark
tables, the flat-memory JSON evidence and the speed notes to the new
`docs/performance.md` (indexed in `docs/README.md`), the costs into the
FAQ. README is now ~240 lines of landing page instead of 294 of research
writeup.

### Phase C -- re-verifiable self-hosting claim + scope callout  [DONE 2026-08-04]

The public-facing review flagged the marquee claim as the least verifiable: the
self-hosting byte-for-byte proof was frozen and not re-runnable. The fix is a
gate: `compiler/selfhost.sh` (`make selfhost-check`, ci step [3n/20], ~50s)
re-runs the self-emission chain (stages 2-4 of docs/bootstrap.md) over the
frozen compiler alone and asserts the byte-identity — corpus-independent, so it
cannot drift the way the retired fixpoint did. The fixpoint was verified to
still hold at HEAD before the gate was written. The README also gains a status
banner (research prototype, pre-1.0) so the landing page cannot overpromise.

### Phase B -- wiki: fill the empty pages  [DONE 2026-08-04]

The wiki (tycho.wiki, a separate repo) already mirrored the reader docs;
its three empty pages are filled: Installing-Tycho (prereqs, clone+make,
platform notes, verifying with the local gate), FAQ (the four README
questions plus the production-readiness answer Home anchors to), and
Contributing (condensed from CONTRIBUTING.md). Pushed to the wiki repo.
