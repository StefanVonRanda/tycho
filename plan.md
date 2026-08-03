# What the next program says the language needs

> This plan is a fresh clone, 2026-08-03: the completed tycho-scheme plan lives
> at `docs/internals/plan-tycho-scheme-DONE.md`. The rule from that plan holds
> here: *does anything that is not the program written to want it need this?* A
> finding becomes a phase only when a second, independent caller exists.

## The program

(TBD -- candidates under investigation.)

Leading candidate: **a bytecode compiler for the Scheme interpreter** --
`tools/tycho-scheme` grows a compiler front end that lowers a program to
tycho-vm bytecode, so tycho-scheme and tycho-vm converge. It would be the
first real customer for tycho-vm's instruction encoding and const pools
outside the tool's own fixtures, and it re-exercises the interpreter's
reader (reuse), symbol interning, and scalar-match dispatch at a new scale.
The demand test applies to anything it needs: a language feature a compiler
front end wants (codegen tables, instruction emission, fixups) becomes a
phase only when a second, independent program needs it too.

## Findings

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
