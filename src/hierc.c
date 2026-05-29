/* hierc - the Hier compiler.
 *
 * Pipeline:  .hi source -> tokens (indentation-aware) -> AST -> type
 * resolution -> C source (with the Hier runtime embedded verbatim) ->
 * invoke `cc` to produce a native binary.
 *
 * Usage:
 *   hierc file.hi [-o name] [--emit-c] [--cc <compiler>]
 *     default: writes <base>.c and compiles it to <base> with `cc`.
 *     --emit-c: only write the C file, do not compile.
 *
 * The language is deliberately tiny (see README). One proc named `main`
 * is the entry point.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "hier_rt_embed.h"   /* defines: static const char *HIER_RUNTIME */

/* ------------------------------------------------------------------ util */

static const char *g_srcname = "<input>";

__attribute__((noreturn, format(printf, 2, 3)))
static void die_at(int line, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "%s:%d: error: ", g_srcname, line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static char *sfmt(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char *s = NULL;
    if (vasprintf(&s, fmt, ap) < 0 || !s) { fprintf(stderr, "hierc: oom\n"); exit(1); }
    va_end(ap);
    return s;
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "hierc: oom\n"); exit(1); }
    return p;
}

static char *xstrndup(const char *s, size_t n) {
    char *r = (char *)xmalloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

/* --------------------------------------------------------------- tokens */

typedef enum {
    TK_EOF, TK_NEWLINE, TK_INDENT, TK_DEDENT,
    TK_IDENT, TK_INT, TK_STR,
    TK_COLONCOLON, TK_COLONEQ, TK_COLON, TK_EQ,
    TK_EQEQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,
    TK_LPAREN, TK_RPAREN, TK_LBRACKET, TK_RBRACKET, TK_COMMA, TK_ARROW,
    TK_FN, TK_RETURN, TK_IF, TK_ELSE, TK_FOR, TK_IN, TK_TRUE, TK_FALSE, TK_STRUCT,
    TK_DOT,
    TK_KW_INT, TK_KW_BOOL, TK_KW_STRING
} TokKind;

typedef struct {
    TokKind kind;
    char   *text;   /* identifier name, or raw string contents */
    long    ival;
    int     line;
} Tok;

typedef struct {
    Tok *v;
    int  n, cap;
} TokVec;

static void tv_push(TokVec *t, Tok tok) {
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 64;
        t->v = (Tok *)realloc(t->v, (size_t)t->cap * sizeof(Tok));
        if (!t->v) { fprintf(stderr, "hierc: oom\n"); exit(1); }
    }
    t->v[t->n++] = tok;
}

static TokKind keyword(const char *s) {
    if (!strcmp(s, "fn"))     return TK_FN;
    if (!strcmp(s, "return")) return TK_RETURN;
    if (!strcmp(s, "if"))     return TK_IF;
    if (!strcmp(s, "else"))   return TK_ELSE;
    if (!strcmp(s, "for"))    return TK_FOR;
    if (!strcmp(s, "in"))     return TK_IN;
    if (!strcmp(s, "struct")) return TK_STRUCT;
    if (!strcmp(s, "true"))   return TK_TRUE;
    if (!strcmp(s, "false"))  return TK_FALSE;
    if (!strcmp(s, "int"))    return TK_KW_INT;
    if (!strcmp(s, "bool"))   return TK_KW_BOOL;
    if (!strcmp(s, "string")) return TK_KW_STRING;
    return TK_IDENT;
}

/* Indentation-aware lexer. Processes the source line by line so blank
 * lines and comment-only lines never affect the indent stack. */
static TokVec lex(const char *src) {
    TokVec out = {0};
    int indent_stack[256];
    int sp = 0;
    indent_stack[0] = 0;
    int line = 0;

    const char *p = src;
    while (*p) {
        line++;
        const char *ls = p;                 /* line start */
        /* count leading spaces */
        int col = 0;
        while (*p == ' ') { col++; p++; }
        if (*p == '\t')
            die_at(line, "tabs are not allowed for indentation; use spaces");

        /* blank or comment-only line: skip without touching indentation */
        if (*p == '\n' || *p == '\0' || *p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* emit INDENT / DEDENT for this logical line */
        if (col > indent_stack[sp]) {
            if (sp + 1 >= 256) die_at(line, "indentation too deep");
            indent_stack[++sp] = col;
            tv_push(&out, (Tok){TK_INDENT, NULL, 0, line});
        } else {
            while (col < indent_stack[sp]) {
                sp--;
                tv_push(&out, (Tok){TK_DEDENT, NULL, 0, line});
            }
            if (col != indent_stack[sp])
                die_at(line, "inconsistent indentation");
        }

        /* lex the rest of the line */
        (void)ls;
        while (*p && *p != '\n' && *p != '#') {
            char c = *p;
            if (c == ' ' || c == '\r') { p++; continue; }

            if (isdigit((unsigned char)c)) {
                long v = 0;
                const char *s = p;
                while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; }
                (void)s;
                tv_push(&out, (Tok){TK_INT, NULL, v, line});
                continue;
            }
            if (isalpha((unsigned char)c) || c == '_') {
                const char *s = p;
                while (isalnum((unsigned char)*p) || *p == '_') p++;
                char *name = xstrndup(s, (size_t)(p - s));
                TokKind k = keyword(name);
                tv_push(&out, (Tok){k, name, 0, line});
                continue;
            }
            if (c == '"') {
                p++;
                /* keep raw contents; Hier escapes (\n \t \\ \") are a
                 * subset of C escapes so they pass straight through to
                 * the generated C string literal. */
                char buf[4096];
                int bn = 0;
                while (*p && *p != '"') {
                    if (*p == '\n') die_at(line, "unterminated string literal");
                    if (*p == '\\') {
                        if (bn + 2 >= (int)sizeof buf) die_at(line, "string too long");
                        buf[bn++] = *p++;
                        if (!*p) die_at(line, "unterminated string literal");
                        char e = *p;
                        if (e != 'n' && e != 't' && e != '\\' && e != '"')
                            die_at(line, "unsupported escape \\%c (use \\n \\t \\\\ \\\")", e);
                        buf[bn++] = *p++;
                    } else {
                        if (bn + 1 >= (int)sizeof buf) die_at(line, "string too long");
                        buf[bn++] = *p++;
                    }
                }
                if (*p != '"') die_at(line, "unterminated string literal");
                p++;
                tv_push(&out, (Tok){TK_STR, xstrndup(buf, (size_t)bn), 0, line});
                continue;
            }

            /* operators (two-char first) */
            char c2 = p[1];
            TokKind k; int len = 1;
            if (c == ':' && c2 == ':')      { k = TK_COLONCOLON; len = 2; }
            else if (c == ':' && c2 == '=') { k = TK_COLONEQ;    len = 2; }
            else if (c == '=' && c2 == '=') { k = TK_EQEQ;       len = 2; }
            else if (c == '!' && c2 == '=') { k = TK_NEQ;        len = 2; }
            else if (c == '<' && c2 == '=') { k = TK_LE;         len = 2; }
            else if (c == '>' && c2 == '=') { k = TK_GE;         len = 2; }
            else if (c == '-' && c2 == '>') { k = TK_ARROW;      len = 2; }
            else if (c == ':') k = TK_COLON;
            else if (c == '=') k = TK_EQ;
            else if (c == '<') k = TK_LT;
            else if (c == '>') k = TK_GT;
            else if (c == '+') k = TK_PLUS;
            else if (c == '-') k = TK_MINUS;
            else if (c == '*') k = TK_STAR;
            else if (c == '/') k = TK_SLASH;
            else if (c == '(') k = TK_LPAREN;
            else if (c == ')') k = TK_RPAREN;
            else if (c == '[') k = TK_LBRACKET;
            else if (c == ']') k = TK_RBRACKET;
            else if (c == '.') k = TK_DOT;
            else if (c == ',') k = TK_COMMA;
            else die_at(line, "unexpected character '%c'", c);
            tv_push(&out, (Tok){k, NULL, 0, line});
            p += len;
        }

        tv_push(&out, (Tok){TK_NEWLINE, NULL, 0, line});
        if (*p == '#') while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    /* close out remaining indentation, then EOF */
    while (sp > 0) { sp--; tv_push(&out, (Tok){TK_DEDENT, NULL, 0, line}); }
    tv_push(&out, (Tok){TK_EOF, NULL, 0, line});
    return out;
}

/* ------------------------------------------------------------------ AST */

/* Type is an int so a struct id can be encoded in it: values >=
 * T_STRUCT_BASE name a struct (id = value - base). The primitive enum
 * constants keep working in every existing == and switch. */
typedef int Type;
enum { T_VOID, T_INT, T_BOOL, T_STRING, T_ARRAY_INT };
#define T_STRUCT_BASE   64
#define IS_STRUCT(t)    ((t) >= T_STRUCT_BASE)
#define STRUCT_ID(t)    ((int)((t) - T_STRUCT_BASE))
#define STRUCT_TYPE(id) (T_STRUCT_BASE + (id))

typedef struct { char *name; Type type; } Field;
typedef struct { char *name; Field fields[64]; int nfields; int line; } StructDef;
static StructDef g_structs[128];
static int g_nstructs = 0;
static int struct_find(const char *name) {
    for (int i = 0; i < g_nstructs; i++)
        if (!strcmp(g_structs[i].name, name)) return i;
    return -1;
}

/* trailing space so "%sh_name" / "%s_ret" / signatures all read right;
 * "char *" needs none because "char *h_name" is already valid */
static const char *c_type(Type t) {
    if (IS_STRUCT(t)) return sfmt("S_%s ", g_structs[STRUCT_ID(t)].name);
    switch (t) {
        case T_INT:       return "long ";
        case T_BOOL:      return "int ";
        case T_STRING:    return "char *";
        case T_ARRAY_INT: return "HierArrInt ";
        default:          return "void ";
    }
}
static const char *type_name(Type t) {
    if (IS_STRUCT(t)) return g_structs[STRUCT_ID(t)].name;
    switch (t) {
        case T_INT:       return "int";
        case T_BOOL:      return "bool";
        case T_STRING:    return "string";
        case T_ARRAY_INT: return "[int]";
        default:          return "void";
    }
}

typedef enum { E_INT, E_STR, E_BOOL, E_IDENT, E_BINOP, E_CALL, E_ARRLIT, E_INDEX,
               E_STRUCTLIT, E_FIELD } ExprKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    Type     type;     /* filled in by resolver */
    int      line;
    long     ival;     /* E_INT / E_BOOL */
    char    *sval;     /* E_STR contents / E_IDENT name / E_CALL callee */
    TokKind  op;       /* E_BINOP */
    Expr    *lhs, *rhs;
    Expr   **args; int nargs;   /* E_CALL */
};

