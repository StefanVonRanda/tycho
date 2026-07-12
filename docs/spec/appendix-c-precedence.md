# Appendix C — Operator precedence and associativity

Normative definition: [§4.5](02-grammar.md#45-operator-precedence-and-associativity).
From tightest-binding (1) to loosest (8). Binary levels are left-associative;
prefix levels are right-associative.

| Precedence | Operators | Kind | Associativity |
|---|---|---|---|
| 1 | `[]` (index/slice) · `.` (field / tuple index) · `()` (call) · `or_return` · `...` | postfix | left |
| 2 | `-` · `&` (address-of) · `~` | unary prefix | right |
| 3 | `*` · `/` · `%` · `<<` · `>>` · `&` (bitwise) | binary | left |
| 4 | `+` · `-` · `\|` · `^` | binary | left |
| 5 | `==` · `!=` · `<` · `>` · `<=` · `>=` · `in` | binary | left |
| 6 | `not` | unary prefix | right |
| 7 | `and` | binary | left |
| 8 | `or` | binary | left |

Notes:

- **Go-style precedence:** shifts (`<<`, `>>`) and bitwise-AND (`&`) bind at the
  multiplicative level (3); bitwise-OR/XOR (`|`, `^`) bind at the additive level
  (4). Every bitwise operator therefore binds tighter than any comparison, so
  `a & b == c` parses as `(a & b) == c`.
- **`&` is overloaded** across levels: unary address-of / `inout` argument
  (level 2) and binary bitwise-AND (level 3).
- **Assignment (`=`), declare-and-infer (`:=`), and compound assignment are not
  expression operators** — they are statement-level and never appear inside an
  expression ([§4.3.1](02-grammar.md#431-simple-statements)).
- The grammar fixes associativity only; the **order of side effects** among
  operands and arguments is unspecified ([§13.4](09-expressions.md#134-evaluation-order),
  [Appendix F](appendix-f-impl-defined.md)).
