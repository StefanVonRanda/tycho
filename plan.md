# Three things nothing checks

Previous plan complete and archived at
[docs/internals/plan-json-grammar-DONE.md](docs/internals/plan-json-grammar-DONE.md).

**This plan opens with a backlog audit** rather than with its own phases, because
the backlog had reached nineteen unchecked items across four rotations, three of
its numbers collided with each other, and two of its claims were false. The audit
is below under "Backlog audit, 2026-08-01"; the three phases are what it left at
the top.

## Goal

Close the three latent breakages that no gate in this tree can see:

1. **`str(float)` corrupts its output under a comma-decimal locale.** Not
   hypothetical — measured at `1,5.0`.
2. **The hand-linked shim lists go stale**, because they track direct imports and
   the dependency that breaks them is transitive. Twice now.
3. **A new lane's golden is invisible until someone clones**, because
   `.gitignore` ignores `*.out` broadly and un-ignores per directory.

Each is a break that ships green. Done looks like: all three fixed, and for 2 and
3 a **mechanical gate** so the next one reddens instead of shipping.

## Pre-flight

- **Worst case, phase 1:** the fix formats floats through a path that is correct
  on this host and wrong on another — a locale bug replaced by a portability bug,
  with the same symptom of nobody noticing. The mitigation is the one phase 1 of
  `docs/internals/plan-json-grammar-DONE.md` used and proved: hold `LC_NUMERIC`
  hostile inside the test itself, so the check runs under the broken locale on
  every host rather than depending on the grader's environment.
- **Worst case, phases 2 and 3:** a gate that cannot fail. Both are "walk a list,
  assert each entry" checks, which is exactly the shape that passes vacuously
  when the walk finds nothing. Each must be proved red by breaking a real entry,
  and each must print what it checked, not just a verdict.
- **Reversibility:** total. `runtime/tycho_rt.c`, harness scripts, and a
  `.gitignore` line; no data is at risk and every change reverts cleanly.
- **Verified — the three claims, re-checked against this tree today, not
  inherited:**
  - `runtime/tycho_rt.c@tycho_float_to_str` formats with `%.15g`, which takes its
    separator from `LC_NUMERIC`, then scans the result for `'.'`, `'e'`, `'E'` or
    an inf/nan marker to decide whether to append `".0"`. Under a comma locale
    `1,5` contains none of those, the guard fires, and the output is `1,5.0` —
    which is neither valid Tycho float syntax nor readable by any parser in this
    tree. Measured by `docs/internals/plan-json-grammar-DONE.md` phase 1's test
    hook, not inferred.
  - `examples/fetch/run.sh:60` hard-codes
    `SHIM="corelib/http/http_shim.c corelib/io/io_shim.c corelib/strings/strings_shim.c"`.
    The third entry was added on 2026-08-01 after `make ci` reddened with
    `undefined reference to strx_parse_double`; the second was added in 2026-07
    for the same class of break. `examples/site/run.sh` greps its own source for
    `core:` imports, which finds **direct** imports only — and the 2026-08-01
    break was transitive (`examples/fetch/main.ty` imports `core:json`, which
    imports `core:strings`, which owns the shim), so copying site's loop into
    fetch would have left the lane red.
  - `grep -rn 'error-unmatch' scripts/ Makefile` returns **nothing**: the gate
    described in the backlog for six weeks does not exist.
- **Assuming — to check, not facts:**
  - That the compiler already computes the transitive shim closure on the normal
    build path (which would be why only the hand-linked `--emit-c` legs ever
    break). If it does, phase 2 exposes it; if it does not, phase 2 is bigger than
    one flag and should say so rather than growing quietly.
  - That a `"C"`-locale float formatter is reachable from `runtime/tycho_rt.c`
    without a new dependency. `snprintf_l` is the obvious route and it is a GNU
    extension, the same trap phase 1 of the previous plan hit with `strtod_l`
    (`_GNU_SOURCE`, not `_POSIX_C_SOURCE`).

## Phases