typedef enum { S_DECL, S_ASSIGN, S_RETURN, S_IF, S_WHILE, S_FORRANGE,
               S_INDEXSET, S_FIELDSET, S_EXPR } StmtKind;

typedef struct Stmt Stmt;
struct Stmt {
    StmtKind kind;
    int      line;
    char    *name;         /* S_DECL / S_ASSIGN target, or S_FORRANGE loop var */
    Type     decl_type;    /* S_DECL resolved type */
    int      typed_decl;   /* S_DECL: had an explicit type annotation */
    Type     annot;        /* explicit annotation when typed_decl */
    Expr    *expr;         /* value / condition / return / S_INDEXSET rhs */
    Expr    *target;       /* S_INDEXSET lvalue (an E_INDEX) */
    Expr    *r_start, *r_stop, *r_step;  /* S_FORRANGE; r_step NULL means 1 */
    Stmt   **body; int nbody;
    Stmt   **els;  int nels;
};

typedef struct { char *name; Type type; } Param;

typedef struct {
    char   *name;
    Param  *params; int nparams;
    Type    ret;
    int     has_ret;       /* explicit -> type present */
    Stmt  **body; int nbody;
    int     line;
} Proc;

typedef struct { Proc **v; int n, cap; } ProcVec;

/* --------------------------------------------------------------- parser */

typedef struct { Tok *t; int p; } Parser;

static Tok *cur(Parser *ps)  { return &ps->t[ps->p]; }
static Tok *peek(Parser *ps, int k) { return &ps->t[ps->p + k]; }
static int  at(Parser *ps, TokKind k) { return cur(ps)->kind == k; }

static Tok *eat(Parser *ps, TokKind k, const char *what) {
    if (!at(ps, k)) die_at(cur(ps)->line, "expected %s", what);
    return &ps->t[ps->p++];
}
static int accept(Parser *ps, TokKind k) {
    if (at(ps, k)) { ps->p++; return 1; }
    return 0;
}

static Type parse_type(Parser *ps) {
    Tok *t = cur(ps);
    if (t->kind == TK_LBRACKET) {        /* [int] */
        ps->p++;
        Type elem = parse_type(ps);
        eat(ps, TK_RBRACKET, "']'");
        if (elem != T_INT) die_at(t->line, "only [int] arrays are supported for now");
        return T_ARRAY_INT;
    }
    if (t->kind == TK_IDENT) {           /* a struct name */
        int sid = struct_find(t->text);
        if (sid < 0) die_at(t->line, "unknown type '%s'", t->text);
        ps->p++;
        return STRUCT_TYPE(sid);
    }
    switch (t->kind) {
        case TK_KW_INT:    ps->p++; return T_INT;
        case TK_KW_BOOL:   ps->p++; return T_BOOL;
        case TK_KW_STRING: ps->p++; return T_STRING;
        default: die_at(t->line, "expected a type (int, bool, string, [int], or a struct)");
    }
    return T_VOID; /* unreachable */
}

static Expr *new_expr(ExprKind k, int line) {
    Expr *e = (Expr *)xmalloc(sizeof(Expr));
    memset(e, 0, sizeof *e);
    e->kind = k; e->line = line;
    return e;
}

static Expr *parse_expr(Parser *ps);

