# 3. Lexical structure

This chapter defines how a Tycho source file is decomposed into a stream of
**tokens**. The token stream — including the synthetic layout tokens `NEWLINE`,
`INDENT`, and `DEDENT` — is the input to the phrase grammar ([§4](02-grammar.md)).

> Provenance: the lexer is `src/tychoc.c:196-445` (`lex`), the token kinds
> `:107-122` (`TokKind`), the keyword table `:156-192` (`keyword`).

## 3.1 Source text

A Tycho source file is a sequence of bytes. The lexer is byte-oriented: it
recognizes ASCII letters (`A`–`Z`, `a`–`z`), ASCII digits (`0`–`9`), and the
punctuation below; all other bytes are meaningful only inside string, f-string,
and character literals and in comments, where they are carried as raw bytes.

There is no byte-order-mark handling and no source-level Unicode escape
processing. Identifiers and keywords are ASCII (§3.6). `string` and `bytes`
*values* are fully byte-safe at run time (interior `NUL` bytes are preserved),
but a `string` *literal* cannot contain a `NUL` (§3.9.4).

An implementation MUST accept files terminated by a final newline and files
whose final line has no trailing newline; the lexer flushes any pending
`DEDENT`s and appends `EOF` at end of input either way.

## 3.2 Logical lines and `NEWLINE`

The lexer is line-oriented. Each non-blank, non-comment-only line produces its
tokens followed by exactly one `NEWLINE` terminal. A **blank line** (only
whitespace) and a **comment-only line** (only whitespace then a comment)
produce no tokens and do not affect indentation (§3.4).

There is no line-continuation character and no implicit line joining: every
statement occupies whole logical lines, and multi-line constructs are expressed
through indented blocks (§3.4), not through bracket-spanning continuations.

## 3.3 Comments

```
Comment ::= "#" (any byte except newline)*
```

A comment begins with `#` and runs to the end of the line. Comments do not
nest, and there is no block-comment form. A `#` inside a string or character
literal is an ordinary byte, not a comment.

> Provenance: `src/tychoc.c:222`, `:244`, `:437`.

## 3.4 Indentation (`INDENT` / `DEDENT`)

Tycho is **indentation-sensitive**. Block structure is expressed by leading
whitespace, and the lexer emits `INDENT` and `DEDENT` tokens to mark it.

The algorithm, applied per logical line after skipping blank and comment-only
lines:

1. **Measure indent depth.** The leading whitespace of a line is a run of space
   (`0x20`) *or* tab (`0x09`) bytes. Its **depth** is the *count* of those
   bytes — not a display width. A file indented consistently with all spaces
   and one indented with all tabs nest identically.
2. **Mixing is an error.** If a single line's leading whitespace contains *both*
   a space and a tab, the program is rejected (`mixed tabs and spaces in
   indentation`). Indentation MUST use one whitespace character consistently
   within each line.
3. **Compare to the stack.** An indent stack holds the depths of the currently
   open blocks (initially `[0]`).
   - If the new depth is greater than the top, push it and emit one `INDENT`.
   - If it is less, pop levels while the top exceeds the new depth, emitting one
     `DEDENT` per pop. After popping, the new depth MUST equal the (new) top of
     the stack; otherwise the program is rejected (`inconsistent indentation`).
   - If it equals the top, emit neither.
4. **Depth bound.** The indent stack has a fixed capacity; nesting deeper than
   255 levels is rejected (`indentation too deep`). This bounds statement
   nesting and, with the expression-nesting bound (§3.9), guarantees the parser
   cannot be driven to stack overflow by crafted input — a *fail-closed*
   requirement (§1.3).
5. **End of input.** At end of file the lexer emits a `DEDENT` for every still-
   open level, then `EOF`.

A block in the phrase grammar is therefore `INDENT Stmt+ DEDENT` ([§4](02-grammar.md)).

> Provenance: `src/tychoc.c:208-240` (measure + INDENT/DEDENT), `:230`
> (depth bound), `:442-443` (EOF flush).

## 3.5 Tokens

The token kinds are: `EOF`, `NEWLINE`, `INDENT`, `DEDENT`; the literal and name
tokens `IDENT`, `INT`, `FLOAT`, `STR`, `CHAR`; the keyword tokens (§3.6); and
the operator and punctuation tokens (§3.8). Whitespace other than the leading
indentation and newlines separates tokens but is otherwise insignificant.

Longest-match applies: the lexer forms the longest valid token at each
position (e.g. `>>` is one shift token, not two `>`; `:=` is one token, not `:`
then `=`).

## 3.6 Keywords

The following words are **reserved**. A reserved word is never an identifier;
using one where a name is expected is a syntax error.

```
and     bool    break   continue elif    else    enum    f32
false   float   fn      for      handle  if      in      inout
int     match   not     null     or      or_return  parallel  ptr
return  select  spawn   string   struct  true    type    u32     u64
```

`bytes` is also reserved (a primitive type keyword). The words `int`, `bool`,
`string`, `float`, `ptr`, `bytes`, `u32`, `u64`, and `f32` are the primitive
**type keywords**; the rest are declaration, control-flow, operator, or literal
keywords. `or_return` is matched as a single word (it is not `or` followed by
`_return`).