- [ ] **Phase 1 — `str(float)` renders in the `"C"` locale, whatever the ambient one**
  - Scope: `runtime/tycho_rt.c`, and a fixture under `tests/`.
  - Render through a `"C"`-locale conversion rather than the ambient one — the
    same shape as `corelib/strings/strings_shim.c`'s `strtod_l` fix, in the
    opposite direction. Whatever feature-test macro that needs is declared where
    the file already declares its own.
  - The `".0"` guard is a second defect hiding behind the first: it decides
    "does this look like a float?" by scanning for `'.'`. Even with the locale
    fixed, state in a comment what that guard's contract is and why the scan is
    sound once the separator is known.
  - Done when: `str(1.5)` is `1.5`, and `parse_float(str(v))` round-trips, **both
    under a hostile `LC_NUMERIC` held inside the test** and under the default.
    `-0.0`, `inf`, `nan` and a 17-significant-digit value each land on a stated,
    tested outcome.
  - Verify: `make test` — this is `runtime/`, so it is the gate, and the fixture
    count was `passed: 560 failed: 0`. Prove the check can fail by reverting the
    fix and showing the red. Not `make corelib`, which cannot see `runtime/`.

- [ ] **Phase 2 — the compiler prints its shim closure, and the hand lists go**
  - Scope: `src/tychoc.c` (a `--print-shims` or equivalent), `examples/fetch/run.sh`,
    `examples/site/run.sh`.
  - Expose the transitive closure the normal build path already computes, then
    have every `--emit-c` lane ask the compiler for it instead of maintaining a
    list. That retires `examples/fetch/run.sh@SHIM` and `examples/site/run.sh`'s
    direct-import grep at once.
  - **Check the Pre-flight assumption first.** If the compiler does not already
    compute a closure, say so and stop before writing a flag onto something that
    is not there.
  - Done when: both lanes derive their shim list from the compiler, and deleting
    `corelib/strings/strings_shim.c` from any hand list is impossible because no
    hand list remains. Prove it by re-running the exact break: a program whose
    only route to a shim is transitive.
  - Verify: `make fetch`, `sh examples/site/run.sh`, and `make test` if
    `src/tychoc.c` changed — which it will.

- [ ] **Phase 3 — every golden a runner names is tracked, and a gate says so**
  - Scope: a new check (its own script, or a step in an existing one),
    `.gitignore` if a lane turns out to be untracked, `scripts/ci.sh`,
    `CLAUDE.md`'s gate table.
  - Mechanical: for every `*/run.sh` that names a golden path, `git ls-files
    --error-unmatch` it. A lane whose golden is ignored fails on a fresh clone
    with `no golden -- run RECORD=1`, and nothing catches it today —
    `tools/tycho-ar/expected.out` shipped that way once and was caught by hand.
  - **The vacuous-pass risk is the whole difficulty.** The check must print the
    list of goldens it found, and it must be proved red by un-tracking one.
  - Done when: the check runs in CI, prints every golden it checked, exits
    non-zero for an untracked one, and `CLAUDE.md`'s table has its row.
  - Verify: the check itself, both directions; `sh scripts/ci.sh` lists the new
    step; and **`make ci` once, last**, since this phase adds a CI step. Never
    `make ci` as a debugging loop.

## Backlog audit, 2026-08-01

Nineteen unchecked items were carried across four plan rotations. Each was
re-checked against this tree — commands run, not claims inherited. **Two were
false**, and the numbering had collided: `docs/internals/plan-json-grammar-DONE.md`
filed new phases 6, 9 and 10 while carrying forward *different* phases 6, 9 and 10
from `docs/internals/plan-ar-DONE.md`. Six items shared three numbers, and the
collision is why this plan renumbers from 1 rather than continuing the sequence.

### Retired — closed, or delivered as filed

| Was | Claim | Verdict |
|---|---|---|
| ar phase 7 | `corelib/test/result/main.ty` says `return (Err(why), buf)` "still fails" | **FALSE, and corrected 2026-08-01.** `corelib/httpd/httpd.ty@read_request_capped`'s own comment already recorded the opposite as MEASURED — that the rewrite compiles and its typed local is "now a style choice ... not a requirement". Two files, one construct, opposite claims, and the wrong one sat in the test that exists to document the construct. `make corelib` green after the fix |
| ar phase 8 | `README.md` documents `make bootstrap` and `make fixpoint`; neither target exists | **TRUE, and fixed 2026-08-01.** `grep -cE '^(bootstrap\|fixpoint):' Makefile` returned 0. The row is removed; `README.md:101-104` already said the tychoc0 lanes were retired on 2026-07-29, so the table contradicted its own document |
| ar phase 15 | a parameter is borrowed read-only, so streaming state needs `inout` | **Delivered as filed.** It asked for a `FRICTION.md` entry and nothing else; it is at `FRICTION.md:1249`, and the ranked finding above it is the same defect at package scale |
| ar phase 16 | a package cannot mark a top-level function internal | **Recorded, not scheduled.** At `FRICTION.md:1440`. A language feature, not a defect — promoting it is a language plan with its own design question (what does `internal` mean across a package boundary), and nothing in the tree is blocked on it today |
| ar phase 17 | `chr(n)` is the only route from a number to a byte | **Recorded, not scheduled.** At `FRICTION.md:1460`. Its own filing measured the cost as "harmless at 120 bytes per file, and it would not be in a hot loop" |