static Expr *parse_primary(Parser *ps) {
    Tok *t = cur(ps);
    if (t->kind == TK_INT)  { ps->p++; Expr *e = new_expr(E_INT, t->line);  e->ival = t->ival; return e; }
    if (t->kind == TK_STR)  { ps->p++; Expr *e = new_expr(E_STR, t->line);  e->sval = t->text; return e; }
    if (t->kind == TK_TRUE) { ps->p++; Expr *e = new_expr(E_BOOL, t->line); e->ival = 1; return e; }
    if (t->kind == TK_FALSE){ ps->p++; Expr *e = new_expr(E_BOOL, t->line); e->ival = 0; return e; }
    if (t->kind == TK_LPAREN) {
        ps->p++;
        Expr *e = parse_expr(ps);
        eat(ps, TK_RPAREN, "')'");
        return e;
    }
    if (t->kind == TK_LBRACKET) {            /* array literal */
        ps->p++;
        Expr *e = new_expr(E_ARRLIT, t->line);
        if (at(ps, TK_RBRACKET)) {           /* empty: []int */
            ps->p++;
            Type elem = parse_type(ps);
            if (elem != T_INT) die_at(t->line, "only [int] arrays are supported for now");
            return e;
        }
        int cap = 0;
        while (!at(ps, TK_RBRACKET)) {
            if (e->nargs == cap) { cap = cap ? cap * 2 : 4; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *)); }
            e->args[e->nargs++] = parse_expr(ps);
            if (!accept(ps, TK_COMMA)) break;
        }
        eat(ps, TK_RBRACKET, "']'");
        return e;
    }
    if (t->kind == TK_IDENT) {
        ps->p++;
        if (at(ps, TK_LPAREN)) {           /* call */
            ps->p++;
            Expr *e = new_expr(E_CALL, t->line);
            e->sval = t->text;
            int cap = 0;
            while (!at(ps, TK_RPAREN)) {
                if (e->nargs == cap) {
                    cap = cap ? cap * 2 : 4;
                    e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *));
                }
                e->args[e->nargs++] = parse_expr(ps);
                if (!accept(ps, TK_COMMA)) break;
            }
            eat(ps, TK_RPAREN, "')'");
            return e;
        }
        Expr *e = new_expr(E_IDENT, t->line);  /* variable */
        e->sval = t->text;
        return e;
    }
    die_at(t->line, "expected an expression");
    return NULL;
}

/* postfix: primary ( '[' expr ']' | '.' field )* */
static Expr *parse_postfix(Parser *ps) {
    Expr *e = parse_primary(ps);
    for (;;) {
        if (at(ps, TK_LBRACKET)) {
            Tok *t = cur(ps); ps->p++;
            Expr *idx = parse_expr(ps);
            eat(ps, TK_RBRACKET, "']'");
            Expr *ix = new_expr(E_INDEX, t->line);
            ix->lhs = e; ix->rhs = idx;
            e = ix;
        } else if (at(ps, TK_DOT)) {
            Tok *t = cur(ps); ps->p++;
            Tok *f = eat(ps, TK_IDENT, "a field name after '.'");
            Expr *fe = new_expr(E_FIELD, t->line);
            fe->lhs = e; fe->sval = f->text;
            e = fe;
        } else break;
    }
    return e;
}

static Expr *parse_unary(Parser *ps) {
    if (at(ps, TK_MINUS)) {
        Tok *t = cur(ps); ps->p++;
        Expr *zero = new_expr(E_INT, t->line); zero->ival = 0;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = TK_MINUS; e->lhs = zero; e->rhs = parse_unary(ps);
        return e;
    }
    return parse_postfix(ps);
}

static Expr *parse_mul(Parser *ps) {
    Expr *l = parse_unary(ps);
    while (at(ps, TK_STAR) || at(ps, TK_SLASH)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = t->kind; e->lhs = l; e->rhs = parse_unary(ps);
        l = e;
    }
    return l;
}

static Expr *parse_add(Parser *ps) {
    Expr *l = parse_mul(ps);
    while (at(ps, TK_PLUS) || at(ps, TK_MINUS)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = t->kind; e->lhs = l; e->rhs = parse_mul(ps);
        l = e;
    }
    return l;
}

static Expr *parse_expr(Parser *ps) {           /* comparison level */
    Expr *l = parse_add(ps);
    while (at(ps, TK_EQEQ) || at(ps, TK_NEQ) || at(ps, TK_LT) ||
           at(ps, TK_GT)   || at(ps, TK_LE)  || at(ps, TK_GE)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = t->kind; e->lhs = l; e->rhs = parse_add(ps);
        l = e;
    }
    return l;
}

static Stmt **parse_block(Parser *ps, int *count);

static Stmt *new_stmt(StmtKind k, int line) {
    Stmt *s = (Stmt *)xmalloc(sizeof(Stmt));
    memset(s, 0, sizeof *s);
    s->kind = k; s->line = line;
    return s;
}

static Stmt *parse_stmt(Parser *ps) {
    Tok *t = cur(ps);

    if (t->kind == TK_RETURN) {
        ps->p++;
        Stmt *s = new_stmt(S_RETURN, t->line);
        if (!at(ps, TK_NEWLINE)) s->expr = parse_expr(ps);
        eat(ps, TK_NEWLINE, "newline");
        return s;
    }
    if (t->kind == TK_IF) {
        ps->p++;
        Stmt *s = new_stmt(S_IF, t->line);
        s->expr = parse_expr(ps);
        eat(ps, TK_COLON, "':' before the block");
        eat(ps, TK_NEWLINE, "newline");
        s->body = parse_block(ps, &s->nbody);
        if (at(ps, TK_ELSE)) {
            ps->p++;
            eat(ps, TK_COLON, "':' after else");
            eat(ps, TK_NEWLINE, "newline after else");
            s->els = parse_block(ps, &s->nels);
        }
        return s;
    }
    if (t->kind == TK_FOR) {
        ps->p++;
        /* counting form: `for i in range(...)` */
        if (at(ps, TK_IDENT) && peek(ps, 1)->kind == TK_IN) {
            Stmt *s = new_stmt(S_FORRANGE, t->line);
            Tok *var = eat(ps, TK_IDENT, "a loop variable");
            s->name = var->text;
            eat(ps, TK_IN, "'in'");
            Tok *rg = eat(ps, TK_IDENT, "'range'");
            if (strcmp(rg->text, "range") != 0)
                die_at(rg->line, "for loops only iterate 'range(...)'");
            eat(ps, TK_LPAREN, "'('");
            Expr *a1 = parse_expr(ps);
            Expr *a2 = NULL, *a3 = NULL;
            if (accept(ps, TK_COMMA)) a2 = parse_expr(ps);
            if (a2 && accept(ps, TK_COMMA)) a3 = parse_expr(ps);
            eat(ps, TK_RPAREN, "')'");
            eat(ps, TK_COLON, "':' before the block");
            eat(ps, TK_NEWLINE, "newline");
            if (!a2) {                       /* range(n): 0 .. n */
                Expr *zero = new_expr(E_INT, t->line); zero->ival = 0;
                s->r_start = zero; s->r_stop = a1; s->r_step = NULL;
            } else {                         /* range(a, b[, step]) */
                s->r_start = a1; s->r_stop = a2; s->r_step = a3;
            }
            s->body = parse_block(ps, &s->nbody);
            return s;
        }
        /* condition form: `for cond:` — does everything a while loop does */
        Stmt *s = new_stmt(S_WHILE, t->line);
        s->expr = parse_expr(ps);
        eat(ps, TK_COLON, "':' before the block");
        eat(ps, TK_NEWLINE, "newline");
        s->body = parse_block(ps, &s->nbody);
        return s;
    }

    /* declaration or assignment begins with an identifier */
    if (t->kind == TK_IDENT &&
        (peek(ps, 1)->kind == TK_COLONEQ ||
         peek(ps, 1)->kind == TK_COLON   ||
         peek(ps, 1)->kind == TK_EQ)) {
        char *name = t->text;
        ps->p++;
        if (accept(ps, TK_COLONEQ)) {
            Stmt *s = new_stmt(S_DECL, t->line);
            s->name = name;
            s->expr = parse_expr(ps);
            eat(ps, TK_NEWLINE, "newline");
            return s;
        }
        if (accept(ps, TK_COLON)) {
            Stmt *s = new_stmt(S_DECL, t->line);
            s->name = name;
            s->typed_decl = 1;
            s->annot = parse_type(ps);
            eat(ps, TK_EQ, "'=' in typed declaration");
            s->expr = parse_expr(ps);
            eat(ps, TK_NEWLINE, "newline");
            return s;
        }
        eat(ps, TK_EQ, "'='");
        Stmt *s = new_stmt(S_ASSIGN, t->line);
        s->name = name;
        s->expr = parse_expr(ps);
        eat(ps, TK_NEWLINE, "newline");
        return s;
    }

    /* expression statement, or an index-assignment `xs[i] = v` */
    Expr *e = parse_expr(ps);
    if (accept(ps, TK_EQ)) {
        if (e->kind != E_INDEX && e->kind != E_FIELD)
            die_at(t->line, "cannot assign to this expression");
        Stmt *s = new_stmt(e->kind == E_INDEX ? S_INDEXSET : S_FIELDSET, t->line);
        s->target = e;
        s->expr = parse_expr(ps);
        eat(ps, TK_NEWLINE, "newline");
        return s;
    }
    Stmt *s = new_stmt(S_EXPR, t->line);
    s->expr = e;
    eat(ps, TK_NEWLINE, "newline");
    return s;
}

