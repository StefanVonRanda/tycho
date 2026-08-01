# `core:json` should accept JSON — close all six gaps

Previous plan complete and archived at
[docs/internals/plan-json-error-DONE.md](docs/internals/plan-json-error-DONE.md).
Its unclosed discoveries carry forward at the bottom.

That plan made the parser unable to *lie*. This one makes it able to *read*.

## Goal

`corelib/json/json.ty` now fails closed with a byte offset instead of inventing
data — and it rejects a large share of real JSON while doing it. Six `# gap:`
lines in that file name what it still cannot do. Done looks like: **all six are
closed**, `core:json` accepts exactly the RFC 8259 grammar, every number it
accepts round-trips through `stringify` without losing a digit, and the gates
prove it.

The six, quoted from `corelib/json/json.ty`:

1. `:39` — no float or exponent value at all. `1.5` is an error.
2. `:43` — no `\uXXXX`. It is an error at the `u`.
3. `:46` — integers wrap silently at 64 bits. **The last silent-wrong-value path
   in the package**, and the reason this is not just a feature plan.
4. `:50` — a trailing comma is accepted; `[1,]` parses as `[1]`.
5. `:53` — leading zeros are accepted (`01` -> 1).
6. `:55` — trailing text after the top-level value is ignored, so `{"a":1} junk`
   is a successful parse.

## Pre-flight

- **Worst case:** gap 3 is closed *badly*. A 20-digit integer that today wraps
  to a wrong `int` must not tomorrow become a `float` that has silently dropped
  its low digits — that trades one silent wrong value for another. The rule this
  plan holds to: **a number the parser cannot represent exactly keeps its
  original lexeme**, so an exact consumer can always recover it, and
  `stringify` re-emits that lexeme rather than a re-rendered value. Every phase
  tests round-trip identity on the awkward numbers, not just parse success.
- **Second worst case, and it is specific to this machine:** `strtod` is
  **locale-sensitive**. This host's environment is `LC_NUMERIC=da_DK.UTF-8`,
  where the decimal separator is a comma, and under that locale `strtod("1.5")`
  returns `1.0` — silently, no error. That is the exact failure this plan exists
  to remove, reintroduced through the C library. `grep -rn setlocale runtime/
  src/ corelib/` returns **nothing**, so a Tycho program starts in the `"C"`
  locale and `strtod` is correct today — but that is an *unstated* dependency on
  a thing no gate checks. Phase 1 must make it explicit and must test it under a
  hostile locale, not assume it.
- **Reversibility:** total. Library code and its tests; no file is written by
  anything in scope; every change is one `git revert` away.
