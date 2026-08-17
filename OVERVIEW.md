# Tycho — state of the project

Measured 2026-08-17 at `8ef466a3` by reading the tree and querying GitHub. Not
from memory, not from a roadmap, not from a gate's exit code. Where a number is
*not* measured it says so.

Untracked and uncommitted on purpose — this is an assessment, not documentation.

---

## 1. What actually exists

| area | files | lines |
|---|---:|---:|
| `src/tychoc.c` — the whole compiler | 1 | **14,719** |
| `runtime/tycho_rt.c` — the whole runtime | 1 | **3,074** |
| `corelib/` — 46 packages | 161 | 18,167 |
| `tools/` — 30 programs written in Tycho | 145 | 41,528 |
| `tests/` | 1,189 | 45,028 |
| `docs/` | 83 | 23,157 |
| `bench/`, `examples/`, `server/`, `editors/`, `fuzz/`, `scripts/` | ~400 | 35,000 |
| **tracked total** | **2,071** | — |

By language: **78,347 lines of Tycho**, 28,392 of C, 29,545 of Markdown.

**The implementation is 17,793 lines of C in two files.** Everything else is
library, programs, tests, or prose about it.

## 2. The three ratios that describe this project

| | |
|---|---|
| tests : implementation | **2.5 : 1** (45,028 vs 17,793) |
| prose : implementation | **1.7 : 1** (29,545 vs 17,793) |
| maintainer-only docs : the entire runtime | **3.0 : 1** (9,352 in `docs/internals/` vs 3,074) |

Written and maintained by one person, for an audience of approximately zero
(§6). This is the shape of the problem, and it is not a quality problem.

## 3. The language

- 28 spec chapters (`docs/spec/`, 6,953 lines) — lexical, grammar, types,
  inference, generics, conversions, memory model, declarations, expressions,
  statements, functions, aggregates, concurrency, FFI, program, builtins,
  runtime, library, plus 8 appendices including a conformance appendix.
- Distinctive features in use, not just specified: implicit hierarchical arenas,
  value semantics with no reference type, affine `handle` types with
  destructors, `subscript` projections, `soa` layouts, `bounded[N]T`, newtypes
  erased at lowering, generics by monomorphization, `parallel for`.
- **A real specification is rare at this stage.** Most 0.x languages have a
  README and a parser.

## 4. Corelib — 46 packages, 18,167 lines

Largest: `json` (1,061), `io` (626), `httpd` (609), `toml` (566),
`strings` (423), `datetime` (396), `bignum` (337), `raster` (333),
`markdown` (273), `cli` (255), `zip` (252), `net` (226).

11 packages carry a C shim (`io`, `strings`, `datetime`, `net`, `crypto`, `os`,
`http`, `image`, `signal`, `regex`, `compress`, `time`, `tls`).

**Every one of the 46 has a test directory.** Measured, zero exceptions.

## 5. Verification — and the honest limit of it

**39 `-check` lanes exist.** Of all programs under `tools/`, only **`prof` and
`tycho-fetch`** have no lane.

Of **92** runner scripts scanned for how they assert:

| how it asserts | count | what it can catch |
|---|---:|---|
| golden only | 31 | a **change** in behaviour — never that the behaviour was right |
| golden + oracle/control | 27 | both |
| oracle/property/control only | 10 | a latent bug that has always been there |
| pattern unmatched (not classified) | 24 | unknown — I did not read these |

