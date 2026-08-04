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
