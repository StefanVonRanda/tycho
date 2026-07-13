#!/bin/sh
# gen_grammar.sh — emit the collected EBNF grammar for spec Appendix A.
#
# Single source of truth: the in-chapter fenced grammar blocks in
#   docs/spec/01-lexical.md  (§3, lexical)
#   docs/spec/02-grammar.md  (§4, phrase)
# A fenced block is a grammar block iff it contains at least one "::=" line, so
# example/keyword-list fences are skipped automatically. Every production is
# emitted verbatim — this listing is a convenience, never a second source of
# truth (spec Appendix A invariant).
#
# `make spec-check` re-runs this and diffs the output against the GENERATED
# region of docs/spec/appendix-a-grammar.md; any drift fails the build.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
lex="$root/docs/spec/01-lexical.md"
phr="$root/docs/spec/02-grammar.md"

# Emit the contents of every fenced block that holds >=1 "::=" production.
extract() {
    awk '
        /^```/ {
            if (infence) { if (has) printf "%s", buf; infence=0; buf=""; has=0 }
            else         { infence=1;  buf=""; has=0 }
            next
        }
        infence { buf = buf $0 "\n"; if ($0 ~ /::=/) has=1 }
    ' "$1"
}

printf '### A.2 Lexical grammar (§3)\n\n'
printf 'Terminals produced by the lexer and consumed by the phrase grammar are\n'
printf '`IDENT`, `INT`, `FLOAT`, `STR`, `CHAR` (§3.5), and the layout tokens\n'
printf '`NEWLINE`, `INDENT`, `DEDENT`, `EOF` (§3.4). The spellings below define the\n'
printf 'literal terminals; `INT` is spelled by `IntLit`, `FLOAT` by `FloatLit`,\n'
printf '`CHAR` by `CharLit`, `STR` by `StrLit`.\n\n'
printf '```ebnf\n'
extract "$lex"
printf '```\n\n'

printf '### A.3 Phrase grammar (§4)\n\n'
printf '```ebnf\n'
extract "$phr"
printf '```\n'
