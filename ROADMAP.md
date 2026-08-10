# Roadmap

Tycho is **0.5 — pre-1.0**, and this file is a direction, not a promise of
dates. Day-to-day work is tracked in [GitHub
issues](https://github.com/StefanVonRanda/tycho/issues); this file is the
high-level shape. What 1.0 now requires is [below](#what-1-0-requires).

## Where it is

The language, the self-hosted compiler, and the 46-package core library are
feature-complete for the thesis they exist to prove. The current strength is the
correctness harness — an adversarial fuzzer, sanitizer lanes over both the compiler and
the programs it emits, and a golden-locked test suite, all green in a local gate
(`make ci`). See [docs/architecture.md](docs/architecture.md) for what each gate proves.

Until 2026-07-29 that harness also included a **two-compiler differential**: every
program was compiled by `tychoc` and by the self-hosted `compiler/tychoc0.ty`, and the
two had to agree. That is over — see below for what it proved and what its loss costs.

## Near-term

Foundation before feature breadth. In rough priority:

1. **Demand-gated corelib / tooling.** New library and tooling work is built against a
   real program that needs it, never ahead of one — e.g. more image codecs, richer
   date/time, additional networking. Requests belong in the issue tracker.
2. **Keeping the compiler honest.** As new language features land, each is
   adversarially fuzzed and gated against recorded goldens before shipping. This is
   ongoing, not a milestone.

   The self-hosted `compiler/tychoc0.ty` is **out of this loop as of 2026-07-26**. It
   proved self-hosting — compiled by itself it reproduced its own emitted C
   byte-for-byte — and is preserved frozen as that artifact. No language change is
   mirrored into it, so it compiles the language as it stood on the freeze date and will
   diverge from `tychoc` on which programs it accepts. It is not deprecated for being
   wrong; it is finished. `tychoc` is the reference implementation and the
   [spec](docs/spec/) is normative.

### The `tychoc0` freeze lanes were retired on 2026-07-29

Between the 2026-07-26 freeze and 2026-07-29, `tychoc0` was still **built and run** by
two hand-run lanes (`compiler/fixpoint.sh`, `scripts/frontparity.sh`) and by fourteen
other non-gated runners. None of them was in `make ci` — the earlier claim that "no gate
builds or runs it" was true only of `make ci` itself, not of the tree.

**"Nothing builds it at all" was written here on 2026-07-29 and was not true until
2026-08-09.** `make selfhost-check` — `make ci` step [3n/20], ~50s of every sweep — went
on building `tychoc0` three ways to re-assert the self-emission fixed point. It was
retired on 2026-08-09 by owner decision, and the sentence is now accurate: no lane builds
`tychoc0`. `compiler/selfhost.sh` is kept unrun with a header recording what it proved.
`tychoc0.ty` is still COMPILED by every sweep as ASan/UBSan input to
`scripts/asan_self.sh` — it remains the compiler's hardest test program — but the
byte-identity of its own emission is no longer checked anywhere.

**Why.** The three-clause `for i := 0; i < n; i += 1:` and bare `for:` replace
`for i in range(...)`, and the `range` builtin is deleted. This is a *breaking* change.
A frozen compiler that must still compile the whole corpus and a corpus that must adopt
new syntax cannot both be satisfied; `tychoc0` cannot parse the new loop forms and never
will, because nothing is mirrored into it. The corpus won.

**What was given up, stated plainly.** Continuous proof that `tychoc0` accepts what
`tychoc` accepts, and that the two produce the same program output. This was not
theatre. The differential caught a real defect: an over-tightening of the newtype path
made `tychoc0` refuse `if dup == ids:` (`tests/newtype_agg.ty`), reddening
`compiler/fixpoint.sh`. The C compiler was perfectly happy with that program — only a
second, independent implementation disagreeing revealed that the frontend had been
silently narrowed. **That is the class of defect that will now go uncaught:** a change
that quietly reduces what the language accepts, where the only compiler left to ask is
the one that was changed. No lane in `make ci` replaces it, and none is planned to.

**What the artifact still is.** `compiler/tychoc0.ty` stays on disk, unchanged. It is
the evidence that Tycho self-hosts; that result is a fact about the commit that proved
it and retiring the lanes does not undo it. It remains the largest single Tycho source
in the tree (~16k lines) and `scripts/asan_self.sh` still feeds it to `tychoc` as
**input** under ASan/UBSan, so it continues to earn its place as the compiler's hardest
test program. What ended is the claim that it is *continuously checked* — not the
artifact, and not the result. Every retired lane keeps its file with a header recording
what it proved and when it stopped running.

## What 1.0 requires

The number was declared on 2026-08-05 and withdrawn on 2026-08-09. What was
wrong with it is worth stating precisely, because it decides the list: **the
engineering was never the problem.** Two platforms green under `make ci`, a
200-seed adversarial fuzzer with zero findings, sanitizer lanes over the
compiler and the programs it emits, 46 corelib packages each tested three ways
and golden-locked, and a normative spec the implementation is gated against —
that is a stronger evidence base than most languages have at 1.x.

What was missing is that **1.0 is a promise not to break people, and there were
no people.** `git tag` showed `v0.1.0` and nothing else; the `[1.0.0]` changelog
entry described a release that was never cut. A freeze nobody has pulled on is
not a freeze, it is a guess about which parts matter.

So the conditions below are mostly about contact with reality, not about
building more.

### 1. Someone other than the author has written a real program

The blocking one, and nothing else on this list substitutes for it. Every
ergonomic finding in [docs/internals/FRICTION.md](docs/internals/FRICTION.md)
came from the author dogfooding against a program he also designed. That
catches a lot — the file is unusually honest about its own defeats — but it
cannot catch what only a stranger's mental model produces.

Concretely: **three non-trivial programs by two people who did not write the
compiler**, each with its friction written down. Until then the API freeze is
guesswork about which parts of the surface people actually reach for.

### 2. The daily papercuts are gone

These are ergonomic, not soundness — which is exactly why they must be fixed
*before* a freeze rather than after, since fixing them later is a breaking
change. All were recorded by the project itself, in `plan.md`'s phase-1
learnings and in FRICTION:

- ~~a package-level `len` **shadows the builtin** inside its own package~~ —
  **closed 2026-08-09**, see the probe table below
- ~~no `defer`~~ — **refused 2026-08-10** (see the probe table); ~~and no bare
  `pass`/no-op statement~~, **closed 2026-08-10**
- ~~**no expression line continuation**~~ — **closed 2026-08-09**
- consts do not cross package boundaries, so levels ship as functions
- `or_return` requires a `Result`-returning function, which `main()` is not
- a newtype-of-array blocks `push`
- an aggregate capturing a still-live binding warns until you write the copy by
  hand (`arg := hostile`)
- the typed empty is `[]string`, which nobody guesses first

Each is small. Together they are what "the language is unpleasant on day two"
is made of.

**Re-probed against the compiler on 2026-08-09** rather than trusted from the
phase-1 notes, because two entries turned out to be wrong:

| Papercut | State |
|---|---|
| package-level `len` shadows the builtin | **closed 2026-08-09** — still legal (§3.7 permits it on purpose), but the compiler now warns at the declaration and names the consequence, so the wrong answer is no longer silent. Fixture `tests/warn/shadow_builtin.ty`. |
| no bare `pass` | **closed 2026-08-10** — `pass` is the no-op statement. CONTEXTUAL, not reserved: significant only as a whole statement, because `pass` was already a variable name in `corelib/test/testing` and `tools/prunner` and reserving it would have broken the runner that scores `make test-fast`. Fixtures `tests/pass_stmt.ty`, `tests/reject/pass_as_value.ty`. |
| no expression line continuation | **closed 2026-08-09** — a line ending on an operator joins the next, when that line is indented deeper. The deeper-indent condition is what keeps a truncated line a truncated line: `tests/diag/caret_expr.ty` caught the naive version degrading its own diagnostic. Fixture `tests/line_continuation.ty`. |
| no `defer` | **refused 2026-08-10, documented.** Not a papercut: this language has three cleanup mechanisms already and `defer` would be a fourth. Memory is arena-freed at scope exit; a `handle` runs its declared `free:` at scope exit (RAII, affine, §25); a channel/task handle is affine with an implicit join. `core:io` is path-based and opens nothing. The tree's `close(` calls are overwhelmingly CHANNEL closes, not resource cleanup. Already found once and filed anyway — `docs/internals/plan-tycho-vm-DONE.md:27-32`, whose own verdict was "nothing needs it; it was filed because the probe surprised me, which is not a reason." |
| `or_return` from `main()` | open, but the diagnostic names the rule |
| newtype-of-array blocks `push` | open — `push's first argument must be an array or soa` for `type Row = [string]` |
| aggregate capturing a live binding warns | open |
| ~~typed empty is `[]string`~~ | **not a defect** — both `xs : [string] = []` and `xs := []string` compile. It is a learning-curve item, not a language gap. |
| consts across package boundaries | not re-probed |

Priority inside the list was `len` shadowing, because it was the only one that
produced a wrong answer instead of an error message. It is closed; the rest are
worked top to bottom by cost.

### 3. The expressiveness gaps close, or become documented refusals

Checked on 2026-08-09 rather than inherited from the FRICTION entry:

| Gap | State |
|---|---|
| `Result(void, E)` not expressible | **open, and recorded too harshly** — see below |
| no comparator-taking sort | **open** — `sort.by_key` takes a derived `int` key only, so sorting by a string key means inventing an int |
| an enum cannot be tested for its variant without binding a payload | open (FRICTION §5, 2026-08-01) |
| two error types cannot share an `or_return` chain | open (FRICTION §6) |
| `core:iter` unusable for a fallible pipeline stage | open (FRICTION §7) |
| `core:decimal` has no `div` | **closed** — `decimal.div` exists |
| `[string]` cannot cross the FFI | open by design — it forced `core:os`'s builder-handle API; either lift it or write down that it never lifts |

Two corrections from the same 2026-08-09 probe.

Testing an enum variant is **mitigated** — since the pattern-discard fix,
`match e: A(_): ...` binds nothing and reads fine, so the gap is stylistic
rather than structural.

`Result(void, E)` was recorded as "not expressible", which overstated it: a
one-field `Unit` struct worked with no compiler change, so the gap was
boilerplate rather than a wall. **CLOSED 2026-08-09**, in two commits.

```tycho
fn touch(x: int) -> Result(void, string):
    if x < 0:
        return Err("neg")
    return Ok()
```

The blocker named here was **`T_VOID` being overloaded**: it was both the
no-return type and the sentinel for *unbound generic type parameter*, so
binding a payload parameter to void would have read as "not yet bound". The
recommendation below — re-sentinel to `T_UNBOUND` — was right, and it was
**necessary but not sufficient**, which the table's "Buys" column had wrong.
`void` was not in the type grammar at all: `parse_type_inner` had no `void`
branch, so the three `== T_VOID` guards inside it were defensive-unreachable
and the collision was purely latent. Re-sentinelling cleared the trap; the
spelling still had to be built on top of it.

What shipped, with the surface chosen by the owner: `void` is spellable in
exactly one position, a `Result`'s ok payload, and it is a type with **no
values** — constructed by a zero-argument `Ok()`, matched by a bare `Ok:` arm,
never bindable. The permission is one level deep, so `Result(Option(void), E)`
is still refused. The `or_return` STATEMENT form (`f() or_return`, previously
rejected as a bare expression with no effect) came with it and is not optional:
with no value to bind there is no `x := f() or_return` to write, so without it
the feature would have had no usable propagation form. Spec:
[§5.3.6](docs/spec/03-types.md#536-enums-option-result).

The four options as they were tabled, for the record:

| Option | Cost | Buys |
|---|---|---|
| **Re-sentinel `T_VOID` → `T_UNBOUND`** *(taken)* | 6 sites, all reached from the single `new_binds()` — the estimate of ~8 was close | it cleared the latent trap. It did NOT buy "the expected spelling": that was the type grammar, the `Ok()` constructor, the bare `Ok:` arm, the `char okv` placeholder in the emitted C, and the `or_return` statement form — a second commit |
| New `T_UNIT` type | full compiler surface — codegen, eq, copy, `str` | not taken: same result, more surface |
| Ship `Unit` in corelib | tiny | not taken: removes the boilerplate, leaves the wart visible |
| Document the workaround | none | not taken |

An earlier note in this file called this "a spelling decision as much as a
type-system one", inferred from the compiler using the word "void" in its own
diagnostics. That was wrong — diagnostic text is not evidence about the type
system — and the sentinel collision is why.

"Documented refusal" is a legitimate answer for any row. An undecided row is
not.

### 4. `core:net` gets a readiness call, or the cap becomes a stated limit

`corelib/net/net_shim.c` has 12 exports and not one is `poll`/`select`/`epoll`
or `O_NONBLOCK`, so a server's worker count is a hard ceiling on concurrent
connections. This was refused once with a number — ~283 lines across 4 files
plus a redesign of `core:httpd`'s blocking read surface — and that refusal is
defensible. What is not defensible at 1.0 is leaving it implicit: either it
lands, or the README says "N workers means N concurrent connections" where
people will read it before they build on it.

### 5. Windows is at parity or its differences are non-goals

`make ci` is green there, which is real. One measured behavioural difference
remains: a thread parked in `recv` on an accepted connection is not released by
the shutdown handler as it is on Linux, so a Windows server winds down within
its idle timeout rather than within a millisecond
([SECURITY.md](SECURITY.md)). Decide whether that is a bug to fix or a
documented platform limit; do not ship a freeze with it undecided.

### 6. A release actually ships, and the support policy is exercised once

Tag it, build the tarballs for each platform, publish, and then **deprecate one
thing through the full path** — doc notice, changelog entry, compiler warning,
removal in the next major. A deprecation policy that has never been run is
prose, not a process.

### 7. An external security review

[SECURITY.md](SECURITY.md) says plainly that there has been no third-party
audit. The internal review was real and found real things — the FFI ownership
conventions were checked per shim, and the shell-injection class it flagged was
closed on 2026-08-09 by `core:os`'s argv path — but the FFI boundary is unsafe
by design and nobody outside the project has looked at it. 1.0 invites people
to build on that boundary.

### Order of work

Set 2026-08-09, cheapest-and-safest first, so each step is verifiable before the
next depends on it. Compiler changes are ordered late within each group because
every one costs a citation re-anchor (~50-110 anchors move) and a full `make ci`.

1. ~~`len` shadowing~~ — **closed**, `b267d2b`. The only silent-wrong-answer item.
2. ~~expression line continuation~~ — **closed**, `621bf64`.
3. ~~`Result(void, E)`~~ — **closed**, `3a67bbe` (re-sentinel) and the commit
   after it (the spelling). The re-anchor cost the table above warned about was
   real: the first commit was written line-count-neutral and moved zero
   citations; the second moved 87 files' worth.
4. ~~`core:sort` comparator~~ — **closed**, `sort.sort_by(xs, cmp)`. The gate
   prediction held exactly: corelib only, no citation moved, `make corelib` and
   `make corelib-examples` were the whole verification.
5. ~~`pass`~~ — **closed 2026-08-10**, and it was NOT a real keyword: two files
   already used the name, so it is contextual like `const`/`delete`. Cost was the
   predicted surface minus the lexer: parser, spec, `appendix-b-keywords`, the
   grammar appendix, `tycho-lsp`, `editors/` (both grammars, zed regenerated),
   fixtures, goldens. `tychofmt` needed nothing — `pass` is one token on a line.
   ~~Then `defer`~~ — **refused 2026-08-10, and the refusal is the point.** This
   page called it "the largest item on this page" and priced the full keyword
   surface. That was scoping a feature nobody had asked what it was for. An
   arena-scoped language with RAII handles has no work for it: memory is
   arena-freed at scope exit, a `handle` runs its declared `free:` at scope exit,
   and channel/task handles are affine with an implicit join. **This was already
   found once** and filed anyway — `docs/internals/plan-tycho-vm-DONE.md:27-32`
   ends "nothing needs it; it was filed because the probe surprised me, which is
   not a reason." Leaving the row open is what made it get re-scoped; it is a
   documented refusal now.
6. Then the remaining 1.0 conditions: `core:net` readiness (§4), the Windows
   parked-`recv` decision (§5), and shipping 0.5.0 plus one exercised
   deprecation (§6).

Item 1 — real programs by other people — is not sequenced here because it is not
a task this list can complete. It gates the freeze, not the work.

### What is explicitly NOT required

More features, more corelib packages, more benchmarks, or a package manager.
The language is feature-complete for the thesis it exists to prove. Adding
surface before anyone has used the existing surface is how the freeze gets
guessed wrong a second time.

## Non-goals

Several things are deliberately, permanently out of scope — traits, a package manager, a
C-style ternary, Hindley-Milner inference, refcounting/GC, and hosted CI among them. The
full list with rationale is in [docs/architecture.md](docs/architecture.md#decided-non-goals).
Please don't open issues proposing them; issues that explore *new* directions are welcome.
