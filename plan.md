# The shim gate, and the cheap end of the friction list

Previous plan complete and archived at
[docs/internals/plan-http-conditional-DONE.md](docs/internals/plan-http-conditional-DONE.md).
`FRICTION.md` was re-scored at `ef06e92`; this plan takes its item 1 and the five
items its own cost ordering put at the top.

## Goal

**Item 1 first, with the gate.** Four corelib shims do not compile standalone
under `-std=c11`. That is not the finding — the finding is that
`corelib/signal/signal_shim.c` was **written this morning** and reproduced a
defect `FRICTION.md` had carried as open through two re-scores, because nothing
in the tree compiles a shim on its own. Nine `#define` lines would fix today's
four and leave the fifth to arrive the same way.

**Then the cheap batch**, which the re-score ordered by cost: a missing FFI spec
paragraph, one diagnostic line, one doc link, ~15 lines of stale comment, and a
naming decision.

Done looks like: a gate that compiles every shim standalone and fails when one
cannot, the four current failures fixed, and five friction items closed.

## Pre-flight

- **Worst case:** a gate that passes because it is not really compiling anything.
  A `-fsyntax-only` run that silently finds no headers, or that picks up the
  project's own include path and so never sees the isolation the gate exists to
  test, would be green and worthless. The gate must be **proven to fail** by
  reverting one fix, and the proof must name the file and the error.
- **Reversibility:** full. Feature-test macros, a new script, a CI step, and
  documentation.
- **Verified — the population is four, not three, and it was measured not
  assumed.** `cc -std=c11 -fsyntax-only -Iruntime <shim>` over all twelve:
  `corelib/tls/tls_shim.c` 9 errors, `corelib/net/net_shim.c` 4,
  `corelib/signal/signal_shim.c` 3, `corelib/image/image_shim.c` 1, and the other
  eight clean. `FRICTION.md`'s item names three — `image_shim.c` had never been
  counted because nobody had compiled it in isolation.
- **Verified — the macro is not the answer everywhere.** Eight shims carry no
  feature-test macro and only four fail, so "add `_POSIX_C_SOURCE` to every
  shim" would be cargo-cult. Four already have one
  (`datetime`, `io`, `os`, `time`) and are the pattern to copy.
- **Verified — the defect recurs, which is why the gate is the deliverable.**
  `FRICTION.md` recorded this as open across two re-scores while a new shim was
  added that reproduced it. A fix without a gate has already been tried.
- **Assuming — `-fsyntax-only -Iruntime` is the right isolation, and I have not
  proven it is what the real build does differently.** The shims are compiled by
  the corelib build with whatever flags that uses; a standalone syntax check may
  be stricter *or* looser than the build in ways that matter. **Risk if wrong:**
  a gate that reddens on something the build tolerates, or that misses something
  the build would catch. **Phase 1 must read how the corelib build actually
  invokes `cc` on a shim and say whether the gate matches it, diverges
  deliberately, or needs different flags.**
- **Assuming — the five cheap items are as small as the re-score's ordering
  says.** That ordering is by *lines*, which is not the same as by effort, and
  every estimate in this session that converted one into the other has been
  wrong. Phase 2 should report what each actually took rather than confirming a
  prediction.

## Phases

- [ ] **Phase 1 — the shim gate, and the four failures it names**
  - Scope: a new gate script, its `Makefile` target, its `scripts/ci.sh` step,
    `CLAUDE.md`'s gate table, and the shims the gate names.
  - **Read how the corelib build compiles a shim first** and settle the
    Pre-flight's flag question before writing the gate. A gate whose flags do not
    correspond to anything real tests nothing.
  - Fix the four by copying the pattern from the four shims that already carry a
    feature-test macro — do not invent a new spelling, and do not add a macro to
    a shim that does not need one.
  - **The gate must be proven to fail**: revert one fix, show it reddening and
    naming that file and its error, restore, show it green. A gate that has only
    ever been seen green is not evidence.
  - Follow the sub-lane numbering convention in `scripts/ci.sh` rather than
    renumbering the thirteen steps, and add the row to `CLAUDE.md`'s table so the
    next agent runs the targeted gate instead of the full sweep.
  - Done when: every shim compiles standalone, the gate is wired and green, the
    deliberate-break proof is recorded, and `make ci` is green with an observed
    exit code.
  - Verify: the gate standalone, the break proof both directions, then `make ci`
    once, waited on in-turn, exit status **observed**.

- [ ] **Phase 2 — the five cheap friction items**
  - Scope: `docs/spec/14-ffi.md`, `src/tychoc.c` (one diagnostic string),
    `docs/README.md`, ~6 files carrying stale comments, and a naming decision in
    `corelib/cli/`.
  - The items, in the re-score's own order:
    - **Item 11** — `docs/spec/14-ffi.md` §24.1.1 documents one of the **two**
      slot arrangements a shim can use. The other is what `iox_stat_mtime` and
      `iox_stat_size` do, and `corelib/io/io.ty` derives it by hand across three
      cross-referencing comment blocks — a sibling's comment doing a spec's job.
      Which slot each half takes is *forced*: a `bytes` payload cannot be an
      `inout` at all, a scalar payload can, so the collision moves to the code
      space. Write that.
    - **Item 2** — `spawn f(x)` as a bare statement is rejected with a message
      that never states the rule: a task handle must be bound so the compiler can
      hang the implicit join on it. One diagnostic string.
    - **Item 3** — `docs/bootstrap.md` is not reachable from `docs/README.md`.
      One link. Note the item also raises the real question behind it — no gate
      checks that a document is reachable — which is **not** in this phase's
      scope; file it if it is not already filed.
    - **Item 5** — ~15 lines of comment across 6 files asserting constraints the
      freeze retirement killed. The re-score located all six.
    - **Item 12** — `cli.has` answers a narrower question than its name and no
      diagnostic says so. A naming decision, not a line: decide, and if the answer
      is "rename", say what breaks.
  - **Report what each actually cost.** The ordering is by lines; this session's
    record on converting that to effort is bad, and the honest number is worth
    more than a confirmed prediction.
  - Done when: all five are closed or explicitly declined with a reason, and
    `FRICTION.md` is struck through per its convention.
  - Verify: `make test` for the diagnostic change, then the three doc gates. Not
    `make ci` — phase 1 owns the sweep.

## Out of scope

- **Item 8**, the `ParallelFor` width slot. The last genuinely hard item on the
  list and its own plan.
- **Items 9 and 10** — a property of this machine (libpng) and a property of this
  file (its own coordinates drifting). The re-score placed both last and neither
  is a line of code.
- **Items 4 and 6** — the `send` collision, gated on a shadowable-builtins
  decision, and the `ends_with` corelib layering rule. Both are decisions with
  consequences beyond a line, and neither is in the cheap batch.
