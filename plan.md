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

- [x] **Phase 1 — a parser that cannot spin, and an error channel**
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

  **Evidence (2026-08-01).**

  **Before — all three failures reproduced against the unmodified parser**, in a
  throwaway program under the scratchpad, not in the gated test. Built with
  `TYCHO_CORELIB=$PWD/corelib ./tychoc <probe>/main.ty -o <bin>`:

  ```
  1.5          -> 1                              exit 0
  [{"a":1.5}] -> [{"a":1,"5}]":null}]            exit 0
  ```

  The second is worse than the Pre-flight predicted: the fabricated key is `5}]`
  and it carries a `null` value, so the document grew a **second column** nobody
  wrote, at exit 0, with no symptom. `[1.5]` was probed separately under
  `ulimit -v 262144` with a 20 s timeout, because it exhausts memory:

  ```
  tycho: out of memory                           exit 1
  ```

  **After — the same three inputs, now as locked lines of
  `corelib/test/json.out`:**

  ```
  f scalar    = null
  f scalar r  = err fraction or exponent (floats are out of scope) at byte 1
  f in obj    = null
  f in obj r  = err fraction or exponent (floats are out of scope) at byte 7
  f in arr    = null
  f in arr r  = err fraction or exponent (floats are out of scope) at byte 2
  f in arr off= 2
  ```

  The offset is asserted against the byte it must name, not against non-zero: in
  `[1.5]` the `.` is at index 2, in `[{"a":1.5}]` at index 7, in `1.5` at index 1.
  Re-running the `[1.5]` probe against the fixed parser under the same
  `ulimit -v 262144` prints `[1.5] -> null` and exits 0 — the OOM is gone, not
  merely unreached by the test corpus.

  Twelve more failure classes are locked in the same golden (`bad byte`,
  `bad number`, `bad keyword`, `bad escape`, `bad key`, `no colon`, `no sep`,
  three `unclosed` cases, `empty`), plus three lines asserting what stays
  deliberately lenient (`trailing ,`, `trailing txt`, and the `\b \f \r \/`
  escapes that are now decoded rather than guessed).

  **The two halves.** The structural anti-spin guard is `before := pos` plus a
  `pos == before` test in both `corelib/json/json.ty@parse_array` and
  `corelib/json/json.ty@parse_object`; the comment above `parse_array` enumerates
  every byte class and shows that no path reaches the guard today, which is the
  point — it is the invariant, not the fix. The fix that ends the OOM is that
  `corelib/json/json.ty@parse_value` no longer falls through to `parse_number`
  for a byte that begins no value; it sets `JE_BYTE` and stops.
  `parse_object`'s unconditional `pos = pos + 1  # skip :` is now a checked
  `s[pos] != 58` test, which is what closed the fabricated key. The fallible
  entry point is `corelib/json/json.ty@parse_checked`, returning
  `Result(Json, JsonErr)`; `corelib/json/json.ty@parse` keeps its signature and
  discards the error, so no caller changed.

  **The error type carries a payload on both arms**, as
  `docs/internals/plan-q-DONE.md` phase 1 required: all ten `JsonErr` variants
  carry the byte offset, so `corelib/json/json.ty@err_offset` is total without a
  sentinel. Internally the recursion threads one extra `inout int` rather than a
  `Result`, under the invariant that when the code is not `JE_OK` the cursor is
  the failing byte and nothing advances it again — stated in the header and
  enforced by an immediate `return` at every site that sets it.

  **`# gap:` lines added to the package header** (`corelib/json/json.ty`), each
  naming the ceiling and the way out: no float/exponent value; no `\uXXXX`;
  **integers wrap silently at 64 bits**, which is the one silent-wrong-value path
  left in the parser; trailing commas accepted; leading zeros accepted; trailing
  text after the top-level value ignored.

  **Gates.**

  - `make test` → `passed: 560   failed: 0`, `all green`. 560 before (`CLAUDE.md`'s
    table), 560 after — no fixture lost.
  - `make corelib` → `ok   json`, `corelib: all green`. **This, not `make test`,
    is what actually gates `corelib/test/json.out`** — see the new phase 23 below.
  - `examples/corelib/run.sh` → `ok   json`, `corelib examples: all green`.
  - All six consumers still compile untouched, checked one at a time with
    `TYCHO_CORELIB=$PWD/corelib ./tychoc <file> -o <tmp>`: `examples/site/main.ty`,
    `examples/fetch/main.ty`, `examples/corelib/json/main.ty`,
    `bench/json/json.ty`, `tools/tycho-q/main.ty`, `corelib/test/json/main.ty`.