static Stmt **parse_block(Parser *ps, int *count) {
    eat(ps, TK_INDENT, "an indented block");
    Stmt **body = NULL; int n = 0, cap = 0;
    while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
        if (accept(ps, TK_NEWLINE)) continue;
        if (n == cap) { cap = cap ? cap * 2 : 8; body = (Stmt **)realloc(body, (size_t)cap * sizeof(Stmt *)); }
        body[n++] = parse_stmt(ps);
    }
    eat(ps, TK_DEDENT, "dedent");
    *count = n;
    return body;
}

static Proc *parse_fn(Parser *ps) {
    eat(ps, TK_FN, "'fn'");
    Tok *nameT = eat(ps, TK_IDENT, "a procedure name");
    eat(ps, TK_LPAREN, "'('");

    Proc *pr = (Proc *)xmalloc(sizeof(Proc));
    memset(pr, 0, sizeof *pr);
    pr->name = nameT->text;
    pr->line = nameT->line;

    int cap = 0;
    while (!at(ps, TK_RPAREN)) {
        Tok *pn = eat(ps, TK_IDENT, "a parameter name");
        eat(ps, TK_COLON, "':' after parameter name");
        Type pt = parse_type(ps);
        if (pr->nparams == cap) { cap = cap ? cap * 2 : 4; pr->params = (Param *)realloc(pr->params, (size_t)cap * sizeof(Param)); }
        pr->params[pr->nparams].name = pn->text;
        pr->params[pr->nparams].type = pt;
        pr->nparams++;
        if (!accept(ps, TK_COMMA)) break;
    }
    eat(ps, TK_RPAREN, "')'");

    if (accept(ps, TK_ARROW)) { pr->ret = parse_type(ps); pr->has_ret = 1; }
    else pr->ret = T_VOID;

    eat(ps, TK_COLON, "':' before the block");
    eat(ps, TK_NEWLINE, "newline");
    pr->body = parse_block(ps, &pr->nbody);
    return pr;
}

/* struct Name:
 *     field: type
 *     ...
 * Registered into g_structs immediately so later declarations can name it
 * as a type (a struct must be defined before it is used as a type). */
static void parse_struct(Parser *ps) {
    eat(ps, TK_STRUCT, "'struct'");
    Tok *nameT = eat(ps, TK_IDENT, "a struct name");
    if (struct_find(nameT->text) >= 0) die_at(nameT->line, "'%s' is already defined", nameT->text);
    if (g_nstructs >= 128) die_at(nameT->line, "too many structs");
    eat(ps, TK_COLON, "':' before the block");
    eat(ps, TK_NEWLINE, "newline");
    eat(ps, TK_INDENT, "an indented field list");

    StructDef *sd = &g_structs[g_nstructs];
    sd->name = nameT->text;
    sd->nfields = 0;
    sd->line = nameT->line;
    while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
        if (accept(ps, TK_NEWLINE)) continue;
        Tok *fn = eat(ps, TK_IDENT, "a field name");
        eat(ps, TK_COLON, "':' after field name");
        Type ft = parse_type(ps);
        if (ft == T_STRING || ft == T_ARRAY_INT)
            die_at(fn->line, "struct fields must be int, bool, or another struct for now");
        if (sd->nfields >= 64) die_at(fn->line, "too many fields (max 64)");
        sd->fields[sd->nfields].name = fn->text;
        sd->fields[sd->nfields].type = ft;
        sd->nfields++;
        eat(ps, TK_NEWLINE, "newline");
    }
    eat(ps, TK_DEDENT, "dedent");
    if (sd->nfields == 0) die_at(nameT->line, "a struct needs at least one field");
    g_nstructs++;                 /* commit only once fully parsed */
}

static ProcVec parse_program(Tok *toks) {
    Parser ps = { toks, 0 };
    ProcVec out = {0};
    while (!at(&ps, TK_EOF)) {
        if (accept(&ps, TK_NEWLINE)) continue;
        if (at(&ps, TK_STRUCT)) { parse_struct(&ps); continue; }
        Proc *pr = parse_fn(&ps);
        if (out.n == out.cap) { out.cap = out.cap ? out.cap * 2 : 8; out.v = (Proc **)realloc(out.v, (size_t)out.cap * sizeof(Proc *)); }
        out.v[out.n++] = pr;
    }
    return out;
}

/* ------------------------------------------------------- function table */

typedef struct {
    const char *name;
    Type        ret;
    Type        params[8];
    int         nparams;
    int         builtin;
} Sig;

static Sig  g_sigs[256];
static int  g_nsigs = 0;

static Sig *sig_find(const char *name) {
    for (int i = 0; i < g_nsigs; i++)
        if (!strcmp(g_sigs[i].name, name)) return &g_sigs[i];
    return NULL;
}

static void register_builtins(void) {
    g_sigs[g_nsigs++] = (Sig){ "print",  T_VOID,   { T_STRING },                  1, 1 };
    g_sigs[g_nsigs++] = (Sig){ "input",  T_STRING, { 0 },                         0, 1 };
    g_sigs[g_nsigs++] = (Sig){ "str",    T_STRING, { T_INT },                     1, 1 };
    g_sigs[g_nsigs++] = (Sig){ "substr", T_STRING, { T_STRING, T_INT, T_INT },    3, 1 };
    g_sigs[g_nsigs++] = (Sig){ "find",   T_INT,    { T_STRING, T_STRING },        2, 1 };
}

/* ---------------------------------------------------- variable scoping */

/* can_mutate: may the variable's aggregate be mutated in place (push /
 * index-set)? Locals yes; parameters are immutable borrows (no). */
typedef struct { char *name; Type type; int can_mutate; } Var;
static Var g_vars[1024];
static int g_nvars = 0;

static int  vars_mark(void) { return g_nvars; }
static void vars_restore(int m) { g_nvars = m; }
static void vars_push(const char *name, Type t, int can_mutate) {
    if (g_nvars >= 1024) { fprintf(stderr, "hierc: too many variables\n"); exit(1); }
    g_vars[g_nvars].name = (char *)name;
    g_vars[g_nvars].type = t;
    g_vars[g_nvars].can_mutate = can_mutate;
    g_nvars++;
}
static int vars_find(const char *name, Type *out) {
    for (int i = g_nvars - 1; i >= 0; i--)
        if (!strcmp(g_vars[i].name, name)) { *out = g_vars[i].type; return 1; }
    return 0;
}
static int vars_can_mutate(const char *name) {
    for (int i = g_nvars - 1; i >= 0; i--)
        if (!strcmp(g_vars[i].name, name)) return g_vars[i].can_mutate;
    return 1;
}

/* --------------------------------------------------------- type resolve */

