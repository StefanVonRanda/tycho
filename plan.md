# What the next program says the language needs

> This plan is a fresh clone, 2026-08-03: the completed tycho-vm plan lives at
> `docs/internals/plan-tycho-vm-DONE.md`. The rule from that plan holds here:
> *does anything that is not the program written to want it need this?* A
> finding becomes a phase only when a second, independent caller exists.

## The program

**tycho-scheme** -- a Scheme interpreter in `tools/tycho-scheme/`, the first
program in the tree to use upward closures (returning a function that captures),
recursive value trees at interpreter scale, and an environment pool (the
flat-index idiom the thesis recommends). It is the classic stress test: the
reader, the evaluator, the printer, and a small standard library all in Tycho,
gated the tycho-vm way (compile programs, lock goldens byte-identical).

Findings recorded while probing the design, before a line of the interpreter:

- **Upward closures work** (probed: `make_adder` returns a closure, composes,
  calls correctly -- the spec says capture by deep copy at creation, §09-expressions.md:188-191). The architecture doc's "closures (downward value-capture)" wording is STALE, the same docs-drift pattern as the inout premise. This program is the first upward-closure customer.
- **A recursive enum can carry itself directly** (`VPair(Val, Val)` compiles and
  runs), and a recursive enum is a legal map VALUE (`[]string: Val`). The json
  module's note that "maps don't take a recursive value type here" (corelib/json/json.ty) needs re-checking against that.

Phases below appear as the interpreter finds friction; a finding is a phase only
when a second, independent caller needs it.

## Findings, while writing it

- **Upward closures work** (probed before a line of the interpreter; also proven
  at run time by `progs/closures.scm` -- `make-adder` returns a closure, and two
  counters from one factory stay independent). The architecture doc's "closures
  (downward value-capture)" wording is STALE; the spec's capture-by-deep-copy
  (§09-expressions.md) is the truth. Recorded, not a phase -- the wording fix is
  a one-line docs correction when the tree is touched next.
- **No mutable module-level state.** A top-level `g_envs: [Env] = []` is
  rejected ("expected 'fn'"); Tycho's mutable state lives in scopes. The
  interpreter's environment pool is therefore owned by `main` and threaded
  through every evaluator call as `inout [Env]` -- the explicit-state idiom at
  interpreter scale (every eval/apply/lookup carries it). Works, and it is the
  demand answer for "should Tycho grow mutable globals?": one customer, and
  threading is fine. Recorded, not a phase.
- **A captured binding cannot be mutated by `define`.** `define` writes the
  CURRENT frame, so a closure that closes over a counter would reset it per
  call. The fix is `set!`: walk the environment chain and mutate the frame that
  DEFINES the name -- the explicit mutation Scheme requires anyway.
- **Deep generated-code recursion SEGFAULTS.** `(sum (range 0 5000))` crashes
  with SIGSEGV instead of failing closed: the compiler's recursion gate guards
  the compiler's own parse depth, not the C stack of generated code, and the
  emitted frames for eval-apply-eval blow ~8 MB at ~5k Scheme levels. The
  interpreter's programs stay shallow; the gate records why. A real gap with
  potentially many customers (any deeply recursive Tycho program, the Json
  walker at depth) -- a phase candidate if a second caller appears.
- **Maps are legal struct fields; the type is `[K: V]`** (colon, spaces) -- the
  literal is `[]K: V`. A syntax gotcha that cost three probes, not a gap.

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