- **Verified — the numeric surface, read, not assumed:**
  - There is **no string-to-float conversion anywhere in the language or the
    corelib.** `to_float` is a builtin that takes "an int, a sized int, f32, or a
    float newtype" (`src/tychoc.c:5659-5663`) — never a `string`. `grep -rn
    'to_float\|parse_float\|atof\|strtod' --include='*.ty' --include='*.c'
    corelib/ src/ runtime/` finds `strtod` only in the *compiler*, at
    `src/tychoc.c:298`, converting float literals at compile time. So phase 1 has
    to build the conversion; it cannot call one.
  - `float` is IEEE-754 binary64 (`docs/spec/03-types.md` §5.2.2), `/` is true
    division and does not trap, `NaN` is unordered.
  - Twelve corelib packages carry a `<pkg>_shim.c`; `corelib/strings/` and
    `corelib/fmath/` carry none, so adding one is a new file in an existing,
    gated pattern. `make shim-check` compiles each shim standalone under
    `-std=c11` and is the **only** gate that can catch a missing feature-test
    macro — `make corelib` cannot, because the real build appends the shim to
    generated C on one `cc` line with no `-std` (`CLAUDE.md`'s gate table).
    Anything needing `strtod_l`/`newlocale` must declare its own macros.
- **Verified — the blast radius, unchanged from the previous plan and re-checked
  because this one changes the `Json` enum:** six consumers of `core:json`
  (`examples/site/main.ty`, `examples/fetch/main.ty`,
  `examples/corelib/json/main.ty`, `bench/json/json.ty`, `tools/tycho-q/main.ty`,
  `corelib/test/json/main.ty`), and the only mention of a `Json` variant outside
  `corelib/json/json.ty` is a constructor call in `examples/corelib/json/main.ty:16`.
  **No external `match` over `Json` exists**, so adding a variant breaks no
  exhaustive match outside the package. Inside it, `stringify`, `kind`, `get`,
  `at`, `keys`, `len_of`, `as_num`, `as_str` and `as_bool` all match on `Json`
  and all must gain the new arm.
- **Assuming — things to check, not facts:**
  - That an enum variant may carry two payloads of different types
    (`JFloat(float, string)`). §5.3.6 allows up to 8 and `JObj([string], [Json])`
    already carries two, so the risk is low.
  - That gap 6 (trailing text) can be closed without breaking a consumer.
    `corelib/json/json.ty@parse`'s header has promised "trailing text is ignored"
    since the package was written; a caller may be relying on it. Phase 4 checks
    all six consumers before changing it, and if one relies on it, that is a
    finding to record rather than a reason to skip the gap.
- **Deliberately not in this plan:** `decimal.div` (carried-forward phase 20),
  and performance. `bench/json/json.ty` exists; a slowdown it measures is a
  finding to record, not a licence to rewrite the parser.

## Phases

- [x] **Phase 1 — `strings.parse_float`, and the locale trap**
  - Scope: `corelib/strings/strings.ty`, new `corelib/strings/strings_shim.c`,
    `corelib/test/strings/main.ty`, `corelib/test/strings.out`.
  - A strict, `Result`-returning string-to-float. Strict means: the **whole**
    string must be consumed, the accepted grammar is written down before the
    code, and anything else is an error — never a partial parse, never a 0.
    `corelib/strings/strings.ty@parse_int` fails open (carried-forward phase 13);
    this is its opposite and the header must say so where a reader will meet
    both.
  - **The locale dependency must be removed, not documented.** Use `strtod_l`
    against a `newlocale(LC_NUMERIC_MASK, "C", (locale_t)0)` handle so the
    conversion is correct whatever the caller's locale is, and declare whatever
    feature-test macro that needs at the top of the shim. If `newlocale` is
    unavailable, the fallback must be stated in the shim and still correct.
  - Done when: `parse_float("1.5")` is `1.5` and `parse_float("1,5")` is an
    error, **both proved under `LC_ALL=da_DK.UTF-8` as well as the default** —
    run the test binary under a hostile locale and paste both runs. Plus: the
    empty string, a lone `-`, `1.5x`, ` 1.5`, `1e400` (overflow), `1e-400`
    (underflow), `-0.0`, and a 25-digit integer each land on a stated,
    tested outcome.
  - Verify: `make shim-check` (the only gate that can catch a missing
    feature-test macro), then `make corelib`, then the hostile-locale run. Do NOT
    run `make ci`. `make test` cannot redden for a `corelib/` change
    (`docs/internals/plan-json-error-DONE.md` phase 23 — `tests/run.sh:113` globs
    top level only), so run it only if you touch something outside `corelib/`.

  **Evidence.**

  Added `corelib/strings/strings.ty@parse_float` returning
  `Result(float, FloatErr)`, `corelib/strings/strings.ty@FloatErr` with four
  payload-free variants, and a new `corelib/strings/strings_shim.c` holding
  `corelib/strings/strings_shim.c@strx_parse_double`. No `deps` file: pure libc,
  so `core:strings` stays core tier.

  *The accepted grammar* (whole string, or `Err(Syntax)`), written down above the
  code:

      number := sign? ( digits frac? | frac ) exp?
      sign   := '+' | '-'
      digits := ('0'..'9')+
      frac   := '.' digits          -- a point NEEDS digits after it
      exp    := ('e' | 'E') sign? digits

  A deliberate **superset** of RFC 8259's number grammar (it takes `+1`, `.5`,
  `01`), so phase 2's JSON lexer can check its own stricter shape and then hand
  the lexeme here. Refused although bare `strtod` would take them: leading or
  trailing whitespace, `inf`/`nan`, hex floats, a comma separator, any trailing
  byte, and a NUL byte plus whatever follows it — the scan walks all `len(s)`
  bytes, so a length-counted Tycho string cannot smuggle bytes past C's NUL.

  *The locale dependency is removed, not documented.* `strtod_l` against a
  `newlocale(LC_NUMERIC_MASK, "C", (locale_t)0)` handle built once under
  `pthread_once` (a Tycho task is a pthread, so a bare lazy assignment is a data
  race that also leaks the handle). Two fallbacks, both correct: a libc without
  `strtod_l`, or `newlocale` failing at run time, take
  `corelib/strings/strings_shim.c@ty_strtod_localeconv`, which rewrites `'.'` to
  the running locale's own `localeconv()->decimal_point` and calls plain
  `strtod`.

  The feature-test macro is **`_GNU_SOURCE`, not `_POSIX_C_SOURCE`**, and that
  was measured rather than assumed: `newlocale`/`locale_t` are POSIX 2008 but
  `strtod_l` is not, so with `_POSIX_C_SOURCE 200809L` and `-std=c11` the file
  fails. `make shim-check` is the only gate that sees it — proved by breaking it
  on purpose:

      FAIL corelib/strings/strings_shim.c
             corelib/strings/strings_shim.c: In function ‘strx_parse_double’:
             error: implicit declaration of function ‘strtod_l’; did you mean ‘strtok_r’?

  *Both locale runs.* The test forces LC_NUMERIC hostile itself, via a hook in
  the shim that `corelib/test/strings/main.ty` declares as its own `extern` (it
  is deliberately not part of `core:strings`' API — no library function should
  move a process-wide global). `hostile=1` is the printed proof the switch took
  effect; a host with no comma-decimal locale prints `hostile=0` and reddens with
  a line naming the reason instead of quietly testing `"C"`. Nothing in the block
  prints a raw float, because `str(float)` is itself locale-sensitive (see the
  new phase below), so the two runs are byte-identical:

      $ ./st                                  # default env (LANG=en_GB.UTF-8, LC_NUMERIC unset)
      $ LC_ALL=da_DK.UTF-8 ./st
      hostile=1
      pf 1.5      ok eq1.5=yes eq1.0=NO
      pf 1,5      err Syntax
      pf empty    err Empty
      pf -        err Syntax
      pf 1.5x     err Syntax
      pf ' 1.5'   err Syntax
      pf 1e400    err Overflow
      pf 1e-400   err Underflow
      pf -0.0     ok eq0=yes neg=yes
      pf 0        ok eq0=yes neg=no
      pf 1e3      ok eq1000=yes
      pf 1E+3     ok eq1000=yes
      pf 25digit  ok eq=yes
      $ cmp default.txt dadk.txt   ->   identical, byte for byte

  `da_DK.utf8` is installed here (`locale -a | grep -i da_DK` lists
  `da_DK`, `da_DK.utf8`), so no substitute locale was needed.

  | input | outcome | why |
  |---|---|---|
  | `"1.5"` | `Ok(1.5)`, `== 1.5` true, `== 1.0` false | the whole point |
  | `"1,5"` | `Err(Syntax)` | the comma is not in the grammar, under any locale |
  | `""` | `Err(Empty)` | told apart from junk, unlike `parse_int` |
  | `"-"` | `Err(Syntax)` | a sign with no digits |
  | `"1.5x"` | `Err(Syntax)` | no partial parse — never `1.5` |
  | `" 1.5"` | `Err(Syntax)` | no leading whitespace; the caller trims if it means to |
  | `"1e400"` | `Err(Overflow)` | never `+inf` |
  | `"1e-400"` | `Err(Underflow)` | never `0.0` |
  | `"-0.0"` | `Ok(-0.0)` — `== 0.0`, and `1.0/v < 0` | the sign survives |
  | `"0"` | `Ok(0.0)`, `1.0/v > 0` | positive zero |
  | `"1e3"` | `Ok(1000.0)` | |
  | `"1E+3"` | `Ok(1000.0)` | capital `E` and a `+` exponent |
  | `"1234567890123456789012345"` | `Ok(1.2345678901234568e24)` | 25 digits are ROUNDED, not refused; the header says the digits are not preserved |

  `"1e-320"` is also `Err(Underflow)`: glibc sets `ERANGE` for a subnormal
  result, and a subnormal has already lost digits. Measured with a standalone C
  probe before the shim was written.

  *The test can fail* — checked by breaking the shim two ways and rebuilding:

  - forced onto the `localeconv` fallback (both `strtod_l` and the C-locale
    handle bypassed): `pf 1.5 ok eq1.5=yes` — the fallback is genuinely correct,
    not merely unreached.
  - replaced by bare `strtod`: `pf 1.5 err Syntax eq1.5=err eq1.0=err`. Under
    the hostile locale bare `strtod` stops at the `'.'`, the shim's
    whole-string check catches the leftover, and the test reddens. The
    standalone C probe shows the raw damage directly:
    `plain strtod 1.5 -> 1 rest=[.5]` versus `strtod_l(C) 1.5 -> 1.5 rest=[]`.

  *Gates, all foreground, in the briefed order:*

      make shim-check   ->  shim-check: 12 ok, 1 skipped, 0 failed
      make corelib      ->  ok   strings ... corelib: all green (tychoc matches goldens)
      sh examples/corelib/run.sh  ->  ok   strings ... corelib examples: all green
      LC_ALL=da_DK.UTF-8 <test binary>  ->  byte-identical to the default run (above)
      python3 scripts/check_citations.py  ->  citation check: ok

  `make ci` and `make test` were **not** run: nothing outside `corelib/` changed,
  and `tests/run.sh:113` globs the top level only.

  `corelib/strings/strings.ty@parse_int` is unchanged, as briefed. The two now sit
  together with a block above `parse_float` saying which to reach for, and the
  package header names the split in four lines so a reader meets it first.

- [ ] **Phase 2 — numbers: the float path and the 64-bit wrap (gaps 1 and 3)**
  - Scope: `corelib/json/json.ty`, `corelib/test/json/main.ty`,
    `corelib/test/json.out`.
  - Add `JFloat(float, string)` — the binary64 value **and the original
    lexeme**. `parse_number` accepts the full RFC 8259 number grammar
    (`-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?`) and yields:
    - `JNum(int)` for an integer that round-trips exactly through 64 bits;
    - `JFloat(v, lexeme)` for anything with a `.` or an exponent, **and for an
      integer that does not round-trip** — which is how gap 3 stops being silent.
      Verify the round trip by rendering the accumulated int back and comparing
      to the lexeme; do not rely on detecting overflow after it happens.
  - `stringify` emits the **stored lexeme** for `JFloat`, never a re-rendered
    float, so `parse -> stringify` is byte-identical for every number in the
    test corpus. This is the property to test; a float re-rendered through `str`
    would lose digits and that is the failure this design exists to prevent.
  - Every `match` over `Json` inside the package gains its arm: `kind` (decide
    and state whether a float is `"num"` or `"float"` — and check the six
    consumers before choosing), `get`, `at`, `keys`, `len_of`, `as_num`,
    `as_str`, `as_bool`. Add `as_float` and the accessor that returns the lexeme.
  - Done when: `1.5`, `-0.0`, `1e3`, `1E+3`, `2.5e-3`, `123456789012345678901234567890`,
    and `9223372036854775808` (one past `int` max) each parse to a stated value
    and `stringify` back **byte-identically**; `1.` and `.5` and `01` and `+1`
    and `1e` are still errors with the right byte offset; and gaps 1 and 3 are
    deleted from the header because they are closed.
  - Verify: `make corelib`, `sh examples/corelib/run.sh`, and `make shim-check`
    if phase 1's shim changed. Not `make ci`.

- [ ] **Phase 3 — strings: `\uXXXX` and surrogate pairs (gap 2)**
  - Scope: `corelib/json/json.ty`, `corelib/test/json/main.ty`,
    `corelib/test/json.out`.
  - Decode `\uXXXX` to UTF-8. A leading surrogate must combine with its trailing
    partner; a **lone** surrogate, a truncated escape, and a non-hex digit are
    each errors at the right byte offset — not a substituted replacement
    character, which would be a guess.
  - `esc`/`stringify` must round-trip whatever `parse` accepts. State in a
    comment which characters are escaped on output and why.
  - Done when: `"A"` is `A`; `"é"` and a literal `é` are the same
    string; `"😀"` is a 4-byte UTF-8 emoji; a lone `"\ud83d"`, `"\u00"`
    and `"\uZZZZ"` are errors with offsets; and parse -> stringify -> parse is a
    fixed point on all of them. Gap 2 deleted from the header.
  - Verify: `make corelib`, `sh examples/corelib/run.sh`.

- [ ] **Phase 4 — the grammar gets strict (gaps 4, 5, 6)**
  - Scope: `corelib/json/json.ty`, `corelib/test/json/main.ty`,
    `corelib/test/json.out`, and — only if a consumer breaks — that consumer.
  - Trailing comma rejected (`[1,]`, `{"a":1,}`). Leading zeros rejected (`01`,
    `-01`); `0` and `0.5` still fine. Trailing text after the top-level value
    rejected (`{"a":1} junk`), with whitespace still allowed.
  - **Gap 6 is the one that can break a caller.** The package header has
    promised "trailing text is ignored" since it was written. Before changing it,
    check all six consumers for anything that hands `parse` a buffer with more
    than one document in it, and say in the evidence what each does. If one
    relies on it, record that as a finding and give it the lenient entry point
    rather than skipping the gap.
  - Done when: each of the three is an error with the right byte offset, the
    valid neighbours (`[1]`, `{"a":1}`, `0`, `0.5`, `{"a":1}   `) still parse,
    gaps 4, 5 and 6 are deleted from the header, and **no `# gap:` line about the
    grammar remains** — anything still unhandled gets a fresh, honest one.
  - Verify: `make corelib`, `sh examples/corelib/run.sh`.

