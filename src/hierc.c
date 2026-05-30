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
    TK_INOUT, TK_AMP,
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
    if (!strcmp(s, "inout"))  return TK_INOUT;
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
            else if (c == '&') k = TK_AMP;
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
enum { T_VOID, T_INT, T_BOOL, T_STRING, T_ARRAY_INT, T_ARRAY_STRING, T_MAP_SI };
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

/* A "heap" type owns arena-allocated bytes outside its own value word(s):
 * string (char* into an arena), [int]/[string] (a buffer), or any struct
 * that (transitively) contains such a field. int/bool and pure structs are
 * not heap: copying the value word is a complete copy. This is what decides
 * whether a move (decl/assign/return/field-set/construction) must deep-copy
 * to keep the implicit-arena model sound. Structs are defined before use, so
 * a field's struct type is fully known here — no cycles, recursion ends. */
static int type_is_heap(Type t) {
    if (t == T_STRING || t == T_ARRAY_INT || t == T_ARRAY_STRING || t == T_MAP_SI) return 1;
    if (IS_STRUCT(t)) {
        StructDef *sd = &g_structs[STRUCT_ID(t)];
        for (int i = 0; i < sd->nfields; i++)
            if (type_is_heap(sd->fields[i].type)) return 1;
    }
    return 0;
}

/* trailing space so "%sh_name" / "%s_ret" / signatures all read right;
 * "char *" needs none because "char *h_name" is already valid */
static const char *c_type(Type t) {
    if (IS_STRUCT(t)) return sfmt("S_%s ", g_structs[STRUCT_ID(t)].name);
    switch (t) {
        case T_INT:          return "long ";
        case T_BOOL:         return "int ";
        case T_STRING:       return "char *";
        case T_ARRAY_INT:    return "HierArrInt ";
        case T_ARRAY_STRING: return "HierArrStr ";
        case T_MAP_SI:       return "HierMapSI ";
        default:             return "void ";
    }
}
static const char *type_name(Type t) {
    if (IS_STRUCT(t)) return g_structs[STRUCT_ID(t)].name;
    switch (t) {
        case T_INT:          return "int";
        case T_BOOL:         return "bool";
        case T_STRING:       return "string";
        case T_ARRAY_INT:    return "[int]";
        case T_ARRAY_STRING: return "[string]";
        case T_MAP_SI:       return "[string: int]";
        default:             return "void";
    }
}

typedef enum { E_INT, E_STR, E_BOOL, E_IDENT, E_BINOP, E_CALL, E_ARRLIT, E_INDEX,
               E_STRUCTLIT, E_FIELD, E_ADDR } ExprKind;

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

