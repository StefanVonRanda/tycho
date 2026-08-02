# Four of seven

Previous plan complete and archived at
[docs/internals/plan-four-found-DONE.md](docs/internals/plan-four-found-DONE.md).

That plan filed seven follow-up phases. **Each was re-checked against this tree
before this plan was written** — commands run, not filings trusted — and three
are retired without work. The audit is below; the four that survived are the
phases.

## Goal

1. **An overflowing float literal emits `inf` into generated C**, which is not a
   C identifier, so the user gets a `cc` error instead of a Tycho diagnostic.
2. **`weblog` and `webserver` have goldens no gate ever compares.** The previous
   plan spent a phase making those goldens visible; nothing reads them.
3. **Nothing in CI compiles under a hostile `LC_NUMERIC`**, so the two locale
   fixes of the last two plans can rot without a symptom.
4. **A citation in `scripts/asan_self.sh` points at the wrong recipe.**

## Pre-flight

- **Worst case, phase 2:** a new golden lane is added that compares output which
  is not deterministic — a timestamp, a port number, a path — and the gate goes
  red on an innocent machine or, worse, gets `RECORD=1`-ed into meaninglessness.
  Read what those two programs print before wiring anything, and if either is
  non-deterministic say so and gate the deterministic part only.
- **Worst case, phase 3:** a CI lane that costs nineteen minutes forever and
  cannot fail. `docs/internals/plan-four-found-DONE.md` phase 1 measured that
  `LC_ALL=<comma locale> ./tychoc` is **inert** — a C program stays in the `"C"`
  locale until something calls `setlocale`, so the obvious spelling of this lane
  proves nothing. The lane must use the `LD_PRELOAD` constructor trick that
  phase proved, and it must be shown red against a reverted fix.
- **Reversibility:** total. Compiler source, a CI step, a runner and a comment.
- **Verified — each of the four, re-checked today, with the command:**
  - `./tychoc` on a program containing `1e400`, then `grep -n inf` on the emitted
    C, gives `double h_x = inf;`. `inf` is not a C keyword or a standard macro.
  - `grep -n 'weblog\|webserver' scripts/entrypoints.sh` puts both in `MUST` at
    `scripts/entrypoints.sh:40`, and that script is **compile-only**;
    `scripts/ci.sh:104` says in as many words that the examples with their own
    runner "were outside this". Both `examples/weblog/run.sh` and
    `examples/webserver/run.sh` exist and are run by nothing in `make ci`.
  - `grep -n 'LC_ALL\|LC_NUMERIC\|LD_PRELOAD' scripts/ci.sh Makefile` returns
    **nothing**.
  - `scripts/asan_self.sh:10` cites `Makefile:103-106` as "documents that lane";
    `sed -n '103,106p' Makefile` is the **wiki sync recipe**
    (`python3 scripts/sync-wiki.py`), not the ASan lane.
- **Assuming — to check, not fact:** that `examples/weblog` and
  `examples/webserver` produce deterministic output at all. Their goldens exist
  and were being protected, which suggests their runners compare them locally;
  phase 2 confirms that before wiring, and if a runner does not compare its own
  golden, that is the finding.

## Phases

- [ ] **Phase 1 — the stale citation, and nothing else**
  - `scripts/asan_self.sh:10` says `Makefile:103-106     documents that lane`.
    Those lines are `make wiki`'s recipe. Find where the ASan lane is actually
    documented in the `Makefile` and cite that, in whichever form
    `CLAUDE.md`'s citation section now sanctions — the previous plan's phase 4
    widened `@SYMBOL` to any target that **has a name of its own**, and added
    `Makefile` to the Markdown side's accepted paths.
  - A bare range is the honest form when there is no single subject token
    (`CLAUDE.md` says so explicitly, and says not to invent one). Decide which
    applies here and say why.
  - Done when: the citation names the lane it claims to, and the gate is proved
    unable to pass silently — break the thing it points at, show the red,
    restore, show the green.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.
    **Not** `sh scripts/asan_self.sh` — this changes a `#`-led comment line and
    nothing that script executes. Say so rather than running it.

- [ ] **Phase 2 — an overflowing float literal is a Tycho diagnostic, not a `cc` error**
  - `1e400` reaches codegen as an infinity and is emitted as the bare token
    `inf`, so the generated C fails to compile with `'inf' undeclared`. The user
    wrote Tycho and got an error about C.
  - Decide, and write the reason down: is an out-of-range float literal a
    **compile error in Tycho** (with the file, line and the literal named), or
    is it a legal value that must be emitted portably (`HUGE_VAL`, `INFINITY`
    from `<math.h>`, or a `1.0/0.0` construction)? Both are defensible. What is
    not defensible is the current state, where it is accepted and then explodes
    one layer down.
  - Check the same question for the underflow side (`1e-400`) and for `nan` if
    any path can produce one, rather than fixing the one case in the filing.
  - Scope: `src/tychoc.c`, and a fixture under `tests/`. If the answer is a
    diagnostic, the fixture is a `.ty` that must fail to compile — read how the
    corpus already expresses that (`tests/` has negative fixtures; find their
    convention rather than inventing one).
  - Done when: `1e400` produces a Tycho-level message naming the literal, or
    emits C that compiles and behaves; and a fixture holds whichever it is.
  - Verify: `make`, then `make test` (~8 min, timeout at least 900000 ms). It
    was `passed: 562 failed: 0`; your fixture should make it 563, and a number
    that moves DOWN is a silent loss. Prove the check can fail by reverting.