> Provenance: the complete reserved set is exactly `keyword()`,
> `src/tychoc.c:156-192`. There is no `while` keyword (the loop keyword is
> `for`, §4); there is no `char` or `void` type keyword (the `char` type arises
> only from character literals and inference, and `void` is the implicit
> no-return type).

## 3.7 Contextual identifiers

Several words are significant **only in a specific position** and are ordinary
identifiers everywhere else. A variable, parameter, or field of the same name
is unaffected. They are **not** reserved:

- **Top-level leaders:** `package`, `import`, `extern`, `const`, `subscript`.
- **Statement leaders:** `const` (local), `delete` — each significant only when
  immediately followed by an identifier.
- **Type / expression position:** `soa`, `where`, `channel`, and the built-in
  generic type names `Option`, `Result`, `Channel`.
- **Parameter modifier:** `sink`.
- **Construct bodies:** `yield` (in a `subscript`), `free` (in a `handle`),
  `range` (only in the head of a `for … in`).
- **Value constructors treated as identifiers:** `None`, `Some`, `Ok`, `Err`,
  and the match wildcard `_`.
- **Built-in functions:** every builtin (`len`, `push`, `pop`, `print`, `str`,
  `to_int`, `wait`, `send`, `recv`, `close`, …) is an ordinary identifier
  resolved as a call; none is reserved ([§29](16-builtins.md), forthcoming).

> Provenance: contextual dispatch at `src/tychoc.c:3689-3698` (top level),
> `:2611`/`:2627` (`const`/`delete`), `:1582`/`:2026` (`soa`), `:3028`
> (`where`), `:2994` (`sink`), `:2748` (`range`).

## 3.8 Operators and punctuation

The operator and punctuation tokens, longest-match first:

| Spelling | Role |
|---|---|
| `...` | variadic parameter (`...T`) and spread (`x...`) |
| `:=` | declare-and-infer |
| `==` `!=` `<=` `>=` | comparison |
| `->` | return-type / function-type arrow |
| `<<` `>>` | left / (logical) right shift |
| `::` | reserved token, **currently unused** by the grammar |
| `:` | block colon, typed-declaration colon, map-type / slice colon |
| `=` | assignment |
| `<` `>` | comparison |
| `+` `-` `*` `/` `%` | arithmetic |
| `&` | bitwise-AND (binary) and address-of / `inout` argument (unary) |
| `\|` `^` `~` | bitwise OR, XOR, NOT |
| `(` `)` `[` `]` | grouping, calls, tuples; arrays, indexing, slices, maps |
| `.` | field / tuple-index access |
| `,` | separator |
| `$` | generic type-parameter sigil (`$T`) and explicit type arguments (`f$(T)`) |

