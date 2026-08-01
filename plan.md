# `core:json` cannot report a failure — fix that

Previous plan complete and archived at
[docs/internals/plan-q-DONE.md](docs/internals/plan-q-DONE.md).
Its unclosed discoveries carry forward at the bottom.

This plan is phase 21 of that one, promoted to a plan of its own because the
user asked for it directly.

## Goal

`corelib/json/json.ty` mis-handles input it cannot represent, three ways, and
cannot report any of them. Done looks like: no input can make the parser loop
without advancing, no input is silently reinterpreted into a different document,
every caller can ask whether a parse succeeded, and `make test` proves all of it
with tests that fail against today's parser.

## Pre-flight

- **Worst case, and it is the reason this is being fixed:** the failure with no
  symptom. `json.parse('[{"a":1.5}]')` **exits 0 today** having parsed `.5}]` as
  the next key — a fabricated column, in a document the caller believes it read.
  A program that reports a wrong number confidently is worse than one that
  crashes. Every phase's tests must therefore assert the **whole document**, not
  that a call returned.
- **Reversibility:** total, and there is no data at risk — this is a parser in a
  library, no file is written by anything in scope, and every change is one
  `git revert` away.
- **Verified — the three failures, traced in the source, not assumed.** Read
  `corelib/json/json.ty` before touching it; the trace below is where to start,
  not a substitute for reading it.
  - **The spin.** `corelib/json/json.ty@parse_array` loops on `pos < n and
    s[pos] != 93` and advances only on `,` or `]`. `corelib/json/json.ty@parse_value`
    falls through to `corelib/json/json.ty@parse_number` for any character that
    is not `{ [ " t f n`, and `parse_number` consumes nothing when the character
    is not `-` or a digit. So on `[1.5]` the parser reaches `.`, consumes
    nothing, pushes `JNum(0)`, and does it again forever. **Five bytes of input
    exhaust memory.** `corelib/json/json.ty@parse_object` has the same shape.
  - **The silent truncation.** `parse_number` stops at the first non-digit, so
    `json.parse("1.5")` returns `JNum(1)` at exit 0.
  - **The fabricated key.** `parse_object` does `pos = pos + 1  # skip :`
    unconditionally and calls `parse_string` on whatever is there, so a value it
    cannot parse leaves the cursor mid-token and the next loop iteration reads
    the remainder as a key.
  - **The root cause is the missing error channel, not the missing float path.**
    `corelib/json/json.ty@parse` returns `Json`, not `Result`. The header's
    promise that truncated input "fails closed to JNull"
    (`corelib/json/json.ty:12-13`) is not closed when the failure mode is an OOM
    or an invented column.
- **Verified — the blast radius.** `grep -rl 'core:json' --include='*.ty' .`
  (excluding the frozen `compiler/tychoc0.ty`) returns exactly six consumers:
  `examples/site/main.ty`, `examples/fetch/main.ty`,
  `examples/corelib/json/main.ty`, `bench/json/json.ty`,
  `tools/tycho-q/main.ty`, `corelib/test/json/main.ty`. Outside
  `corelib/json/json.ty` the only mention of a `Json` variant anywhere in the
  tree is a constructor call in `examples/corelib/json/main.ty:16` — **no
  external `match` over `Json` exists**, so an added variant would not break an
  exhaustive match anywhere. `examples/json.ty` looks like a counter-example and
  is not: it is a standalone copy with its own `enum Json` and does not import
  the package.
- **Assuming — written as things to check, not facts:**
  - That `Result(Json, E)` is expressible in a corelib package and crosses the
    package boundary to an importing program. `corelib/result/` exists and
    `core:net` was converted to `Option`/`Result`
    (`docs/internals/plan-option-result-DONE.md`), so the risk is low — but
    `docs/internals/plan-q-DONE.md` phase 1 recorded that `Result(void, E)` is
    **not** expressible, so the error type must carry a payload on both arms.
  - That the fix does not need a float path. `1.5` becoming a hard error is a
    behaviour change from `JNum(1)`, and it is the *correct* one — the package
    header already declares floats out of scope, so this makes a declared scope
    enforced instead of silently violated. A real float path is filed as
    phase 3 and deliberately not run here.
- **Deliberately not in this plan:** adding a float variant to `Json` (phase 3,
  filed), `decimal.div` (carried-forward phase 20), and any performance work —
  `bench/json/json.ty` exists and a slowdown there is a finding, not a licence
  to optimise inside this plan.

## Phases

