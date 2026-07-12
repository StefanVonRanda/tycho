# Appendix A — Collected grammar

The complete grammar is defined in two places, which together are normative:

- the **lexical grammar** — [§3](01-lexical.md): the token set (§3.5–§3.8) and
  literal productions (§3.9), plus the indentation algorithm (§3.4) that
  produces the `INDENT`/`DEDENT`/`NEWLINE` layout tokens;
- the **phrase grammar** — [§4](02-grammar.md): declarations (§4.1), types
  (§4.2), statements (§4.3), and expressions (§4.4), with the precedence table
  (§4.5).

> Draft note: this appendix will present those productions **collected into a
> single, de-duplicated EBNF listing** for at-a-glance reference. Until that
> flattening pass, §3 and §4 are the authoritative grammar; this appendix adds no
> new rules. When collected, every production here MUST be identical to its
> in-chapter definition — the flattened listing is a convenience, not a second
> source of truth.