**The owner's criticism is partly right and worth stating precisely.** `make
test`'s 723 fixtures are golden comparisons: they prove the compiler still does
what it did, not that what it does is correct. At a stage where the language is
not changing much, a pure regression suite reports "no change" and calls it
green. That is exactly what it is designed to do and it is not the same as
finding anything.

**Where it is wrong:** `src/tychoc.c` and `runtime/tycho_rt.c` took **72 commits
in the last 7 days** and 164 in the last 30. A regression suite over a compiler
under that much change is not idle — green means 72 commits landed without
breaking 723 recorded behaviours.

**The real gap:** the lanes that have ever found a *latent* defect are the
oracle ones — differentials against Python (`format-diff`, `math-diff`,
`bignum_diff`), the property checks (`path.safe_join`, `core:cli` partition),
and the security lanes (`tls-verify`, `http-verify`, `crypto_hygiene`,
`image-ceiling`). Those are 37 of 92. Growing that number is the only thing that
makes the suite find things rather than confirm things.

**Not verified by me:** the 24 unclassified runners, and whether the fuzz lanes,
`ilp32`, `asan-self` or the concurrency suite pass right now. I ran four lanes
today (`test`, `corelib`, `entrypoints`, `check-links`) — all green — and that
is 4 of ~44.

## 6. Distribution — the actual blocker

Measured from the GitHub API 2026-08-17:

| | |
|---|---:|
| releases published | 3 (v0.5.0, v0.6.0, **v0.7.0 on 2026-08-15**) |
| **downloads, every release, every asset** | **6** |
| stars / forks / watchers | 2 / 0 / 0 |
| **issues + PRs ever opened** | **0** |
| commits, all time | 1,693 — **every one by the owner** |

Release assets exist for `linux-x86_64` and `mingw64-x86_64`, with checksums.

`ROADMAP.md` §1 (someone other than the author writes a real program) and §7 (an
external security review) both require a person who has heard of Tycho.
Essentially none has. **Neither is blocked on engineering.** Nothing in the plan
addresses distribution, and with the outward items unreachable, effort defaults
to the reachable ones — docs, gates, the site. In the last 30 commits on `main`,
16 were documentation or gating and 12 touched `src/`, `runtime/` or `corelib/`.

## 7. Platform — and a correction

**Tycho has been developed and tested on macOS / Apple Silicon.** I wrote the
opposite in the first draft of this file, taking it from `ROADMAP.md`. It is
wrong, and the evidence is in the repo:

- `README.md:231` — "developed and gated on Debian (x86-64), and **benchmarked
  on macOS (Apple Silicon)**".
- `135454c8` (2026-08-10) — "**Checked with `make test` (608/608) and `make ci`
  on macOS**". A full local sweep on a Mac.
- `3cc7c3d2` — `conc` and `recursion` runners fixed for macOS: no `timeout`,
  and `ulimit -v` fails with "setrlimit failed: invalid argument". You only
  learn that by running it there.
- `6cb89f22` — the leak-fuzz lane skipped because **Apple's ASan ships no
  LeakSanitizer**. Same: found by running.
- `8306651c` — a macOS notes document existed and was folded into the README.
- 38 commits mention macOS / Darwin / ARM.

**`ROADMAP.md:578` says "There is no macOS build and no ARM64 build on any
platform."** That sentence is false and contradicts the README in the same
repository.

What *is* true: **no macOS or ARM64 binary is published.** The v0.7.0 release
assets are `linux-x86_64` and `mingw64-x86_64` only — verified via the GitHub
API. "Never shipped" and "never run" are different claims and the roadmap
conflates them.

- `runtime/tycho_rt.c` carries 14 platform conditionals including explicit
  Darwin and `__aarch64__` branches; `src/tychoc.c` carries 9.
- Separately verified today: `zig cc -target wasm32-wasi` builds `src/tychoc.c`
  into a working 2.2 MB WASI binary whose diagnostics match native. The runtime
  does not cross that way — 20 errors, all in the signal-based stack guard and
  threads.

## 7b. The finding this correction points at

**The most serious problem in this repository is not code. It is that its
documents are confidently, specifically wrong, and well-written enough to be
more convincing than the running system.**

Four instances, all found on 2026-08-17, all by me trusting a document:

| document | claim | reality |
|---|---|---|
| `ROADMAP.md` §6 | tarballs "built and NOT published — the owner's call" | v0.7.0 published 2026-08-15 |
| `ROADMAP.md:578` | "no macOS build and no ARM64 build on any platform" | `make ci` run on macOS 2026-08-10; README says Apple Silicon |
| site, §04 | a second compiler "written in Tycho **that compiles itself**" | retired July; the Makefile says "no tychoc0 binary is built" |
| site, §03 vs stat band | "38 modules" vs "45 stdlib packages" | 45, counted |

`scripts/check_citations.py` verifies that every `path:line` reference resolves.
**Nothing verifies that two documents' claims agree with each other, or with the
tree.** A citation can be perfectly anchored and the sentence around it false.

This is what makes onboarding hard, and it is worse than missing documentation:
a newcomer reading `ROADMAP.md` learns that Tycho does not run on a Mac.

## 7c. Comment density — measured

| file | comment lines / non-blank | share |
|---|---:|---:|
| `Makefile` | 748 / 1,009 | **74%** |
| `scripts/ci.sh` | 271 / 475 | **57%** |
| `.githooks/pre-push` | 52 / 98 | **53%** |
| `tests/run.sh` | 167 / 481 | 35% |
| `corelib/json/json.ty` | 315 / 1,022 | 31% |
| `runtime/tycho_rt.c` | 839 / 2,920 | 29% |
| `src/tychoc.c` | 3,657 / 14,363 | 25% |

**A build file that is three-quarters prose is not documented, it is buried.**
`src/tychoc.c` alone carries 3,657 comment lines — more than the entire runtime
is long.

The house rule in force during this work is "never write more comments than
code". Every file above except the two newest obeys it only on a technicality,
and `Makefile`, `ci.sh` and `pre-push` violate it outright. The style is
self-reinforcing: each comment explains why an earlier decision was made, so the
next editor adds one explaining theirs.

This is the mechanical half of "onboarding is near impossible". The other half
is §7b — the prose is not merely voluminous, some of it is wrong.

## 8. What today actually found

Every defect found on 2026-08-17 was on the **website** (`gh-pages`, 5 files):
ten WCAG contrast failures, three dead CSS tokens, a figure whose animation
contradicted its own caption, a stale "compiles itself" claim about a compiler
retired in July, a `38 modules` / `45 packages` self-contradiction, and
**Listing 01 — the first code a visitor sees — using `range()`, removed from the
language.**

**Zero defects were found in the compiler, runtime or corelib.**

Two new gates now cover that surface (`contrast-check`, `site-code-check`, both
with negative controls, ~1.3 s combined). Before today, `core.hooksPath` was
relative, so **every site push in the project's history bypassed the pre-push
hook entirely**; it is absolute now.

## 9. Honest summary

A small, well-specified language with an unusually complete standard library,
30 real programs written in itself, a genuine specification, and a verification
apparatus more careful than most shipping languages — carrying 2.5× its own
weight in tests and 1.7× in prose, maintained by one person, and installed by
approximately nobody.

The engineering is not the problem. **The documentation is** — not because there
is too little of it, but because there is an enormous amount, some of it is
false, nothing checks it against the tree, and it is dense enough that a reader
cannot tell the wrong parts from the right ones. Three of my four worst mistakes
today came from believing a well-written sentence in this repository.

The things that would change the project's trajectory, in order:

1. **Make the documents agree with the tree.** `ROADMAP.md` alone told me 0.7
   was unpublished and that Tycho does not run on macOS; both false, both
   contradicted by other tracked files. Until this is fixed, no assessment of
   this project — mine or a newcomer's — can be trusted, and that is the actual
   reason it feels like it is going nowhere.
2. **Cut the prose.** A 74%-comment Makefile and 3,657 comment lines in the
   compiler are not documentation, they are a second codebase with no tests.
3. **Someone other than the owner has to hear it exists.** Everything §1 and §7
   of the roadmap need follows from this and nothing else does.
4. **Convert golden lanes to oracle lanes** where an external oracle exists.
   That is what turns the suite from confirming into finding.

**On the website:** it was rebuilt today and its measurable defects are fixed
(contrast, dead code sample, self-contradictory counts, a figure that disagreed
with its own caption), but the owner's assessment is that it is unprofessional,
and that judgement is not mine to overrule. Its opening sentence still calls
Tycho "a research project testing one idea", which contradicts the stated goal
of shipping a production language. That single sentence should be settled before
anything else on that page is touched again.

Items I raised today and did **not** close, because they need a decision that is
not mine: what Tycho *is* now in one sentence (the site still opens "A research
project testing one idea", which contradicts the stated goal); whether
announcing is on the table at all; and how far to take the removal of `tychoc0`
references (744 mentions across 278 files, most of them load-bearing rationale
rather than advertising).
