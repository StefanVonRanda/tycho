# The Tycho language specification

> **Status: DRAFT / in progress.** This is the formal specification of the Tycho
> language — the normative contract a conforming implementation must satisfy.
> It is being written per [ROADMAP §1.8](../../ROADMAP.md) and the build plan in
> [docs/internals/spec-plan.md](../internals/spec-plan.md).
>
> Last synced against `src/tychoc.c` + `compiler/tychoc0.ty`: **2026-07-12**.
> A complete first draft of all chapters (1–33) and appendices (A–H) now exists.
> It is a **draft**: it has not yet been through the Phase-5 conformance pass
> (`make spec-check` + the coverage matrix), a handful of corners carry
> `Editor's note` markers pending further probing, and the reference-doc drifts
> in [Appendix H](appendix-h-differences.md) remain to be back-ported. For any
> area, the [language reference](../reference/index.md) remains a useful
> companion.

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
SHOULD, MAY) follow [RFC 2119/8174](00-conventions.md#requirement-keywords).
The grammar is W3C-style EBNF over lexer-produced tokens (Tycho is
indentation-sensitive; see [§3](01-lexical.md)).

## Contents

| # | File | Chapters | Status |
|---|------|----------|--------|
| — | [README](README.md) | index | — |
| 00 | [Conventions](00-conventions.md) | 1 Scope & conformance · 2 Notation | **draft** |
| 01 | [Lexical structure](01-lexical.md) | 3 Source text, tokens, indentation | **draft** |
| 02 | [Grammar](02-grammar.md) | 4 The phrase grammar | **draft** |
| 03 | [Types](03-types.md) | 5 | **draft** |
| 04 | [Type inference](04-inference.md) | 6 | **draft** |
| 05 | [Generics](05-generics.md) | 7 | **draft** |
| 06 | [Conversions](06-conversions.md) | 8 | **draft** |
| 07 | [Memory & object model](07-memory-model.md) | 9 Value semantics · 10 Arenas · 11 `inout` | **draft** |
| 08 | [Declarations & scoping](08-declarations.md) | 12 | **draft** |
| 09 | [Expressions](09-expressions.md) | 13 | **draft** |
| 10 | [Statements & control flow](10-statements.md) | 14 | **draft** |
| 11 | [Functions](11-functions.md) | 15 | **draft** |
| 12 | [Aggregates](12-aggregates.md) | 16 arrays · 17 structs/tuples · 18 maps · 19 enums/match | **draft** |
| 13 | [Concurrency](13-concurrency.md) | 20–23 | **draft** |
| 14 | [FFI](14-ffi.md) | 24–26 | **draft** |
| 15 | [Program & packages](15-program.md) | 27 Program structure · 28 Packages & modules | **draft** |
| 16 | [Builtins](16-builtins.md) | 29 | **draft** |
| 17 | [Runtime behavior](17-runtime.md) | 30 | **draft** |
| 18 | [Standard library](18-library.md) | 31–33 corelib | **draft** |
| A | [Collected grammar](appendix-a-grammar.md) | pointer to §3–§4 | **draft** |
| B | [Keywords](appendix-b-keywords.md) | reserved + contextual | **draft** |
| C | [Precedence](appendix-c-precedence.md) | operator table | **draft** |
| D | [Builtin index](appendix-d-builtins.md) | locator | **draft** |
| E | [Conformance map](appendix-e-conformance.md) | clause → fixture | **draft** |
| F | [Impl-defined register](appendix-f-impl-defined.md) | unspecified + impl-defined | **draft** |
| G | [Glossary](appendix-g-glossary.md) | terms | **draft** |
| H | [Differences](appendix-h-differences.md) | reference-doc drift log | **draft** |

## Conformance in one paragraph

An implementation **conforms at the core tier** iff, across the conformance
suite ([Appendix E](appendix-e-conformance.md)), it accepts exactly the
programs the reference accepts, rejects exactly those it rejects, and produces
byte-identical program output — for the language and the pure-Tycho + libc-only
corelib. The **extended tier** additionally provides the `deps`/pkg-config
corelib packages (`http`, `crypto`, `compress`, `image`, `tls`); an
implementation MAY omit them and still conform at the core tier
([§1](00-conventions.md#conformance)).