- [ ] **Phase 1 — a parser that cannot spin, and an error channel**
  - Scope: `corelib/json/json.ty`, `corelib/test/json/main.ty`,
    `corelib/test/json.out`. No caller changes in this phase.
  - **Write the failing tests first.** Before changing the parser, add the three
    cases to `corelib/test/json/main.ty` and record what today's parser does with
    each — `1.5`, `[{"a":1.5}]`, and (with care, since it exhausts memory) `[1.5]`.
    Paste the observed before-behaviour into the evidence. A test that passes
    against the broken parser is not a test.
  - The fix has two independent halves and both are required:
    1. **No loop may iterate without the cursor advancing.** Guard it
       structurally in `parse_array` and `parse_object` — if a nested parse
       leaves `pos` where it found it, that is a malformed document and the
       parser stops. This half alone ends the OOM, and it must hold for *every*
       character, not only `.`; enumerate the classes in a comment.
    2. **A fallible entry point.** Add a `Result`-returning parse that reports
       *what* was wrong and *where* (byte offset). Keep the existing
       `corelib/json/json.ty@parse` signature working for the six consumers —
       it becomes the wrapper that discards the error — so this phase changes no
       caller. Say in the header which one a new caller should use and why.
  - Fail closed, per `parser-safety`: an input the parser cannot represent is an
    error naming the byte offset, never a guessed value. `1.5` errors; it does
    not become `1`. Enumerate every input shape the parser accepts in a comment
    block with a real example of each, and mark what it still cannot do with a
    `# gap:` line naming the ceiling and the way out.
  - Done when: the three cases are tests that fail before the fix and pass after
    (show both runs); no input in the test corpus can make the parser allocate
    unboundedly; `parse` still returns `Json` and the six consumers still
    compile untouched; and the new fallible entry point reports a byte offset
    that is checked against an expected value, not merely non-zero.
  - Verify: **`make test`** — this is a corelib change, so it is the gate. State
    the fixture count before and after; it was 560 as of `CLAUDE.md`'s table, and
    a number that moves down is a silent loss to investigate, not to accept. Do
    NOT run `make ci`, `make q-check`, `make ar-check` or the tools gates.

- [ ] **Phase 2 — the callers stop working around it**
  - Scope: `tools/tycho-q/main.ty`, `tools/tycho-q/expected.out`, and whichever
    of the other five consumers phase 1's evidence shows needs a change.
  - `tools/tycho-q/main.ty@json_guard` exists only because the corelib could not
    report a failure: it validates the raw bytes before handing them over, which
    is most of a second parser. Replace it with the fallible entry point, keeping
    `tycho-q`'s user-visible behaviour identical — a bad JSON input must still
    exit non-zero with a message on stderr and **nothing on stdout**. Do not
    delete the comment block explaining why the guard existed; rewrite it to say
    what replaced it, so the reasoning survives the code.
  - Re-record `tools/tycho-q/expected.out` only if the transcript genuinely
    moved, and if it does, say in the evidence **which line moved and why**. A
    silently re-recorded golden is how a regression ships.
  - Done when: `tycho-q`'s ten failure legs still exit non-zero with empty
    stdout; the two `core:json` guard legs in `tools/tycho-q/run.sh` still fire,
    now on the corelib's error rather than the local guard; and no consumer
    still carries a hand-written pre-validator.
  - Verify: **`make q-check`** (~3s, and it is the only lane that runs anything
    under `tools/tycho-q/`), then **`make test`** for the corelib consumers under
    `examples/`. Then `make ci` **once**, last, as the closing sweep of this
    chain — and if it reddens, fix with the failing step's own gate and re-run
    that gate, never `make ci` as a loop.

- [ ] **Phase 3 — a float path for `Json` (FILED, NOT PART OF THIS PLAN)**
  - After phases 1 and 2 a JSON document containing `1.5` is a hard error with a
    byte offset. That is correct — the package header declares floats out of
    scope — but it is still a JSON parser that cannot read ordinary JSON, and
    every real feed has a float in it somewhere.
  - The Pre-flight measured what this would cost: **no `match` over `Json` exists
    outside `corelib/json/json.ty`**, so adding a variant breaks no external
    exhaustive match. The real question is which numeric tower it lands in —
    `float` loses exactness, and `core:decimal` is the only exact tower here but
    has no `div` (carried-forward phase 20), so the two interact and should be
    decided together.
  - Not started. Promoting it is a decision for the user, not for a phase agent.

## Carried forward

Unclosed discoveries from `docs/internals/plan-q-DONE.md` and its predecessors,
preserved with their original numbering. None is part of this plan's completion.
Its phase 21 is not listed: it is this plan.

