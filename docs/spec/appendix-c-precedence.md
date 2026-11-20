# Appendix C — Operator precedence and associativity

Normative definition: [§4.5](02-grammar.md#45-operator-precedence-and-associativity).
From tightest-binding (1) to loosest (9). Binary levels are left-associative;
prefix levels are right-associative.

| Precedence | Operators | Kind | Associativity |
|---|---|---|---|
| 1 | `[]` (index/slice) · `.` (field / tuple index) · `()` (call) · `or_return` · `or_else` · `...` | postfix | left |
| 2 | `-` · `&` (address-of) · `~` | unary prefix | right |
| 3 | `*` · `/` · `%` · `<<` · `>>` · `&` (bitwise) | binary | left |
| 4 | `+` · `-` · `\|` · `^` | binary | left |
| 5 | `is` | binary, non-associative | — |
| 6 | `==` · `!=` · `<` · `>` · `<=` · `>=` · `in` | binary | left |
| 7 | `not` | unary prefix | right |
| 8 | `and` | binary | left |
| 9 | `or` | binary | left |

This is Go's precedence: every bitwise operator binds tighter than any
comparison, so `a & b == c` parses as `(a & b) == c`. Assignment, `:=` and
compound assignment are statements, not operators. The grammar fixes
associativity only; the order of side effects among operands and arguments is
unspecified ([§13.4](09-expressions.md#134-evaluation-order),
[Appendix F](appendix-f-impl-defined.md)).
