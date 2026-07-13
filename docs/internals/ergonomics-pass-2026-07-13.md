# Hands-on ergonomics pass — 2026-07-13

> A first-contact usability read, done the way a newcomer meets the language:
> write small idiomatic programs from intuition, compile+run each, record what
> bites. Every result below is **verified by running `tychoc`** (built from
> `src/tychoc.c` on 2026-07-13), not asserted from memory or docs.
>
> This is *not* a correctness or perf audit — the engine is solid (self-hosts,
> 248 tests, flat-memory workloads). This is purely about the *surface*: how
> intuitive, discoverable, and papercut-free the language feels on day one.

## Method

- 24 "what a newcomer would guess" programs + 11 follow-up probes.
- Each failure classified: **design choice** (principled, keep) vs
  **papercut** (fixable friction) vs **missing** (feature/affordance absent).
- For every failure I then found and ran the *correct* spelling, to separate
  "I guessed wrong" from "the language can't do this."

## Verdict

The **happy path reads clean and Pythonic** once you know the spelling. The
problem is getting there: a newcomer hits **3–4 sharp edges inside the first
20 lines**, all in basic surface syntax (comments, literals, printing,
string indexing), and the errors for them are often cryptic. The engine is
far ahead of the on-ramp. Highest-leverage work is not new features — it's
smoothing literals, comments, printing, and a handful of diagnostics.

## Status of fixes (updated 2026-07-13, same day)

| # | Finding | Status |
|---|---|---|
| F1 | Multi-line collection literals | **Fixed, both compilers** — implicit line-join inside `(...)`/`[...]` + trailing comma. `make fixpoint` (B==C) + `make test` green. `tests/multiline_literals.ty`. |
| F2 | `//` is not a comment | **Fixed (tychoc)** — clear diagnostic: *use `#` for comments, or `/` for division*. (tychoc0 already rejects it; diagnostics-only, no parity impact.) |
| F3 | `s[i]` yields int byte | **Fixed (tychoc diagnostic)** — kept Go-style byte semantics (deliberate); `to_int(s[i])` now says *x is already an int … use `chr(x)`*. Docs already document `s[i]`-as-byte + `chr`. |
| F4 | No named struct fields | **Fixed, both compilers** — `Point(x: 1, y: 2)`, order-independent; rejects unknown field / mixed named+positional / named-on-function. `make fixpoint` green. `tests/named_fields.ty`. |
| F6 | `println` only takes string | **Fixed (tychoc)** — str()-able scalar to a builtin string param now suggests `str(...)`. |
| F5 | No `str()`/print of aggregates | **Open** — largest item; needs a per-type recursive `str()` generator (mirrors the `tycho_eq_S_*`/`_copy` families) across both compilers. Its own pass. |
| F7 | Match arm bodies can't be inline | Open (minor). |
| F8 | stdlib not guessable / no `sort(xs)` | Open. |
| F9/F10 | `while` / int-float mix cryptic errors | Open (diagnostics polish). |

## What already works well (the good news)

All verified running:

| Feature | Example that ran clean |
|---|---|
| Tuples (literal, return, destructure) | `t := (1,"a")`; `a,b := pair()` |
| f-strings | `println(f"x is {x}")` |
| Inline closures | `apply(fn(x:int)->int: x*2, 21)` |
| `for … in`, `range`, `break`/`continue` | all fine |
| `for cond:` (the while-form) | `for i < 3:` |
| Option + match (multi-line arms) | `Some(i):` / `None:` |
| Maps: literal / empty / `.get` | `["a":1]`, `[]string:int`, `m.get(k,0)` |
| int `/` `%`, explicit float cast | `7/2`→3, `to_float(x)/y`→3.5 |
| Early return, if-without-else | fine |
| Value semantics (deep copy on `:=`) | independent copies, verified |

## Findings (ranked by day-one pain)

Severity: 🔴 bites immediately + surprising + cryptic error · 🟡 papercut ·
🟢 minor / defensible-by-design.

### 🔴 F1 — Collection literals can't span multiple lines
```
xs := [
    1,
    2,
]                     # error: expected an expression (at the first item line)
```
Must be one line: `xs := [1, 2, 3]`. Any real-world list/array/struct-array
literal wants wrapping. This is the single most common formatting reflex and
it fails with a confusing message. Cause is the line-oriented lexer emitting
NEWLINE inside brackets.
**Fix (likely cheap, high impact):** suppress NEWLINE while bracket/paren
depth > 0. Purely lexer-local.

### 🔴 F2 — `//` is not a comment (only `#` is)
```
// like this        # error: expected 'fn' / expected an expression
# only this works
```
Verified invalid at the `//` line across 3 separate programs. Everyone
arriving from C/C++/Go/Rust/JS/Java types `//` by reflex, and the error never
mentions comments. Grammar (`docs/spec/01-lexical.md:40`) is `#`-only by
design.
**Fix:** either accept `//` line comments, or special-case a leading `//`
token into a diagnostic: `use '#' for comments`.
**⚠ Anomaly to double-check:** my very first test program had two `//` header
lines yet compiled *past* them (failed later at line 14 on an unrelated
`{`). I could not reproduce this afterward — 3 byte-identical retries all
fail at the `//` line. Worth a maintainer glance in case the lexer has a
state-dependent path for `//`.