static int is_cmp(TokKind op) {
    return op == TK_EQEQ || op == TK_NEQ || op == TK_LT ||
           op == TK_GT   || op == TK_LE  || op == TK_GE;
}

static Type resolve_expr(Expr *e) {
    switch (e->kind) {
        case E_INT:  return e->type = T_INT;
        case E_BOOL: return e->type = T_BOOL;
        case E_STR:  return e->type = T_STRING;
        case E_IDENT: {
            Type t;
            if (!vars_find(e->sval, &t))
                die_at(e->line, "unknown variable '%s'", e->sval);
            return e->type = t;
        }
        case E_ARRLIT: {
            for (int i = 0; i < e->nargs; i++)
                if (resolve_expr(e->args[i]) != T_INT)
                    die_at(e->line, "array elements must be int");
            return e->type = T_ARRAY_INT;
        }
        case E_INDEX: {
            Type bt = resolve_expr(e->lhs);
            if (resolve_expr(e->rhs) != T_INT)
                die_at(e->line, "index must be int");
            if (bt != T_ARRAY_INT && bt != T_STRING)
                die_at(e->line, "can only index an [int] array or a string");
            return e->type = T_INT;   /* array element or string byte */
        }
        case E_FIELD: {
            Type bt = resolve_expr(e->lhs);
            if (!IS_STRUCT(bt))
                die_at(e->line, "'.%s' on a non-struct value", e->sval);
            StructDef *sd = &g_structs[STRUCT_ID(bt)];
            for (int i = 0; i < sd->nfields; i++)
                if (!strcmp(sd->fields[i].name, e->sval))
                    return e->type = sd->fields[i].type;
            die_at(e->line, "struct %s has no field '%s'", sd->name, e->sval);
        }
        case E_STRUCTLIT:   /* produced by resolving E_CALL; already typed */
            return e->type;
        case E_CALL: {
            /* a call whose name is a struct is positional construction */
            int sid = struct_find(e->sval);
            if (sid >= 0) {
                StructDef *sd = &g_structs[sid];
                if (e->nargs != sd->nfields)
                    die_at(e->line, "%s takes %d field value(s), got %d",
                           sd->name, sd->nfields, e->nargs);
                for (int i = 0; i < e->nargs; i++) {
                    Type at_ = resolve_expr(e->args[i]);
                    if (at_ != sd->fields[i].type)
                        die_at(e->line, "field '%s' of %s is %s, got %s",
                               sd->fields[i].name, sd->name,
                               type_name(sd->fields[i].type), type_name(at_));
                }
                e->kind = E_STRUCTLIT;          /* reinterpret for codegen */
                return e->type = STRUCT_TYPE(sid);
            }
            /* array builtins (don't fit the scalar Sig table) */
            if (!strcmp(e->sval, "len")) {
                if (e->nargs != 1) die_at(e->line, "len(...) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (at_ != T_ARRAY_INT && at_ != T_STRING)
                    die_at(e->line, "len(...) takes an [int] array or a string");
                return e->type = T_INT;
            }
            if (!strcmp(e->sval, "push")) {
                if (e->nargs != 2) die_at(e->line, "push(arr, value) takes two arguments");
                if (e->args[0]->kind != E_IDENT)
                    die_at(e->line, "push's first argument must be an array variable");
                if (resolve_expr(e->args[0]) != T_ARRAY_INT)
                    die_at(e->line, "push's first argument must be an [int] array");
                if (!vars_can_mutate(e->args[0]->sval))
                    die_at(e->line, "cannot mutate parameter '%s' (it is borrowed read-only; copy it with `y := %s` first)",
                           e->args[0]->sval, e->args[0]->sval);
                if (resolve_expr(e->args[1]) != T_INT)
                    die_at(e->line, "push's value must be int");
                return e->type = T_VOID;
            }
            Sig *s = sig_find(e->sval);
            if (!s) die_at(e->line, "unknown procedure '%s'", e->sval);
            if (e->nargs != s->nparams)
                die_at(e->line, "'%s' takes %d argument(s), got %d",
                       e->sval, s->nparams, e->nargs);
            for (int i = 0; i < e->nargs; i++) {
                Type at_ = resolve_expr(e->args[i]);
                if (at_ != s->params[i])
                    die_at(e->line, "argument %d of '%s' is %s, expected %s",
                           i + 1, e->sval, type_name(at_), type_name(s->params[i]));
            }
            if (s->ret == T_VOID && e->nargs >= 0) { /* allowed only as stmt; checked at codegen use */ }
            return e->type = s->ret;
        }
        case E_BINOP: {
            Type lt = resolve_expr(e->lhs);
            Type rt = resolve_expr(e->rhs);
            if (is_cmp(e->op)) {
                if (e->op == TK_EQEQ || e->op == TK_NEQ) {
                    if (lt != rt)
                        die_at(e->line, "cannot compare %s with %s", type_name(lt), type_name(rt));
                    if (lt == T_VOID) die_at(e->line, "cannot compare void");
                    if (lt == T_ARRAY_INT) die_at(e->line, "cannot compare arrays");
                    if (IS_STRUCT(lt)) die_at(e->line, "cannot compare structs");
                } else {
                    int ok = (lt == T_INT && rt == T_INT) ||
                             (lt == T_STRING && rt == T_STRING);
                    if (!ok)
                        die_at(e->line, "ordering compares two ints or two strings");
                }
                return e->type = T_BOOL;
            }
            if (e->op == TK_PLUS && lt == T_STRING) {
                if (rt != T_STRING)
                    die_at(e->line, "cannot concatenate string with %s", type_name(rt));
                return e->type = T_STRING;
            }
            if (lt != T_INT || rt != T_INT)
                die_at(e->line, "arithmetic requires ints (got %s, %s)",
                       type_name(lt), type_name(rt));
            return e->type = T_INT;
        }
    }
    return T_VOID;
}

static void resolve_block(Stmt **body, int n, Type ret);

static void resolve_stmt(Stmt *s, Type ret) {
    switch (s->kind) {
        case S_DECL: {
            Type t = resolve_expr(s->expr);
            if (t == T_VOID) die_at(s->line, "cannot bind a void value");
            if (s->typed_decl) {
                if (t != s->annot)
                    die_at(s->line, "declared type %s but value is %s",
                           type_name(s->annot), type_name(t));
                t = s->annot;
            }
            s->decl_type = t;
            vars_push(s->name, t, 1);
            break;
        }
        case S_ASSIGN: {
            Type vt;
            if (!vars_find(s->name, &vt))
                die_at(s->line, "assignment to unknown variable '%s'", s->name);
            Type t = resolve_expr(s->expr);
            if (t != vt)
                die_at(s->line, "cannot assign %s to '%s' of type %s",
                       type_name(t), s->name, type_name(vt));
            break;
        }
        case S_RETURN: {
            if (s->expr) {
                Type t = resolve_expr(s->expr);
                if (ret == T_VOID) die_at(s->line, "this proc returns nothing");
                if (t != ret)
                    die_at(s->line, "returning %s but proc returns %s",
                           type_name(t), type_name(ret));
            } else if (ret != T_VOID) {
                die_at(s->line, "missing return value (proc returns %s)", type_name(ret));
            }
            break;
        }
        case S_IF: {
            if (resolve_expr(s->expr) != T_BOOL)
                die_at(s->line, "if condition must be bool");
            resolve_block(s->body, s->nbody, ret);
            if (s->els) resolve_block(s->els, s->nels, ret);
            break;
        }
        case S_WHILE: {
            if (resolve_expr(s->expr) != T_BOOL)
                die_at(s->line, "for condition must be bool");
            resolve_block(s->body, s->nbody, ret);
            break;
        }
        case S_FORRANGE: {
            if (resolve_expr(s->r_start) != T_INT ||
                resolve_expr(s->r_stop)  != T_INT ||
                (s->r_step && resolve_expr(s->r_step) != T_INT))
                die_at(s->line, "range(...) arguments must be int");
            int m = vars_mark();
            vars_push(s->name, T_INT, 1);   /* loop variable is int, scoped to the loop */
            resolve_block(s->body, s->nbody, ret);
            vars_restore(m);
            break;
        }
        case S_INDEXSET: {
            if (s->target->lhs->kind != E_IDENT)
                die_at(s->line, "can only index-assign an array variable");
            if (!vars_can_mutate(s->target->lhs->sval))
                die_at(s->line, "cannot mutate parameter '%s' (it is borrowed read-only; copy it with `y := %s` first)",
                       s->target->lhs->sval, s->target->lhs->sval);
            if (resolve_expr(s->target->lhs) != T_ARRAY_INT)
                die_at(s->line, "can only index-assign an [int] array (strings are immutable)");
            Type tt = resolve_expr(s->target);    /* E_INDEX -> int, checks array */
            Type vt = resolve_expr(s->expr);
            if (tt != vt)
                die_at(s->line, "cannot assign %s to an int element", type_name(vt));
            break;
        }
        case S_FIELDSET: {
            /* the variable at the root of the field chain must be mutable */
            Expr *root = s->target;
            while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
            if (root->kind == E_IDENT && !vars_can_mutate(root->sval))
                die_at(s->line, "cannot mutate parameter '%s' (it is borrowed read-only)", root->sval);
            Type tt = resolve_expr(s->target);   /* E_FIELD -> field type */
            Type vt = resolve_expr(s->expr);
            if (tt != vt)
                die_at(s->line, "cannot assign %s to a %s field", type_name(vt), type_name(tt));
            break;
        }
        case S_EXPR:
            resolve_expr(s->expr);
            break;
    }
}

static void resolve_block(Stmt **body, int n, Type ret) {
    int m = vars_mark();
    for (int i = 0; i < n; i++) resolve_stmt(body[i], ret);
    vars_restore(m);
}

static void resolve_program(ProcVec *prog) {
    register_builtins();
    /* register every user proc up front so calls can be forward refs */
    for (int i = 0; i < prog->n; i++) {
        Proc *pr = prog->v[i];
        if (sig_find(pr->name))
            die_at(pr->line, "'%s' is already defined", pr->name);
        Sig s; memset(&s, 0, sizeof s);
        s.name = pr->name; s.ret = pr->ret; s.nparams = pr->nparams; s.builtin = 0;
        if (pr->nparams > 8) die_at(pr->line, "too many parameters (max 8)");
        for (int j = 0; j < pr->nparams; j++) s.params[j] = pr->params[j].type;
        g_sigs[g_nsigs++] = s;
    }
    Sig *m = sig_find("main");
    if (!m) { fprintf(stderr, "%s: error: no 'main' procedure\n", g_srcname); exit(1); }
    for (int i = 0; i < prog->n; i++) {
        Proc *pr = prog->v[i];
        g_nvars = 0;
        /* arrays are passed as read-only borrows; value params (int/bool/
         * string/pure-struct) are copies, so they are mutable locals */
        for (int j = 0; j < pr->nparams; j++)
            vars_push(pr->params[j].name, pr->params[j].type,
                      pr->params[j].type != T_ARRAY_INT);
        if (!strcmp(pr->name, "main") && (pr->nparams != 0 || pr->ret != T_VOID))
            die_at(pr->line, "'main' must be 'fn main():' with no return");
        resolve_block(pr->body, pr->nbody, pr->ret);
    }
}

/* ------------------------------------------------------------- codegen */
/* gen_expr returns a freshly allocated C expression string. `arena` is
 * the name of the arena into which any allocation produced by this
 * expression should go (so return values land in the caller's arena). */

static char *gen_expr(Expr *e, const char *arena);

static int g_blk = 0;   /* unique-name counter for block subarenas / literals */

/* During codegen we track which arena owns each live variable's storage,
 * so an assignment (or array push) can allocate in the *variable's* arena
 * rather than the current (possibly inner, soon-to-collapse) one. This is
 * what keeps the implicit model sound: a value never outlives its arena. */
typedef struct { const char *name; const char *arena; } CVar;
static CVar g_cv[1024];
static int  g_ncv = 0;
static int  cv_mark(void) { return g_ncv; }
static void cv_restore(int m) { g_ncv = m; }
static void cv_push(const char *name, const char *arena) {
    if (g_ncv >= 1024) { fprintf(stderr, "hierc: too many variables\n"); exit(1); }
    g_cv[g_ncv].name = name; g_cv[g_ncv].arena = arena; g_ncv++;
}
static const char *cv_arena(const char *name) {
    for (int i = g_ncv - 1; i >= 0; i--)
        if (!strcmp(g_cv[i].name, name)) return g_cv[i].arena;
    return NULL;
}

static char *gen_call(Expr *e, const char *arena) {
    if (!strcmp(e->sval, "len")) {
        char *a = gen_expr(e->args[0], arena);
        if (e->args[0]->type == T_STRING)
            return sfmt("hier_str_len(%s)", a);
        return sfmt("((%s).len)", a);
    }
    if (!strcmp(e->sval, "substr")) {
        char *s = gen_expr(e->args[0], arena);
        char *a = gen_expr(e->args[1], arena);
        char *b = gen_expr(e->args[2], arena);
        return sfmt("hier_str_substr(%s, %s, %s, %s)", arena, s, a, b);
    }
    if (!strcmp(e->sval, "find")) {
        char *s   = gen_expr(e->args[0], arena);
        char *sub = gen_expr(e->args[1], arena);
        return sfmt("hier_str_find(%s, %s)", s, sub);
    }
    if (!strcmp(e->sval, "push")) {
        /* grow the array in *its owning arena*, not the current one */
        const char *nm = e->args[0]->sval;       /* checked E_IDENT in resolve */
        const char *owner = cv_arena(nm);
        if (!owner) owner = arena;
        char *v = gen_expr(e->args[1], arena);
        return sfmt("hier_arr_int_push(%s, &h_%s, %s)", owner, nm, v);
    }
    if (!strcmp(e->sval, "print")) {
        char *a = gen_expr(e->args[0], arena);
        return sfmt("hier_print(%s)", a);
    }
    if (!strcmp(e->sval, "input")) {
        return sfmt("hier_input(%s)", arena);
    }
    if (!strcmp(e->sval, "str")) {
        char *a = gen_expr(e->args[0], arena);
        return sfmt("hier_int_to_str(%s, %s)", arena, a);
    }
    /* user proc: first arg is the destination arena for its return */
    char *out = sfmt("h_%s(%s", e->sval, arena);
    for (int i = 0; i < e->nargs; i++) {
        char *a = gen_expr(e->args[i], arena);
        char *tmp = sfmt("%s, %s", out, a);
        out = tmp;
    }
    return sfmt("%s)", out);
}

static const char *op_str(TokKind op) {
    switch (op) {
        case TK_PLUS:  return "+";
        case TK_MINUS: return "-";
        case TK_STAR:  return "*";
        case TK_SLASH: return "/";
        case TK_LT:    return "<";
        case TK_GT:    return ">";
        case TK_LE:    return "<=";
        case TK_GE:    return ">=";
        case TK_EQEQ:  return "==";
        case TK_NEQ:   return "!=";
        default:       return "?";
    }
}

static char *gen_expr(Expr *e, const char *arena) {
    switch (e->kind) {
        case E_INT:  return sfmt("%ldL", e->ival);
        case E_BOOL: return sfmt("%ld", e->ival);
        case E_STR:  return sfmt("\"%s\"", e->sval);
        case E_IDENT:return sfmt("h_%s", e->sval);
        case E_CALL: return gen_call(e, arena);
        case E_INDEX: {
            char *a = gen_expr(e->lhs, arena);
            char *ix = gen_expr(e->rhs, arena);
            if (e->lhs->type == T_STRING)
                return sfmt("hier_str_get(%s, %s)", a, ix);
            return sfmt("hier_arr_int_get(%s, %s)", a, ix);
        }
        case E_ARRLIT: {
            /* GNU statement-expression so a literal is a single value */
            int id = g_blk++;
            char *out = sfmt("({ HierArrInt _l%d = hier_arr_int_with_cap(%s, %dL);",
                             id, arena, e->nargs);
            for (int i = 0; i < e->nargs; i++)
                out = sfmt("%s _l%d.data[%d] = %s;", out, id, i, gen_expr(e->args[i], arena));
            return sfmt("%s _l%d.len = %dL; _l%d; })", out, id, e->nargs, id);
        }
        case E_FIELD: {
            char *b = gen_expr(e->lhs, arena);
            return sfmt("((%s).f_%s)", b, e->sval);
        }
        case E_STRUCTLIT: {
            /* positional C99 compound literal; pure value, no arena needed */
            const char *nm = g_structs[STRUCT_ID(e->type)].name;
            char *out = sfmt("((S_%s){ ", nm);
            for (int i = 0; i < e->nargs; i++)
                out = sfmt("%s%s%s", out, gen_expr(e->args[i], arena), i + 1 < e->nargs ? ", " : "");
            return sfmt("%s })", out);
        }
        case E_BINOP: {
            char *l = gen_expr(e->lhs, arena);
            char *r = gen_expr(e->rhs, arena);
            if (e->op == TK_PLUS && e->lhs->type == T_STRING)
                return sfmt("hier_str_concat(%s, %s, %s)", arena, l, r);
            /* every comparison on strings goes through strcmp (==/!= and ordering) */
            if (is_cmp(e->op) && e->lhs->type == T_STRING)
                return sfmt("(strcmp(%s, %s) %s 0)", l, r, op_str(e->op));
            return sfmt("(%s %s %s)", l, op_str(e->op), r);
        }
    }
    return sfmt("0");
}

static void indent(FILE *o, int n) { for (int i = 0; i < n; i++) fputs("    ", o); }

/* `scope` = arena for local allocations; `parent` = arena for returns. */
static void gen_block(FILE *o, Stmt **body, int n, int ind,
                      const char *scope, Type ret);

static int block_ends_in_return(Stmt **body, int n) {
    return n > 0 && body[n - 1]->kind == S_RETURN;
}

/* `scope` is a C expression of type Arena* into which local allocations
 * go. Returns always promote/collapse the proc's own arena, named
 * "_scope" in every generated proc body. */
static void gen_stmt(FILE *o, Stmt *s, int ind, const char *scope, Type ret) {
    switch (s->kind) {
        case S_DECL: {
            char *v = gen_expr(s->expr, scope);
            /* value semantics: binding a plain array *variable* aliases its
             * buffer, so deep-copy. A literal or call result is already a
             * freshly-owned value — no copy needed. */
            if (s->decl_type == T_ARRAY_INT && s->expr->kind == E_IDENT)
                v = sfmt("hier_arr_int_copy(%s, %s)", scope, v);
            indent(o, ind);
            fprintf(o, "%sh_%s = %s;\n", c_type(s->decl_type), s->name, v);
            cv_push(s->name, scope);   /* this variable lives in `scope` */
            break;
        }
        case S_ASSIGN: {
            /* allocate the value where the variable lives, not where we
             * currently are, so it survives any inner scope collapsing */
            const char *owner = cv_arena(s->name);
            if (!owner) owner = scope;
            char *v = gen_expr(s->expr, owner);
            /* a bare aggregate variable is only an alias into some (possibly
             * inner, soon-to-collapse) scope; deep-copy it into the target's
             * arena so it survives. A literal/call/concat result is already
             * freshly allocated in `owner` — no copy needed. */
            if (s->expr->kind == E_IDENT) {
                if (s->expr->type == T_ARRAY_INT)
                    v = sfmt("hier_arr_int_copy(%s, %s)", owner, v);
                else if (s->expr->type == T_STRING)
                    v = sfmt("hier_str_copy(%s, %s)", owner, v);
            }
            indent(o, ind);
            fprintf(o, "h_%s = %s;\n", s->name, v);
            break;
        }
        case S_INDEXSET: {
            const char *nm = s->target->lhs->sval;   /* array variable */
            char *ix = gen_expr(s->target->rhs, scope);
            char *v  = gen_expr(s->expr, scope);
            indent(o, ind);
            fprintf(o, "hier_arr_int_set(&h_%s, %s, %s);\n", nm, ix, v);
            break;
        }
        case S_FIELDSET: {
            /* gen_expr(E_FIELD) is a valid C lvalue, e.g. (h_p).f_x */
            char *lv = gen_expr(s->target, scope);
            char *v  = gen_expr(s->expr, scope);
            indent(o, ind);
            fprintf(o, "%s = %s;\n", lv, v);
            break;
        }
        case S_EXPR: {
            char *v = gen_expr(s->expr, scope);
            indent(o, ind);
            fprintf(o, "%s;\n", v);
            break;
        }
        case S_RETURN: {
            if (!s->expr) {
                indent(o, ind); fprintf(o, "{ arena_free(&_scope); return; }\n");
            } else if (ret == T_STRING) {
                /* promote up. A fresh value (literal/call/concat) is built
                 * directly in the caller's arena; a bare string variable is
                 * only a pointer into this scope, so deep-copy it into the
                 * caller's arena before freeing this scope. */
                if (s->expr->kind == E_IDENT) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ char *_ret = hier_str_copy(_parent, %s); arena_free(&_scope); return _ret; }\n", v);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ char *_ret = %s; arena_free(&_scope); return _ret; }\n", v);
                }
            } else if (ret == T_ARRAY_INT) {
                /* promote up. A fresh value (literal/call) is built directly
                 * in the caller's arena; a borrowed/local variable is
                 * deep-copied into it. Either way nothing dangles. */
                if (s->expr->kind == E_IDENT) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ HierArrInt _ret = hier_arr_int_copy(_parent, %s); arena_free(&_scope); return _ret; }\n", v);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ HierArrInt _ret = %s; arena_free(&_scope); return _ret; }\n", v);
                }
            } else {
                /* int/bool, or a pure-value struct: a value, nothing on the
                 * heap to keep alive — copy it out and free the scope */
                char *v = gen_expr(s->expr, "&_scope");
                indent(o, ind); fprintf(o, "{ %s_ret = %s; arena_free(&_scope); return _ret; }\n",
                                        c_type(ret), v);
            }
            break;
        }
        case S_IF: {
            int id = g_blk++;
            char *c = gen_expr(s->expr, scope);
            indent(o, ind); fprintf(o, "if (%s) {\n", c);
            indent(o, ind + 1); fprintf(o, "Arena _b%d = arena_child(%s);\n", id, scope);
            char *bs = sfmt("&_b%d", id);
            gen_block(o, s->body, s->nbody, ind + 1, bs, ret);
            indent(o, ind + 1); fprintf(o, "arena_free(&_b%d);\n", id);
            indent(o, ind); fprintf(o, "}");
            if (s->els) {
                int eid = g_blk++;
                fprintf(o, " else {\n");
                indent(o, ind + 1); fprintf(o, "Arena _b%d = arena_child(%s);\n", eid, scope);
                char *es = sfmt("&_b%d", eid);
                gen_block(o, s->els, s->nels, ind + 1, es, ret);
                indent(o, ind + 1); fprintf(o, "arena_free(&_b%d);\n", eid);
                indent(o, ind); fprintf(o, "}\n");
            } else {
                fprintf(o, "\n");
            }
            break;
        }
        case S_WHILE: {
            int id = g_blk++;
            indent(o, ind); fprintf(o, "{\n");
            indent(o, ind + 1); fprintf(o, "Arena _scr%d = arena_child(%s);\n", id, scope);
            char *c = gen_expr(s->expr, scope);
            indent(o, ind + 1); fprintf(o, "while (%s) {\n", c);
            indent(o, ind + 2); fprintf(o, "arena_reset(&_scr%d);\n", id);
            char *ss = sfmt("&_scr%d", id);
            gen_block(o, s->body, s->nbody, ind + 2, ss, ret);
            indent(o, ind + 1); fprintf(o, "}\n");
            indent(o, ind + 1); fprintf(o, "arena_free(&_scr%d);\n", id);
            indent(o, ind); fprintf(o, "}\n");
            break;
        }
        case S_FORRANGE: {
            int id = g_blk++;
            char *start = gen_expr(s->r_start, scope);
            char *stop  = gen_expr(s->r_stop,  scope);
            char *step  = s->r_step ? gen_expr(s->r_step, scope) : sfmt("1L");
            char *ss = sfmt("&_scr%d", id);
            indent(o, ind); fprintf(o, "{\n");
            indent(o, ind + 1); fprintf(o, "Arena _scr%d = arena_child(%s);\n", id, scope);
            indent(o, ind + 1); fprintf(o, "long _stop%d = %s, _step%d = %s;\n", id, stop, id, step);
            indent(o, ind + 1);
            fprintf(o, "for (long h_%s = %s; _step%d > 0 ? h_%s < _stop%d : h_%s > _stop%d; h_%s += _step%d) {\n",
                    s->name, start, id, s->name, id, s->name, id, s->name, id);
            indent(o, ind + 2); fprintf(o, "arena_reset(&_scr%d);\n", id);
            int m = cv_mark();
            cv_push(s->name, ss);   /* loop var is an int value; owner is irrelevant but tracked */
            gen_block(o, s->body, s->nbody, ind + 2, ss, ret);
            cv_restore(m);
            indent(o, ind + 1); fprintf(o, "}\n");
            indent(o, ind + 1); fprintf(o, "arena_free(&_scr%d);\n", id);
            indent(o, ind); fprintf(o, "}\n");
            break;
        }
    }
}

