# Roadmap

Tycho is an experimental proof-of-concept, and this is a direction, not a promise of
dates. Day-to-day work is tracked in [GitHub issues](https://github.com/StefanVonRanda/tycho/issues);
this file is the high-level shape.

## Where it is

The language, the self-hosted compiler, and the 36-package core library are
feature-complete for the thesis they exist to prove. The current strength is the
correctness harness — two independent compilers held to byte-identical self-hosting, a
differential fuzzer, and a golden-locked test suite, all green in a local gate
(`make ci`). See [docs/architecture.md](docs/architecture.md) for what each gate proves.

## Near-term

Foundation before feature breadth. In rough priority:

1. **Demand-gated corelib / tooling.** New library and tooling work is built against a
   real program that needs it, never ahead of one — e.g. more image codecs, richer
   date/time, additional networking. Requests belong in the issue tracker.
2. **Keeping the two compilers honest.** As new language features land, each is
   adversarially fuzzed and gated before shipping. This is ongoing, not a milestone.

## Non-goals

Several things are deliberately, permanently out of scope — traits, a package manager, a
C-style ternary, Hindley-Milner inference, refcounting/GC, and hosted CI among them. The
full list with rationale is in [docs/architecture.md](docs/architecture.md#decided-non-goals).
Please don't open issues proposing them; issues that explore *new* directions are welcome.
