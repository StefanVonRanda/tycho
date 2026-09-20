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
  interpreter's programs stay shallow; the gate records why. **FIXED as phase 1
  below** -- a runtime stack guard (plan phase 1 [DONE]) turns stack exhaustion
  into a clean fail-closed exit for every generated program, including spawned
  tasks; the second caller that justified the phase was the json walker
  (`corelib/json/json.ty@parse_value` recurses per nesting level), which
  segfaulted at ~100k-deep documents (measured: 10,000 nests fine, 100,000
  SIGSEGV).
- **Maps are legal struct fields; the type is `[K: V]`** (colon, spaces) -- the
  literal is `[]K: V`. A syntax gotcha that cost three probes, not a gap.

## Phases

### Phase 1 -- generated-code recursion must fail closed  [DONE 2026-08-03]

**Why now:** the recursion finding gained its second caller. The compiler fails
closed on pathological input (`tests/recursion/run.sh`); generated code had
nothing -- deep recursion in emitted C overflows the C stack and dies with
SIGSEGV (exit 139), no diagnostic, no cleanup. Two measured callers: the
interpreter at ~5k Scheme levels (`(sum (range 0 5000))`), and the json walker
at ~100k nests (10,000 ok / 100,000 SIGSEGV, verified 2026-08-03). The compiler
side is not the model to copy: `make recursion` guards the compiler's OWN parse
depth, and it is compiler code, not emitted code.

**Mechanism -- chosen by measurement, not decree.** The runtime stack guard
(option 1) won, and the other two are dominated rather than merely outvoted:

- **Measured, option 1** (the winner): a SIGSEGV handler on an alternate stack,
  installed once by a runtime constructor; on a fault it compares the faulting
  SP (from the ucontext) against the thread's recorded stack region and, past
  the bottom, prints `tycho: stack overflow -- recursion too deep` and exits 1;
  anything else re-raises with the default disposition, so a null deref still
  crashes as a real crash (exit 139, debugger-visible). Zero steady-state cost:
  nothing runs between the constructor and an overflow. Measured fib(30)x300
  before/after: 2.2568/2.2658/2.2539 s vs 2.2653/2.2545/2.2550 s -- identical
  within noise.
- **Option 2 (emitted per-call depth counter) is dominated**: every call pays
  an increment+decrement for a guarantee that is strictly weaker -- it counts
  function ENTRIES, so a single deep expression `f(f(f(...)))` (100k nested
  calls in one expression, one function entered once) still blows the stack
  unguarded. Option 1 catches that shape.
- **Option 3 (SCC-only accounting) is dominated**: the same entry-counting
  weakness, plus a call-graph pass the compiler does not have, for the only
  benefit of sparing non-recursive programs a cost option 1 does not charge.

**The price of covering spawned tasks: +7% on spawn-bound workloads.** A
spawned task runs on its own thread with its own stack, so the trampoline
(tycho_task_start) installs per-thread bounds AND an alternate stack before the
task's code runs. Measured 2000 spawn+wait of a trivial fn: 38.7/40.2/40.8 ms
before vs 42.2/42.6/43.2 ms after (~1.4 us/spawn: pthread_getattr_np + one
sigaltstack syscall). Everything else (fib, sort, non-spawning programs) shows
no movement. The trampoline's start struct is bump-allocated in the task's own
root arena, not malloc'd -- a heap alloc per spawn would have cost more.

**Two real finds during implementation, both fixed:**

- **sigaltstack is PER-THREAD.** The constructor installs the alternate stack
  on the main thread only; a spawned task's overflow then runs the handler on
  the exhausted stack and dies a SECOND time (SIGSEGV on SIGSEGV, exit 139).
  The trampoline had to install bounds AND altstack on the new thread. The
  pre-fix crash and the post-fix clean exit are both in the record above
  (spawn-deep: 139, then 1).