- [x] **Phase 2 — the callers stop working around it**
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

  **Evidence (2026-08-01).**

  **Which consumers changed: one.** `tools/tycho-q/main.ty` was the only one of
  the six carrying a hand-written pre-validator. The other five were each read
  and left alone, and here is what each actually does with the package, so the
  "left alone" is a finding and not an omission:

  | consumer | what it calls | why untouched |
  |---|---|---|
  | `examples/site/main.ty` | `json.parse` on `site.json`, then `as_str`/`get` | no validator; a config the example ships itself |
  | `examples/fetch/main.ty` | `json.parse` on a response body, then `keys`/`len_of` | no validator; failure is genuinely not actionable there |
  | `examples/corelib/json/main.ty` | `json.parse` on a literal, plus `json.JArr(...)` constructors | no validator; it is the lenient path's own demo |
  | `bench/json/json.ty` | `json.parse` on a document it generates | no validator; changing it would move the benchmark |
  | `corelib/test/json/main.ty` | both `parse` and `parse_checked` | phase 1 already converted it; it is the golden |

  `tools/tycho-q/run.sh` also changed, and had to: the two guard legs asserted on
  substrings of the guard's own messages, so leaving it alone would have left a
  gate asserting against deleted code.

  **What was deleted.** `json_guard` (72 lines), its `word_at` helper and its
  `C_OBJ` / `C_ARR` constants — all four were reachable from nothing else
  (`is_ws` and `is_digit` have other callers in the lexer and stayed). What
  replaced it is 15 lines: `tools/tycho-q/main.ty@json_root`, which calls
  `corelib/json/json.ty@parse_checked` and relabels the failure, and
  `tools/tycho-q/main.ty@json_err`, which formats it. `load_json`'s two lines
  `_ := json_guard(path, text) or_return` / `root := json.parse(text)` became the
  single `root := json_root(path, text) or_return`.

  **The comment block was rewritten, not deleted**, in both places the reasoning
  lived: DECISION 3 in `tools/tycho-q/main.ty`'s header now records the probe in
  the past tense, says plainly that the guard was most of a second JSON parser
  written only because the first one could not speak, and says what replaced it;
  the block where the code used to sit keeps the two-layer description of what
  the guard checked and adds why an approximation of a parser could never be
  right (its own comment had admitted `[1 2]` passed it). The one thing tycho-q
  still says for itself is the float advice, and the comment says why: the
  corelib's "fraction or exponent (floats are out of scope)" is true and is not
  advice.

  **Before/after of the two failure-leg messages**, run against the real binary
  (`TYCHO_CORELIB=$PWD/corelib ./tychoc tools/tycho-q/main.ty -o /tmp/q`, then
  `/tmp/q 'select * from <fixture>'` in a scratch fixture dir):

  ```
  [{"a":1.5}]  before: float.json: byte 6: JSON numbers here must be integers, and `1.5` is not -- core:json ... would read it as 1 ...
               after : float.json: byte 7: fraction or exponent (floats are out of scope) -- JSON numbers here must be integers, and core:json has no float path at all. Write the value as a string ...
  [}]          before: spin.json: byte 1: a `}` here closes nothing that was opened
               after : spin.json: byte 1: byte begins no JSON value
  ```

  The "before" column is not from reading the deleted code — an offset inferred
  that way was **wrong by one** and the run corrected it. It is from building
  `git show HEAD:tools/tycho-q/main.ty` into `/tmp/oldq/q` and running the same
  two queries against the same two fixtures. Both versions `exit=1` with
  `stdout=0`, observed on all four runs.

  Two things moved and both are improvements: the float offset went from 6 (the
  `1`, i.e. the token start, which is where the guard's own scan happened to be)
  to **7, the actual `.`** — the parser reports the byte that failed, not the
  byte a second scanner guessed; and the `[}]` message is now about the byte
  rather than about brackets, which is what actually goes wrong.

  **`tools/tycho-q/run.sh`'s legs, both still firing.** `refuses 'json float'`
  keeps its substring `JSON numbers here must be integers` **unchanged** — the
  advice half of the new message deliberately preserves it, so that leg is a
  true before/after control. `refuses 'json unbalanced'` was renamed
  `refuses 'json bad byte'` and its substring changed from
  `closes nothing that was opened` to the whole line
  `spin.json: byte 1: byte begins no JSON value`, which is stricter than what it
  replaced: it now pins the offset as well as the reason. Header `[5]` and the
  green summary line were rewritten to match; the two stale citations in them
  (`corelib/json/json.ty:81-92`, which now names the `JE_*` constants, and the
  claim that the finding was "NOT fixed here") are gone.

  **The golden did NOT move and was NOT re-recorded.** `tools/tycho-q/expected.out`
  is untouched — `git status --short` after all edits lists only
  `tools/tycho-q/main.ty` and `tools/tycho-q/run.sh` — and `make q-check` reports
  `31-query transcript == golden`. That is the expected result rather than a
  lucky one: the guard only ever *rejected*, every fixture in the transcript
  passed both it and `parse_checked`, so no accepted document changed shape.

  **Gates, in the order the brief set them, each run in the foreground.**

  - `make q-check` → `tycho-q: green (31-query transcript == golden; select *
    over 2 fixtures byte-identical to the input; CSV and JSON agree under cmp;
    malformed query, missing file, unknown column, bad comparison, inexact / and
    both core:json parse errors all refused with empty stdout)`. Ten failure legs,
    all refusing with empty stdout.
  - `make corelib` → `corelib: all green (tychoc matches goldens)`.
  - `sh examples/corelib/run.sh` → `corelib examples: all green`.
  - `make test` → `passed: 560   failed: 0`, `all green`. 560 at phase 1, 560
    here — nothing lost.
  - `python3 scripts/check_citations.py` → `citation check: ok`.
  - `sh scripts/check_links.sh` → `link check: ok (142 markdown files, no dead
    relative links)`.
  - `make ci` → **`CI GREEN -- tree is good`, exit code 0**, first and only run.
    Its `[3f]` leg carries the new tycho-q line verbatim; `[3c] site`, `[3c]
    fetch` and `[3] corelib examples` are green with the three untouched
    `examples/` consumers in them.

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

- [ ] **Phase 23 — `make test` cannot redden for a `corelib/` change, and the
      gate table does not say so** (found by phase 1, out of its scope)
  - Phase 1's brief said "this is a corelib change, so `make test` is the gate".
    It is not. `tests/run.sh:113` globs `examples/*.ty tests/*.ty` — top level
    only, no descent — so nothing under `corelib/` and nothing under
    `examples/*/` is in its 560 fixtures. A change to `corelib/json/json.ty` that
    broke every corelib golden would leave `make test` green.
  - What actually gates `corelib/test/<name>.out` is `corelib/run.sh`, i.e.
    **`make corelib`** (`Makefile:224-225`), which is a few seconds. `CLAUDE.md`'s
    gate-budget table does not list `make corelib` at all, and its rule line says
    "**A `.ty` fixture or a corelib change** → `make test`" — which sends every
    corelib phase to the eight-minute gate that cannot see its change, and away
    from the three-second one that can. The same holds for
    `examples/corelib/run.sh` (`make corelib-examples`) and the `examples/*/`
    dogfoods.
  - Phase 1 ran both and reported both, so nothing shipped unverified; the defect
    is in the table, not in this plan's result.
  - Scope: `CLAUDE.md` only — add `make corelib` and `make corelib-examples` rows
    with their real cost and what they redden for, and correct the rule line to
    send a corelib change to `make corelib` first. A doc change, so the gate is
    `python3 scripts/check_citations.py` and `sh scripts/check_links.sh`. **Not**
    `make test` — which is the whole point of the finding.

- [ ] **Phase 24 — `FRICTION.md`'s top-ranked finding is fixed and the file does
      not know it** (found by phase 2, out of its scope)
  - `FRICTION.md`'s `## Re-scored against a type-system-shaped program,
    2026-08-01` section opens with item **1. `core:json` accepts input it cannot
    represent, three ways, and cannot report any of them**, and its preamble says
    the two corelib defects at the top are "**deliberately not fixed here**". As
    of this plan, item 1 IS fixed — that is what this plan was. A reader landing
    on the ranked list is told the tree's worst finding is open when it is
    closed, which is worse than not ranking it.
  - Phase 2 did not edit it, on scope lock: the brief named six consumers plus
    `tools/tycho-q/`, and `FRICTION.md` is none of them. The one reference phase 2
    *could* reach — `tools/tycho-q/run.sh`'s `[5]` header, which pointed at
    "FRICTION.md's 2026-08-01 section, item 1" as unfixed — was rewritten.
  - Scope: `FRICTION.md` only. Mark item 1 FIXED with the commit that did it and
    leave the finding's text intact (it is the record of what was wrong), and
    re-check the preamble's "deliberately not fixed here" sentence, which still
    correctly describes item 2 (`core:decimal` has no `div`, carried-forward
    phase 20) and now over-claims for item 1. A doc change, so the gates are
    `python3 scripts/check_citations.py` and `sh scripts/check_links.sh`. **Not**
    `make test` and **not** `make ci`.

## Status — PLAN COMPLETE

Both phases of this plan are done and committed. Phases 3, 23 and 24 are filed
follow-ups discovered along the way, not conditions of this plan's completion —
the same standing as everything under "Carried forward".

**What the parser could not report before.** `corelib/json/json.ty@parse`
returned `Json`, not a `Result`. There was no error channel anywhere in the
package, so no caller could ask whether the document it got back was the document
it handed in — and the parser had three separate answers for input it could not
represent, all of them silent. `1.5` became `1` at exit 0. `[{"a":1.5}]` became
`[{"a":1,"5}]":null}]` at exit 0: a **second column that nobody wrote**, in a
document the caller believed it had read. `[1.5]` — five bytes — exhausted memory,
because `parse_value` fell through to `parse_number` at the `.`, consumed nothing,
returned `JNum(0)`, and the array loop advanced only on `,` or `]`.

**What it reports now.** `corelib/json/json.ty@parse_checked` returns
`Result(Json, JsonErr)`. All ten `JsonErr` variants carry the byte offset of the
byte that failed, so `corelib/json/json.ty@err_offset` is total without a
sentinel and `corelib/json/json.ty@err_str` is a one-line stderr message. The
three failures above are now `BadFloat` at byte 1, byte 7 and byte 2
respectively — asserted against the byte each must name, not against non-zero —
plus twelve further failure classes locked in `corelib/test/json.out`. The spin
is not caught, it is **unreachable**: `parse_value` no longer falls through for a
byte that begins no value, and both container loops carry a structural
cursor-must-advance guard that no path can reach today and that stays as the
invariant. `parse` keeps its signature and discards the error, so the lenient
callers were untouched.

**And the working-around stopped.** `tools/tycho-q/main.ty` carried `json_guard`:
72 lines that re-scanned the raw bytes ahead of the parser, because checking in
front of a parser you cannot question is the only place left to check. It is
gone, replaced by 15 lines that call `parse_checked` and relabel the failure. The
two gate legs that used to prove the guard was in front of the corelib now prove
the corelib's own error crosses the package boundary and reaches stderr. No
consumer in the tree carries a hand-written pre-validator any more.

**What is still unfixed, named rather than implied.** Six `# gap:` lines sit in
`corelib/json/json.ty`'s header, each with its ceiling and its way out:

