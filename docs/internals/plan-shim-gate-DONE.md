# The shim gate, and the cheap end of the friction list

Previous plan complete and archived at
[plan-http-conditional-DONE.md](plan-http-conditional-DONE.md).
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

- [x] **Phase 1 — the shim gate, and the four failures it names**
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

  **Evidence.**

  Gate is `scripts/shim_check.sh`, `make shim-check`, `scripts/ci.sh` step
  `[3d/13]`, row added to `CLAUDE.md`'s two tables. Fixed by copying
  `corelib/io/io_shim.c`'s `_DEFAULT_SOURCE` pattern:
  `corelib/net/net_shim.c`, `corelib/signal/signal_shim.c`,
  `corelib/tls/tls_shim.c` — three `#ifndef`/`#define`/`#endif` blocks, not nine
  `#define` lines, and **three shims, not four**.

  **The flag question the Pre-flight flagged, settled.** The real build never
  compiles a shim alone: `src/tychoc.c@add_shim` appends it to the generated `.c`
  on one command, `cc -O3 -fwrapv -pthread -o <out> <gen.c> <shims...> -lm …
  <pkgdeps>`. Three deliberate divergences, each recorded in the script's header:
  `-std=c11` **added** (the build passes no `-std`, so cc's default gnu dialect
  leaves `__STRICT_ANSI__` undefined and glibc turns `_DEFAULT_SOURCE` on by
  itself — that is exactly why the defect is invisible); `-fsyntax-only`
  replacing `-O3 -fwrapv -o`; and `-Iruntime` **dropped**, because
  `grep '#include "' corelib/*/*_shim.c` is empty — no shim includes a project
  header, so the flag tests nothing and would undercut the isolation being
  tested. The pkg-config cflags from `<dir>/deps` are **kept**, and a shim whose
  package is absent is skipped, matching `corelib/run.sh:39`.

  **Two things the brief got wrong, both found by measuring.**

  1. **`corelib/image/image_shim.c` is not a defect and needs no macro.** Its one
     "error" is `png.h: No such file or directory` — and `pkg-config --exists
     libpng` fails on this box. It is FRICTION item 9, the libpng property of
     this machine that this plan puts out of scope, reaching the Pre-flight
     through a command that omitted the build's own `--cflags`. The population of
     real macro defects is three, which is what `FRICTION.md` said before the
     Pre-flight "corrected" it to four. The gate skips it rather than counting it:
     `skip corelib/image/image_shim.c (missing dependency: libpng)`.

  2. **`-pthread` silently disarmed the gate, and this is the finding worth
     keeping.** The first version kept `-pthread` for build fidelity. It reported
     `11 ok, 1 skipped, 0 failed` — with the `signal` fix reverted. `-pthread`
     defines `_REENTRANT`, and glibc's system `<features.h>` (the guard block
     comment "compatibility synonyms for _POSIX_C_SOURCE=199506L", lines 332-343
     of that header as installed here) raises `_POSIX_C_SOURCE` to `199506L`,
     handing back the declarations `-std=c11` had just withheld.
     Measured on the unfixed shim: `cc -std=c11 -fsyntax-only` → 3 errors,
     `cc -std=c11 -pthread -fsyntax-only` → 0. So the Pre-flight's "worst case"
     — a gate green because it is not really compiling anything — was **built,
     and caught only because the break proof was mandatory**. A gate seen only
     green would have shipped it. `-pthread` is now dropped and the reason is in
     the script header, because it is the one flag that decides whether this gate
     exists.

  **Verification, each one command, foreground.**

  - Gate standalone: `sh scripts/shim_check.sh` → `11 ok, 1 skipped, 0 failed`,
    `EXIT=0`. `make -s shim-check` → same, `MAKE_EXIT=0`.
  - Break proof, red: `git stash push corelib/signal/signal_shim.c` then the
    gate → `FAIL corelib/signal/signal_shim.c` printing its three errors
    (`storage size of 'sa' isn't known`, implicit `sigemptyset`, implicit
    `sigaction`), `shim-check: 10 ok, 1 skipped, 1 failed`, `EXIT=1`.
  - Break proof, green: `git stash pop` then the gate → `11 ok, 1 skipped, 0
    failed`, `EXIT=0`.
  - Doc gates after the citation fallout below: `citation check: ok`,
    `link check: ok`.
  - `make ci`: run once, last, **observed `CI_EXIT=0`**. `make test` held at
    `passed: 560   failed: 0`, and the new lane reported inside the sweep:
    `>>> [3d/13] make shim-check` → `11 ok, 1 skipped, 0 failed`. One caveat
    recorded rather than hidden: the sweep was launched before a late
    **comment-only** reword of `scripts/shim_check.sh`'s header (an absolute
    system path in a comment, which `check_citations.py` rejects in Markdown).
    The `cc` invocation and the skip logic were untouched, and the gate was
    re-run green after the edit.

  **Citation fallout, in scope and repaired.** The three-line macro block shifted
  `corelib/signal/signal_shim.c` by +3 and the `Makefile` target shifted
  `SKIPPED` by +9, staling 24 anchored refs across six files. These are live
  pointers, deliberately anchored so "the citation gate re-checks the mapping on
  every future edit to that file"
  (`docs/internals/plan-signals-DONE.md:696-697`) — not record lines, so the
  repair is to repoint, not to drop anchors. Each target line was asserted to
  contain its anchor token before rewriting. Also repointed the bare range
  `corelib/signal/signal_shim.c:204-210`, which the gate cannot check and would
  have silently gone stale.