- [ ] **Phase 5 — the consumers, and the closing sweep**
  - Scope: `tools/tycho-q/main.ty`, `tools/tycho-q/run.sh`,
    `tools/tycho-q/expected.out`, whichever of the other five consumers needs a
    change, `FRICTION.md`, `CLAUDE.md`.
  - `tycho-q` refuses JSON floats today because `core:json` could not read one.
    Now it can: map `JFloat`'s **lexeme** through `decimal.from_str` so a JSON
    float lands in `VDec` and stays exact — the same treatment a CSV cell already
    gets — rather than through the binary64 value, which would round. The
    `json float` failure leg in `tools/tycho-q/run.sh` becomes a *success* case
    and the golden moves; say which lines moved and why.
  - Fold in the two open documentation phases while the context is here, since
    both are about this work and neither can redden a build gate:
    carried-forward phase 23 (`CLAUDE.md`'s gate table sends corelib changes to
    `make test`, which cannot see them — add `make corelib` and
    `make corelib-examples` rows and fix the rule line) and phase 24
    (`FRICTION.md`'s top-ranked finding is fixed and the file still reads as
    open — mark it FIXED with the commit, leave the finding's text intact, and
    add what *this* plan closed).
  - Done when: `tycho-q` reads a JSON float exactly, its ten failure legs still
    behave, all six consumers compile, and both documents tell the truth.
  - Verify, each foreground, in order: `make q-check`, `make corelib`,
    `sh examples/corelib/run.sh`, `make fetch` and `make site` if those examples
    changed, `make test` (was `passed: 560 failed: 0`), `python3
    scripts/check_citations.py`, `sh scripts/check_links.sh`, and **`make ci`
    once, last** as the closing sweep. If `make ci` reddens, fix with the failing
    step's own gate and re-run that gate — never `make ci` as a loop.