The byte `!` occurs **only** as part of `!=`; a bare `!` is a lexical error.
Boolean negation is the keyword `not`, not `!`. The braces `{` and `}` are
**not** tokens of the language; they are significant only inside an f-string
literal (§3.9.5). There is no range operator (`..`); ranges are written with the
`range(…)` form in a `for` head (§4). Operator precedence and associativity are
defined with the expression grammar in [§4](02-grammar.md#expression-precedence).

> Provenance: `src/tychoc.c:398-433`. `::` is lexed at `:402` but no grammar
> production consumes it.

## 3.9 Literals

To bound parser recursion, expression nesting (parentheses and unary operator
chains) is limited to a fixed depth; a more deeply nested expression is rejected
(`expression nesting too deep`) — a fail-closed guard, the expression-level
counterpart to the indentation-depth bound (§3.4).

> Provenance: `src/tychoc.c:2227-2232`.

### 3.9.1 Integer literals

```
IntLit ::= [0-9]+
```

An integer literal is a run of one or more decimal digits. There is **no**
hexadecimal, octal, or binary form, **no** digit-group separator (`_`), and
**no** type suffix (such as `u32` or `L`). An integer literal denotes a value
in the range `0` through `9223372036854775807` (`2^63 − 1`); a literal larger
than that is rejected (`integer literal out of range`).

Because a literal is non-negative and negation is a separate unary operator
(§4), the most negative `int` value, `−9223372036854775808`, has no literal
spelling; it is obtained by computation (for example negating a smaller value,
relying on defined wraparound — §30, forthcoming). An integer literal adapts to
a `float`, `u32`, `u64`, or `f32` context by the literal-adaptation rules of the
type system (§8, forthcoming); it does not change the literal's syntax.

> Provenance: `src/tychoc.c:279-285` (accumulation + overflow check).

### 3.9.2 Float literals

```
FloatLit ::= [0-9]+ "." [0-9]+ Exp?
           | [0-9]+ Exp
           | "." [0-9]+ Exp?          /* leading-dot form, position-restricted */
Exp      ::= ("e" | "E") ("+" | "-")? [0-9]+
```

A float literal has a fractional part (`3.14`), an exponent (`1e10`, `2E8`), or
both (`1.5e-3`), or begins with a dot (`.5`). The value is that of the C
`strtod` parse of the same text and denotes an IEEE-754 binary64 (`float`); a
float literal adapts to `f32` by literal adaptation (§8, forthcoming). There is
no hexadecimal-float form.

Two disambiguation rules are normative:

- **Leading-dot restriction.** A `.` immediately followed by a digit begins a
  float literal *only* when the preceding token is not value-producing (i.e. not
  an identifier, number, string, character literal, `)`, or `]`). After a
  value-producing token, `.` is the field/tuple-index operator, so `t.0` is a
  tuple index and `x.5` is a field access followed by a number, never a float.
- **Malformed exponent.** An `e`/`E` not followed by an optional sign and at
  least one digit is not part of the number; the `e…` is lexed as a separate
  identifier. Thus `1e`, `1.e5`, and `1e+` do not form float literals.

> Provenance: `src/tychoc.c:249-277`; the leading-dot predicate is
> `tok_postfixable`, `:151-154`.

### 3.9.3 Character literals

```
CharLit ::= "'" ( CharEscape | (any byte except "'", "\", newline) ) "'"
CharEscape ::= "\" ( "n" | "t" | "r" | "0" | "\" | "'" )
```

A character literal is delimited by single quotes and denotes exactly **one
byte** (a value `0`–`255`) of type `char`. The supported escapes are `\n`, `\t`,
`\r`, `\0`, `\\`, and `\'`. An empty literal (`''`), an unterminated literal, or
one holding more than one byte is a lexical error.

> Provenance: `src/tychoc.c:371-395`.

### 3.9.4 String literals

```
StrLit ::= '"' StrElem* '"'
StrElem ::= StrEscape | (any byte except '"', "\", newline, and raw control bytes below 0x20 other than tab)
StrEscape ::= "\" ( "n" | "t" | "\" | '"' )
```

A string literal is delimited by double quotes and denotes a `string` value. It
is single-line: an embedded newline is an error (`unterminated string
literal`). The supported escapes are exactly `\n`, `\t`, `\\`, and `\"` — a
smaller set than C; any other `\`-escape is rejected. A raw control byte below
`0x20` (except tab) is rejected and MUST be written with an escape. A single
string literal is limited to a fixed maximum length (`string too long` beyond
it). Consequently a string *literal* cannot contain a `NUL` byte; a byte-safe
`string`/`bytes` *value* containing interior `NUL`s is produced at run time
(e.g. via `bytes`), not written as a literal.

> Provenance: `src/tychoc.c:297-368`; escape set `:350-351`; control-byte
> rejection `:359-360`; length bound `:304`,`:310`.

### 3.9.5 f-string (interpolated) literals

An f-string is a string literal prefixed with `f`. Inside it, a `{ expr }`
**hole** interpolates the value of an expression, and `{{` and `}}` denote
literal `{` and `}`. Everything outside a hole obeys the string-literal rules of
§3.9.4; text inside a hole is Tycho source, re-lexed and parsed as an
expression, and nested string literals inside a hole are permitted.

An f-string is **syntactic sugar**: at parse time `f"a{e}b"` becomes the
concatenation `"a" + str(e) + "b"`. There is no distinct f-string AST node.
Because the desugaring wraps each hole in `str(…)`, a hole expression MUST be of
a type accepted by `str` (the numeric and string scalars); other hole types are
rejected with the same diagnostic `str` gives ([§29](16-builtins.md),
forthcoming).

> Provenance: lexing `src/tychoc.c:289`,`:297-367`; desugar `interp_join` /
> `desugar_interp`, `:1826-1866`.

### 3.9.6 Boolean and pointer literals

`true` and `false` are the two `bool` literals (§3.6). `null` is the literal of
the opaque FFI pointer type `ptr`; it denotes a null pointer and participates
only in FFI passing, `null`-comparison, and `is_null` ([§24](14-ffi.md),
forthcoming). There is no `bytes` literal — a `bytes` value is produced by
`to_bytes` from a `string`.

## 3.10 Non-normative tree-sitter grammar

The tree-sitter grammar in `editors/zed/grammars/tycho/` exists for editor
syntax highlighting and is **not** part of this specification. It is a flat
token stream that models no indentation, and it diverges from the language as
defined here in at least the following ways (all resolved in favor of this
specification):

1. it lists `while` as a keyword — there is no `while` in the language;
2. it lists `char` and `void` as type keywords — neither is spellable as a type;
3. it treats `import`, `package`, `extern`, and `soa` as reserved keywords —
   they are contextual identifiers (§3.7) — and omits the reserved word `handle`;
4. its number pattern does not model exponents or the leading-dot form (§3.9.2);
5. it lists `{`/`}` as punctuation and omits `...` — braces are not tokens and
   `...` is a real operator (§3.8);
6. its builtin list is partial and includes the removed names `map_get`/
   `map_set` (which the language rejects at parse time);
7. it models no `INDENT`/`DEDENT`/`NEWLINE` (§3.4), so it cannot represent block
   structure.

A conforming implementation MUST follow §3.1–§3.9; the tree-sitter grammar has
no normative force.