### 🔴 F3 — Indexing a string yields a raw `int` byte
```
s := "abc"
s[0]                  # this is int 97, not a char and not "a"
println(s[0])         # error: argument 1 of 'println' is int, expected string
to_int(s[0])          # error: to_int doesn't take an int (it's already one)
```
Surprising to essentially everyone: `s[i]` is neither a 1-char string (Python)
nor a `char` you can pass to `to_int`. To render a character you go through
`chr(n)` (`int -> 1-byte string`). The `to_int` error actively misleads,
since it lists `char` as accepted but the value is already `int`.
**Fix:** at minimum document loudly; consider making `s[i]` a `char` (which
the language has) so `str(s[i])` / `to_int(s[i])` behave as newcomers expect.

### 🟡 F4 — No named-field struct construction
```
P{x: 1, y: 2}         # error: unexpected character '{'
P(1, 2)               # positional only
```
Fine for 2 fields; for wide structs, positional-only construction hurts
readability and invites arg-order bugs. Design choice, but a real ergonomic
cost as programs grow.
**Fix (optional):** allow `P(x: 1, y: 2)` named args alongside positional.

### 🟡 F5 — No auto stringify / debug-print for aggregates
```
println(p)            # error: argument 1 is P, expected string
str(p)                # error: str() takes int/float/bool/char/string…, not a struct
```
No `str(struct)`, no `repr`/`debug`. The first thing you do when lost is print
a value; for any struct/array/map you can't. (`str(bool)` *does* work — it's
only aggregates.)
**Fix:** derive `str()` for structs/enums/arrays/maps, or add a `debug(x)`
builtin. Big quality-of-life win while learning.

### 🟡 F6 — `println` only accepts `string`
```
println(3 > 2)        # error: bool, expected string   → need str(...)
println(42)           # same
```
Coming from Python/Go, `print(anything)` is muscle memory. You must wrap every
non-string. Compounds with F5.
**Fix:** auto-`str` scalar args, or a variadic `print`.

### 🟡 F7 — match arm bodies can't be inline
```
match o:
    Some(i): return x     # error: expected newline
    Some(i):
        return x          # ok — body must be on the next line
```
Python allows the one-liner; here every arm needs a block. Minor but frequent.

### 🟡 F8 — stdlib is not guessable, and `sort(xs)` for the common case is missing
- `sort(xs)` → *unknown procedure 'sort'; did you mean 'sqrt'?* The `sort`
  package exposes only `by_key`, `argsort`, `argsort_desc` — no dead-simple
  "sort these ints ascending."
- `upper("hi")` → unknown; real name is `strings.to_upper` **but** even
  `import "core:strings"; strings.to_upper(...)` returned *package 'strings'
  has no symbol 'to_upper'* despite `fn to_upper` existing in the package
  source — export/visibility is unclear and worth verifying.
**Fix:** add `sort.ints`/`sort.strs` (or overload `by_key` with identity
default); audit `core:strings` exports; the "did you mean" hints are great —
lean on them.

### 🟢 F9 — No `while` keyword (`for cond:` instead)
Documented and fine, but `while i < 3:` fails with *a bare expression has no
effect* — cryptic. **Fix:** special-case the `while` identifier → *use `for
cond:`*.

### 🟢 F10 — No implicit int/float mixing
`x / y` with `x:int, y:float` → *arithmetic requires two ints or two floats*.
Principled (Odin/Go-style, keep it). The error is clear; a *did-you-mean
`to_float(x)`* hint would close the loop.

## Recommended order of attack (cheap → high impact first)

1. **F1 multi-line collection literals** — parser-local; removes the most
   common formatting papercut. Do this first.
2. **F2 `//` comment (or diagnostic)** — universal muscle memory; even just a
   pointed error is a big win. (Resolve the anomaly note while here.)
3. **F5+F6 print/str for aggregates & scalars** — the debugging on-ramp;
   makes the language feel alive while learning.
4. **Diagnostics polish: F3/F9/F10** — turn three cryptic errors into
   guiding ones (`s[i]` is a byte; use `for cond:`; use `to_float`).
5. **F4 named struct fields, F8 `sort.ints` + strings export audit** —
   readability + discoverability.

None of these touch the memory model, the thesis, or codegen. They're all
surface — which is exactly where a strong language loses newcomers.

## Appendix — raw case results

24-case battery (RUN-OK = compiled and ran correctly):

```
05 tuple_basic          RUN-OK      12 option/match         guessed-wrong (no `case` kw; multi-line arms) → works
06 tuple_return         RUN-OK      13 array push/len       RUN-OK
07 fstring              RUN-OK      14 sort(xs)             F8
08 lambda_map           RUN-OK      15 upper()              F8
10 range                RUN-OK      16 int div/mod          RUN-OK
11 break/continue       RUN-OK      17 int/float mix        F10
18 map fn over list     RUN-OK      19 // comment           F2
20 # comment            RUN-OK      21 to_int(s[i])         F3
22 if no-else           RUN-OK      23 println(bool)        F6
24 map literal ["a":1]  RUN-OK      01 P{x:1}               F4
02 multiline array      F1          03 println(struct)      F5
04 str(struct)          F5          09 while                F9
```

Reproduce: scripts in `/home/igzo/.claude/jobs/7eb581e2/tmp/ergo/` (gen.sh,
run.sh, probe.sh) — throwaway, not committed.