typedef struct { char *name; Type type; int is_inout; } Param;

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
    if (t->kind == TK_LBRACKET) {        /* [int] / [string] / [string: int] */
        ps->p++;
        Type elem = parse_type(ps);
        if (at(ps, TK_COLON)) {          /* map type: [K: V] */
            ps->p++;
            Type val = parse_type(ps);
            eat(ps, TK_RBRACKET, "']'");
            if (elem == T_STRING && val == T_INT) return T_MAP_SI;
            die_at(t->line, "only [string: int] maps are supported");
        }
        eat(ps, TK_RBRACKET, "']'");
        if (elem == T_INT)    return T_ARRAY_INT;
        if (elem == T_STRING) return T_ARRAY_STRING;
        die_at(t->line, "only [int] and [string] arrays are supported");
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
    if (t->kind == TK_LBRACKET) {            /* array or map literal */
        ps->p++;
        Expr *e = new_expr(E_ARRLIT, t->line);
        if (at(ps, TK_RBRACKET)) {           /* empty: []int / []string / []string: int */
            ps->p++;
            Type elem = parse_type(ps);
            if (at(ps, TK_COLON)) {          /* empty map literal []K: V */
                ps->p++;
                Type val = parse_type(ps);
                if (elem == T_STRING && val == T_INT) { e->ival = T_MAP_SI; e->op = TK_COLON; }
                else die_at(t->line, "only [string: int] maps are supported");
                return e;
            }
            if (elem == T_INT)         e->ival = T_ARRAY_INT;      /* type carried */
            else if (elem == T_STRING) e->ival = T_ARRAY_STRING;   /* to resolver */
            else die_at(t->line, "only [int] and [string] arrays are supported");
            return e;
        }
        int cap = 0;
        /* first element decides array vs map: a `key: value` pair => map. The
         * map literal interleaves args as k0,v0,k1,v1,... and is flagged with
         * op == TK_COLON (E_ARRLIT otherwise leaves op unset). */
        Expr *first = parse_expr(ps);
        if (at(ps, TK_COLON)) {              /* map literal ["k": v, ...] */
            e->op = TK_COLON;
            ps->p++;
            cap = 4; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *));
            e->args[e->nargs++] = first;             /* key0 */
            e->args[e->nargs++] = parse_expr(ps);    /* val0 */
            while (accept(ps, TK_COMMA)) {
                if (at(ps, TK_RBRACKET)) break;       /* trailing comma */
                if (e->nargs + 2 > cap) { cap *= 2; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *)); }
                e->args[e->nargs++] = parse_expr(ps);   /* key */
                eat(ps, TK_COLON, "':' in a map literal entry");
                e->args[e->nargs++] = parse_expr(ps);   /* value */
            }
            eat(ps, TK_RBRACKET, "']'");
            return e;
        }
        cap = 4; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *));
        e->args[e->nargs++] = first;
        while (accept(ps, TK_COMMA)) {
            if (at(ps, TK_RBRACKET)) break;          /* trailing comma */
            if (e->nargs == cap) { cap = cap * 2; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *)); }
            e->args[e->nargs++] = parse_expr(ps);
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
    if (at(ps, TK_AMP)) {                  /* &lvalue — an inout argument */
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_ADDR, t->line);
        e->lhs = parse_unary(ps);
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
        int is_inout = accept(ps, TK_INOUT);   /* `name: inout type` */
        Type pt = parse_type(ps);
        if (pr->nparams == cap) { cap = cap ? cap * 2 : 4; pr->params = (Param *)realloc(pr->params, (size_t)cap * sizeof(Param)); }
        pr->params[pr->nparams].name = pn->text;
        pr->params[pr->nparams].type = pt;
        pr->params[pr->nparams].is_inout = is_inout;
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
        Type ft = parse_type(ps);   /* int, bool, string, [int], [string], or struct */
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
    int         inout[8];   /* per-param: is it an inout (by-pointer) param? */
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
    /* designated initializers: robust to field order (inout[] sits between
     * params and nparams). All builtins are by-value (no inout). */
    g_sigs[g_nsigs++] = (Sig){ .name="print",  .ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="input",  .ret=T_STRING,       .params={ 0 },                       .nparams=0, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="str",    .ret=T_STRING,       .params={ T_INT },                   .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="substr", .ret=T_STRING,       .params={ T_STRING, T_INT, T_INT },  .nparams=3, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="find",   .ret=T_INT,          .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="split",  .ret=T_ARRAY_STRING, .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
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
            if (e->op == TK_COLON) {           /* map literal [k: v, ...] */
                /* args interleave k0,v0,k1,v1,...; keys string, values int */
                for (int i = 0; i < e->nargs; i += 2) {
                    if (resolve_expr(e->args[i]) != T_STRING)
                        die_at(e->line, "map keys must be string");
                    if (resolve_expr(e->args[i + 1]) != T_INT)
                        die_at(e->line, "map values must be int (only [string: int] maps exist)");
                }
                return e->type = T_MAP_SI;
            }
            if (e->nargs == 0)                 /* empty literal: type from []T / []K: V */
                return e->type = (Type)e->ival;
            Type elem = resolve_expr(e->args[0]);
            if (elem != T_INT && elem != T_STRING)
                die_at(e->line, "array elements must be int or string");
            for (int i = 1; i < e->nargs; i++)
                if (resolve_expr(e->args[i]) != elem)
                    die_at(e->line, "array elements must all have the same type");
            return e->type = (elem == T_STRING ? T_ARRAY_STRING : T_ARRAY_INT);
        }
        case E_INDEX: {
            Type bt = resolve_expr(e->lhs);
            if (resolve_expr(e->rhs) != T_INT)
                die_at(e->line, "index must be int");
            if (bt == T_ARRAY_STRING) return e->type = T_STRING;  /* element */
            if (bt != T_ARRAY_INT && bt != T_STRING)
                die_at(e->line, "can only index an [int]/[string] array or a string");
            return e->type = T_INT;   /* [int] element or string byte */
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
        case E_ADDR:   /* &place; only valid as an inout argument (checked at
                        * the call site). Its type is the underlying place's. */
            return e->type = resolve_expr(e->lhs);
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
                if (at_ != T_ARRAY_INT && at_ != T_ARRAY_STRING && at_ != T_STRING && at_ != T_MAP_SI)
                    die_at(e->line, "len(...) takes an array, a string, or a map");
                return e->type = T_INT;
            }
            /* map builtins ([string: int]). map_set is pure (returns a new
             * map); the m = map_set(m, ...) self-rebind is grown in place by
             * the accumulator pass, exactly like array push / string append. */
            if (!strcmp(e->sval, "map_set")) {
                if (e->nargs != 3) die_at(e->line, "map_set(m, key, value) takes three arguments");
                if (resolve_expr(e->args[0]) != T_MAP_SI)
                    die_at(e->line, "map_set's first argument must be a [string: int] map");
                if (resolve_expr(e->args[1]) != T_STRING) die_at(e->line, "map_set key must be string");
                if (resolve_expr(e->args[2]) != T_INT)    die_at(e->line, "map_set value must be int");
                return e->type = T_MAP_SI;
            }
            if (!strcmp(e->sval, "map_get")) {
                if (e->nargs != 3) die_at(e->line, "map_get(m, key, default) takes three arguments");
                if (resolve_expr(e->args[0]) != T_MAP_SI)
                    die_at(e->line, "map_get's first argument must be a [string: int] map");
                if (resolve_expr(e->args[1]) != T_STRING) die_at(e->line, "map_get key must be string");
                if (resolve_expr(e->args[2]) != T_INT)    die_at(e->line, "map_get default must be int");
                return e->type = T_INT;
            }
            if (!strcmp(e->sval, "map_has")) {
                if (e->nargs != 2) die_at(e->line, "map_has(m, key) takes two arguments");
                if (resolve_expr(e->args[0]) != T_MAP_SI)
                    die_at(e->line, "map_has's first argument must be a [string: int] map");
                if (resolve_expr(e->args[1]) != T_STRING) die_at(e->line, "map_has key must be string");
                return e->type = T_BOOL;
            }
            /* map_del is pure (returns a new map); the m = map_del(m, k)
             * self-rebind is rewritten to an in-place tombstone delete by the
             * accumulator pass, exactly like map_set's in-place put. */
            if (!strcmp(e->sval, "map_del")) {
                if (e->nargs != 2) die_at(e->line, "map_del(m, key) takes two arguments");
                if (resolve_expr(e->args[0]) != T_MAP_SI)
                    die_at(e->line, "map_del's first argument must be a [string: int] map");
                if (resolve_expr(e->args[1]) != T_STRING) die_at(e->line, "map_del key must be string");
                return e->type = T_MAP_SI;
            }
            /* keys(m) -> [string]: the map's live keys, for iteration. */
            if (!strcmp(e->sval, "keys")) {
                if (e->nargs != 1) die_at(e->line, "keys(m) takes one argument");
                if (resolve_expr(e->args[0]) != T_MAP_SI)
                    die_at(e->line, "keys's argument must be a [string: int] map");
                return e->type = T_ARRAY_STRING;
            }
            if (!strcmp(e->sval, "push")) {
                if (e->nargs != 2) die_at(e->line, "push(arr, value) takes two arguments");
                /* target may be an array variable or a struct's array field
                 * (e.g. push(p.tags, x)); the root variable must be mutable.
                 * Struct params are deep-copied on entry, so their array
                 * fields are owned locally and safe to grow. */
                Expr *root = e->args[0];
                while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
                if (root->kind != E_IDENT)
                    die_at(e->line, "push's first argument must be an array variable or field");
                Type arrt = resolve_expr(e->args[0]);
                if (arrt != T_ARRAY_INT && arrt != T_ARRAY_STRING)
                    die_at(e->line, "push's first argument must be an [int] or [string] array");
                /* push through a heap inout array is allowed: the regrow
                 * targets the value's owning arena (carried as _ina_<name>),
                 * so the new buffer outlives the call and the caller sees the
                 * updated descriptor. */
                if (!vars_can_mutate(root->sval))
                    die_at(e->line, "cannot mutate parameter '%s' (it is borrowed read-only; copy it with `y := %s` first)",
                           root->sval, root->sval);
                Type want = (arrt == T_ARRAY_STRING) ? T_STRING : T_INT;
                if (resolve_expr(e->args[1]) != want)
                    die_at(e->line, "push's value must be %s", type_name(want));
                return e->type = T_VOID;
            }
            Sig *s = sig_find(e->sval);
            if (!s) die_at(e->line, "unknown procedure '%s'", e->sval);
            if (e->nargs != s->nparams)
                die_at(e->line, "'%s' takes %d argument(s), got %d",
                       e->sval, s->nparams, e->nargs);
            for (int i = 0; i < e->nargs; i++) {
                Type at_ = resolve_expr(e->args[i]);
                /* inout parameter: the argument must be `&place` naming a
                 * mutable variable (an lvalue we can write back through). A
                 * by-value param rejects `&`. */
                if (s->inout[i]) {
                    if (e->args[i]->kind != E_ADDR)
                        die_at(e->line, "argument %d of '%s' is inout; pass it as '&variable'",
                               i + 1, e->sval);
                    Expr *tgt = e->args[i]->lhs;
                    Expr *root = tgt;
                    while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
                    if (root->kind != E_IDENT)
                        die_at(e->line, "inout argument %d of '%s' must name a variable", i + 1, e->sval);
                    if (!vars_can_mutate(root->sval))
                        die_at(e->line, "cannot pass borrowed parameter '%s' as inout (it is read-only; copy it first)", root->sval);
                } else if (e->args[i]->kind == E_ADDR) {
                    die_at(e->line, "argument %d of '%s' is not inout; remove the '&'", i + 1, e->sval);
                }
                if (at_ != s->params[i])
                    die_at(e->line, "argument %d of '%s' is %s, expected %s",
                           i + 1, e->sval, type_name(at_), type_name(s->params[i]));
            }
            /* exclusivity (Law of Exclusivity): the same variable may not be
             * passed to two inout params of one call — that would be two
             * overlapping writes, breaking the x = f(x) value-semantics model.
             * No globals/closures exist in Hier, so this is the only alias. */
            for (int i = 0; i < e->nargs; i++) {
                if (!s->inout[i]) continue;
                Expr *ri = e->args[i]->lhs;
                while (ri->kind == E_FIELD || ri->kind == E_INDEX) ri = ri->lhs;
                for (int j = i + 1; j < e->nargs; j++) {
                    if (!s->inout[j]) continue;
                    Expr *rj = e->args[j]->lhs;
                    while (rj->kind == E_FIELD || rj->kind == E_INDEX) rj = rj->lhs;
                    if (ri->kind == E_IDENT && rj->kind == E_IDENT && !strcmp(ri->sval, rj->sval))
                        die_at(e->line, "variable '%s' passed to two inout parameters of '%s' (overlapping mutable access)", ri->sval, e->sval);
                }
            }
            return e->type = s->ret;
        }
        case E_BINOP: {
            Type lt = resolve_expr(e->lhs);
            Type rt = resolve_expr(e->rhs);
            if (is_cmp(e->op)) {
                if (e->op == TK_EQEQ || e->op == TK_NEQ) {
                    /* equality is structural for every type (value semantics):
                     * ints/bools directly, strings/arrays/structs by content,
                     * recursing through nesting. Only void is incomparable. */
                    if (lt != rt)
                        die_at(e->line, "cannot compare %s with %s", type_name(lt), type_name(rt));
                    if (lt == T_VOID) die_at(e->line, "cannot compare void");
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
            /* lhs is the array being indexed: a variable or a struct's array
             * field (e.g. p.tags[0] = v). The root variable must be mutable. */
            Expr *root = s->target->lhs;
            while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
            if (root->kind != E_IDENT)
                die_at(s->line, "can only index-assign an array variable or field");
            if (!vars_can_mutate(root->sval))
                die_at(s->line, "cannot mutate parameter '%s' (it is borrowed read-only; copy it with `y := %s` first)",
                       root->sval, root->sval);
            Type arrt = resolve_expr(s->target->lhs);
            if (arrt != T_ARRAY_INT && arrt != T_ARRAY_STRING)
                die_at(s->line, "can only index-assign an [int] or [string] array (strings themselves are immutable)");
            Type tt = resolve_expr(s->target);    /* E_INDEX -> element type */
            Type vt = resolve_expr(s->expr);
            if (tt != vt)
                die_at(s->line, "cannot assign %s to a %s element", type_name(vt), type_name(tt));
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
        for (int j = 0; j < pr->nparams; j++) {
            s.params[j] = pr->params[j].type;
            s.inout[j]  = pr->params[j].is_inout;
            /* inout: non-heap types (int/bool/pure struct) and the mutable
             * aggregates [int]/[string]/heap-bearing structs. A heap inout
             * carries its value's owning arena (_ina_<name>), so any
             * allocating mutation (element copy, field copy, regrow/push)
             * lands in the caller's arena where the value lives. Plain
             * `string` stays excluded: it's immutable (only reassignable), so
             * an inout buys nothing. */
            if (pr->params[j].is_inout && pr->params[j].type == T_STRING)
                die_at(pr->line, "inout parameter '%s': `string` is immutable; "
                       "inout supports int/bool/struct and [int]/[string]",
                       pr->params[j].name);
        }
        g_sigs[g_nsigs++] = s;
    }
    Sig *m = sig_find("main");
    if (!m) { fprintf(stderr, "%s: error: no 'main' procedure\n", g_srcname); exit(1); }
    for (int i = 0; i < prog->n; i++) {
        Proc *pr = prog->v[i];
        g_nvars = 0;
        /* arrays ([int]/[string]) are passed as read-only borrows (their
         * buffer is shared, so in-place push/set would hit the caller); all
         * other value params — int/bool/string/struct — are copies and so are
         * mutable locals (a struct field-set rebinds only the local copy). */
        for (int j = 0; j < pr->nparams; j++) {
            Type pt = pr->params[j].type;
            /* arrays and maps are read-only borrows EXCEPT an inout one, which
             * is a by-pointer share the callee may mutate in place. */
            int mutable = (pt != T_ARRAY_INT && pt != T_ARRAY_STRING && pt != T_MAP_SI)
                          || pr->params[j].is_inout;
            vars_push(pr->params[j].name, pt, mutable);
        }
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

/* true iff a live variable's storage is the caller's arena (_parent). Set by
 * the return-slot optimization below; gates skipping the deep copy at return. */
static int cv_in_parent(const char *name) {
    const char *a = cv_arena(name);
    return a && !strcmp(a, "_parent");
}

/* names of the current proc's inout params: in the generated body they are
 * C pointers (T *h_x), so every read/lvalue use derefs as (*h_x). Reset per
 * proc; a proc has at most 8 params. */
static const char *g_inout[8];
static int g_ninout = 0;
static int is_inout_param(const char *name) {
    for (int i = 0; i < g_ninout; i++)
        if (!strcmp(g_inout[i], name)) return 1;
    return 0;
}

/* HEAP inout params additionally carry their value's owning arena as a hidden
 * C parameter `_ina_<name>`. Any allocating mutation of the param (a [string]
 * element copy, a heap struct field copy, an array regrow/push) must allocate
 * into THAT arena — the caller's, where the value lives — not the callee's
 * _scope. Non-heap inout (int/bool/pure struct) never allocates, so it has no
 * arena param. Populated per proc alongside g_inout. */
static const char *g_heap_inout[8];
static int g_nheap_inout = 0;
static int is_heap_inout_param(const char *name) {
    for (int i = 0; i < g_nheap_inout; i++)
        if (!strcmp(g_heap_inout[i], name)) return 1;
    return 0;
}
/* owning-arena C expression for a variable's *root*: the carried _ina_ param
 * for a heap inout, otherwise the variable's tracked arena (cv_arena). */
static char *owner_arena_of(const char *root) {
    if (is_heap_inout_param(root)) return sfmt("_ina_%s", root);
    const char *a = cv_arena(root);
    return (char *)(a ? a : "&_scope");
}

/* --- return-slot optimization (escape analysis) -------------------------
 * A function-top-level local that is returned by name (`return r`) is
 * allocated in the caller's arena (_parent) from birth, so the return needs
 * no deep copy — the bytes are already where the caller will read them. This
 * is the move-elision that removes the O(n) promote-by-copy for the common
 * "build a value locally, then return it" pattern.
 *
 * Soundness: allocating a local in _parent is ALWAYS memory-safe (the parent
 * strictly outlives this scope); the only cost of over-marking is mild
 * retention. So the analysis may safely over-approximate. We collect the set
 * of names that appear as `return <ident>` anywhere in the body (recursing
 * into nested blocks); a top-level heap decl with a matching name is then
 * built in _parent. The copy is skipped at return ONLY when cv_in_parent()
 * confirms the value truly lives there, so the skip can never dangle. */
static const char *g_esc[256];
static int g_nesc = 0;
static void collect_escapes(Stmt **body, int n) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (s->kind == S_RETURN && s->expr && s->expr->kind == E_IDENT)
            if (g_nesc < 256) g_esc[g_nesc++] = s->expr->sval;
        if (s->body) collect_escapes(s->body, s->nbody);   /* if/while/for body */
        if (s->els)  collect_escapes(s->els, s->nels);     /* else body */
    }
}
static int name_escapes(const char *nm) {
    for (int i = 0; i < g_nesc; i++)
        if (!strcmp(g_esc[i], nm)) return 1;
    return 0;
}

/* --- accumulator analysis (in-place string append) ----------------------
 * A string variable that is the target of a self-append `v = v + e` (v on the
 * LEFT of +; concat is not commutative) can grow in place instead of
 * re-concatenating each time, turning a loop of appends from O(N^2) into
 * O(N). Value semantics guarantees v is uniquely owned at the rebind, so the
 * in-place mutation is invisible to every other variable (a `b := v` bind
 * already deep-copies). We pre-scan the body for such names; an eligible
 * variable carries sidecar len/cap C locals (emitted AT its declaration, in
 * v's own C scope — never hoisted, so a loop-body accumulator's sidecars
 * reset in lockstep with its buffer each iteration). */
static const char *g_accum[256];
static int g_naccum = 0;
static int is_self_append(Stmt *s) {
    return s->kind == S_ASSIGN && s->expr
        && s->expr->kind == E_BINOP && s->expr->op == TK_PLUS
        && s->expr->type == T_STRING
        && s->expr->lhs->kind == E_IDENT
        && !strcmp(s->expr->lhs->sval, s->name);
}
/* `m = map_set(m, k, v)` — a self-rebind of a map accumulator. The rebind
 * reuses m's unique backing table via an in-place put instead of the pure
 * deep-copy-then-insert (amortized O(1) vs O(n) per step). Soundness is the
 * same uniqueness argument as the string accumulator: value semantics already
 * deep-copied any snapshot (`b := m`/`b = m`) eagerly at its own program
 * point, so mutating m's table in place afterward can never be observed
 * through another binding. */
static int is_self_mapset(Stmt *s) {
    return s->kind == S_ASSIGN
        && s->expr->kind == E_CALL
        && !strcmp(s->expr->sval, "map_set")
        && s->expr->nargs == 3
        && s->expr->args[0]->kind == E_IDENT
        && !strcmp(s->expr->args[0]->sval, s->name);
}
/* `m = map_del(m, k)` — the delete twin of is_self_mapset; rewritten to an
 * in-place tombstone delete on m's unique table instead of a pure deep-copy. */
static int is_self_mapdel(Stmt *s) {
    return s->kind == S_ASSIGN
        && s->expr->kind == E_CALL
        && !strcmp(s->expr->sval, "map_del")
        && s->expr->nargs == 2
        && s->expr->args[0]->kind == E_IDENT
        && !strcmp(s->expr->args[0]->sval, s->name);
}
static void collect_accums(Stmt **body, int n) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if ((is_self_append(s) || is_self_mapset(s) || is_self_mapdel(s)) && g_naccum < 256) g_accum[g_naccum++] = s->name;
        if (s->body) collect_accums(s->body, s->nbody);
        if (s->els)  collect_accums(s->els, s->nels);
    }
}
static int is_accum(const char *nm) {
    for (int i = 0; i < g_naccum; i++)
        if (!strcmp(g_accum[i], nm)) return 1;
    return 0;
}