- [ ] **Phase 3 — the two orphan lanes get compared, not just compiled**
  - `examples/weblog/expected.out` and `examples/webserver/expected.out` are
    tracked, and as of the previous plan they are un-ignored and gate-protected —
    and **nothing in `make ci` compares them**. `scripts/entrypoints.sh` proves
    only that the programs compile.
  - **Read both runners first** and report what they do: if a runner already
    compares its golden, this phase is only about wiring it into `make ci`; if it
    does not, the runner needs the comparison too. Do not assume which.
  - Determinism is the risk. Read what each program prints. A port number, a
    timestamp, a temp path or an iteration order in the output means the golden
    is a record of the minute it was recorded — gate the deterministic part and
    say plainly what is excluded, in the runner, where the next reader meets it.
  - Scope: `examples/weblog/run.sh`, `examples/webserver/run.sh`, `Makefile`,
    `scripts/ci.sh`, `CLAUDE.md`'s gate table.
  - Done when: both goldens are compared by something `make ci` runs, each lane
    is proved red by changing its program's output and green after reverting, and
    `CLAUDE.md`'s table names the new lane and its measured cost.
  - Verify: the new lane both directions, `sh scripts/entrypoints.sh`, and
    `make goldens-check` (the new lane must not break the golden inventory).
    Hold `make ci` for phase 4, which closes the chain.

- [ ] **Phase 4 — a CI lane that compiles under a hostile locale**
  - Two plans have now fixed a locale defect — `runtime/tycho_rt.c@tycho_float_to_str`
    and both float-literal sites in `src/tychoc.c` — and **nothing in CI would
    notice either regressing**, because the defect is latent until a linked
    library calls `setlocale`.
  - The lane must use the mechanism `docs/internals/plan-four-found-DONE.md`
    phase 1 proved: a small `LD_PRELOAD` shared object whose constructor calls
    `setlocale(LC_ALL, "")`, with `LC_ALL` set to a comma-decimal locale. The
    obvious spelling without the preload is **inert** and was measured so.
  - **The lane must skip, loudly and by name, on a host with no comma-decimal
    locale** — `locale -a | grep` decides, and a silent skip here is the vacuous
    pass this chain has hit five times. It must also not leave a `.so` in the
    tree; build it in a temp dir.
  - Scope: a new script under `scripts/`, `Makefile`, `scripts/ci.sh`,
    `CLAUDE.md`'s gate table.
  - Done when: the lane compiles and runs at least the two existing locale
    fixtures under the hostile locale, is proved red by reverting either fix, and
    prints its skip reason when it cannot run.
  - Verify: the lane both directions, then **`make ci` once, last** (~19 min,
    timeout at least 1500000 ms) — this phase adds a CI step and closes the
    chain, the two conditions that earn the sweep. If it reddens, fix with the
    failing step's own gate and re-run that gate, never `make ci` as a loop.

## Audit of the seven filed phases, 2026-08-02

Each was re-checked against the tree with a command before this plan was
written. Four survived and are the phases above. Three are retired:

| Filed as | Claim | Verdict |
|---|---|---|
| phase 7 | `examples/sqlite/run.sh`, `bench/dbquery/run.sh` and `bench/fair_rest.sh` hand-name `sqlite3`, which `--print-deps` cannot see | **Retired — not a defect.** Confirmed they hand-name it (`examples/sqlite/run.sh:29-30`), and confirmed the filing's own reason: the dependency arrives through `extern "Lib"`, not a `deps` file, so `--print-deps` correctly prints nothing. The hand-naming is therefore **necessary and correct**, not stale-prone in the way the shim lists were. Closing it means teaching the compiler to surface `extern "Lib"` dependencies — a design change with no demonstrated breakage behind it. Ladder rung 1: it does not need to exist |
| phase 10 | the citation gate's docstring still spells a live line ref as a grammar example | **Retired — already done.** `grep -n 'Makefile:[0-9]' scripts/check_citations.py` returns nothing. The previous plan's phase 4 retired that example when it converted the refs; the follow-up was filed against a state that no longer existed by the time the commit landed |
| phase 11 | 67 Markdown `Makefile:N` refs are checked by nothing | **Retired — against stated policy, and it would redden frozen archives.** The count is 96 occurrences today, and the anchored ones live in `docs/internals/plan-loops-cleanup-DONE.md` and `docs/internals/plan-three-gates-DONE.md` — frozen records whose numbers are *data*. `CLAUDE.md` is explicit on both halves: the bare-ref count "is not a backlog", the reachable ones "stay bare", and a widening that puts gate pressure on a record line is the failure mode it warns about by name (it happened once already when `SRC_PREFIX` widened). Enforcing this would produce exactly the hand sweep this repo has declined three times with measurements each time |

Retiring these deletes nothing. Their filings stay in
`docs/internals/plan-four-found-DONE.md` with the reasoning that produced them.

## Carried forward

The eleven backlog items ranked by the audit in
`docs/internals/plan-three-gates-DONE.md`, unchanged and unstarted:
`decimal.div`, JSON UTF-8 validation, `strings.parse_int` failing open beside a
strict `parse_float`, `io.write_bytes`, `io.make_dirs`, writable mtime, an
incremental digest, `eprintln`, the `image` shim nothing compiles here, a
document-reachability gate, and the `ParallelFor` width slot.
