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
[docs/thesis.md](../thesis.md) and [docs/guides/perf.md](../guides/perf.md)).

Tycho is defined **abstractly**: scalar widths and behavior are fixed
independently of any compilation target (§5). The reference
implementation transpiles to C; that lowering is *one conforming realization*,
never the definition. The single, minimal register of behavior an
implementation is permitted to vary is collected in
[Appendix F](appendix-f-impl-defined.md).

### 1.2 The reference implementation

**`tychoc`** — the transpiler in `src/tychoc.c` — is the **reference
implementation** of this specification. Where an example, a fixture, or a
diagnostic is cited below, it is `tychoc`'s.

This document is nonetheless *normative* and `tychoc` is not: where the two
disagree, it is a defect in one of them, to be reconciled. The specification
does not defer to the implementation, and no implementation's behavior
establishes a requirement this document does not state.

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

Conformance is a claim about **this specification**, not about agreement with any
particular compiler. It is *checked* against the reference implementation and the
golden fixture corpus of [Appendix E](appendix-e-conformance.md) — a fixture
records the behavior this document requires, so a second implementation that
reproduces the corpus and this document's requirements conforms whether or not it
resembles `tychoc` internally.

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

This specification defines **Tycho 0.5** and is maintained against the current
release (0.7.0); where the two differ, `CHANGELOG.md` records the change. It was
ratified on 2026-07-13 under the
name *Tycho 1.0* and renamed on 2026-08-09 when the project was demoted to 0.5;
no technical content changed with the rename.

**0.5 is NOT a frozen version.** The freeze was the substance of the 1.0 claim,
and it is withdrawn with it: until 1.0, a change to observable behavior is issued
as a changelog entry against a named version rather than as an erratum, and there
is no deprecation window. The document remains normative — the implementation is
gated against it, so a divergence is a bug in one of the two, not a licence to
ignore the spec.

From 1.0 onward the original scheme applies: the version is frozen, subsequent
releases are named by year (e.g. `Tycho 2027`), a release names the language
version it specifies, a conforming implementation states which version it
implements, and corrections that change observable behavior are issued as errata
against a named version, never silently.

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

The tree-sitter grammar under `editors/zed/` is a **non-normative** editor
highlighter. It does not model indentation and is known to diverge from the
language in several places (enumerated in [§3.10](01-lexical.md#310-non-normative-tree-sitter-grammar));
it MUST NOT be treated as a grammar of record.

### 2.3 Examples and code fences

Fenced code blocks carry a language tag identifying their kind:

- **`ebnf`** — a grammar production (see §2.1). The collected grammar in
  [Appendix A](appendix-a-grammar.md) is generated from these blocks.
- **`tycho`** — Tycho source. Most such blocks are *illustrative fragments* — a
  declaration, an expression, or a few statements shown in isolation, often
  referring to names defined in the surrounding prose. A fragment is not a
  complete program and is not executed.
- **`output`** — the exact expected standard output of the immediately preceding
  `tycho` block.
- **`text`** — a keyword list, a shell/`cc` invocation, or *meta-syntax*: prose
  notation that is not literal Tycho. Meta-syntax uses `…` for elision, `<name>`
  for a placeholder, `{ a | b }` for a choice, and `[ x ]` for an optional part.

A **runnable example** is a `tycho` block immediately followed (blank lines
permitted) by an `output` block: the `tycho` block is then a complete program
and its standard output MUST equal the `output` block byte-for-byte. The `make
spec-check` gate ([Appendix E.3](appendix-e-conformance.md)) builds and runs
every runnable example on the reference `tychoc` and asserts this — the two-compiler oracle of
[§1.3](#13-conformance) applied to the spec's own examples.

Examples are illustrative; a conflict between an example and a normative rule is
a defect to be corrected in favor of the rule.
