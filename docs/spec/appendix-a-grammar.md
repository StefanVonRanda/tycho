# Appendix A — Collected grammar

This appendix collects every grammar production of the language into a single
listing for at-a-glance reference. It is **normative but not authoritative**: the
productions are reproduced verbatim from their defining chapters, which remain
the source of truth. On any discrepancy the in-chapter definition governs, and
the discrepancy is a bug in this appendix (the `make spec-check` gate exists to
prevent one — see the invariant note at the end).

The grammar is defined in two chapters:

- the **lexical grammar** — [§3](01-lexical.md): the token set (§3.5–§3.8) and
  literal productions (§3.9), plus the indentation algorithm (§3.4) that
  produces the `INDENT`/`DEDENT`/`NEWLINE` layout tokens;
- the **phrase grammar** — [§4](02-grammar.md): declarations (§4.1), types
  (§4.2), statements (§4.3), and expressions (§4.4), with the precedence table
  (§4.5), reproduced in [Appendix C](appendix-c-precedence.md).

## A.1 Notation and terminals

Productions use the W3C-style EBNF of [§2](00-conventions.md); `X?` is optional,
`X*` zero-or-more, `X+` one-or-more, `( … )` groups, `|` alternates, and a
double-quoted string is a literal token. Grammar terminals are the token kinds of
[§3.5](01-lexical.md#35-tokens): the name/literal tokens `IDENT`, `INT`, `FLOAT`,
`STR`, `CHAR`, the layout tokens `NEWLINE`, `INDENT`, `DEDENT`, `EOF`, and the
reserved words ([§3.6](01-lexical.md#36-keywords), listed in
[Appendix B](appendix-b-keywords.md)). Some words the grammar uses as literal
terminals — `soa`, `channel`, `None`, `Some`, `Ok`, `Err`, and the builtin names
— are **contextual identifiers** ([§3.7](01-lexical.md#37-contextual-identifiers)),
not reserved: they are keywords only in the positions shown here.

<!-- BEGIN GENERATED: scripts/gen_grammar.sh — do not edit by hand; run `make spec-check` -->
### A.2 Lexical grammar (§3)

Terminals produced by the lexer and consumed by the phrase grammar are
`IDENT`, `INT`, `FLOAT`, `STR`, `CHAR` (§3.5), and the layout tokens
`NEWLINE`, `INDENT`, `DEDENT`, `EOF` (§3.4). The spellings below define the
literal terminals; `INT` is spelled by `IntLit`, `FLOAT` by `FloatLit`,
`CHAR` by `CharLit`, `STR` by `StrLit`.

```ebnf
Comment ::= "#" (any byte except newline)*
IntLit ::= [0-9]+
FloatLit ::= [0-9]+ "." [0-9]+ Exp?
           | [0-9]+ Exp
           | "." [0-9]+ Exp?          /* leading-dot form, position-restricted */
Exp      ::= ("e" | "E") ("+" | "-")? [0-9]+
CharLit ::= "'" ( CharEscape | (any byte except "'", "\", newline) ) "'"
CharEscape ::= "\" ( "n" | "t" | "r" | "0" | "\" | "'" )
StrLit ::= '"' StrElem* '"'
StrElem ::= StrEscape | (any byte except '"', "\", newline, and raw control bytes below 0x20 other than tab)
StrEscape ::= "\" ( "n" | "t" | "\" | '"' )
```

### A.3 Phrase grammar (§4)

```ebnf
Program   ::= TopDecl*
TopDecl   ::= PackageDecl | ImportDecl | ExternFn | ConstDecl
            | Subscript | Struct | Enum | Handle | TypeDecl | Fn
PackageDecl ::= "package" IDENT NEWLINE
ImportDecl  ::= "import" IDENT? STR NEWLINE
ConstDecl   ::= "const" IDENT "=" ConstExpr NEWLINE
Fn         ::= "fn" IDENT "(" ParamList? ")" ( "->" Type )? WhereClause? ":" NEWLINE Block
ParamList  ::= Param ( "," Param )*
Param      ::= IDENT ":" ( "inout" | "sink" )? "..."? Type
WhereClause ::= "where" Constraint ( "," Constraint )*
Constraint  ::= Predicate "(" IDENT ")"
              | IDENT ":" Type ( "|" Type )*
Predicate   ::= "numeric" | "comparable" | "has_str" | "hashable" | "defaultable"
Struct     ::= "struct" IDENT TypeParams? ":" NEWLINE INDENT FieldDecl+ DEDENT
FieldDecl  ::= IDENT ":" Type NEWLINE
Enum       ::= "enum" IDENT TypeParams? ":" NEWLINE INDENT VariantDecl+ DEDENT
VariantDecl::= IDENT ( "(" Type ( "," Type )* ")" )? NEWLINE
Handle     ::= "handle" IDENT ":" NEWLINE INDENT "free" ":" IDENT NEWLINE DEDENT
TypeDecl   ::= "type" IDENT "=" Type NEWLINE
TypeParams ::= "(" "$" IDENT ( "," "$" IDENT )* ")"
ExternFn   ::= "extern" STR? "fn" IDENT "(" ExternParamList? ")" ( "->" Type )? NEWLINE
Subscript  ::= "subscript" IDENT "(" ParamList? ")" "->" "inout" Type ":" NEWLINE
               INDENT "yield" "&" Place NEWLINE DEDENT
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
Block ::= INDENT Stmt+ DEDENT
Stmt  ::= ConstStmt | DeleteStmt | Return | Break | Continue
        | Select | Match | If | ParallelFor | For
        | MultiDecl | MultiAssign
        | Decl | TypedDecl | Assign | PlaceAssign | CompoundAssign
        | ExprStmt
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
Place ::= IDENT ( "[" Expr "]" | "." IDENT | "." INT | "(" ArgList? ")" )*
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
<!-- END GENERATED -->

## A.4 Non-terminals defined in prose elsewhere

Four names appearing on the right-hand sides above are defined by prose in their
feature chapters rather than by a `::=` production, and so are not repeated in the
listing:

- **`Call`** — a `Postfix` (§4.4) whose outermost operator is a call `( … )`;
  used by `ExprStmt` and after `spawn` ([§4.4](02-grammar.md#44-expressions)).
- **`ConstExpr`** — an expression that folds to a single literal at compile time
  (integer arithmetic, bitwise, unary, and backward references to earlier
  top-level consts), defined in
  [§4.1](02-grammar.md#41-program-and-top-level-declarations) and
  [§12](08-declarations.md).
- **`ArrayOrMapLit`** — the `[ … ]` array, map, and `soa` literal forms, defined
  with their element/key typing rules in [§16](12-aggregates.md) (arrays) and
  [§18](12-aggregates.md#18-maps-and-subscripts) (maps).
- **`ExternParamList`** — the parameter list of an `extern fn`, defined with the
  FFI-permitted parameter types in [§24](14-ffi.md).

## A.5 The identical-to-source invariant

Everything between the `BEGIN GENERATED` / `END GENERATED` markers above is
produced mechanically by `scripts/gen_grammar.sh`, which extracts every fenced
grammar block (any block containing a `::=` production) from
[§3](01-lexical.md) and [§4](02-grammar.md) verbatim, in source order. The
`make spec-check` gate re-runs the generator and diffs its output against this
region; any drift between a production here and its defining chapter fails the
build. This appendix therefore cannot silently become a stale second source of
truth — it is a checked projection of the chapters, not a parallel copy.