/* Wrap a generated C expression `val` of type `t` in the deep-copy call that
 * re-homes its bytes into `arena`. For non-heap types (int/bool/pure struct)
 * the value word is already a complete copy — returned unchanged. */
static char *copy_into(Type t, const char *arena, char *val) {
    switch (t) {
        case T_STRING:       return sfmt("hier_str_copy(%s, %s)", arena, val);
        case T_ARRAY_INT:    return sfmt("hier_arr_int_copy(%s, %s)", arena, val);
        case T_ARRAY_STRING: return sfmt("hier_arr_str_copy(%s, %s)", arena, val);
        case T_MAP_SI:       return sfmt("hier_map_si_copy(%s, %s)", arena, val);
        default:
            if (IS_STRUCT(t) && type_is_heap(t))
                return sfmt("hier_copy_S_%s(%s, %s)", g_structs[STRUCT_ID(t)].name, arena, val);
            return val;   /* int/bool/pure struct: nothing to re-home */
    }
}

/* A "place" expression denotes existing storage (a variable, a field of one,
 * an array element) rather than a freshly-built value. Reading a place only
 * aliases its bytes, so storing a *heap* place into a same-or-longer-lived
 * location must deep-copy. A literal/call/concat/split result is already a
 * fresh value owned by the arena it was built in — no copy needed. */