- [ ] **Phase 6 — `str(float)` is locale-sensitive, and it renders corrupt output**
  - Found in phase 1, out of its scope (`runtime/`, not `corelib/`), so recorded
    rather than absorbed.
  - Phase 1 removed the locale dependency from the string **to** float direction.
    The float **to** string direction still has it, and it is worse than a wrong
    separator. Measured with the phase 1 test hook holding LC_NUMERIC at
    `da_DK.UTF-8`, `str(1.5)` printed:

        1,5.0

    The mechanism is read, not guessed: `runtime/tycho_rt.c@tycho_float_to_str`
    formats with `%.15g`, which takes its separator from LC_NUMERIC, and then
    scans the result for `'.'`, `'e'`, `'E'` or an inf/nan marker to decide
    whether to append `".0"` so the value is "never mistaken for an int". Under a
    comma locale `1,5` contains none of those, so the guard fires and appends
    `".0"` to a string that already had a fraction. The result is neither valid
    Tycho float syntax nor a number any parser in this tree will read back, so
    `parse_float(str(v))` is not a round trip under a comma-decimal locale.
  - Today nothing in this tree calls `setlocale`, so a Tycho program never leaves
    `"C"` and this is latent — exactly the position `strtod` was in before phase
    1. One linked C library calling `setlocale(LC_ALL, "")` makes every float
    this runtime prints wrong, and no gate would notice.
  - The fix is the same shape as phase 1's: render through a `"C"`-locale
    conversion (`snprintf_l`, or format the digits without printf's separator),
    not through the ambient locale. Scope is `runtime/`, so it needs `make test`,
    not `make corelib`.
  - The phase 1 test deliberately prints **no** raw float for this reason; a
    future phase that wants one in a golden has to close this first.

## Carried forward

From `docs/internals/plan-json-error-DONE.md` and its predecessors, original
numbering. Phases 23 and 24 are **not** listed: phase 5 above absorbs them
deliberately, because they are documentation about this exact work.

- [ ] **Phase 5(old)** — a skipped shim is compiled by nothing on this host:
      `make corelib` and `scripts/shim_check.sh` both skip `image` for the same
      missing libpng, so a real defect there is invisible here.
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
      and it exits, so a non-fatal warning is inexpressible.
- [ ] **Phase 11** — no `mkdir -p` in `core:io`; every caller that writes into a
      tree it does not own builds the component chain itself, as
      `tools/tycho-ar/main.ty@mkdir_p` does in 18 lines.
- [ ] **Phase 12** — mtime is readable and not writable, so `tycho-ar` stores a
      faithful mtime it cannot restore.
- [ ] **Phase 13** — `strings.parse_int` fails open (`corelib/strings/strings.ty@parse_int`).
      **Phase 1 of this plan adds its strict opposite next to it** and must say
      where the two differ.
- [ ] **Phase 14** — no incremental digest anywhere in the corelib, so hashing a
      large file in bounded memory means writing your own.
- [ ] **Phase 15** — a Tycho parameter is borrowed read-only, so a streaming
      state cannot be threaded through calls without `inout`. A `FRICTION.md`
      entry, not a code change.
- [ ] **Phase 16** — a package cannot mark a top-level function internal, so
      every corelib helper is public API by accident.
- [ ] **Phase 17** — `chr(n)` is the only route from a number to a byte; there is
      no `bytes` builder from integers.
- [ ] **Phase 19** — a new lane's golden is invisible until someone clones. The
      cheap gate is mechanical: for every `*/run.sh` naming a golden path,
      `git ls-files --error-unmatch` it.
- [ ] **Phase 20** — `core:decimal` has no `div`, so `select total / count`
      fails on almost all real data. The fix is `div(a, b, scale, mode)` with
      both named by the caller. **Interacts with phase 5 above**, which routes
      JSON floats into `Decimal`.
- [ ] **Phase 22** — four `Makefile:<N>@SKIPPED` citations move every time anyone
      adds a target; six edits across two phases carried zero information. Teach
      the source-to-source citation form the no-line-number `path@SYMBOL`
      spelling the Markdown side already has.

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item.
- **Optimising `core:json`.** A slowdown `bench/json/json.ty` measures is a
  finding to record, not a licence to rewrite the parser.
