# 1. Scope and conformance · 2. Notation

## 1. Scope and conformance

### 1.1 Scope

This document specifies the syntax and semantics of the Tycho programming
language and its standard library (the *corelib*). It defines:

- the lexical and phrase grammar of a Tycho source program ([§3](01-lexical.md),
  [§4](02-grammar.md));
- the type system, type inference, and generic instantiation;
- the memory and object model — value semantics and the implicit hierarchical
  arena model on which the language rests;
- the meaning of every declaration, statement, and expression;
- the concurrency model and its ordering guarantees;
- the foreign-function interface (FFI) to C;
- the built-in functions and the standard library.

It does **not** specify: the compiler's command-line interface, its diagnostic
message text, the layout of generated C, or the performance of any construct
(performance characteristics are described informatively in
[docs/thesis.md](../thesis.md) and [docs/perf.md](../perf.md)).

Tycho is defined **abstractly**: scalar widths and behavior are fixed
independently of any compilation target (§5). The reference
implementation transpiles to C; that lowering is *one conforming realization*,
never the definition. The single, minimal register of behavior an
implementation is permitted to vary is collected in
[Appendix F](appendix-f-impl-defined.md).

### 1.2 The two-implementation contract

Tycho is unusual among specified languages: its behavioral contract is already
pinned by **two independent implementations that agree byte-for-byte**.

- `tychoc` — the reference transpiler (C).
- `tychoc0` — a self-hosted transpiler written in Tycho.

The project asserts, as a standing gate, that `tychoc0` compiled by itself
reproduces its own emitted C byte-for-byte (the *fixpoint*), that both
transpilers produce identical program output across the whole test suite, and
that both make identical accept/reject decisions (differential fuzzing plus the
`typeparity`/`eqparity`/`unaryparity`/`parforparity` lanes). See
[STATUS.md](../../STATUS.md).

This specification is written *from* that contract. Every normative clause is
required to be verifiable against both implementations. Where this document and
the implementations disagree, it is a defect in one or the other, to be
reconciled — not a license for an implementation to differ.

### 1.3 Conformance

A **conforming program** is a Tycho source program (or package) that this
specification permits, together with any inputs it reads.

A **conforming implementation** is one that:

1. accepts every conforming program this specification requires to be accepted,
   and rejects (with a diagnostic, before producing an executable) every program
   this specification requires to be rejected; and
2. for an accepted program, produces an executable whose observable behavior
   (its output, exit status, and externally-visible effects) is one this
   specification permits for that program on its inputs.

Conformance is defined at two tiers:

- **Core tier.** The language ([§3](01-lexical.md) onward) and the corelib
  packages that are pure Tycho or depend only on the C standard library. A
  core-tier implementation MUST provide all of these.
- **Extended tier.** Additionally the corelib packages that depend on an
  external C library via `pkg-config` (`http` → libcurl, `crypto` → libcrypto,
  `compress` → zlib, `image` → libpng, `tls` → openssl). An implementation MAY
  omit the extended tier and still conform at the core tier; a program that
  imports an absent extended package MUST be diagnosed, not silently mis-linked.

The reference test harness already embodies this split: `deps`-gated package
tests skip (rather than fail) when the external library is absent.

An implementation MUST NOT accept a program this specification requires to be
rejected. **Failing closed** — rejecting a doubtful program rather than
compiling it to undefined behavior — is a normative principle of this language
([§30](17-runtime.md)), inherited from the reference
implementation's design.

### 1.4 Requirement keywords

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHALL**, **SHALL NOT**,
**SHOULD**, **SHOULD NOT**, **RECOMMENDED**, **MAY**, and **OPTIONAL** in this
document are to be interpreted as described in RFC 2119 and RFC 8174 when, and
only when, they appear in all capitals.

Two further terms are used, always explicitly and collected in
[Appendix F](appendix-f-impl-defined.md):

- **unspecified** — behavior for which this specification imposes no
  requirement and provides no choices. A conforming program MUST NOT depend on
  it; different conforming implementations may behave differently, and the same
  implementation may behave differently on different occasions.
- **implementation-defined** — behavior an implementation is permitted to
  choose but MUST document. Tycho's abstract-exact policy keeps this register
  deliberately small.

### 1.5 Versioning

This specification defines **Tycho 1.0**, which is the first *frozen* version of
the language. Subsequent releases are named by year under a date-based scheme
(e.g. `Tycho 2027`), following the project's stated versioning direction. A
release names the language version it specifies; a conforming implementation
states which version it implements.

Within a frozen version, this document is normative and stable; corrections that
change observable behavior are issued as errata against a named version, never
silently.

## 2. Notation

### 2.1 Grammar notation

Tycho's grammar is presented in two layers.

- The **lexical grammar** ([§3](01-lexical.md)) maps source *characters* to
  *tokens*, including the synthetic layout tokens `NEWLINE`, `INDENT`, and
  `DEDENT`. Tycho is indentation-sensitive, so this layer is not context-free
  and is described partly by prose (the indentation algorithm, §3.4).
- The **phrase grammar** ([§4](02-grammar.md)) maps *tokens* to program
  structure. Its terminals are token kinds, written in `UPPER_CASE` (e.g.
  `IDENT`, `INT`, `NEWLINE`) or as the literal spelling of punctuation and
  keywords in double quotes (e.g. `"fn"`, `":="`, `"->"`).

Both use a W3C-style EBNF:

| Form | Meaning |
|---|---|
| `A ::= …` | production defining nonterminal `A` |
| `"x"` | a literal terminal (keyword or punctuation spelling) |
| `UPPER` | a terminal token kind |
| `A B` | `A` followed by `B` |
| <code>A &#124; B</code> | `A` or `B` |
| `A?` | zero or one `A` |
| `A*` | zero or more `A` |
| `A+` | one or more `A` |
| `( … )` | grouping |
| `[abc]`, `[0-9]` | a character class (lexical grammar only) |
| `/* … */` | a comment on the production, non-normative |

Where the grammar accepts a form that a later static rule then rejects (for
example, a value `if` with no `else`), the grammar production is annotated and
the rejecting rule is stated in the relevant semantic section. The grammar
alone does not define validity; a program is valid only if it also satisfies
the static-semantic rules.

### 2.2 Source of record

The reference transpiler `src/tychoc.c` is the authoritative source for the
grammar and for behavior; `compiler/tychoc0.ty` is the co-equal cross-check.
Sections of this document carry a `Provenance:` note citing the authoritative
location, so any clause can be re-verified. These notes are non-normative
aids; the prose and grammar are the specification.

The tree-sitter grammar under `editors/zed/` is a **non-normative** editor
highlighter. It does not model indentation and is known to diverge from the
language in several places (enumerated in [§3.10](01-lexical.md#310-non-normative-tree-sitter-grammar));
it MUST NOT be treated as a grammar of record.

### 2.3 Examples

Every fenced Tycho example in this specification is a complete or clearly
excerpted program that compiles and runs on both reference implementations and
produces the output shown in its trailing comment. A forthcoming `make
spec-check` gate enforces this, as the reference pages are already enforced.
Examples are illustrative; a conflict between an example and a normative rule
is a defect to be corrected in favor of the rule.