- [ ] **Phase 5** — a skipped shim is compiled by nothing on this host: `make
      corelib` and `scripts/shim_check.sh` both skip `image` for the same missing
      libpng, so a real defect there is invisible here.
- [ ] **Phase 6** — nothing checks that a document is *reachable*; the cheap
      version of that gate would have stayed green through `docs/bootstrap.md`'s
      entire outage.
- [ ] **Phase 7** — `corelib/test/result/main.ty` claims a construct "still
      fails"; a compile probe shows it builds clean. Disproved, not yet corrected.
- [ ] **Phase 8** — `README.md:223` documents `make bootstrap` and `make
      fixpoint`; neither target exists in the `Makefile`.
- [ ] **Phase 9** — `io.read_bytes` has no counterpart: writing bytes is
      `io.write(p, to_str(b))`, correct only because `tycho_write_file` is
      length-header-driven, which the `string` signature does not say.
- [ ] **Phase 10** — there is no `eprintln`: `die` is the only route to stderr
      and it exits. A non-fatal warning is inexpressible, so it lands on stdout
      alongside a tool's actual output.
- [ ] **Phase 11** — no `mkdir -p` in `core:io`. `io.make_dir` is one `mkdir(2)`
      (`corelib/io/io_shim.c@iox_make_dir`); every caller that writes into a tree
      it does not own has to build the component chain itself, as
      `tools/tycho-ar/main.ty@mkdir_p` does in 18 lines.
- [ ] **Phase 12** — mtime is readable and not writable. `io.mtime` exists;
      nothing in `corelib/io/io.ty` or `corelib/io/io_shim.c` sets one, so
      `tycho-ar` stores a faithful mtime it cannot restore.
- [ ] **Phase 13** — `strings.parse_int` fails open: `""` and a leading
      non-digit both return 0, and it stops silently at the first non-digit
      (`corelib/strings/strings.ty@parse_int`). There is no strict or
      `Result`-returning counterpart in `core:strings`. **Phase 1 of this plan
      writes the same strict-vs-lenient pair for `core:json`** and should say so
      where the two now differ.
- [ ] **Phase 14** — no incremental digest anywhere in the corelib.
      `core:sha256` is `digest(msg)` / `hex(msg)` over a whole `string`, so
      hashing a large file in bounded memory means writing your own, as
      `tools/tycho-ar/main.ty@sha_feed` does.
- [ ] **Phase 15** — a Tycho parameter is borrowed read-only and `y := a` is a
      copy, so a streaming state cannot be threaded through calls without
      `inout`. A language default steering library shape; a `FRICTION.md` entry,
      not a code change.
- [ ] **Phase 16** — a package cannot mark a top-level function internal. Every
      `fn` in `corelib/sha256/sha256.ty` is reachable as `sha256.<name>` from an
      importing program, so every corelib helper is public API by accident.
- [ ] **Phase 17** — `chr(n)` is the only route from a number to a byte; there is
      no `bytes` builder from integers, only `to_bytes(string)`. Pairs with
      phase 9.
- [ ] **Phase 19** — a new lane's golden is invisible until someone clones.
      `.gitignore` ignores `*.out` broadly and un-ignores it per directory. The
      cheap gate is mechanical: for every `*/run.sh` naming a golden path,
      `git ls-files --error-unmatch` it. (`tools/tycho-q` closed its own case;
      the gate that would stop the next one still does not exist.)
- [ ] **Phase 20** — `core:decimal` has no `div`. `tycho-q` refuses `/` whenever
      the answer is not exact, so `select total / count` — the ordinary
      averaging query — fails on almost all real data. The fix is not
      `div(a, b)` but `div(a, b, scale, mode)` with both named by the caller,
      plus rounding modes spelled out (at minimum half-up and toward-zero, since
      `corelib/decimal/decimal.ty@rescale` already truncates toward zero and a
      second policy must not silently disagree with it). **Interacts with this
      plan's phase 3.** A corelib change, so `make test`.
- [ ] **Phase 22** — four citations move every time anyone adds a `Makefile`
      target, and two consecutive phases have now repaired them without either
      being about them. `scripts/asan_self.sh` (twice),
      `scripts/check_citations.py` and `scripts/editors_check.sh` all cite the
      ilp32 recipe's "ASan lane SKIPPED" line as `Makefile:<N>@SKIPPED`. Six
      edits, zero information. Scope: teach the source->source citation form the
      same no-line-number `path@SYMBOL` spelling the Markdown side already has.
      A doc-gate change, **not** `make test`.

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. Still a
  language change and still its own plan.
- **Optimising `core:json`.** `bench/json/json.ty` measures it; a slowdown found
  by phase 1 is a finding to record, not a licence to rewrite the parser.