- [x] **Phase 2 — the five cheap friction items**
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

  **Evidence.**

  All five closed; `FRICTION.md` items 1, 2, 3, 5, 11, 12 struck (item 1 was
  phase 1's and was still unstruck). Three new items filed: **13** the
  reachability gate item 3 named, **14** a disproved claim in
  `corelib/test/result/main.ty`, **15** two `make` targets `README.md` documents
  that the `Makefile` does not have.

  **What each actually cost, since the brief asked for the number rather than the
  prediction.** The re-score ordered these by lines and the order was wrong twice.

  - **Item 11 (spec paragraph)** — ran to estimate. Wrote the mirror arrangement
    into `docs/spec/14-ffi.md` §24.1.1 and repointed `corelib/io/io.ty`'s three
    comment blocks at it. One thing the item did not predict: the old provenance
    block cited `gen_extern_proto` as `src/tychoc.c:10920-10949` and the function
    is at `src/tychoc.c@gen_extern_proto`, 43 lines below — a **bare range, so no
    gate could see it**. Rewrote the block in `path@SYMBOL` form, which is why it
    cannot go stale that way again.
  - **Item 2 (one diagnostic line)** — **by far the most expensive of the five,
    and the ordering had it second-cheapest.** The line itself was minutes:
    `E_SPAWN` tested ahead of the generic bare-expression `die_at`, reproduced
    before and after on the same scratch program. No `tests/diag/` fixture pinned
    the old text (grepped; the string lives only in `docs/internals/`), so the
    corpus count is untouched. The cost was the **7-line insertion at
    `src/tychoc.c`'s line 3574 staling 80 anchored citations across 16 files** —
    ~10x the +3 shift phase 1 absorbed, because the insertion point is 3,000 lines
    higher and everything below it is downstream. Repaired mechanically against a
    real `difflib` old→new line map, each target asserted to contain its anchor
    token on exactly one line before rewriting. **21 of the 80 are record lines**
    (18 before/after table rows in
    `docs/internals/plan-postfreeze-rawstring-DONE.md`, plus three prose repair-log
    lines a shape detector misses — "cited X … Repaired to Y") and took
    `CLAUDE.md`'s "drop the anchor, keep the number" rather than a repoint — the
    first time that rule has been exercised at scale.
  - **Item 3 (one link)** — one link, as advertised, plus a **seventh site of item
    5's class found in the same file**: `docs/README.md`'s Contributing paragraph
    still promised "every feature must work in *both* compilers, or the fixpoint
    goes red" while the `CONTRIBUTING.md` it points at already says those gates are
    gone. Fixed with it. The reachability question is filed as item 13, not solved.
  - **Item 5 (~15 lines across 6 files)** — cost **more** than item 11 despite the
    ordering. Rewriting an expired reason means first establishing what the
    surviving reason is, and for `corelib/httpd/httpd.ty@read_request_capped` that
    took a compile probe: patched the tail to `return (Err(why), buf)` /
    `return (parse_request(buf), buf)`, compiled `corelib/test/httpd/main.ty`
    **clean**, reverted. That **disproves** `corelib/test/result/main.ty`'s claim
    that the rewrite "still fails" — filed as item 14 rather than absorbed.
  - **Item 12 (a decision)** — the **cheapest**, and the ordering had it eighth.
    Closed as working-as-intended. A rename to `has_value` is cheap (three call
    sites in `corelib/test/cli/main.ty`, one line of `docs/guides/corelib.md`) but
    moves the guess instead of removing it — the caller still picks between
    `has_value` and `flag`, same silent false, same absent diagnostic. So the fix
    went into `corelib/cli/cli.ty@has`'s doc comment: name the failure mode, name
    `flag`, and give `cli.has(c, k) || cli.flag(c, k)` as the call-site spelling.

  **Verification, one command each, foreground.**

  - `python3 scripts/check_citations.py` → `citation check: ok` (178 anchored,
    124 `path@SYMBOL`). It **reddened first**, with 80 stale, which is how the
    blast radius above was measured rather than guessed.
  - `sh scripts/check_links.sh` → `link check: ok (139 markdown files, no dead
    relative links)`.
  - `sh scripts/spec_check.sh` → `9 runnable example(s), all pass`,
    `Appendix A grammar matches §3/§4 (ok)`,
    `all Appendix E fixture citations resolve (ok)`.
  - `make test` → `passed: 560   failed: 0`, `all green`, observed `TEST_EXIT=0`.
    Held at 560, which is the number that would have moved had the new `E_SPAWN`
    arm rejected something the corpus relies on.
  - Not run, deliberately: `make ci` (phase 1 spent it) and `make shim-check` /
    `make corelib` (no shim and no corelib *code* changed — the corelib edits are
    comments, and the five entry points that were touched were each compiled
    individually as a sanity check: `corelib/test/cli`, `corelib/test/result`,
    `corelib/test/net`, `corelib/test/httpd`, `examples/corelib/httpd` all build).
    `tools/lsp.ty` does not link standalone — **verified identical at HEAD**, a
    pre-existing missing-shim link error, not this phase's.

- [ ] **Phase 3 — the shim gate's blind spot: a skipped shim is never compiled
      at all** *(filed by phase 1, out of its scope)*
  - `make shim-check` skips a shim whose `deps` package is absent, which is the
    right call for `make ci` staying green on a host without the library — it is
    `corelib/run.sh:39`'s own rule. But the consequence is that on this box
    `corelib/image/image_shim.c` is compiled by **nothing**: `make corelib` skips
    its module for the same missing libpng, and now so does the shim gate. A real
    defect in it — the feature-test-macro class this plan exists to catch, or any
    other — is invisible here and would surface only on a host with libpng.
  - That is not a phase-1 bug; the skip is correct and the alternative (failing
    on a missing optional dependency) is worse. The open question is whether the
    tree wants a way to know which shims went uncompiled, e.g. the gate's summary
    line naming the skipped set as a warning rather than a quiet `skip`, or a CI
    lane on a host with the optional dependencies installed.
  - Related and cheaper: the citation gate cannot check a **bare range** into a
    source file, so `corelib/signal/signal_shim.c:204-210` and its kind go stale
    silently. Phase 1 repointed that one by hand only because it happened to be
    reading the file. Worth knowing whether that class is large before deciding
    it needs anything.

## Status — PLAN COMPLETE

Both phases are done and committed. Phase 3 below is **filed, not planned** — it
was raised by phase 1 as out of its own scope and is left unchecked deliberately,
for whoever picks up the shim gate's blind spot. It is not work this plan owes.

`FRICTION.md`'s open list went from **eleven entries to eight**: six struck (1, 2,
3, 5, 11, 12) and three filed (13, 14, 15). **None of the eight survivors is a line
of code** — 4 and 6 want decisions, 8 wants a design, 9 and 10 are properties of
the machine and of the file, 13 needs a definition of "reachable" that the cheap
gate would not satisfy, and 14 and 15 are stale claims that want re-deriving
rather than deleting. The cheap end of the list is spent.

## Out of scope

- **Item 8**, the `ParallelFor` width slot. The last genuinely hard item on the
  list and its own plan.
- **Items 9 and 10** — a property of this machine (libpng) and a property of this
  file (its own coordinates drifting). The re-score placed both last and neither
  is a line of code.
- **Items 4 and 6** — the `send` collision, gated on a shadowable-builtins
  decision, and the `ends_with` corelib layering rule. Both are decisions with
  consequences beyond a line, and neither is in the cheap batch.
