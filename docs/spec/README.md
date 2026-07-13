# The Tycho language specification

> **Status: ratified — Tycho 1.0 (2026-07-13).** The formal specification of the
> Tycho language: the normative contract a conforming implementation must satisfy
> ([ROADMAP §1.8](../../ROADMAP.md)).
>
> **Every chapter (1–33) and appendix (A–H) is cross-checked against the reference
> compiler `src/tychoc.c`**, one adversarial audit per chapter with each finding
> re-verified against source. Internal links, `§`-labels, and section
> cross-references are verified mechanically; every behavioral corner (float/`char`
> semantics, evaluation order, `>>` sign behavior, `range` step 0, the concurrency
> ordering guarantees, the FFI sized-int round-trip) is pinned by differential
> probing on both compilers. The content is verified against the implementation,
> not merely asserted.
>
> The specification is kept honest by the `make spec-check` gate (CI step 17): it
> regenerates the collected grammar ([Appendix A](appendix-a-grammar.md)) from the
> defining chapters §3/§4 and diffs it, asserts every fixture the conformance
> matrix ([Appendix E](appendix-e-conformance.md)) cites exists, and builds and
> runs every runnable `tycho`/`output` example
> ([§2.3](00-conventions.md#23-examples-and-code-fences)) on **both** compilers
> (`tychoc` and the self-hosted `tychoc0`) against its shown output. New material
> is gated automatically.

## What this document is

Tycho already has a mechanically-checkable behavioral contract: **two
implementations that agree byte-for-byte** (`tychoc`, the C reference
transpiler, and `tychoc0`, the self-hosted one), a **differential fuzzer**, a
**274-fixture golden suite**, and **accept/reject parity lanes**. This
specification *distills that contract into prose and grammar* — it is what lets
a third implementation reproduce Tycho exactly.

Where the [reference pages](../reference/index.md) teach the language by
example, this document defines it by rule. On any conflict, **the specification
governs**, and the divergence is logged in [Appendix H](appendix-h-differences.md).

## How to read it

The files are numbered in normative reading order. Requirement keywords (MUST,
SHOULD, MAY) follow [RFC 2119/8174](00-conventions.md#14-requirement-keywords).
The grammar is W3C-style EBNF over lexer-produced tokens (Tycho is
indentation-sensitive; see [§3](01-lexical.md)).

## Contents

| # | File | Chapters | Status |
|---|------|----------|--------|
| — | [README](README.md) | index | — |
| 00 | [Conventions](00-conventions.md) | 1 Scope & conformance · 2 Notation | **1.0** |
| 01 | [Lexical structure](01-lexical.md) | 3 Source text, tokens, indentation | **1.0** |
| 02 | [Grammar](02-grammar.md) | 4 The phrase grammar | **1.0** |
| 03 | [Types](03-types.md) | 5 | **1.0** |
| 04 | [Type inference](04-inference.md) | 6 | **1.0** |
| 05 | [Generics](05-generics.md) | 7 | **1.0** |
| 06 | [Conversions](06-conversions.md) | 8 | **1.0** |
| 07 | [Memory & object model](07-memory-model.md) | 9 Value semantics · 10 Arenas · 11 `inout` | **1.0** |
| 08 | [Declarations & scoping](08-declarations.md) | 12 | **1.0** |
| 09 | [Expressions](09-expressions.md) | 13 | **1.0** |
| 10 | [Statements & control flow](10-statements.md) | 14 | **1.0** |
| 11 | [Functions](11-functions.md) | 15 | **1.0** |
| 12 | [Aggregates](12-aggregates.md) | 16 arrays · 17 structs/tuples · 18 maps · 19 enums/match | **1.0** |
| 13 | [Concurrency](13-concurrency.md) | 20–23 | **1.0** |
| 14 | [FFI](14-ffi.md) | 24–26 | **1.0** |
| 15 | [Program & packages](15-program.md) | 27 Program structure · 28 Packages & modules | **1.0** |
| 16 | [Builtins](16-builtins.md) | 29 | **1.0** |
| 17 | [Runtime behavior](17-runtime.md) | 30 | **1.0** |
| 18 | [Standard library](18-library.md) | 31–33 corelib | **1.0** |
| A | [Collected grammar](appendix-a-grammar.md) | collected EBNF (§3–§4) | **1.0** |
| B | [Keywords](appendix-b-keywords.md) | reserved + contextual | **1.0** |
| C | [Precedence](appendix-c-precedence.md) | operator table | **1.0** |
| D | [Builtin index](appendix-d-builtins.md) | locator | **1.0** |
| E | [Conformance map](appendix-e-conformance.md) | clause → fixture | **1.0** |
| F | [Impl-defined register](appendix-f-impl-defined.md) | unspecified + impl-defined | **1.0** |
| G | [Glossary](appendix-g-glossary.md) | terms | **1.0** |
| H | [Differences](appendix-h-differences.md) | reference-doc drift log | **1.0** |

## Conformance in one paragraph

An implementation **conforms at the core tier** iff, across the conformance
suite ([Appendix E](appendix-e-conformance.md)), it accepts exactly the
programs the reference accepts, rejects exactly those it rejects, and produces
byte-identical program output — for the language and the pure-Tycho + libc-only
corelib. The **extended tier** additionally provides the `deps`/pkg-config
corelib packages (`http`, `crypto`, `compress`, `image`, `tls`); an
implementation MAY omit them and still conform at the core tier
([§1](00-conventions.md#13-conformance)).