static int is_place(Expr *e) {
    return e->kind == E_IDENT || e->kind == E_FIELD || e->kind == E_INDEX;
}

/* C expression that is nonzero iff the two operands of type `t` are equal by
 * *value* — the mirror of copy_into. int/bool compare directly; strings by
 * byte; arrays element-wise; structs field-wise via a generated hier_eq_S_X.
 * Recurses through nesting exactly as the deep copy does. */
static char *gen_eq(Type t, const char *a, const char *b) {
    if (t == T_STRING)       return sfmt("(strcmp(%s, %s) == 0)", a, b);
    if (t == T_ARRAY_INT)    return sfmt("hier_arr_int_eq(%s, %s)", a, b);
    if (t == T_ARRAY_STRING) return sfmt("hier_arr_str_eq(%s, %s)", a, b);
    if (t == T_MAP_SI)       return sfmt("hier_map_si_eq(%s, %s)", a, b);
    if (IS_STRUCT(t))        return sfmt("hier_eq_S_%s(%s, %s)", g_structs[STRUCT_ID(t)].name, a, b);
    return sfmt("(%s == %s)", a, b);   /* int/bool */
}

static char *gen_call(Expr *e, const char *arena) {
    if (!strcmp(e->sval, "len")) {
        char *a = gen_expr(e->args[0], arena);
        if (e->args[0]->type == T_STRING)
            return sfmt("hier_str_len(%s)", a);
        return sfmt("((%s).len)", a);   /* arrays AND maps both have .len */
    }
    /* map builtins: map_set is pure (deep-copy + insert into `arena`); the
     * accumulator pass rewrites a self-rebind to an in-place put separately. */
    if (!strcmp(e->sval, "map_set")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = gen_expr(e->args[1], arena);
        char *v = gen_expr(e->args[2], arena);
        return sfmt("hier_map_si_set(%s, %s, %s, %s)", arena, m, k, v);
    }
    if (!strcmp(e->sval, "map_get")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = gen_expr(e->args[1], arena);
        char *d = gen_expr(e->args[2], arena);
        return sfmt("hier_map_si_get(%s, %s, %s)", m, k, d);
    }
    if (!strcmp(e->sval, "map_has")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = gen_expr(e->args[1], arena);
        return sfmt("hier_map_si_has(%s, %s)", m, k);
    }
    /* map_del pure: deep-copy + delete into `arena`; the accumulator pass
     * rewrites a self-rebind to an in-place tombstone delete separately. */
    if (!strcmp(e->sval, "map_del")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = gen_expr(e->args[1], arena);
        return sfmt("hier_map_si_del_pure(%s, %s, %s)", arena, m, k);
    }
    if (!strcmp(e->sval, "keys")) {
        char *m = gen_expr(e->args[0], arena);
        return sfmt("hier_map_si_keys(%s, %s)", arena, m);
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
        /* grow the array in *its owning arena* (the root variable's), not the
         * current one. The target may be a variable or a struct's array field;
         * &(lvalue) works for both (h_xs / ((h_p).f_tags)). For [string] the
         * runtime push copies the element bytes into that arena, so a pushed
         * loop-scratch temporary does not dangle. */
        Expr *root = e->args[0];
        while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
        /* regrow targets the array's owning arena — the carried _ina_ arena
         * if the root is a heap inout param (so the new buffer outlives the
         * call and the caller sees the updated descriptor). */
        const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : arena;
        char *arr = gen_expr(e->args[0], arena);
        char *v = gen_expr(e->args[1], arena);
        if (e->args[0]->type == T_ARRAY_STRING)
            return sfmt("hier_arr_str_push(%s, &(%s), %s)", owner, arr, v);
        return sfmt("hier_arr_int_push(%s, &(%s), %s)", owner, arr, v);
    }
    if (!strcmp(e->sval, "split")) {
        char *s   = gen_expr(e->args[0], arena);
        char *sep = gen_expr(e->args[1], arena);
        return sfmt("hier_str_split(%s, %s, %s)", arena, s, sep);
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
    /* user proc: first arg is the destination arena for its return. A heap
     * inout parameter takes TWO C args: the value's owning arena, then the
     * &pointer — so an allocating mutation in the callee lands where the
     * value lives. The owner is computed from the argument's root variable
     * (which, if it's itself a heap inout param here, yields its carried
     * _ina_ arena — threading the real owner across recursion). */
    Sig *cs = sig_find(e->sval);
    char *out = sfmt("h_%s(%s", e->sval, arena);
    for (int i = 0; i < e->nargs; i++) {
        char *a = gen_expr(e->args[i], arena);
        if (cs && i < cs->nparams && cs->inout[i] && type_is_heap(cs->params[i])
            && e->args[i]->kind == E_ADDR) {
            Expr *root = e->args[i]->lhs;
            while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
            const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : arena;
            out = sfmt("%s, %s, %s", out, owner, a);
        } else {
            out = sfmt("%s, %s", out, a);
        }
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
        case E_IDENT:return is_inout_param(e->sval) ? sfmt("(*h_%s)", e->sval)
                                                    : sfmt("h_%s", e->sval);
        case E_ADDR: /* &place as an inout arg: address of the underlying
                      * lvalue (gen_expr already derefs an inout root) */
            return sfmt("&(%s)", gen_expr(e->lhs, arena));
        case E_CALL: return gen_call(e, arena);
        case E_INDEX: {
            char *a = gen_expr(e->lhs, arena);
            char *ix = gen_expr(e->rhs, arena);
            if (e->lhs->type == T_STRING)
                return sfmt("hier_str_get(%s, %s)", a, ix);
            if (e->lhs->type == T_ARRAY_STRING)
                return sfmt("hier_arr_str_get(%s, %s)", a, ix);
            return sfmt("hier_arr_int_get(%s, %s)", a, ix);
        }
        case E_ARRLIT: {
            /* GNU statement-expression so a literal is a single value */
            int id = g_blk++;
            if (e->type == T_MAP_SI) {
                /* map literal: build empty in `arena`, then put each pair. The
                 * runtime put copies the key bytes into `arena`. args interleave
                 * k0,v0,k1,v1,...; an empty literal (nargs 0) just yields {0}. */
                char *out = sfmt("({ HierMapSI _l%d = hier_map_si_with_cap(%s, 0L);",
                                 id, arena);
                for (int i = 0; i + 1 < e->nargs; i += 2)
                    out = sfmt("%s hier_map_si_put(%s, &_l%d, %s, %s);",
                               out, arena, id, gen_expr(e->args[i], arena),
                               gen_expr(e->args[i + 1], arena));
                return sfmt("%s _l%d; })", out, id);
            }
            if (e->type == T_ARRAY_STRING) {
                /* copy each element into `arena` so the literal owns its bytes */
                char *out = sfmt("({ HierArrStr _l%d = hier_arr_str_with_cap(%s, %dL);",
                                 id, arena, e->nargs);
                for (int i = 0; i < e->nargs; i++)
                    out = sfmt("%s _l%d.data[%d] = hier_str_copy(%s, %s);",
                               out, id, i, arena, gen_expr(e->args[i], arena));
                return sfmt("%s _l%d.len = %dL; _l%d; })", out, id, e->nargs, id);
            }
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
            /* positional C99 compound literal. Each heap field that is built
             * from a *place* (variable/field/element) must be deep-copied into
             * `arena` so the new struct owns its bytes; fresh values and
             * non-heap fields pass through. */
            StructDef *sd = &g_structs[STRUCT_ID(e->type)];
            char *out = sfmt("((S_%s){ ", sd->name);
            for (int i = 0; i < e->nargs; i++) {
                char *a = gen_expr(e->args[i], arena);
                Type ft = sd->fields[i].type;
                if (type_is_heap(ft) && is_place(e->args[i]))
                    a = copy_into(ft, arena, a);
                out = sfmt("%s%s%s", out, a, i + 1 < e->nargs ? ", " : "");
            }
            return sfmt("%s })", out);
        }
        case E_BINOP: {
            char *l = gen_expr(e->lhs, arena);
            char *r = gen_expr(e->rhs, arena);
            if (e->op == TK_PLUS && e->lhs->type == T_STRING)
                return sfmt("hier_str_concat(%s, %s, %s)", arena, l, r);
            /* equality dispatches by type (deep/structural); != negates it */
            if (e->op == TK_EQEQ || e->op == TK_NEQ) {
                char *eq = gen_eq(e->lhs->type, l, r);
                return e->op == TK_EQEQ ? eq : sfmt("(!%s)", eq);
            }
            /* ordering on strings is lexicographic via strcmp */
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

/* Stack of enclosing loop/if block arenas live at the current codegen point,
 * outermost-first (e.g. "&_scr3", "&_b7"). Each is an INDEPENDENT arena
 * (arena_child is a fresh block list, not physically nested in _scope), so an
 * early `return` must free them explicitly or they leak — this is the
 * loop-return scratch leak. Reset per proc; pushed around each block body. */
static const char *g_ascope[64];
static int g_nascope = 0;
static void ascope_push(const char *a) {
    if (g_nascope >= 64) { fprintf(stderr, "hierc: block nesting too deep\n"); exit(1); }
    g_ascope[g_nascope++] = a;
}

/* The free sequence an (early) return must run: every enclosing block arena
 * innermost-first, then the proc's own _scope. The return value already lives
 * in _parent (built or deep-copied there), which strictly outlives all of
 * these, so freeing them here can never touch the returned bytes. At proc top
 * level (g_nascope == 0) this is exactly "arena_free(&_scope);" — unchanged,
 * so a top-level return emits byte-identical C to before. */
static char *return_frees(void) {
    char *s = sfmt("%s", "");
    for (int i = g_nascope - 1; i >= 0; i--)
        s = sfmt("%sarena_free(%s); ", s, g_ascope[i]);
    return sfmt("%sarena_free(&_scope);", s);
}

/* `scope` is a C expression of type Arena* into which local allocations
 * go. Returns always promote/collapse the proc's own arena, named
 * "_scope" in every generated proc body. */
static void gen_stmt(FILE *o, Stmt *s, int ind, const char *scope, Type ret) {
    switch (s->kind) {
        case S_DECL: {
            /* return-slot optimization: a function-top-level heap local that
             * is returned by name is built directly in the caller's arena, so
             * the eventual `return` is a no-op move instead of a deep copy.
             * Top-level only (scope is the proc's own "&_scope") — never a
             * loop/if body, so a bounded loop-scratch local is never promoted
             * to function lifetime. */
            const char *owner = scope;
            if (!strcmp(scope, "&_scope") && type_is_heap(s->decl_type)
                && name_escapes(s->name))
                owner = "_parent";
            char *v = gen_expr(s->expr, owner);
            /* value semantics: binding from a heap *place* aliases its bytes,
             * so deep-copy into the owner arena. A literal/call/concat result
             * is already a freshly-owned value built in `owner` — no copy. */
            if (is_place(s->expr) && type_is_heap(s->decl_type))
                v = copy_into(s->decl_type, owner, v);
            indent(o, ind);
            fprintf(o, "%sh_%s = %s;\n", c_type(s->decl_type), s->name, v);
            /* in-place append sidecars, declared HERE (same C scope as h_v, so
             * a loop-body accumulator re-inits them each iteration in lockstep
             * with its buffer — never hoist these). cap 0 = "not growable in
             * place yet", so the first append allocates and the initial buffer
             * (possibly a string literal in .rodata) is never written. */
            if (s->decl_type == T_STRING && is_accum(s->name)) {
                indent(o, ind);
                fprintf(o, "long _len_h_%s = (long)strlen(h_%s); long _cap_h_%s = 0;\n",
                        s->name, s->name, s->name);
            }
            cv_push(s->name, owner);   /* this variable lives in `owner` */
            break;
        }
        case S_ASSIGN: {
            /* allocate the value where the variable lives, not where we
             * currently are, so it survives any inner scope collapsing. For a
             * heap inout param the value lives in the caller's arena (_ina_),
             * so a whole-map/array reassignment must build there, not in this
             * callee's _scope (which would dangle once the call returns). */
            const char *owner = cv_arena(s->name);
            if (!owner) owner = scope;
            if (is_heap_inout_param(s->name)) owner = owner_arena_of(s->name);
            /* in-place append: `acc = acc + e` on a tracked accumulator grows
             * acc's buffer in its OWNER arena (cv_arena), not the current loop
             * scratch scope. The append result re-homes acc, so the rest of
             * the function still sees an ordinary NUL-terminated char*. e is
             * fully evaluated before the buffer is touched (handles acc=acc+acc
             * and acc=acc+f(acc)). */
            if (is_accum(s->name) && is_self_append(s)) {
                char *e = gen_expr(s->expr->rhs, owner);
                indent(o, ind);
                fprintf(o, "hier_str_append(%s, &h_%s, &_len_h_%s, &_cap_h_%s, %s);\n",
                        owner, s->name, s->name, s->name, e);
                break;
            }
            /* in-place map accumulator: `m = map_set(m, k, v)` grows m's unique
             * table in its OWNER arena via put, instead of the pure deep-copy
             * set. The key/value args are fully evaluated before the put runs
             * (so `map_set(m, w, map_get(m, w, 0) + 1)` reads the old m first);
             * no sidecars needed — len/cap live inside HierMapSI. */
            if (is_accum(s->name) && is_self_mapset(s)) {
                /* the map's owning arena and a pointer to its descriptor: for an
                 * inout map param the descriptor is the caller's (pointer h_m,
                 * arena _ina_m) so the put lands where the value lives; for a
                 * local it is &h_m in the local's own arena. */
                const char *mo = owner_arena_of(s->name);
                const char *mp = is_heap_inout_param(s->name) ? sfmt("h_%s", s->name)
                                                              : sfmt("&h_%s", s->name);
                char *k = gen_expr(s->expr->args[1], mo);
                char *v = gen_expr(s->expr->args[2], mo);
                indent(o, ind);
                fprintf(o, "hier_map_si_put(%s, %s, %s, %s);\n", mo, mp, k, v);
                break;
            }
            /* in-place map delete: `m = map_del(m, k)` tombstones in place. No
             * allocation, so no arena arg; the pointer is the inout pointer for
             * an inout map, else the address of the local descriptor. */
            if (is_accum(s->name) && is_self_mapdel(s)) {
                const char *mo = owner_arena_of(s->name);
                const char *mp = is_heap_inout_param(s->name) ? sfmt("h_%s", s->name)
                                                              : sfmt("&h_%s", s->name);
                char *k = gen_expr(s->expr->args[1], mo);
                indent(o, ind);
                fprintf(o, "hier_map_si_del(%s, %s);\n", mp, k);
                break;
            }
            char *v = gen_expr(s->expr, owner);
            /* a heap *place* is only an alias into some (possibly inner,
             * soon-to-collapse) scope; deep-copy it into the target's arena
             * so it survives. A literal/call/concat result is already freshly
             * allocated in `owner` — no copy needed. */
            if (is_place(s->expr) && type_is_heap(s->expr->type))
                v = copy_into(s->expr->type, owner, v);
            indent(o, ind);
            /* an inout param is a pointer in the body; assign through it */
            if (is_inout_param(s->name))
                fprintf(o, "(*h_%s) = %s;\n", s->name, v);
            else
                fprintf(o, "h_%s = %s;\n", s->name, v);
            /* a non-self assignment to a tracked accumulator rebinds its
             * buffer; resync sidecars (cap 0 = the new buffer isn't ours to
             * grow in place — forces the next append to allocate). */
            if (is_accum(s->name) && s->expr->type == T_STRING)
                fprintf(o, "%*s_len_h_%s = (long)strlen(h_%s); _cap_h_%s = 0;\n",
                        ind * 4, "", s->name, s->name, s->name);
            break;
        }
        case S_INDEXSET: {
            /* the array being indexed: a variable or a struct's array field.
             * &(lvalue) gives a HierArr* for both. */
            Expr *arrx = s->target->lhs;
            Expr *root = arrx;
            while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
            char *arr = gen_expr(arrx, scope);
            char *ix  = gen_expr(s->target->rhs, scope);
            char *v   = gen_expr(s->expr, scope);
            indent(o, ind);
            if (arrx->type == T_ARRAY_STRING) {
                /* set copies the element into the array's owning arena — the
                 * carried _ina_ arena if the root is a heap inout param. */
                const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : scope;
                fprintf(o, "hier_arr_str_set(%s, &(%s), %s, %s);\n", owner, arr, ix, v);
            } else {
                fprintf(o, "hier_arr_int_set(&(%s), %s, %s);\n", arr, ix, v);
            }
            break;
        }
        case S_FIELDSET: {
            /* gen_expr(E_FIELD) is a valid C lvalue, e.g. (h_p).f_x. The
             * struct lives in its root variable's arena, so a heap field's
             * new bytes must go there too (not the current block scope, which
             * may collapse first); a heap *place* RHS is also deep-copied. */
            Expr *root = s->target;
            while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
            /* heap field's new bytes go in the struct's owning arena — the
             * carried _ina_ arena if the root is a heap inout param. */
            const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : scope;
            char *lv = gen_expr(s->target, scope);
            char *v  = gen_expr(s->expr, owner);
            if (type_is_heap(s->target->type) && is_place(s->expr))
                v = copy_into(s->target->type, owner, v);
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
            /* `rf` frees every arena live at this return — enclosing loop/if
             * block arenas (innermost-first) then _scope — and ends with
             * "arena_free(&_scope);". At proc top level it IS just that, so a
             * top-level return is byte-identical to before; inside a loop/if it
             * additionally frees the scratch arena that used to leak. */
            char *rf = return_frees();
            if (!s->expr) {
                indent(o, ind); fprintf(o, "{ %s return; }\n", rf);
            } else if (ret == T_STRING) {
                /* promote up. A fresh value (literal/call/concat) is built
                 * directly in the caller's arena; a bare string variable is
                 * only a pointer into this scope, so deep-copy it into the
                 * caller's arena before freeing this scope — UNLESS the
                 * return-slot optimization already built it in _parent. */
                if (s->expr->kind == E_IDENT && !cv_in_parent(s->expr->sval)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ char *_ret = hier_str_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ char *_ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (ret == T_ARRAY_INT) {
                /* promote up. A fresh value (literal/call) is built directly
                 * in the caller's arena; a borrowed/local variable is
                 * deep-copied into it — UNLESS the return-slot optimization
                 * already built it in _parent (then it's a no-op move). */
                if (s->expr->kind == E_IDENT && !cv_in_parent(s->expr->sval)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ HierArrInt _ret = hier_arr_int_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ HierArrInt _ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (ret == T_ARRAY_STRING) {
                /* promote up. A fresh value (literal/split/call) is built
                 * directly in the caller's arena; a bare variable is
                 * deep-copied (buffer + every element) into it — UNLESS the
                 * return-slot optimization already built it in _parent. */
                if (s->expr->kind == E_IDENT && !cv_in_parent(s->expr->sval)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ HierArrStr _ret = hier_arr_str_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ HierArrStr _ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (ret == T_MAP_SI) {
                /* promote up, exactly like the array cases: a bare map variable
                 * is only a value whose tables live in this scope, so deep-copy
                 * into the caller's arena before freeing — UNLESS the return-slot
                 * optimization already built it in _parent (then a no-op move). */
                if (s->expr->kind == E_IDENT && !cv_in_parent(s->expr->sval)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ HierMapSI _ret = hier_map_si_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ HierMapSI _ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (IS_STRUCT(ret) && type_is_heap(ret)) {
                /* promote up. A heap struct built from a *place* is deep-copied
                 * into the caller's arena; a fresh struct literal/call is built
                 * directly there (its construction re-homes any heap fields).
                 * A return-slot local (already in _parent) needs neither — it's
                 * a no-op move, like the array cases above. */
                if (s->expr->kind == E_IDENT && cv_in_parent(s->expr->sval)) {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ %s_ret = %s; %s return _ret; }\n",
                                            c_type(ret), v, rf);
                } else if (is_place(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ %s_ret = %s; %s return _ret; }\n",
                                            c_type(ret), copy_into(ret, "_parent", v), rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ %s_ret = %s; %s return _ret; }\n",
                                            c_type(ret), v, rf);
                }
            } else {
                /* int/bool, or a pure-value struct: a value, nothing on the
                 * heap to keep alive — copy it out and free the scope */
                char *v = gen_expr(s->expr, "&_scope");
                indent(o, ind); fprintf(o, "{ %s_ret = %s; %s return _ret; }\n",
                                        c_type(ret), v, rf);
            }
            break;
        }
        case S_IF: {
            int id = g_blk++;
            char *c = gen_expr(s->expr, scope);
            indent(o, ind); fprintf(o, "if (%s) {\n", c);
            indent(o, ind + 1); fprintf(o, "Arena _b%d = arena_child(%s);\n", id, scope);
            char *bs = sfmt("&_b%d", id);
            ascope_push(bs);   /* a return inside the block must free _bN too */
            gen_block(o, s->body, s->nbody, ind + 1, bs, ret);
            g_nascope--;
            indent(o, ind + 1); fprintf(o, "arena_free(&_b%d);\n", id);
            indent(o, ind); fprintf(o, "}");
            if (s->els) {
                int eid = g_blk++;
                fprintf(o, " else {\n");
                indent(o, ind + 1); fprintf(o, "Arena _b%d = arena_child(%s);\n", eid, scope);
                char *es = sfmt("&_b%d", eid);
                ascope_push(es);
                gen_block(o, s->els, s->nels, ind + 1, es, ret);
                g_nascope--;
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
            ascope_push(ss);   /* a return inside the loop must free _scrN too */
            gen_block(o, s->body, s->nbody, ind + 2, ss, ret);
            g_nascope--;
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
            ascope_push(ss);        /* a return inside the loop must free _scrN too */
            gen_block(o, s->body, s->nbody, ind + 2, ss, ret);
            g_nascope--;
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
    for (int i = 0; i < pr->nparams; i++) {
        /* inout params are received by pointer so writes reach the caller's
         * storage (copy-in copy-out, realized as call-by-reference). A HEAP
         * inout additionally carries its value's owning arena (_ina_<name>),
         * passed just before the pointer, so allocating mutations land where
         * the value lives rather than in this callee's _scope. */
        Type pt = pr->params[i].type;
        if (pr->params[i].is_inout && type_is_heap(pt))
            fprintf(o, ", Arena *_ina_%s, %s*h_%s",
                    pr->params[i].name, c_type(pt), pr->params[i].name);
        else if (pr->params[i].is_inout)
            fprintf(o, ", %s*h_%s", c_type(pt), pr->params[i].name);
        else
            fprintf(o, ", %sh_%s", c_type(pt), pr->params[i].name);
    }
    fprintf(o, ")");
}

static void gen_proto(FILE *o, Proc *pr) { gen_signature(o, pr); fprintf(o, ";\n"); }

static void gen_proc(FILE *o, Proc *pr) {
    gen_signature(o, pr);
    fprintf(o, " {\n");
    indent(o, 1); fprintf(o, "Arena _scope = arena_child(_parent);\n");
    g_ncv = 0;
    g_nascope = 0;   /* no enclosing block arenas at the proc body top level */
    /* return-slot optimization: which top-level locals escape via return */
    g_nesc = 0;
    collect_escapes(pr->body, pr->nbody);
    /* in-place append: which string locals are self-append accumulators */
    g_naccum = 0;
    collect_accums(pr->body, pr->nbody);
    /* the string accumulator opt declares its sidecar len/cap locals at the
     * variable's S_DECL; a by-value parameter has no S_DECL, so a self-append on
     * a string param (`s = s + e`) must NOT take the in-place path — its
     * sidecars would be undeclared C. Drop NON-inout params from the accumulator
     * set; they fall back to ordinary concat/pure-set-and-rebind, which is
     * correct. An inout param is KEPT: it carries no string sidecars (inout
     * string is forbidden, so the only inout accumulator is a map), and its
     * in-place map put/del is exactly the wanted mutation — landing in the
     * caller's arena (_ina_) so it survives the call, where the pure fallback
     * would allocate in this callee's _scope and dangle after return. */
    for (int p = 0; p < pr->nparams; p++) {
        if (pr->params[p].is_inout) continue;
        for (int a = 0; a < g_naccum; a++)
            if (!strcmp(g_accum[a], pr->params[p].name)) {
                g_accum[a] = g_accum[--g_naccum]; a--;
            }
    }
    /* register this proc's inout params so the body derefs them as (*h_x) */
    g_ninout = 0;
    g_nheap_inout = 0;
    for (int i = 0; i < pr->nparams; i++)
        if (pr->params[i].is_inout) {
            g_inout[g_ninout++] = pr->params[i].name;
            if (type_is_heap(pr->params[i].type))
                g_heap_inout[g_nheap_inout++] = pr->params[i].name;
        }
    /* a reassigned param must land in this proc's scope to outlive any
     * inner block; the incoming pointer itself is borrowed from the caller.
     * EXCEPT a returned `string` parameter: its bytes already live in the
     * caller's arena (_parent or an ancestor — a string param is a char* into
     * the caller), and a string is immutable, so handing those same bytes back
     * is both lifetime-safe and value-safe (no mutation can ever observe the
     * aliasing). Track it as living in _parent so the return-slot path skips
     * the O(n) deep copy; a reassignment of such a param then also stores into
     * _parent — always memory-safe, at most mild retention. Strings only:
     * arrays are mutable (aliasing would break value semantics) and heap
     * structs are deep-copied into _scope on entry (bytes not in _parent). */
    for (int i = 0; i < pr->nparams; i++) {
        const char *pa = "&_scope";
        if (pr->params[i].type == T_STRING && !pr->params[i].is_inout
            && name_escapes(pr->params[i].name))
            pa = "_parent";
        cv_push(pr->params[i].name, pa);
    }
    /* Structs are passed by value, but C copies them shallowly — a heap field
     * (string/array) still points at the caller's bytes. Deep-copy heap-
     * bearing struct params into this scope so the parameter is a truly
     * independent value: mutating its array field cannot touch the caller, and
     * the copy is owned here. ([int]/[string] params stay read-only borrows;
     * string params are immutable, so neither needs this.) */
    for (int i = 0; i < pr->nparams; i++) {
        Type pt = pr->params[i].type;
        /* an inout struct is a pointer to the caller's value — must NOT be
         * deep-copied (the whole point is to mutate the caller's). Only
         * by-value heap struct params are copied for independence. */
        if (IS_STRUCT(pt) && type_is_heap(pt) && !pr->params[i].is_inout) {
            indent(o, 1);
            fprintf(o, "h_%s = hier_copy_S_%s(&_scope, h_%s);\n",
                    pr->params[i].name, g_structs[STRUCT_ID(pt)].name, pr->params[i].name);
        }
    }
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
    /* deep-copy function per heap-bearing struct: re-home every heap field
     * into arena `a`. Non-heap fields are copied by the initial `r = v`.
     * Emitted in definition order, so a nested struct field's copy fn (a
     * lower index, since it was defined first) is already declared above. */
    for (int i = 0; i < g_nstructs; i++) {
        StructDef *sd = &g_structs[i];
        if (!type_is_heap(STRUCT_TYPE(i))) continue;
        fprintf(o, "static S_%s hier_copy_S_%s(Arena *a, S_%s v) {\n", sd->name, sd->name, sd->name);
        fprintf(o, "    S_%s r = v;\n", sd->name);
        for (int j = 0; j < sd->nfields; j++) {
            Type ft = sd->fields[j].type;
            if (!type_is_heap(ft)) continue;
            char *src = sfmt("v.f_%s", sd->fields[j].name);
            fprintf(o, "    r.f_%s = %s;\n", sd->fields[j].name, copy_into(ft, "a", src));
        }
        fprintf(o, "    return r;\n}\n\n");
    }
    /* structural-equality function per struct: field-wise, recursing into
     * nested structs/arrays/strings. Emitted in definition order so a nested
     * field's eq fn (lower index) is already declared above. */
    for (int i = 0; i < g_nstructs; i++) {
        StructDef *sd = &g_structs[i];
        fprintf(o, "static int hier_eq_S_%s(S_%s a, S_%s b) {\n", sd->name, sd->name, sd->name);
        fprintf(o, "    return ");
        if (sd->nfields == 0) {
            fprintf(o, "1");
        } else {
            for (int j = 0; j < sd->nfields; j++) {
                char *af = sfmt("a.f_%s", sd->fields[j].name);
                char *bf = sfmt("b.f_%s", sd->fields[j].name);
                fprintf(o, "%s%s", gen_eq(sd->fields[j].type, af, bf),
                        j + 1 < sd->nfields ? "\n        && " : "");
            }
        }
        fprintf(o, ";\n}\n\n");
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