- **ASan munmaps whatever altstack is current at thread destroy** -- assuming
  it is ASan's own. With our static buffer in place, every spawned task under
  -fsanitize=address reported `failed to deallocate 0x8000 bytes ... Failed to
  munmap` (EINVAL on a non-mmap'd region) and `make conc` failed 13 ASan
  variants. Fix: clear the altstack (SS_DISABLE) after the task's fn returns,
  ASan builds only -- destroy then has nothing to unmap. Native builds skip the
  syscall. (ASan's own stack-overflow detection does NOT fire first for plain
  deep recursion -- our handler does, so the altstack must stay under ASan too.)

**Where it lives:** `runtime/tycho_rt.c` -- the constructor, the handler, and
`tycho_task_trampoline`. The runtime is embedded verbatim into every generated
C file, so no compiler change was needed; `_GNU_SOURCE` was added to the
runtime's feature-macro block (pthread_getattr_np and the REG_* ucontext
indices are `__USE_GNU`-gated; the 13 corelib shims define no GNU-conflicting
names, checked before widening).

**Tests:** `tests/recursion/run.sh` grew a generated-code side: three deep
programs (big-frame `n + f(n-1)`, small-frame tail `f(n-1)`, and a SPAWNED
task's own stack) must compile, then die cleanly -- exit 1-127, empty stdout,
`stack overflow` on stderr -- plus the modestly-nested counterparts that must
run and print the right answer. Inputs are generated in the runner, so no
goldens moved.

**Gates, all green:** `make test` 582/0 (expected count, no drift), `make
ilp32` 582/0 (the -m32 REG_ESP path compiles and the whole suite passes),
`make conc` 38/0 (the trampoline's lane), asan-self 599/0 (the compiler under
ASan+UBSan with the guard live), `make corelib` green and shim-check 12/0 (the
_GNU_SOURCE widening changed how appended shims compile), the four tool lanes
(vm/ar/q/scheme) green, and the extended recursion runner green. `make ci`
once, below.

**What the ci sweep caught (both real, both fixed):**

- **rtparity** — the new `tycho: stack overflow -- recursion too deep`
  diagnostic is new user-visible runtime surface, and that lane refuses it
  silently: added to the oracle (`tests/rtparity/run.py`) with its reason,
  alongside the other runtime traps. New surface is a deliberate act here.
- **spec-check was RED AT HEAD** — the scalar-match plan (commit 1a2041f)
  added ScalarPattern/ScalarElem to Appendix A by hand without teaching §4
  (`docs/spec/02-grammar.md`), so the generator could never reproduce the
  committed appendix. The gate had been failing since that commit, unnoticed
  (the plan repointed its citations but never regenerated the grammar). §4 is
  the single source of truth; the productions now live there and the appendix
  matches again.

### Phase 2 -- correct the two stale docs proven wrong by the program  [DONE 2026-08-03]

Both were recorded as findings while writing the interpreter; both are now
proven false on this tree, and each is a one-line correction, own commit:

1. `docs/architecture.md:52` -- "closures (downward value-capture)" is wrong:
   `tools/tycho-scheme/progs/closures.scm` returns closures that capture and
   two counters from one factory stay independent (`1 2 1`). The spec's
   capture-by-deep-copy (§09-expressions.md) is the truth. **Gate: the two doc
   gates only** (`check_citations.py`, `check_links.sh`).
2. `corelib/json/json.ty:15-17` -- "Objects are parallel key/value arrays
   because tycho's maps don't take a recursive value type here" is wrong: a
   recursive enum as a map value compiles and runs (`[]string: Tree` with
   `TNode(Tree, Tree)`, probed 2026-08-03). The comment documents a design
   choice (parallel arrays) as forced. Correct the wording; the parallel-array
   design itself stays (changing it is not this phase). **Gate: `make corelib`
   (49s -- the letter of the corelib rule, even though a comment edit cannot
   redden a compiled golden), plus the two doc gates.**
