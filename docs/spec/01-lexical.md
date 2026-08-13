# 3. Lexical structure

This chapter defines how a Tycho source file is decomposed into a stream of
**tokens**. The token stream — including the synthetic layout tokens `NEWLINE`,
`INDENT`, and `DEDENT` — is the input to the phrase grammar ([§4](02-grammar.md)).

> Provenance: the lexer is `src/tychoc.c:285-606` (`lex`), the token kinds
> `:188-204` (`TokKind`), the keyword table `:239-281` (`keyword`).

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

There is no line-continuation **character** (no trailing backslash). A logical
line may still span several physical lines, by two implicit joins:

1. **Bracket depth.** While any `(` or `[` is open, a physical line boundary
   produces no `NEWLINE` and the next line's leading whitespace is not
   indentation. This has always been the case.
2. **A trailing operator.** A physical line whose last token cannot *end* an
   expression — an arithmetic, comparison, bitwise, or logical operator, `,`,
   `=`, or `:=` — joins the line that follows, provided that line is indented
   **strictly deeper** than the first physical line of the statement. The
   continuation's leading whitespace is not indentation.

Only tokens that cannot end an expression qualify, so no program that was
accepted before either join can change meaning. `:` is excluded: it opens a
block and must end its line.

**The deeper-indent condition is normative, and it is what preserves the
diagnostic for a truncated line.** Given

```text
x := 1 +
print(str(x))
```

at equal indent, the `+` does *not* join, and the program is rejected where the
operand is missing rather than being parsed as `x := 1 + print(str(x))` and
rejected later, on the wrong line, for an unrelated reason.

Multi-line *constructs* are still expressed through indented blocks (§3.4);
these joins are for a single over-long expression.

## 3.3 Comments

```ebnf
Comment ::= "#" (any byte except newline)*
```

A comment begins with `#` and runs to the end of the line. Comments do not
nest, and there is no block-comment form. A `#` inside a string or character
literal is an ordinary byte, not a comment.

> Provenance: a comment-only line is skipped without touching the indent stack `src/tychoc.c:518@*p == '#') {`; the token loop stops at `#` `:548@*p != '\n' && *p != '#'`; the trailing comment is consumed at `:874@if (*p == '#') while`.

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

> Provenance: `src/tychoc.c:298-334` (measure + INDENT/DEDENT),
> `:533@indentation too deep` (depth bound), `:603-604` (EOF flush).

## 3.5 Tokens

The token kinds are: `EOF`, `NEWLINE`, `INDENT`, `DEDENT`; the literal and name
tokens `IDENT`, `INT`, `FLOAT`, `STR`, `CHAR`; the keyword tokens (§3.6); and
the operator and punctuation tokens (§3.8). Whitespace other than the leading
indentation and newlines separates tokens but is otherwise insignificant.

Longest-match applies: the lexer forms the longest valid token at each
position (e.g. `>>` is one shift token, not two `>`; `:=` is one token, not `:`
then `=`).

### 3.5.1 Case and the two name spaces

An `IDENT` may hold any letter case in any position; the token shape does not
depend on it. What the case decides is **which space the name may declare in**:

- A name that starts with an **uppercase letter** `A`–`Z` belongs to the
  **compile-time** space: types (§8), enum variants ([§19.1](12-aggregates.md#191-enum-declaration-and-variants)),
  and `const` declarations (§14).
- A name bound at **run time** — a local, a parameter (including `inout`,
  `sink` and variadic parameters and lambda parameters), a destructuring
  target, a `for` loop variable, a match-arm payload binding, and a `select`
  `recv` binding — **must start with a lowercase letter or `_`**. A conforming
  implementation rejects any other spelling.

The rule is exclusion, not preference: because no run-time binding can be
spelled the way a variant is spelled, a binding can never shadow a constructor,
and a bare `Name` in a payload slot is unambiguously a **pattern** rather than a
binding. `_` and a leading-underscore name stay legal in every binding position
(`_` is the discard, §22.4). Procedure names are unaffected — they are neither
run-time bindings nor uppercase by convention — except for the four constructor
names §3.7 already excludes.

> Provenance: one guard at the single point every binding form reaches,
> `src/tychoc.c@vars_push` — parameters, `:=`, destructuring, loop variables,
> match binds and `select` binds all pass through it. It runs *after*
> `src/tychoc.c@match_arm_payload` promotes a bare `Err(A)` from a binding to a
> pattern, which is why a nullary variant in a payload slot is still written
> with its own uppercase spelling.

## 3.6 Keywords

The following words are **reserved**. A reserved word is never an identifier;
using one where a name is expected is a syntax error.

```text
and     bool    break   continue elif    else    enum    f32
false   float   fn      for      handle  if      in      inout
int     is      match   not      null    or      or_return  parallel
ptr     return  select  spawn    string  struct  true    type
u32     u64
```

`bytes` is also reserved (a primitive type keyword). The words `int`, `bool`,
`string`, `float`, `ptr`, `bytes`, `u32`, `u64`, and `f32` are the primitive
**type keywords**; the rest are declaration, control-flow, operator, or literal
keywords. `or_return` is matched as a single word (it is not `or` followed by
`_return`).

> Provenance: the complete reserved set is exactly `keyword()`,
> `src/tychoc.c:239-281`. There is no `while` keyword (the loop keyword is
> `for`, §4); there is no `char` or `void` type keyword (the `char` type arises
> only from character literals and inference, and `void` is the implicit
> no-return type).

## 3.7 Contextual identifiers

Several words are significant **only in a specific position** and are ordinary
identifiers everywhere else. A variable, parameter, or field of the same name
is unaffected — subject to [§3.5.1](#351-case-and-the-two-name-spaces), which
bars an uppercase spelling from every run-time binding position. They are
**not** reserved:

- **Top-level leaders:** `package`, `import`, `extern`, `const`, `subscript`.
- **Statement leaders:** `const` (local), `delete` — each significant only when
  immediately followed by an identifier.
- **Type / expression position:** `soa`, `where`, `channel`, and the built-in
  generic type names `Option`, `Result`, `Channel`.
- **Parameter modifier:** `sink`.
- **Construct bodies:** `yield` (in a `subscript`), `free` (in a `handle`),
  `range` (in the head of a `for … in`, **only to refuse it** since 2026-07-29).
- **Value constructors treated as identifiers:** `None`, `Some`, `Ok`, `Err`,
  and the match wildcard `_`. Unlike the builtin functions below, these four
  are refused **everywhere**: a `fn` or `const` of one of those names is
  rejected in every package, qualified spelling or not
  ([§19.1](12-aggregates.md#191-enum-declaration-and-variants)).
- **Built-in functions:** every builtin (`len`, `push`, `pop`, `print`, `str`,
  `to_int`, `wait`, `send`, `recv`, `close`, …) is an ordinary identifier
  resolved as a call; none is reserved ([§29](16-builtins.md)).

  A builtin name may **not** be shadowed. In an unqualified call `f(...)`, if
  `f` names a builtin then the **builtin is selected**, always — whatever else
  the file declares, and including a call written inside a same-named
  procedure's own body, which therefore does not recurse.

  That governs the **call**, and it reaches a declaration only through
  reachability — so the two declaration positions differ:

  - In the **main program** — the entry file, whether or not it opens with
    `package main` — a procedure has no spelling other than the unqualified
    one. One named after a builtin could never be called, so declaring it
    **is an error**. Pick a different name: `core:utf8` hit this and renamed
    its `len` to `count`.
  - In a **package**, `pkg.f(...)` is a different construct: it names `pkg`'s
    procedure and never a builtin. Such a declaration stays reachable, and is
    therefore **permitted** — a package MAY export a procedure whose name a
    builtin also owns, and callers MAY call it. `core:json` does exactly that;
    `json.keys(j)` resolves to `corelib/json/json.ty@keys` and is legal. Only
    the unqualified spelling, written inside that package's own body, is taken
    by the builtin.

  > **gap:** the error in the first case is under-enforced. Measured
  > 2026-08-12 across the 54 names `src/tychoc.c@shadows_builtin` lists: in the
  > main program 26 are rejected at the declaration with `'X' is already
  > defined`, and the other 28 are accepted with a warning and then overtaken
  > by the builtin at the call site — so what a caller sees turns on the
  > **arguments**, not the name. Arguments the builtin's own contract refuses
  > produce that builtin's diagnostic (with a two-parameter `hash` declared,
  > `hash(1, 2)` fails as `hash(x) takes one argument`), which is confusing
  > since the local's signature fits perfectly. Arguments it accepts are
  > answered **silently**, the local body never entered: with
  > `fn to_u8(x: int) -> int` returning `99`, `to_u8(5)` evaluates to `5`.
  > That silent case is the undiagnosed one, and those 28 declarations are
  > what this gap covers — each ought to be rejected outright.
  >
  > Inside a package all 54 declare successfully and the qualified call reaches
  > the local in every one. That is the rule above rather than a gap. The
  > warning still fires, but only when the file being compiled is itself inside
  > the corelib tree; a consumer's build mutes it (`src/tychoc.c@g_mute_warn`),
  > the collision not being theirs to fix. The warning is quality of
  > implementation, not a conformance requirement; **selection** is the
  > normative part.

> Provenance: contextual dispatch at `src/tychoc.c:4648-4657` (top level),
> `:3691@"const"`/`:3717@"delete"` (`const`/`delete`), `:2360@soa [Struct]`/`:2877@soa []Struct` (`soa`),
> `:4331@"where"` (`where`), `:4297@"sink"` (`sink`), `:3973@"range"` (`range`, refusal only).

## 3.8 Operators and punctuation

The operator and punctuation tokens, longest-match first:

| Spelling | Role |
|---|---|
| `...` `..<` | variadic parameter (`...T`) and spread (`x...`); half-open counting range (`0..<N`, §22) |
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
| `,` `;` | separator: argument / element lists; three-clause `for`-header clauses (§14.4) |
| `$` | generic type-parameter sigil (`$T`) and explicit type arguments (`f$(T)`) |

The byte `!` occurs **only** as part of `!=`; a bare `!` is a lexical error.
Boolean negation is the keyword `not`, not `!`. The braces `{` and `}` are
**not** tokens of the language; they are significant only inside an f-string
literal (§3.9.5). The **only** range operator is `..<`; `..` alone is not a
token, and the `range(…)` form it replaced is gone (§14.4). Operator precedence and associativity are
defined with the expression grammar in [§4.5](02-grammar.md#45-operator-precedence-and-associativity).

> Provenance: `src/tychoc.c:553-589`. `::` is lexed at `:822@TK_COLONCOLON` but no grammar
> production consumes it; `..<` at `:820@TK_DOTLT`, tested after `...` so maximal munch holds; `;` at `:845@TK_SEMI`.

## 3.9 Literals

To bound parser recursion, expression nesting (parentheses and unary operator
chains) is limited to a fixed depth; a more deeply nested expression is rejected
(`expression nesting too deep`) — a fail-closed guard, the expression-level
counterpart to the indentation-depth bound (§3.4).

> Provenance: `src/tychoc.c:2783-2789`.

### 3.9.1 Integer literals

```ebnf
IntLit    ::= DecimalLit | HexLit | BinLit
DecimalLit ::= [0-9]+
HexLit    ::= "0" ("x" | "X") HexDigit+
BinLit    ::= "0" ("b" | "B") BinDigit+
HexDigit  ::= [0-9a-fA-F]
BinDigit  ::= [01]
```

An integer literal is decimal, hexadecimal (`0x`/`0X`), or binary
(`0b`/`0B`). The prefixed forms are recognized only when the prefix directly
follows the digit `0`, so `10x` is the integer `10` followed by an
identifier — the same tie-break as C and Go — and a prefixed literal is
always an integer: there is **no** hexadecimal-float form (§3.9.2). A `0x`
or `0b` prefix with no following digit is a lexical error, as is a binary
literal containing a digit other than `0` or `1`. There is **no** octal
form, **no** digit-group separator (`_`), and **no** type suffix (such as
`u32` or `L`). An integer literal denotes a value in the range `0` through
`9223372036854775807` (`2^63 − 1`); a literal larger than that is rejected
(`integer literal out of range`).

Because a literal is non-negative and negation is a separate unary operator
(§4), the most negative `int` value, `−9223372036854775808`, has no literal
spelling; it is obtained by computation (for example negating a smaller value,
relying on defined wraparound — §30). An integer literal adapts to
a `float`, `u32`, `u64`, or `f32` context by the literal-adaptation rules of the
type system (§8); it does not change the literal's syntax.

> Provenance: `src/tychoc.c:373-380` (accumulation + overflow check).

### 3.9.2 Float literals

```ebnf
FloatLit ::= [0-9]+ "." [0-9]+ Exp?
           | [0-9]+ Exp
           | "." [0-9]+ Exp?          /* leading-dot form, position-restricted */
Exp      ::= ("e" | "E") ("+" | "-")? [0-9]+
```

A float literal has a fractional part (`3.14`), an exponent (`1e10`, `2E8`), or
both (`1.5e-3`), or begins with a dot (`.5`). The value is that of the C
`strtod` parse of the same text and denotes an IEEE-754 binary64 (`float`); a
float literal adapts to `f32` by literal adaptation (§8). There is
no hexadecimal-float form.

Two disambiguation rules are normative:

- **Leading-dot restriction.** A `.` immediately followed by a digit begins a
  float literal *only* when the preceding token is not value-producing (i.e. not
  an identifier, number, string, character literal, `)`, or `]`). After a
  value-producing token, `.` is the field/tuple-index operator, so `t.0` is a
  tuple index and `x.5` is a tuple index (`.` then the integer `5`), never a float.
- **Malformed exponent.** An `e`/`E` not followed by an optional sign and at
  least one digit is not part of the number; the `e…` is lexed as a separate
  identifier. Thus `1e` and `1e+` do not form float literals (`1e` tokenizes as
  `INT DOT?`… i.e. the `e` becomes a lone identifier). `1.e5` also does not form a
  float, but for a different reason: the `.` is not followed by a digit, so no
  fractional part forms and it tokenizes as `INT(1) "." IDENT(e5)`.

> Provenance: `src/tychoc.c:343-368`; the leading-dot predicate is
> `tok_postfixable`, `:234-237`.

### 3.9.3 Character literals

```ebnf
CharLit ::= "'" ( CharEscape | (any byte except "'", "\", newline) ) "'"
CharEscape ::= "\" ( "n" | "t" | "r" | "0" | "\" | "'" ) | "\x" HexDigit HexDigit
HexDigit ::= "0".."9" | "a".."f" | "A".."F"
```

A character literal is delimited by single quotes and denotes exactly **one
byte** (a value `0`–`255`) of type `char`. The supported escapes are `\n`, `\t`,
`\r`, `\0`, `\\`, `\'`, and `\xNN`. An empty literal (`''`), an unterminated
literal, or one holding more than one byte is a lexical error.

`\xNN` takes **exactly two** hex digits, in either case: `'\x41'` is `'A'` and
`'\x0a'` is `'\n'`. The fixed width is normative and is not C's greedy `\x`;
`'\x4'` is a lexical error rather than a short read. Together with `\0` it makes
every one of the 256 byte values writable as a literal.

**`\xNN` is a character-literal escape only** — a string literal still rejects it
([§3.9.4](#394-string-literals)), and for a reason that is about representation
rather than about hex. A character literal is decoded to its byte at lex time and
never reaches a C string literal, whereas a string literal's text is carried as
*escaped source* into codegen, where the adjacent-piece join is sound only while
every escape is exactly two characters. `\0` has the same asymmetry, and for the
same reason.

> Provenance: `src/tychoc.c:528-553`; the `\x` arm `:800@case 'x'`; the escape
> table `:799-800@case '0'`; the fixed-width refusal `:802@two hex digits`.
> Conformance: `tests/char_hex_escape.ty`, `tests/reject/hex_escape_one_digit.ty`,
> `tests/reject/hex_escape_in_string.ty`.

### 3.9.4 String literals

```ebnf
StrLit ::= StrPiece StrPiece*
StrPiece ::= QuotedPiece | RawPiece
QuotedPiece ::= '"' StrElem* '"'
StrElem ::= StrEscape | (any byte except '"', "\", newline, and raw control bytes below 0x20 other than tab)
StrEscape ::= "\" ( "n" | "t" | "r" | "\" | '"' )
RawPiece ::= "`" RawElem* "`"
RawElem ::= (any byte except "`" and raw control bytes below 0x20 other than tab and newline)
```

A string literal is delimited by double quotes and denotes a `string` value. One
piece is single-line: an embedded newline is an error (`unterminated string
literal`). The supported escapes are exactly `\n`, `\t`, `\r`, `\\`, and `\"` — a
smaller set than C; any other `\`-escape is rejected. A raw control byte below
`0x20` (except tab) is rejected and MUST be written with an escape. A single
string-literal *piece* is limited to a fixed maximum length (`string too long`
beyond it). Consequently a string *literal* cannot contain a `NUL` byte; a
byte-safe `string`/`bytes` *value* containing interior `NUL`s is produced at run
time (e.g. via `bytes`), not written as a literal.

**Adjacent pieces join.** Two or more string literals written next to one another
are one literal, concatenated left to right at parse time: `"ab" "cd"` is `"abcd"`.
Because the implicit line-join inside `(`…`)` and `[`…`]`
([§3.2](#32-logical-lines-and-newline)) suppresses the newline, this is how a
**multi-line string** is written:

```tycho
page := ("<!doctype html>\n"
         "<title>hello</title>\n"
         "<p>body</p>\n")
```

Apart from a raw piece (below) there is no other multi-line string form, and
there is no backslash line-continuation. An **f-string never joins**
([§3.9.5](#395-f-string-interpolated-literals)): it is
already sugar for a `+` chain, so `f"a" "b"` and `"a" f"b"` are each a syntax
error, as they were before joining existed. Joining is defined on the literals'
*escaped source text*, which is sound only because every escape is exactly two
characters — the reason `\0` and `\xNN` are not in **this** escape set (a greedy
C-style `\x` would absorb a hex digit across a join, so `"\x4" "1"` would mean one
byte where the author wrote two, and a `\0` would truncate the interned literal,
whose length comes from `strlen`). Both are legal in a *character* literal
([§3.9.3](#393-character-literals)), which is decoded to a byte at lex time and so
has no such exposure; the asymmetry is deliberate and normative.

**Raw pieces.** A piece delimited by backticks — `` `…` `` — is a *raw* string
piece. **No escape is interpreted inside it:** a backslash is a backslash, so
`` `a\nb` `` is the four characters `a`, `\`, `n`, `b` and equals `"a\\nb"`. A
`"` needs no escape. An embedded **newline is a literal newline byte**, so a raw
piece is the one literal form that genuinely spans source lines:

```tycho
page := `<!doctype html>
<title>hello</title>`
```

Because no escape exists, **there is no way to write a backtick inside a raw
piece** — the first backtick after the opener closes it. A backtick is an
ordinary byte inside a *quoted* piece, so a literal that needs one is written by
joining: ``s := `a` "`" `b` `` is the three characters `` a`b ``. A raw
piece that is never closed before end of file is rejected (`unterminated raw
string literal`, reported at the *opening* line). Raw control bytes below `0x20`
are rejected exactly as in a quoted piece, with tab **and newline** excepted.
The same fixed per-piece length bound applies (`string too long`).

A raw piece **is** a `StrPiece`, so it joins with adjacent pieces of either kind:
`` `raw ` "and normal" `` and `` "normal and " `raw` `` are each one literal.
This is sound under the escaped-source-text rule above because the scanner
re-escapes a raw piece's bytes into the ordinary two-character escapes as it
reads them; the token a raw piece produces is an ordinary string token carrying
escaped text, and nothing downstream can tell which spelling produced it. A raw
piece is never interpolated: there is no `` f`…` `` form, and `{` inside a raw
piece is an ordinary byte.

Concatenating two string literals with `+` also folds to one literal in a
`const` ([§12.2](08-declarations.md#122-constants)), so `const TERM = "\r\n" + "\r\n"`
is a single four-byte literal and not a run-time concatenation.

> Provenance: quoted piece `src/tychoc.c:393-475`; escape set `:448-457`;
> control-byte rejection `:464-466`; per-piece length bound `:664@char buf[4096]`,`:670@bn + 2 >= (int)sizeof buf`;
> raw piece `:477-524`, its re-escape table `:505-508`, its control-byte
> rejection `:509-510`, its per-piece bound `:775@rn + 2 >= (int)sizeof rbuf`,`:778@rn + 1 >= (int)sizeof rbuf`,
> its unterminated diagnostic `:782@unterminated raw string literal`; adjacent join `:2445-2457`; `const` string fold
> `:4587-4591`; codegen pastes the escaped text into a C string literal
> `:10737@TYCHO_LIT`; that literal's decoded length `runtime/tycho_rt.c:1262@sizeof s - 1`.
> Fixtures: `tests/rawstring.ty`,
> `tests/reject/rawstring_unterminated.ty`.

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
rejected with the same diagnostic `str` gives ([§29](16-builtins.md)).

> Provenance: lexing — the identifier scanner declines the `f` of `f"…"` `src/tychoc.c:649@!(c == 'f' && p[1] == '"')`, the string scanner takes it `:393-475`;
> desugar `interp_join` / `desugar_interp`, `:2326-2390`.

### 3.9.6 Boolean and pointer literals

`true` and `false` are the two `bool` literals (§3.6). `null` is the literal of
the opaque FFI pointer type `ptr`; it denotes a null pointer and participates
only in FFI passing, `null`-comparison, and `is_null` ([§24](14-ffi.md)). There is no `bytes` literal — a `bytes` value is produced by
`to_bytes` from a `string` or from an `[int]` of byte values (§8).

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