static void gen_block(FILE *o, Stmt **body, int n, int ind,
                      const char *scope, Type ret) {
    int m = cv_mark();
    for (int i = 0; i < n; i++) gen_stmt(o, body[i], ind, scope, ret);
    cv_restore(m);   /* variables declared in this block go out of scope */
}

static void gen_signature(FILE *o, Proc *pr) {
    fprintf(o, "%sh_%s(Arena *_parent", c_type(pr->ret), pr->name);
    for (int i = 0; i < pr->nparams; i++)
        fprintf(o, ", %sh_%s", c_type(pr->params[i].type), pr->params[i].name);
    fprintf(o, ")");
}

static void gen_proto(FILE *o, Proc *pr) { gen_signature(o, pr); fprintf(o, ";\n"); }

static void gen_proc(FILE *o, Proc *pr) {
    gen_signature(o, pr);
    fprintf(o, " {\n");
    indent(o, 1); fprintf(o, "Arena _scope = arena_child(_parent);\n");
    g_ncv = 0;
    /* a reassigned param must land in this proc's scope to outlive any
     * inner block; the incoming pointer itself is borrowed from the caller */
    for (int i = 0; i < pr->nparams; i++) cv_push(pr->params[i].name, "&_scope");
    gen_block(o, pr->body, pr->nbody, 1, "&_scope", pr->ret);
    if (!block_ends_in_return(pr->body, pr->nbody)) {
        if (pr->ret == T_VOID) {
            indent(o, 1); fprintf(o, "arena_free(&_scope);\n");
        } else {
            /* defensive: a well-typed proc always returns on every path,
             * but keep cc quiet for ones that fall through */
            indent(o, 1); fprintf(o, "arena_free(&_scope); return (%s)0;\n", c_type(pr->ret));
        }
    }
    fprintf(o, "}\n\n");
}

