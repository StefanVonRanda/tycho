# Roadmap

Tycho is 1.0 — the language surface and the spec are stable, and this file is
a direction, not a promise of dates. Day-to-day work is tracked in [GitHub
issues](https://github.com/StefanVonRanda/tycho/issues); this file is the
high-level shape.

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

## Non-goals

Several things are deliberately, permanently out of scope — traits, a package manager, a
C-style ternary, Hindley-Milner inference, refcounting/GC, and hosted CI among them. The
full list with rationale is in [docs/architecture.md](docs/architecture.md#decided-non-goals).
Please don't open issues proposing them; issues that explore *new* directions are welcome.
