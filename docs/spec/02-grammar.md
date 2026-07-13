# 4. Grammar

This chapter defines the **phrase grammar** — the mapping from the token stream
(§3) to program structure. Terminals are token kinds in `UPPER_CASE` (`IDENT`,
`INT`, `FLOAT`, `STR`, `CHAR`, `NEWLINE`, `INDENT`, `DEDENT`) or the literal
spellings of keywords and punctuation in double quotes. The notation is defined
in [§2.1](00-conventions.md#21-grammar-notation).

The grammar defines *shape*, not *validity*: some productions accept forms that
a later static rule rejects (for example, a value `if` without an `else`, §4.4).
Such forms are flagged here and constrained in the semantic chapters. A program
is valid only if it parses **and** satisfies every static-semantic rule.

> Provenance: parser entry `src/tychoc.c:3683-3708` (`parse_program`); the
> per-construct functions are cited at each section.

## 4.1 Program and top-level declarations

A compilation input is a sequence of top-level declarations. Blank lines
between them are insignificant.

```
Program   ::= TopDecl*
TopDecl   ::= PackageDecl | ImportDecl | ExternFn | ConstDecl
            | Subscript | Struct | Enum | Handle | TypeDecl | Fn
```

The leader disambiguates: `package`, `import`, `const`, `subscript` (contextual
identifiers, §3.7), `extern` (contextual, when not immediately followed by `(`),
and the keywords `struct`, `enum`, `handle`, `type` each select their
declaration; anything else begins a function.

```
PackageDecl ::= "package" IDENT NEWLINE
ImportDecl  ::= "import" IDENT? STR NEWLINE
ConstDecl   ::= "const" IDENT "=" ConstExpr NEWLINE
```

`ImportDecl`'s optional `IDENT` is an alias for the imported package's prefix;
the `STR` is the path (a `core:` prefix selects the corelib root). `ConstExpr`
is an expression that folds to a single literal at compile time (integer
arithmetic, bitwise, unary, and backward references to earlier top-level
constants); its rules are given in §8 and §13. Package resolution,
visibility, and merging are specified in §28.

> Provenance: `parse_package_decl` `src/tychoc.c:3459`, `parse_import_decl` `:3466`, `parse_const`
> (`src/tychoc.c:3664-3681`).

### 4.1.1 Functions

```
Fn         ::= "fn" IDENT "(" ParamList? ")" ( "->" Type )? WhereClause? ":" NEWLINE Block
ParamList  ::= Param ( "," Param )*
Param      ::= IDENT ":" ( "inout" | "sink" )? "..."? Type
```

A parameter is a name, a colon, an optional passing mode (`inout` for a
copy-in/copy-out borrow, or the contextual `sink` for an owned/consumed
parameter — §15), an optional `...` marking a **variadic**
parameter, and its type. A variadic parameter has type `[T]` (the call packs
its trailing arguments into it) and MUST be the last parameter; it cannot also
be `inout` or `sink`. A missing `->` return type means the function returns
nothing (`void`). A function MUST NOT return a `handle` (§25).

Type parameters are introduced *inside* `Type` by the `$` sigil (§4.2); a
function that mentions any `$T` type parameter or `$N` size parameter in its
signature is **generic**. A `WhereClause` constrains a generic function:

```
WhereClause ::= "where" Constraint ( "," Constraint )*
Constraint  ::= Predicate "(" IDENT ")"
              | IDENT ":" Type ( "|" Type )*
Predicate   ::= "numeric" | "comparable" | "has_str" | "hashable" | "defaultable"
```

The predicate set is **closed** (exactly the five names above; an unknown
predicate is rejected) — this is the deliberate anti-traits stance (§7,
forthcoming). The type-set form `T: A | B | …` constrains `T` to one of a listed
set (up to 16 types). A `where` clause requires a generic function; at most 8
constraints are allowed.

> Provenance: `parse_fn`, `src/tychoc.c:2972-3072` (params `:2985-3011`,
> variadic-last `:3012-3014`, `where` `:3026-3064`).

### 4.1.2 Structs, enums, newtypes, handles

```
Struct     ::= "struct" IDENT TypeParams? ":" NEWLINE INDENT FieldDecl+ DEDENT
FieldDecl  ::= IDENT ":" Type NEWLINE
Enum       ::= "enum" IDENT TypeParams? ":" NEWLINE INDENT VariantDecl+ DEDENT
VariantDecl::= IDENT ( "(" Type ( "," Type )* ")" )? NEWLINE
Handle     ::= "handle" IDENT ":" NEWLINE INDENT "free" ":" IDENT NEWLINE DEDENT
TypeDecl   ::= "type" IDENT "=" Type NEWLINE
TypeParams ::= "(" "$" IDENT ( "," "$" IDENT )* ")"
```

A `struct` has one or more fields; an `enum` has one or more variants, each with
an optional payload of up to 8 types. A `handle` names an opaque FFI resource
and its C free function (§25). A `type` declaration introduces a
distinct **newtype** over an underlying type; the permitted underlying types are
constrained in §5. Recursion through a struct field is permitted
only via a container (e.g. `[Node]`), never as a direct by-value self-field
(§17).

> Provenance: `parse_struct`/`parse_enum`/`parse_handle`/`parse_typedecl`,
> `src/tychoc.c:3294-3446`.

### 4.1.3 Extern functions and subscripts

```
ExternFn   ::= "extern" STR? "fn" IDENT "(" ExternParamList? ")" ( "->" Type )? NEWLINE
Subscript  ::= "subscript" IDENT "(" ParamList? ")" "->" "inout" Type ":" NEWLINE
               INDENT "yield" "&" Place NEWLINE DEDENT
```

An `ExternFn` is bodyless and binds to a C symbol; its optional `STR` names a
link library. Its parameter and return types are restricted to the
FFI-crossable set — the scalars (`int`, `char`, `float`, `bool`), `string`,
`bytes`, `ptr`, `[int]`/`[float]`, typed handles, the first-class fixed-width
numerics `u8`…`u64` / `i8`…`i64` / `f32`, plus `inout` **scalar** out-parameters (not
`string`) and an `Option(string)` return ([§24](14-ffi.md)).

A `Subscript` declares a user-defined projection: it yields a place (an lvalue)
rooted in one of its parameters. Its rules — the place must be rooted in a
parameter, each parameter used at most once — are given in §18.
`Place` is defined in §4.4.

> Provenance: `parse_extern_fn` (`:3212-3282`), `parse_subscript`
> (`:3092-3143`).

## 4.2 Types

```
Type      ::= "$" IDENT                                        /* type parameter */
            | "soa" "[" Type "]"                               /* struct-of-arrays */
            | "fn" "(" ( Type ( "," Type )* )? ")" ( "->" Type )?
            | "(" Type ( "," Type )+ ")"                       /* tuple, 2..8 elements */
            | "[" ArrayOrMap
            | "Option" "(" Type ")"
            | "Result" "(" Type "," Type ")"
            | "Channel" "(" Type ")"
            | QualName TypeArgs?                               /* struct/enum/newtype/handle */
            | PrimType
ArrayOrMap::= "$" IDENT "]" Type                              /* [$N]T  size-parameter array */
            | INT "]" Type                                     /* [N]T   fixed-size array */
            | IDENT "]" Type                                   /* [C]T   fixed size named by an int const */
            | Type "]"                                         /* [T]    dynamic array */
            | Type ":" Type "]"                                /* [K:V]  map */
TypeArgs  ::= "(" Type ( "," Type )* ")"
QualName  ::= IDENT ( "." IDENT )?                            /* Name or pkg.Name */
PrimType  ::= "int" | "float" | "bool" | "string" | "ptr" | "bytes"
            | "u32" | "u64" | "f32"
```

Notes (constrained further in §5–§7):

- A `$IDENT` after `[` denotes a **size parameter** (`[$N]T`, a const-generic
  array) — and is distinguished from a dynamic `[$T]` (type-parameter element)
  by whether a *type* follows the `]`. A dynamic `[$T]` return followed by a
  `where` clause is disambiguated because `where` is not a type.
- A `[N]T` or `[C]T` (integer literal, or an `int` const name, before `]`
  followed by an element type) is a **fixed-size array**; a bare `[T]` is a
  **dynamic array**.
- A tuple type has 2–8 elements. A function type has up to 8 parameters. No
  element or parameter may be `void`.
- `QualName TypeArgs` applies a generic struct or enum to concrete type
  arguments (`Box(int)`); applying a generic to exactly its own type parameters
  is a recursive self-reference.
- There is no `char` or `void` type spelling (§3.6).

> Provenance: `parse_type_inner`, `src/tychoc.c:1569-1811`.

## 4.3 Blocks and statements

A block is a newline-introduced, indentation-delimited sequence of statements:

```
Block ::= INDENT Stmt+ DEDENT
Stmt  ::= ConstStmt | DeleteStmt | Return | Break | Continue
        | Select | Match | If | ParallelFor | For
        | MultiDecl | MultiAssign
        | Decl | TypedDecl | Assign | PlaceAssign | CompoundAssign
        | ExprStmt
```

### 4.3.1 Simple statements

```
ConstStmt      ::= "const" IDENT "=" ConstExpr NEWLINE
DeleteStmt     ::= "delete" Postfix NEWLINE            /* Postfix MUST be an index m[k] */
Return         ::= "return" ( Expr ( "," Expr )* )? NEWLINE
                 | "return" ValueCtrl
Break          ::= "break" NEWLINE
Continue       ::= "continue" NEWLINE
Decl           ::= IDENT ":=" ( Expr | ValueCtrl ) NEWLINE
TypedDecl      ::= IDENT ":" Type "=" ( Expr | ValueCtrl ) NEWLINE
Assign         ::= IDENT "=" ( Expr | ValueCtrl ) NEWLINE
MultiDecl      ::= IDENT ( "," IDENT )+ ":=" Expr NEWLINE
MultiAssign    ::= IDENT ( "," IDENT )+ "=" Expr NEWLINE
PlaceAssign    ::= Place "=" ( Expr | ValueCtrl ) NEWLINE
CompoundAssign ::= Place CompoundOp "=" Expr NEWLINE
CompoundOp     ::= "+" | "-" | "*" | "/" | "%" | "&" | "|" | "^" | "<<" | ">>"
ExprStmt       ::= Call NEWLINE
```

- `Decl` (`x := e`) declares a variable and infers its type; `TypedDecl`
  (`x: T = e`) declares with an explicit type; `Assign` (`x = e`) assigns to an
  existing variable. `MultiDecl`/`MultiAssign` destructure a tuple-valued RHS
  into 2–8 names.
- `PlaceAssign` and `CompoundAssign` write through a **place** (§4.4): an index,
  a field, a tuple element, or a user-subscript call. In `CompoundAssign`, a
  side-effecting call inside the place is evaluated once (hoisted); a pure index
  sub-expression may be evaluated twice, as writing the form out longhand would
  (this single-vs-double evaluation is pinned in §13).
- `DeleteStmt` removes a map element; the `Postfix` MUST be an index `m[k]`.
- The **only** valid bare-expression statement is a call. A bare variable,
  index, field, or `or_return` expression is rejected as having no effect.
- `ConstExpr` and `ValueCtrl` are defined in §4.1 and §4.3.2.

> Provenance: `parse_stmt`, `src/tychoc.c:2605-2950`; `ExprStmt` restriction
> `:2940-2949`; compound-assign hoist `:2919-2938`.

### 4.3.2 Compound statements

```
If          ::= "if" Expr ":" NEWLINE Block
                ( "elif" Expr ":" NEWLINE Block )*
                ( "else" ":" NEWLINE Block )?
Match       ::= "match" Expr ":" NEWLINE INDENT MatchArm+ DEDENT
MatchArm    ::= Pattern ":" NEWLINE Block
Pattern     ::= IDENT ( "(" IDENT ( "," IDENT )* ")" )?     /* variant, with 0..8 bindings */
              | IDENT "." IDENT                             /* pkg.Variant */
              | "_"                                         /* wildcard */
For         ::= "for" IDENT "in" "range" "(" Expr ( "," Expr ( "," Expr )? )? ")" ":" NEWLINE Block
              | "for" IDENT "in" Expr ":" NEWLINE Block
              | "for" Expr ":" NEWLINE Block
ParallelFor ::= "parallel" For          /* the For MUST be a range or foreach form */
Select      ::= "select" ":" NEWLINE INDENT SelectArm+ DEDENT
SelectArm   ::= "recv" "(" Expr "," IDENT ")" ":" NEWLINE Block
              | "default" ":" NEWLINE Block
              | "closed" ":" NEWLINE Block
ValueCtrl   ::= If | Match              /* value form: single-expression branches, tail position */
```

- `elif` is sugar for an `else` block containing a single nested `if`.
- The three `for` shapes are: **counting** (`for i in range(a[, b[, step]])`;
  one argument means `0..n`), **foreach** (`for x in collection`, over an array
  or a string's bytes), and **condition** (`for cond:`, the while-loop form).
  `break`/`continue` are valid in every shape and only inside a loop.
- `parallel for` applies only to a range or foreach loop (§22).
- `match` is exhaustive; a bare variant name is a nullary-variant pattern, a
  parenthesized list binds a variant's payload, `pkg.Variant` matches a
  qualified variant, and `_` is the wildcard (§19).
- **`ValueCtrl`** is the value-producing form of `if`/`match`: it may appear only
  as the tail of a declaration (`:=`, typed `=`), an assignment, a place
  assignment, or a `return`. Each branch/arm is a single expression; a value
  `if` MUST have an `else`; a value `match` MUST be exhaustive; all branches MUST
  unify to one type. These rules are given in §13/§14.

> Provenance: `parse_if` (`:2338`), `parse_match` (`:2409`, `:2723`), `for`/
> `parallel` (`:2731-2827`), `select` (`:2686-2722`), value-control routing
> (`:2655`,`:2858`,`:2872`,`:2881`,`:2903`).

## 4.4 Expressions

The expression grammar encodes precedence and associativity by nesting: each
level parses the next-tighter level and then, left-associatively, its own
operators (except the prefix levels, which are right-recursive).

```
Expr      ::= OrExpr
OrExpr    ::= AndExpr ( "or" AndExpr )*
AndExpr   ::= NotExpr ( "and" NotExpr )*
NotExpr   ::= "not" NotExpr | CmpExpr
CmpExpr   ::= AddExpr ( ( "==" | "!=" | "<" | ">" | "<=" | ">=" | "in" ) AddExpr )*
AddExpr   ::= MulExpr ( ( "+" | "-" | "|" | "^" ) MulExpr )*
MulExpr   ::= UnaryExpr ( ( "*" | "/" | "%" | "<<" | ">>" | "&" ) UnaryExpr )*
UnaryExpr ::= ( "-" | "&" | "~" ) UnaryExpr | Postfix
Postfix   ::= Primary PostfixOp* "or_return"? "..."?
PostfixOp ::= "[" Expr "]"                              /* index */
            | "[" Expr? ":" Expr? "]"                   /* slice (either bound optional) */
            | "." IDENT                                 /* field access */
            | "." INT                                   /* tuple index */
            | "." IDENT "(" ArgList? ")"                /* qualified pkg call (on a bare identifier) */
            | "(" ArgList? ")"                          /* call */
ArgList   ::= Expr ( "," Expr )*
```

The **place** grammar (assignable lvalues, §4.3.1) is the subset of `Postfix`
whose spine is an index, field, tuple-index, or subscript call rooted in a
variable:

```
Place ::= IDENT ( "[" Expr "]" | "." IDENT | "." INT | "(" ArgList? ")" )*
```

Primary expressions:

```
Primary ::= INT | FLOAT | STR | CHAR
          | "true" | "false" | "null"
          | "(" Expr ")"                                /* grouping */
          | "(" Expr ( "," Expr )+ ")"                  /* tuple literal, 2..8 */
          | ArrayOrMapLit                               /* [ ... ] forms (§16, §18) */
          | "soa" "[" "]" Type                          /* empty soa literal */
          | "channel" "(" Type "," Expr ")"             /* only as a declaration RHS */
          | "spawn" Call                                /* spawn a direct call */
          | Lambda
          | "None" | "Some" "(" Expr ")" | "Ok" "(" Expr ")" | "Err" "(" Expr ")"
          | IDENT "$" "(" Type ( "," Type )* ")" ( "(" ArgList? ")" )?  /* explicit type args */
          | IDENT "(" ArgList? ")"                      /* named call */
          | IDENT                                       /* variable, or nullary enum variant */
Lambda  ::= "fn" "(" LambdaParams? ")" ( "->" Type )? ":" Expr
LambdaParams ::= IDENT ( ":" Type )? ( "," IDENT ( ":" Type )? )*
```

A lambda parameter's type is **optional** (inferred from the expected function
type when omitted, §6.2), and a lambda parameter may **not** carry `inout`,
`sink`, or `...` — so a lambda uses `LambdaParams`, not the `ParamList` of §4.1.1.
`Call` in `ExprStmt` and after `spawn` denotes a `Postfix` whose outermost
operation is a call `(...)`. A `Subscript` (§4.1.3) parameter is likewise plain
`IDENT ":" Type` (no passing modes).

- The array/map literal forms (`[e, …]`, `[k: v, …]`, empty typed `[]T` /
  `[]K:V`) and the pending bare `[]`/`None` grounding are detailed in §16/§18
 .
- A lambda's body is a single expression. A closure captures by deep copy at
  creation (§13).
- `IDENT $ ( Type … )` supplies explicit type arguments to a generic call (e.g.
  `zero$(int)`); it is the only use of `$` in expression position.
- `spawn` and `channel(…)` are restricted in where they may appear (§23,
  forthcoming): `spawn` takes a direct call; `channel(T, cap)` is legal only as
  the direct right-hand side of a declaration.
- Assignment (`=`), declare-and-infer (`:=`), and compound assignment are
  **statement-level** (§4.3.1); they are **not** expression operators and never
  appear inside `Expr`.

> Provenance: precedence chain `src/tychoc.c:2256-2324`; postfix `:2130-2219`;
> `parse_primary` `:1880-2127`; unary `:2234-2254`.

## 4.5 Operator precedence and associativity

From tightest-binding to loosest. Every binary level is left-associative; the
prefix levels are right-associative.

| Precedence | Operators | Kind | Assoc |
|---|---|---|---|
| 1 (tightest) | `[]` index/slice · `.` field/tuple-index · `()` call · `or_return` · `...` | postfix | left |
| 2 | `-` `&` `~` | unary prefix | right |
| 3 | `*` `/` `%` `<<` `>>` `&` | binary | left |
| 4 | `+` `-` `\|` `^` | binary | left |
| 5 | `==` `!=` `<` `>` `<=` `>=` `in` | binary | left |
| 6 | `not` | unary prefix | right |
| 7 | `and` | binary | left |
| 8 (loosest) | `or` | binary | left |

This is **Go's precedence**: the shift operators (`<<`, `>>`) and bitwise-AND
(`&`) bind at the multiplicative level, and bitwise-OR/XOR (`|`, `^`) bind at the
additive level — so every bitwise operator binds *tighter* than any comparison.
Consequently `a & b == c` parses as `(a & b) == c`. The unary `&` (address-of /
`inout` argument) and binary `&` (bitwise-AND) share a spelling but occupy
different precedence levels (2 and 3). `and`/`or` short-circuit (§13,
forthcoming). Evaluation order within a precedence level, and the order of
argument and place sub-expression evaluation, is pinned in §13 —
the grammar fixes only associativity, not side-effect order.

> Provenance: `parse_mul`/`parse_add`/`parse_cmp`/`parse_not`/`parse_and`/
> `parse_expr`, `src/tychoc.c:2256-2324`; postfix/unary `:2130-2254`.