1. **no float or exponent value at all** — `1.5` is an error, never a number;
2. **no `\uXXXX`** — an error at the `u`, where it used to become the four hex
   digits as literal text;
3. **integers wrap silently at 64 bits** — `parse_number` accumulates into an
   `int` with no overflow test, so a 20-digit JSON integer still yields a wrong
   number at exit 0. **This is the one silent-wrong-value path left in the
   parser**, and it is the only gap of the six that fails open;
4. **a trailing comma is accepted** — `[1,]` parses as `[1]`; lenient, loses and
   invents nothing;
5. **leading zeros are accepted** — `01` → 1, while a leading `+` is an error;
6. **trailing text after the top-level value is ignored** — `{"a":1} junk` is Ok,
   which is the documented behaviour six callers already had.

**A float path is filed as phase 3 and is not done.** A JSON document containing
`1.5` is a hard error with a byte offset. That is correct — the package header
declares floats out of scope, so this makes a declared scope enforced instead of
silently violated — but it is still a JSON parser that cannot read ordinary JSON,
and every real feed has a float in it somewhere. The Pre-flight measured what
promoting it would cost (no `match` over `Json` exists outside
`corelib/json/json.ty`, so no external exhaustive match breaks); the open question
is which numeric tower it lands in, and it interacts with carried-forward phase 20
(`core:decimal` has no `div`). Promoting it is a decision for the user.

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