static void gen_program(FILE *o, ProcVec *prog) {
    fputs(HIER_RUNTIME, o);
    fputs("\n/* ---- generated from Hier source ---- */\n\n", o);
    /* struct typedefs, in declaration order (define-before-use guarantees a
     * struct field's type is already typedef'd above it) */
    for (int i = 0; i < g_nstructs; i++) {
        StructDef *sd = &g_structs[i];
        fprintf(o, "typedef struct {\n");
        for (int j = 0; j < sd->nfields; j++)
            fprintf(o, "    %sf_%s;\n", c_type(sd->fields[j].type), sd->fields[j].name);
        fprintf(o, "} S_%s;\n\n", sd->name);
    }
    for (int i = 0; i < prog->n; i++) gen_proto(o, prog->v[i]);
    fputs("\n", o);
    for (int i = 0; i < prog->n; i++) gen_proc(o, prog->v[i]);
    fputs("int main(void) {\n", o);
    fputs("    Arena _root = arena_new(0);  /* root arena; default block size */\n", o);
    fputs("    h_main(&_root);\n", o);
    fputs("    arena_free(&_root);\n", o);
    fputs("    return 0;\n}\n", o);
}

/* ---------------------------------------------------------------- main */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "hierc: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)xmalloc((size_t)sz + 1);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fprintf(stderr, "hierc: read error\n"); exit(1); }
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static char *strip_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (dot && (!slash || dot > slash)) return xstrndup(path, (size_t)(dot - path));
    return xstrndup(path, strlen(path));
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *out   = NULL;
    const char *cc    = "cc";
    int emit_c_only = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--emit-c")) emit_c_only = 1;
        else if (!strcmp(argv[i], "--cc") && i + 1 < argc) cc = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "hierc: unknown flag %s\n", argv[i]); return 1; }
        else input = argv[i];
    }
    if (!input) {
        fprintf(stderr, "usage: hierc file.hi [-o name] [--emit-c] [--cc <compiler>]\n");
        return 1;
    }
    g_srcname = input;

    char *base   = out ? xstrndup(out, strlen(out)) : strip_ext(input);
    char *c_path = sfmt("%s.c", base);

    char *src = read_file(input);
    TokVec toks = lex(src);
    ProcVec prog = parse_program(toks.v);
    resolve_program(&prog);

    FILE *o = fopen(c_path, "wb");
    if (!o) { fprintf(stderr, "hierc: cannot write %s\n", c_path); return 1; }
    gen_program(o, &prog);
    fclose(o);

    if (emit_c_only) {
        printf("wrote %s\n", c_path);
        return 0;
    }

    char *cmd = sfmt("%s -O2 -o %s %s", cc, base, c_path);
    int rc = system(cmd);
    if (rc != 0) { fprintf(stderr, "hierc: C compilation failed (%s)\n", cmd); return 1; }
    printf("built %s\n", base);
    return 0;
}