Retiring the last three does not delete anything: the findings stay in
`FRICTION.md` in full, which is where a reader looks for what the language costs.
What is retired is the expectation that a phase is owed.

### Queued — verified still open, ranked

Ranked by harm × likelihood over cost. Phases 1-3 above are the top three.

| # | Item | Was | Still open? |
|---|---|---|---|
| 1 | `str(float)` locale corruption | json-grammar phase 6 | `runtime/tycho_rt.c@tycho_float_to_str` unchanged — **phase 1 above** |
| 2 | transitive shim closure | json-grammar phase 10 | `examples/fetch/run.sh:60` still a hand list — **phase 2 above** |
| 3 | golden-tracking gate | ar phase 19 | `grep -rn error-unmatch scripts/ Makefile` finds nothing — **phase 3 above** |
| 4 | `decimal.div(a, b, scale, mode)` | ar phase 20 | `grep -n 'fn div' corelib/decimal/decimal.ty` finds nothing. Blocks `select total / count`, the ordinary averaging query, on almost all real data |
| 5 | `core:json` does not validate UTF-8 | json-grammar phase 9 | `corelib/test/json.out`'s `cb ok high` line shows a raw `0xFF` parsing. One decoder closes both this and `esc`'s missing output side |
| 6 | `strings.parse_int` fails open | ar phase 13 | `corelib/strings/strings.ty:71` unchanged — and now **inconsistent with `parse_float` at `:164`**, which is strict and `Result`-returning. Two neighbours, opposite contracts |
| 7 | `io.write_bytes` | ar phase 9 | `corelib/io/io.ty` has `read_bytes` at `:124` and no byte-writing counterpart; writing bytes is `io.write(p, to_str(b))`, correct only by an accident of the runtime's length header |
| 8 | `io.make_dirs` (`mkdir -p`) | ar phase 11 | `corelib/io/io.ty:303` is still one `mkdir(2)`; `tools/tycho-ar/main.ty@mkdir_p` is 18 lines of caller-side chain building |
| 9 | mtime is readable, not writable | ar phase 12 | no `utimensat`/`utimes` in `corelib/io/io_shim.c` or `io.ty`. `tycho-ar` stores an mtime it cannot restore, and `diff -r` does not compare mtimes, so the gate cannot see it |
| 10 | incremental digest | ar phase 14 | no `update`-shaped function in `corelib/sha256`, `md5`, `crypto` or `hash`. The `FRICTION.md:1237` entry is the record; the code gap is real |
| 11 | `eprintln` | ar phase 10 | no hit anywhere in `corelib/`, `src/tychoc.c` or `docs/spec/appendix-d-builtins.md`. A non-fatal warning is inexpressible, so it lands on stdout beside a tool's data |
| 12 | `Makefile:<N>@SKIPPED` citations | ar phase 22 | four refs still at `:366` across `scripts/asan_self.sh` (twice), `scripts/check_citations.py` and `scripts/editors_check.sh`. Six repointing edits over two phases, zero information carried |
| 13 | the `image` shim is compiled by nothing here | ar phase 5 | `scripts/shim_check.sh:43` skips it for missing libpng, as does `make corelib`. Only matters on a host that has libpng |
| 14 | no document-reachability gate | ar phase 6 | no such check in `scripts/`. Would have stayed green through `docs/bootstrap.md`'s entire outage |

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. A language
  change and its own plan.
- **Items 4-14 above.** Queued, ranked, and deliberately not started here — a
  plan that claims fourteen phases is a plan nobody finishes.
